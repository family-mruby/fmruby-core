# ブラウザ版を網に入れる手順

> 状態: 進行中 | 更新: 2026-10-11 | ブラウザ版 (wasm) の Family mruby を Asterism のノードとして zenohd につなぐ手順 (B1、手元の http の版)。zenohd の ws/ の待ち受け、ブラウザ版のビルドと起動、ルータの場所の渡し方、PC からの呼び出し、台車の操縦、最小の試しのページ

作りと実測は report/b1.md。https の公開の版から使う形 (wss://) は B2 で扱う。

## 1. しくみの要点

- ブラウザは TCP を開けないので、zenoh-pico は WebSocket でルータにつなぐ (ロケータ `ws/<host>:<port>`)。
  ルータは zenohd の `ws/` の待ち受けで、プラグインは要らない。
- zenoh-pico は Emscripten 向けに、client だけ・WebSocket のリンクだけでビルドする。peer の待ち受け (listen) は
  ページにはできないので使えない。
- Asterism の層 (asterism の gem) は機体・sim と同じ純 Ruby のまま。アプリも同じものが動く。

## 2. zenohd に ws/ の待ち受けを足す

親リポジトリ (family-mruby) で、docker-compose.zenoh-ws.yml を重ねて zenohd を起動し直す。

```
docker compose -f docker-compose.yml -f docker-compose.zenoh-ws.yml up -d zenohd
```

- 待ち受けは tcp/7447 (従来) と ws/7449。7449 は 127.0.0.1 にだけ公開する。
- 機体を LAN からつなぐときも一緒に使うなら、docker-compose.zenoh-lan.yml の**後に**置く
  (zenoh-lan.yml がポートの一覧を置き換えるため、前に置くと 7449 が消える)。

```
docker compose -f docker-compose.yml -f docker-compose.zenoh-lan.yml \
  -f docker-compose.zenoh-ws.yml up -d zenohd
```

- docker-compose.relay.yml (zenohd に設定ファイルを渡す形) とは一緒に使わない。
- 確かめ: `docker logs fmruby_zenohd | grep listen` に `ws/[::]:7449` が出る。

## 3. ブラウザ版をビルドする

fmruby-core で、emsdk を読み込んだシェルから。

```
source ~/emsdk/emsdk_env.sh
rake wasm:mruby     # libmruby (zenoh-pico と Asterism の gem を含む)
rake wasm:web       # core_web.{js,wasm,data} とページ
```

- `rake clean_all` は wasm の libmruby も消す。その後は `rake wasm:mruby` からやり直す。
- lib/add/ の gem や lib/add/family_mruby_wasm.rb を直したら `rake wasm:mruby` を先に。wasm/zenoh/ の C だけなら
  `rake wasm:web` でよい。
- ROS 2 の型 (`/usr/share/asterism/msgs`) は `rake wasm:web` が束に入れる (`rake setup` が asterism の checkout から
  作るもので、git には無い)。
- アプリの Ruby を直しただけなら `rake wasm:web` (束の作り直し) で入る。

## 4. 起動する

```
rake wasm:serve      # http://localhost:8006/ (COOP/COEP つき)
```

ブラウザで開く。`?app=` で開いた直後にアプリを起動できる。

```
http://localhost:8006/wasm/web/index.html?app=/app/test/asterism_demo.app.rb
http://localhost:8006/wasm/web/index.html?app=/app/test/rover_cam.app.rb
```

自律の検証では、親の MCP の `web_up` の `url_args` に同じ引数 (`app=/app/test/asterism_demo.app.rb`) を渡す。

## 5. ルータの場所とノードの名前の決まり方

試しのアプリ (asterism_demo、rover_cam) は、ルータの場所を次の順に探す。

1. `/home/zenoh_echo.txt` の 1 行目 (使う人が置く。ブラウザでは IndexedDB に残る)
2. 設定 `zenoh_router` (system_conf.toml)。ブラウザ版の束は `ws/localhost:7449` を持つ
   (config/system_conf_wasm.toml)。ページの URL の `?zenoh=ws/<host>:<port>` がその回だけ置き換える
   (`?zenoh=ws/192.0.2.10:7449`)。URL の値は保存されない (次に開くときは束の値に戻る)
3. `tcp/zenohd:7447` (sim の既定。ブラウザではつながらない)

`?zenoh=` に通る文字は `ws/` に続く英数字と `_ . : [ ] -` だけ。sim・機体の system_conf には `zenoh_router` が無いので、
これまでどおり 1 と 3 で決まる。

ノードの名前 (ID) は `/home/asterism_node.txt` の 1 行目、無ければ機体の mDNS の名前、それも無ければ機種の名前
(`FmrbConst::BOARD`: sim は `linux`、ブラウザは `wasm`)。同じ PC で sim とブラウザを同時に動かしても名前がぶつからない。
ブラウザを 2 つ以上同時につなぐときは、それぞれ `/home/asterism_node.txt` で名前を分ける
(ランダムな ID と Web Locks による 1 つだけの起動は design.md 9 章の案で、まだ入れていない)。

## 6. PC から呼ぶ

親リポジトリで。

```
ruby tools/fmrb_zenoh.rb call wasm/demo/info status
ruby tools/fmrb_zenoh.rb call wasm/demo/screen say '"hello"'
ruby tools/fmrb_zenoh.rb meta 'wasm/demo/*'
```

asterism-console の図には `wasm` のノード (アプリ demo とオブジェクト 3 つの入れ子) が出る。console から呼ぶには、
Call permissions で node `wasm` / app `demo` の許可を足す。

## 7. 台車を操縦する

MuJoCo の台車 (S1 の compose、親の docker/mujoco/README.md) を起動した上で、`?app=/app/test/rover_cam.app.rb` で開く。
矢印キーで速さと向き、Space で止める。カメラの JPEG はブラウザ版でも画像の欄に出る (表示の側が TJpgDec で復号する)。

## 8. 最小の試し (Family mruby なし)

zenoh-pico と WebSocket のリンクだけのページ。リンクの切り分けに使う。

```
rake wasm:mruby               # 一度 (zenoh-pico の本体は libmruby から取る)
rake wasm:zenoh_probe
rake wasm:serve
# ブラウザで開く
http://localhost:8006/wasm/zenoh/probe/index.html?zenoh=ws/localhost:7449&s=30
# 同時に PC の側 (CRuby の asterism-zenoh。asterism-console の bundle にある)
cd ../asterism-console && bundle exec ruby ../fmruby-core/wasm/zenoh/probe/peer.rb 30
```

ページは 1 秒ごとに `b1/probe/from_browser` へ put し、`b1/probe/to_browser` に来た値を `b1/probe/echo` へ返す。
peer.rb は往復の時間を出し、ページの声が届いて返事がそろえば 0 で終わる。

## 9. できないこと (B1 の時点)

- https のページ (公開の版) からはつながらない。`ws://` は https から使えず、`wss://` は B2 (リバースプロキシで TLS を
  終えてルータの ws/ に流す形)。
- peer モード (listen) は使えない (client だけ)。
- ルータの場所を画面で設定する手段は無い (B2)。
- 7449 は 127.0.0.1 にだけ公開しているので、ページは同じ PC のブラウザから開く。
