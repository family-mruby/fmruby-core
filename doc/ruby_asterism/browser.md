# ブラウザ版を網に入れる手順

> 状態: 進行中 | 更新: 2026-10-12 | ブラウザ版 (wasm) の Family mruby を Asterism のノードとして zenohd につなぐ手順。zenohd の ws/ の待ち受けと wss:// の中継、ブラウザ版のビルドと起動 (手元の http の版と、公開の版と同じ作りを https で配る形)、ルータの設定、ノードの ID の決まり、PC からの呼び出し、台車の操縦、最小の試しのページ

作りと実測は report/b1.md (手元の http の版) と report/b2.md (https の版、wss://、設定、ID)。

## 1. しくみの要点

- ブラウザは TCP を開けないので、zenoh-pico は WebSocket でルータにつなぐ (ロケータ `ws/<host>:<port>`)。
  ルータは zenohd の `ws/` の待ち受けで、プラグインは要らない。
- zenoh-pico は Emscripten 向けに、client だけ・WebSocket のリンクだけでビルドする。peer の待ち受け (listen) は
  ページにはできないので使えない。
- Asterism の層 (asterism の gem) は端末・sim と同じ純 Ruby のまま。アプリも同じものが動く。
- ページが https なら `wss://`、http なら `ws://` でつなぐ。ただし https のページでも、ループバックの
  相手 (`localhost`・`*.localhost`・`127.x.x.x`・`[::1]`) には `ws://` のまま (Chrome と Firefox はループバックを
  混在の例外にしている。report/b2.md 2 章)。ロケータはどちらも `ws/<host>:<port>` で、書き分けない
  (zenoh-pico に wss の名前は無い。選ぶのはページの側、wasm/zenoh/zp_ws_emscripten.c)。

## 2. zenohd に ws/ の待ち受けを足す

親リポジトリ (family-mruby) で、docker-compose.zenoh-ws.yml を重ねて zenohd を起動し直す。

```
docker compose -f docker-compose.yml -f docker-compose.zenoh-ws.yml up -d zenohd
```

- 待ち受けは tcp/7447 (従来) と ws/7449。7449 は 127.0.0.1 にだけ公開する。
- 端末を LAN からつなぐときも一緒に使うなら、docker-compose.zenoh-lan.yml の**後に**置く
  (zenoh-lan.yml がポートの一覧を置き換えるため、前に置くと 7449 が消える)。

```
docker compose -f docker-compose.yml -f docker-compose.zenoh-lan.yml \
  -f docker-compose.zenoh-ws.yml up -d zenohd
```

- docker-compose.relay.yml (zenohd に設定ファイルを渡す形) とは一緒に使わない。
- 確かめ: `docker logs fmruby_zenohd | grep listen` に `ws/[::]:7449` が出る。
- sim_up は zenohd を docker-compose.yml だけで作り直すので、sim を上げた後は上の行をもう一度打つ。

### 2.1 wss:// の中継 (この PC 以外のブラウザ、https のページ)

https のページは、ループバック以外へは `ws://` を開けない (WebSocket を作るところで SecurityError)。そこで
TLS を終えて zenohd の `ws/` に流す中継を前に置く。親リポジトリの docker-compose.zenoh-wss.yml (Caddy)。

```
ZENOH_WSS_HOST=<この PC の名前> docker compose -f docker-compose.yml \
  -f docker-compose.zenoh-lan.yml -f docker-compose.zenoh-ws.yml \
  -f docker-compose.zenoh-wss.yml up -d zenohd zenohd-wss
```

- 待ち受けは `wss://<ZENOH_WSS_HOST>:7450`。ページには `ws/<ZENOH_WSS_HOST>:7450` と書く (https のページなら
  wss:// になる)。
- `ZENOH_WSS_HOST` は必須で、証明書の名前にもなる。ループバックの名前 (localhost など) にすると、ページが
  `ws://` を選んでしまい中継を通らない。同じ PC で試すなら、hosts などで別の名前をこの PC に向ける。
- 公開の範囲は `ZENOH_WSS_BIND` (既定 127.0.0.1)。ほかの PC から使うなら 0.0.0.0 にする (ルータに認証は無いので、
  信用できる網の中だけ)。中継先は `ZENOH_WSS_UPSTREAM` (既定 `zenohd:7449`)。
- 証明書は Caddy が自分の CA で作る (手元の確かめ用。volume `zenohd-wss-data` に残るので起動し直しても同じ)。
  ブラウザは WebSocket の証明書の警告を出さずに断るだけなので、先に同じブラウザで `https://<host>:7450/` を開いて
  証明書を許す (許すと「this browser now accepts the certificate」と出る)。または Caddy の CA
  (`docker cp fmruby_zenohd_wss:/data/caddy/pki/authorities/local/root.crt .`) を信頼に入れる。

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

### 4.1 公開の版と同じ作りを https で配る

公開の版は `rake wasm:dist` が作る wasm/build/dist (doc/wasm/report/hosting.md)。手元で https で確かめるときは、
`rake wasm:serve DIST=1` (http、drive の中継つき) の前に TLS を終える proxy を置く。例 (Caddy、自前の CA の証明書):

```
rake wasm:dist
DIST=1 rake wasm:serve          # http://localhost:8006/ (dist)
# 別の端末で。Caddyfile:
#   { local_certs
#     skip_install_trust }
#   https://localhost:8443 {
#     tls internal
#     reverse_proxy host.docker.internal:8006 { header_up Host {upstream_hostport} }
#   }
docker run --rm -p 127.0.0.1:8443:8443 -v $PWD/caddy:/etc/caddy:ro caddy:2-alpine
```

`https://localhost:8443/index.html?app=/app/test/asterism_demo.app.rb` を開く (証明書を許す)。COOP/COEP は
wasm:serve のものがそのまま通る。`?drive=1` も同じ中継で動くので、tools/fmrb_web.rb (http の 8006 に話す) で
操作できる (ブラウザは自分で起こす。fmrb_web.rb の `up` は http の URL を開く)。

## 5. ルータの場所とノードの名前の決まり方

試しのアプリ (asterism_demo、rover_cam、rover_pilot、rover_brain) は、ルータの場所を次の順に探す。

1. `/home/zenoh_echo.txt` の 1 行目 (使う人が置く。ブラウザでは IndexedDB に残る)
2. 設定 `zenoh_router` (system_conf.toml)。ブラウザ版の束は `ws/localhost:7449` を持つ
   (config/system_conf_wasm.toml)。ページの設定 (下) で変えられ、URL の `?zenoh=ws/<host>:<port>` が
   その回だけ置き換える (`?zenoh=ws/192.0.2.10:7449`)
3. `tcp/zenohd:7447` (sim の既定。ブラウザではつながらない)

sim・端末の system_conf には `zenoh_router` が無いので、これまでどおり 1 と 3 で決まる。

### 5.1 ページの設定: Network の枠の Router

- ページの下の枠 Network の Router に `ws/<host>:<port>` を書いて Enter (または欄の外を押す) で保存される。
  「saved; used from the next start」と出て、次の起動から使われる (動いている機械のつなぎ先は変えない)。
  空にして保存すると束の値 (`ws/localhost:7449`) に戻る。通る文字は `ws/` に続く英数字と `_ . : [ ] -` だけ
  (違えば「not saved」)。
- 保存先は Config の設定と同じ店 (localStorage の `fmrb_web_conf`、page の CONF_KEYS)。起動のたびに
  `--fmrb-conf=zenoh_router=...` で機械に渡る。機械の中で system_conf.toml の `zenoh_router` を直しても、
  ほかの鍵と同じく次の起動に持ち越される。
- `?zenoh=` はその回だけ。保存はされず、保存してある Router も書き換えない。開いている間は枠に
  「this visit: <値> (from the URL)」と出る。
- 束の版が変わると (公開の版を出し直すと)、ほかの設定と同じく Router も束の既定に戻る (CONF_KEYS の決まり)。
- Erase で消え、Download (バックアップ) には入る。

### 5.2 ノードの ID

順に: `/home/asterism_node.txt` の 1 行目 → 端末の mDNS の名前 → 設定 `asterism_node` → 機種の名前
(`FmrbConst::BOARD`: sim は `linux`、ページを通さないブラウザ版は `wasm`)。

ブラウザでは、ページが最初の訪問で ID を作る。

- 形は `wasm-<16 進 6 桁>` (例 `wasm-1a2b3c`。端末の `fmruby-<16 進 6 桁>` に合わせた)。asterism-console は
  この形を端末 (▣) とみなす。ROS 2 のノード名では `-` が `_` になる (`fmruby_pilot_wasm_1a2b3c`)。
- localStorage の `fmrb_web_node` に置き、以後は同じ ID。起動のたびに `--fmrb-conf=asterism_node=...` で渡る。
  Network の枠の見出しの右に `node wasm-1a2b3c` と出る。
- ブラウザの持ち物なので、Erase では消えず (端末が初期化しても mDNS の名前が変わらないのと同じ)、
  バックアップにも入らない (別のブラウザに戻すと同じ名前が 2 つになるため)。
- 1 つのブラウザ (同じ種類・同じプロファイル) で同じ ID が 2 つ網に入らないよう、/home と同じ Web Locks
  (`fmrb-home`) で決める。**2 つ目のタブは、保存しない使い捨ての ID** (毎回新しい `wasm-<16 進 6 桁>`) で網に入る。
  2 つ目のタブは /home も保存されない使い捨ての機械なので、ID もそれに揃えた。枠には
  `node wasm-xxxxxx (this tab only)` と出る。1 つ目のタブを閉じた後に開いたタブは、保存してある ID を使う。
- 別のブラウザ・別のプロファイル・シークレットウィンドウは、それぞれ別の ID になる。保存を断るブラウザでは
  タブの間だけの ID になる (枠に `not kept` と出る)。
- 同じ ID の重複 (同じ店を共有する 2 つの機械、`/home/asterism_node.txt` の書き間違い) を網の上で確かめる
  仕組みは無い。

## 6. PC から呼ぶ

親リポジトリで。

```
ruby tools/fmrb_zenoh.rb call wasm-1a2b3c/demo/info status
ruby tools/fmrb_zenoh.rb call wasm-1a2b3c/demo/screen say '"hello"'
ruby tools/fmrb_zenoh.rb meta 'wasm-1a2b3c/demo/*'
```

`wasm-1a2b3c` はページの Network の枠に出る ID (5.2)。asterism-console の図にはそのノード (アプリ demo と
オブジェクト 3 つの入れ子) が ▣ で出る。console から呼ぶには、Call permissions で node `wasm-*` / app `demo` の
許可を足す。

## 7. 台車を操縦する

MuJoCo の台車 (S1 の compose、親の docker/mujoco/README.md) を起動した上で、`?app=/app/test/rover_cam.app.rb` で開く。
頭脳を通す形 (S4) は `?app=/app/test/rover_pilot.app.rb` (親の README の S4 の節)。ノード名は 5.2 の ID になる。
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

## 9. できないこと・気をつけること

- peer モード (listen) は使えない (client だけ)。
- 7449 は 127.0.0.1 にだけ公開しているので、`ws://` で直接つなぐページは同じ PC のブラウザから開く。
  ほかの PC のブラウザは 2.1 の中継を使う。
- 公開の版 (インターネットのオリジン) から手元のルータ (ループバック・LAN) につなぐと、Chrome は
  「ローカル ネットワークへのアクセス」の許可を求める (Local Network Access。許可しないとつながらない)。
  report/b2.md 2.3。
- http のページから wss:// の中継を指すことはできない (http のページは常に ws:// を選ぶ)。
- ロケータに wss を明示する書き方は無い。ページの https/http と相手がループバックかどうかだけで決まる。
