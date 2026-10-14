# ブラウザ版を網に入れる手順

> 状態: 進行中 | 更新: 2026-10-14 | ブラウザ版 (wasm) の Family mruby を Asterism のノードとして zenohd につなぐ手順。zenohd の ws/ の待ち受けと wss:// の中継、ブラウザ版のビルドと起動 (手元の http の版と、公開の版と同じ作りを https で配る形)、ルータの設定、ノードの ID の決まり、合言葉の付いた wss:// の入口 (G1)、部屋のリンク (G2)、PC からの呼び出し、台車の操縦、最小の試しのページ

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

親リポジトリ (family-mruby) で、docker/asterism/docker-compose.zenoh-ws.yml を重ねて zenohd を起動し直す。

```
docker compose -f docker-compose.yml -f docker/asterism/docker-compose.zenoh-ws.yml up -d zenohd
```

- 待ち受けは tcp/7447 (従来) と ws/7449。7449 は 127.0.0.1 にだけ公開する。
- 端末を LAN からつなぐときも一緒に使うなら、docker/asterism/docker-compose.zenoh-lan.yml の**後に**置く
  (zenoh-lan.yml がポートの一覧を置き換えるため、前に置くと 7449 が消える)。

```
docker compose -f docker-compose.yml -f docker/asterism/docker-compose.zenoh-lan.yml \
  -f docker/asterism/docker-compose.zenoh-ws.yml up -d zenohd
```

- docker/asterism/docker-compose.relay.yml (zenohd に設定ファイルを渡す形) とは一緒に使わない。
- 確かめ: `docker logs fmruby_zenohd | grep listen` に `ws/[::]:7449` が出る。
- sim_up は zenohd を docker-compose.yml だけで作り直すので、sim を上げた後は上の行をもう一度打つ。

### 2.1 wss:// の中継 (この PC 以外のブラウザ、https のページ)

https のページは、ループバック以外へは `ws://` を開けない (WebSocket を作るところで SecurityError)。そこで
TLS を終えて zenohd の `ws/` に流す中継を前に置く。親リポジトリの docker/asterism/docker-compose.zenoh-wss.yml (Caddy)。

```
ZENOH_WSS_HOST=<この PC の名前> docker compose -f docker-compose.yml \
  -f docker/asterism/docker-compose.zenoh-lan.yml -f docker/asterism/docker-compose.zenoh-ws.yml \
  -f docker/asterism/docker-compose.zenoh-wss.yml up -d zenohd zenohd-wss
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

Asterism の試しのアプリ (asterism_demo、othello、rover_*、ros2_*、rt_probe、zenoh_*) は、接続の設定を
`FmrbAsterism.settings` (lib/add/picoruby-fmrb-asterism) の 1 か所から受け取る (CF1、report/cf1.md)。
設定のファイルは `/home/asterism/` の下にまとめてある (ブラウザでは IndexedDB に残る)。

```
/home/asterism/config.toml     router = "tls/203.0.113.5:7447"   ルータの場所
                               node   = "alice"                  ノードの ID (任意)
                               name   = "Alice"                  機械の名前 (プロフィールの name)
/home/asterism/<名前>.tar      console が渡す証明書の tar (置いたまま。G2a)
```

- `config.toml` は書いた鍵だけが効く。どれも省いてよい。値は `"` で囲む (`\"` と `\\` だけ戻す)。
- 開発用の遠隔の道具 (`/fs/get`) は `/home/asterism/` の下を読まない (鍵があるため)。置く (`put`)・一覧・
  消すことはできる。
- **CF1 より前のファイル** (`/home/zenoh_echo.txt` の 1 行目 = ルータ、`/home/asterism_node.txt` の 1 行目 =
  ノードの ID、`/home/asterism_profile.toml` の `name`) は、`config.toml` が無いときに 1 回だけ読み、
  `config.toml` に書き写し、読み直して同じなら消す。`config.toml` ができた後に古いファイルを置いても読まない
  (読まないことをログに 1 行出す)。

ルータの場所は次の順に探す。

1. `config.toml` の `router`
2. 設定 `zenoh_router` (system_conf.toml)。ブラウザ版の束は `ws/localhost:7449` を持つ
   (config/system_conf_wasm.toml)。ページの設定 (下) で変えられ、URL の `?zenoh=ws/<host>:<port>` が
   その回だけ置き換える (`?zenoh=ws/192.0.2.10:7449`)
3. 証明書の tar の `asterism.json` の `router` (tar だけ置けば、そのルータへ TLS で直接繋がる)
4. `tcp/zenohd:7447` (sim の既定。ブラウザではつながらない)

sim・端末の system_conf には `zenoh_router` が無いので、1・3・4 で決まる。ブラウザ版は 2 が tar より先に
効く (ブラウザ版は TLS の口を持たず、ページの決めたルータを使う)。

ルータが `tls/` なら、`/home/asterism/` の tar (2 つ以上なら名前順で最初) の証明書で mTLS で繋ぐ。tar が無い・
読めないときは、繋ぐときにアプリがその理由を出す。`tcp/` や `ws/` のルータでは tar は使わない。

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

順に: `config.toml` の `node` → 端末の mDNS の名前 → 設定 `asterism_node` → 機種の名前
(`FmrbConst::BOARD`: sim は `linux`、ページを通さないブラウザ版は `wasm`)。証明書の tar で TLS で繋ぐときは、
証明書の名前がノードの ID になる (ルータの ACL はその名前で機械を見分けるため。違う `node` を書いてもこちらが勝ち、
ログに 1 行出る)。

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
- 同じ ID の重複 (同じ店を共有する 2 つの機械、`config.toml` の `node` の書き間違い) を網の上で確かめる
  仕組みは無い。
- **リンクが ID を決めることがある (G1.5)**。asterism-console の「Open the browser build」で開いたリンクは、
  フラグメントに `zenoh_node=<ID>` を持つ (ユーザに結び付いた `wasm-<名前>`、2 つ目は `wasm-<名前>_2`)。その回は
  どのタブでもこの ID を使い、保存しない (保存してある ID はそのまま。リンクなしで開けば元の ID)。枠には
  `node wasm-alice (from the link, this visit)` と出る。ノードの ID に使えない形 (`[A-Za-z0-9][A-Za-z0-9._-]{0,62}`
  に合わない) は無視する。`config.toml` に `node` があればそちらが勝つ (アプリの決まりの順のまま)。
- **合言葉があってノードの ID が無いリンクは断る (2026-10-14)**。console の管理する網では、ID は console が
  アカウントのハンドルから決め、console のリンクはどれも合言葉とノードの ID を両方持つ (asterism-console 0.1.2)。
  合言葉だけのリンク (手で作ったもの、古い console のもの。古い形の `?zenoh_pass=` も同じ) で開くと、ページは
  機械を起動せず (合言葉も使わない)、画面に「console から開き直して」と出す。Network の枠は
  `node: none (the link has a pass but no node ID; not connecting)`。合言葉の無いページ (console の無い網: 家の
  LAN の zenohd、手元のルータ、公開の studio) は今どおり自分の `wasm-<16 進 6 桁>` を使う
  (report/console_node.md)。
- **部屋のリンクでは、ブラウザ自身の ID を使う (G2、2026-10-14)**。部屋にはアカウントが無く、部屋の全員が同じ鍵を
  持つので、ID を console が決めて守ることはできない。ID は部屋の中で重ならなければよいので、合言葉の無いページと
  同じ `wasm-<16 進 6 桁>` (保存してある ID、2 つ目のタブは使い捨て) にした。部屋の鍵はキーの前置き
  `room/<枠>` を付けるので、console の網の `wasm-<ハンドル>` とは線の上でも ACL の上でも別の場所になり、ぶつからない。
  リンクに `zenoh_node` があっても部屋では使わない。

## 5.3 合言葉 (wss:// の入口、G1)

インターネットのルータ (G0 の `wss://` の入口) は、WebSocket を開くときに合言葉を確かめる。合言葉は asterism-console の
Browser passes で作り、ブラウザ版を開くリンクに入っている。

```
https://<ブラウザ版の置き場>/index.html?zenoh=ws/router.example.org:7443#zenoh_pass=<合言葉>&zenoh_node=<ID>&app=
```

(ノードの ID の無いリンクは断る。5 章の最後の項目。)

- 合言葉はフラグメント (`#` の後) に置く。フラグメントはページを配るサーバに送られないので、そのサーバの記録に
  残らない。古い形の `?zenoh_pass=` も読むが、こちらはページを配るサーバの記録 (要求の行) に残る。
- ページの最初のスクリプトが合言葉を読み、すぐアドレスの欄から消す (履歴の項目を置き換えるので、戻るでも
  出てこない)。持つのはその回のメモリだけで、保存しない (localStorage・IndexedDB・system_conf.toml に入らない)。
  ページの記録にも出さない。Network の枠に「pass: set for this visit」と出る。
- 初めての訪問で coi-serviceworker がページを読み直すときだけ、合言葉をフラグメントに戻して読み直し、読み直した
  ページがまた消す。
- 合言葉を付けるのは `wss://` を選んだときだけ (`wss://<host>:<port>/?pass=<合言葉>`)。`ws://` (手元・ループバック) には
  付けない。入口の nginx は確かめた後、合言葉を外してルータへ渡す。
- 合言葉は 1 つの開いたページのもの (G1.5b、report/g1_5b.md)。ページは読み込んだときに自分の印 (乱数) を作り、
  合言葉と一緒に送る (`wss://<host>:<port>/?pass=<合言葉>&page=<印>`、印も記録に残さない)。最初に通ったページが
  合言葉を持ち、そのページは合言葉が有効な間 (最初の接続から 4 時間) つなぎ直せる。別のページ・別のブラウザ・
  読み直したページは断られる。入口は合言葉が切れる時刻に接続を切る。切れたら console で新しいリンクを開き直す。
- 切れたら、ページは自分でつなぎ直す (zenoh-pico が 1 秒ごとに試し、ページの WebSocket が失敗の後の間隔を 1・2・4・8・
  16 秒、その後 30 秒ごとに空ける)。合言葉のあるページは 10 回続けて失敗すると諦め、状態の行にそう出す。
- 「Open the browser build」(G1.5) のリンクは、合言葉と一緒にノードの ID と開くアプリもフラグメントで渡す
  (`#zenoh_pass=...&zenoh_node=wasm-alice&app=/app/test/othello.app.rb`)。ページは 3 つとも読んだらアドレスの欄から
  消す。`#app=` は `?app=` と同じ働きで、両方あれば `?app=` が勝つ。リンクの形は console の設定
  `ASTERISM_BROWSER_PAGE` (asterism-console の docs/g1_5.md)。
- G1b (asterism 0.6.0) から、試しのアプリ (othello・rt_probe・rover_brain・rover_pilot) は接続を 1 本しか開かない
  (`Asterism.session`) ので、合言葉 1 つで入れる。
- 1 つのページが 2 つの接続を開くアプリ (Asterism と別のセッションを持つ othello・rt_probe・rover_pilot) は、
  2 つ目が断られる (report/g1.md)。

## 5.4 部屋のリンク (G2)

asterism-console の管理者が Admin > Rooms で部屋を作ると、リンクが出る (console の設定 `ASTERISM_ROOM_PAGE`)。

```
https://<ブラウザ版の置き場>/index.html?zenoh=ws/router.example.org:7443#room=<部屋の鍵>&app=/app/test/othello.app.rb
```

- 部屋の鍵は console が signer に頼んで署名したもの (asterism-console の docs/rooms.md)。フラグメントに置くので、ページを
  配るサーバの記録に残らない。合言葉と同じく、最初のスクリプトが読んでアドレスの欄から消し、保存しない。
- ページは鍵の中身 (署名は確かめない。確かめるのは door) から枠と期限を読み、`wss://<host>:<port>/room?t=<鍵>` で部屋の
  入口 (door) へつなぐ (`wss://` のときだけ。合言葉は付けない)。ページのすべてのセッションのキーに前置き
  `room/<枠>` が付く (picoruby-asterism-zenoh の名前空間。`Module.zenohNamespace` から
  `picoruby_zenoh_default_namespace()` で渡す)。アプリは今のキー (`asterism/...`) のまま部屋の中だけで動く。
- Network の枠の見出しの下に `Room 01 until <時刻>: keys under room/01/, ...`、状態の行に `running in room 01 (until ...)` と
  出る。
- 期限の過ぎた鍵・読めない鍵のリンクは、機械を起動せず「This room has ended」「This room link is broken」と出す。
- 部屋が閉じられる・期限が来ると、door が接続を切り、つなぎ直しは 410 で断られる。ページは 10 回続けて失敗すると
  諦め、状態の行に「the room may have closed or ended」と出す。
- ノードの ID は 5.2 の最後の項目のとおり、ブラウザ自身の `wasm-<16 進 6 桁>`。

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
