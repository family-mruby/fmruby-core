# G2b 報告: 部屋の席の証明書とゲームのホストの証明書

> 状態: 完了 (検収待ち) | 更新: 2026-10-16 | 部屋 (G2) では全員が同じ枠の証明書で door を通るため、zenohd の ACL は部屋の全員を 1 つの主体として扱っていた。G2b で、枠ごとに 16 席の証明書を作り、door が秘密から席を割り当て、席の証明書で zenohd に繋ぐ形にした。ルータが「自分の ID の下にだけ書ける・自分の名前でだけ呼べる」を部屋の中のページごとに守る。ゲームのホストは自分の証明書 `games` で部屋に入る。part 1 (asterism-console 0.3.0・asterism-games 0.5.0・asterism の文書) は CRuby の受け入れ 48 項目、part 2 (ブラウザ版) は手元の構成でブラウザ版のページを使って確認した。古いページ・古いサーバには対応しない (ユーザ決定)

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-console | main | `c9bd672` console 0.3.0 | 席の証明書と ACL、door の席の割り当て、ゲームのホストの部屋の規則、nginx・Caddy・compose、文書 (part 1) |
| asterism-console | main | `41eafac` | door: 断った WebSocket を閉じる前に、ページが送っていたものを読む (nginx の記録に鍵が残る件。4.2) (part 2) |
| asterism-games | main | `6644cf5` games 0.5.0 | 部屋のホストは自分の証明書で入る、席の ID を信じる、ボット (part 1) |
| asterism | main | `c04faf0` | protocol.md 4.7 に席の段落 (part 1) |
| fmruby-core | feature/g2b-browser (develop ce1d5562 から) | この報告と同じコミット | ブラウザ版の席 (main.js・zp_ws_emscripten.c)、卓のアプリの秘密を外す、browser.md・plan.md・この報告 (part 2) |

picoruby-asterism-zenoh は変えていない (ブラウザ版の ws の輸送は fmruby-core の `wasm/zenoh/zp_ws_emscripten.c`)。

## 2. 決めたこと (2026-10-16 ユーザ)

- 席の証明書: 枠ごとに 16 席 (`room-NN-s01` .. `room-NN-s16`)。席のノード ID は `wasm-<枠><席>` (`wasm-0103`)。
- ゲームのホストは自分の証明書 (`games`) で部屋に入る。枠の証明書からはホストの名前に書けない。
- 席の渡し直し: 30 分 (`WS_DOOR_SEAT_IDLE=1800`) 接続の無い席は、空きが無いときに新しいページへ渡す。
- 席のページの `@profile` は部屋の中で許す。
- 古いページ (席より前のブラウザ版)・古いサーバ (席の無い console) には対応しない。`WS_DOOR_ROOM_LEGACY` は 0 のまま。

## 3. part 1: asterism-console 0.3.0・asterism-games 0.5.0 (fmruby-core の外)

### 3.1 asterism-console 0.3.0

- 設定: `ASTERISM_ROOM_SEATS` (既定 16、0..99)、`ASTERISM_ROOM_HOSTS` (既定 `games`、`-` で無し)。席のノード ID は `ASTERISM_BROWSER_NODE_PREFIX` + 枠 + 席。
- 証明書: `bin/asterism setup` が枠ごとに席の証明書を作る (鍵はサーバの上で作り `/rooms/certs/` に置く。.tar は作らない)。8 枠で 128 枚、setup は数秒。
- ACL (`Relay::CloudConfig`):
  - 席: 部屋の中を読む (購読・生存・get、`room/NN/**`・`@adv`・`asterism/*/@profile`) と受け取るのは自由。書く (put・delete・queryable・token・reply) のは `room/NN/asterism/<自分>` の下だけ (`@profile`・`@adv` を含む)。呼び出しは 1 つの証明書に主体 2 つ (CN だけの主体で `call/**` を deny、CN + `link_protocols: ["tls"]` の主体で `call/<自分>` を allow)。1 つの主体の中では deny が勝ち、主体どうしでは allow が勝つため。
  - 枠の証明書 `room-NN`: 席のノードとホストの名前 (`games$*`) への書き込みと、それらを名乗る呼び出しの deny を足した。door は使わない (`WS_DOOR_ROOM_LEGACY=1` のときだけ)。管理者の試験用に残した。
  - ホスト (`ASTERISM_ROOM_HOSTS` の行): `room/*/asterism/games$*` の下の書き込み、部屋の読み・受け取り、`call/games$*`・meta・profile の get。ボットは `games-bot...` の名前で同じ規則に入る。
- rooms.json: 開いている部屋ごとに `"seats"` (生成済みの cloud.json5 が入れている席の数)、上に `"seat_node_prefix"`・`"hosts"`。
- door (`lib/relay/room_door.rb`):
  - `POST /room/seat` (本文 `{"t": 鍵, "s": 秘密 32 桁}`、CORS の事前確認にも答える、`Cache-Control: no-store`)。答えは `{"node": "wasm-0103", "seat": "03"}`。同じ秘密は部屋が終わるまで同じ席。空きが無ければ 1800 秒接続の無い席のうち最も長いものを渡す。それも無ければ 409、席の無い枠は 404 `{"error":"no seats"}`、鍵は WebSocket と同じ確認 (403・410・429)。
  - WebSocket: 席のある部屋では 101 を返してから最初の 1 通を 5 秒待つ。Text `{"s": 秘密}` で席を引き、席の証明書で zenohd に繋ぐ。閉じる符号: 4403 (その秘密に席が無い)、4408 (時間内に秘密が来ない)、4426 (Binary が先 = 0.3.0 より前のページ)、1013 (zenohd が居ない)。
  - 席の表: `WS_DOOR_SEATS_FILE` (`/seats/seats.json`、door の volume `seats`、0600)。部屋ごとに SHA-256("部屋 ID:秘密") と席と最後の使用時刻だけ。door を起動し直しても戻る。部屋が終わると 1 秒以内に消す。
- 画面: 登録の一覧は席の行を 1 行にまとめ、Apply の画面は枠ごとの数で出し、グラフの登録の重ね合わせから席の行を外した。
- 配備: nginx `location = /room/seat` (POST と OPTIONS だけ、`limit_req zone=asterism_room`、`client_max_body_size 2k`)、Caddy の `@seat`、compose の door に `seats` volume と環境変数 3 つ、install.md「From 0.2.3 to 0.3.0」。
- 試験: console の試験 414 件・rubocop 0 件 (part 2 の直しの後は 415 件)。

### 3.2 asterism-games 0.5.0

- 部屋のホストは、rooms.json の `"hosts"` に自分のノードがあれば自分の証明書と namespace `room/NN` で入る。無ければ (0.3.0 より前の console) 枠の .tar で今どおり。
- 呼び出し元の信頼: 席のある枠では、席のノード ID とホストの名前で始まる ID (ボット) を秘密なしで受ける。それ以外 (旧ページの `wasm-<6 桁>`) は `rooms.secret_ids` (既定 true) の間は秘密で確認する。
- 席の渡し直し: 席の ID が 1800 秒呼んでいなければ新しいページとみなし、古い会員を leave させてから入れる (新しいページに古い手牌は渡らない)。
- ボット: `bin/mahjong-bots --credentials games.tar --namespace room/01` で `games-bot<pid>-<i>` を名乗る。
- 試験 141 件・失敗 0 (skip 1 は既存のネットワーク試験)。

### 3.3 part 1 の受け入れ (deploy/local、zenohd 1.10.1 mTLS)

ブラウザの代わりに、zenoh-c (asterism-zenoh 0.4.1) の TCP を WebSocket に包み直す橋を使った (nginx → door → 席の証明書 → zenohd の道はブラウザと同じ)。

| 区分 | 項目数 | 結果 |
|---|---|---|
| 線の上 | 24 | 全部合格 |
| Asterism の上 | 8 | 全部合格 |
| 席の割り当て | 14 | 全部合格 |
| 移行の door (`WS_DOOR_ROOM_LEGACY=1`) | 2 | 全部合格 |

主なもの (g2b の調べの受け入れ条件 5.2 の番号):

- 5.2-1 席 A が `call/<A>` で呼ぶと B だけが受けて返事が戻る。`call/<B>` (名乗りの偽り)・`call/games` は誰にも届かない。
- 5.2-2 他人の場所・卓・`asterism/<自分>` の外への put・delete・token・queryable、他人のキーでの返事はすべて落ちる。偽の卓の queryable は何も受けず、本物の卓だけが答える。
- 5.2-3 `call/*`・`call/**` は誰にも届かない。`**` の get は受け手に届くだけで Asterism は返事しない。
- 5.2-4 自分の前置きを宣言して suffix で他人の場所へ連結した put は落ちる。
- 5.2-5 220 KB の put (分割される) は自分の場所なら届き、他人の場所では落ちる。
- 5.2-6 同じ秘密は別のリンク・door の起動し直しの後も同じ席。待ち時間 20 秒の試験で、接続の無い席だけが 17 人目に渡る。
- 5.2-7 席の無い秘密の WebSocket は 4403 で閉じ、zenohd には何も行かない。旧ページは 4426。
- 5.2-8 16 席が埋まると 409。部屋を閉じると席の表から消え (16 → 0)、接続が切れ、席の要求は 410。
- 5.2-9 nginx (access・error)・door・console のログに、使った秘密 19 個・その SHA-256・`"s":` は 0 回。
- 5.2-10 席のページが別の席のページを呼び、`current_call.from` が呼んだ側の席 ID になる。
- 5.2-11 席のページは部屋の卓に秘密なしで join でき、mine で同じ会員が戻る。参加者が卓のキーに put・token を出しても卓は乗っ取れない。
- 部屋の卓とボット: games.tar のボット 3 人 + CPU で東風戦を 1 局最後まで (206 手)。
- 5.2-12 console のグラフに部屋の席のノードが出る。

zenohd の大きさと起動 (8 枠 × 16 席):

| | 席なし | 席あり |
|---|---|---|
| 設定 | 19.4 KB | 197 KB (規則 338、主体 269、方針 269) |
| 最初のログから待ち受けまで | 5-7 ms | 24-26 ms (3 回) |
| 常駐 (VmRSS) | 12.0 MB | 17.5 MB |

door は試験の後で 15-16 MiB。

part 1 の気づき: ホストの規則のキーは `games*` ではなく `games$*` と書く必要がある (`*` は区切り 1 つ分)。席の渡し直しで卓が古い会員の token と手牌を新しいページに返すため、卓の側で長く呼んでいない席の会員を leave させる形にした。

## 4. part 2: ブラウザ版 (fmruby-core)

### 4.1 変えたこと

- `wasm/web/main.js`:
  - 部屋では、機械を起動する前に `POST https://<host>:<port>/room/seat` で席を取る (本文 `{"t": 鍵, "s": 秘密}`、`Content-Type: text/plain`、`cache: "no-store"`、`referrerPolicy: "no-referrer"`、`credentials: "omit"`)。宛先はリンクのルータ (`?zenoh=`、無ければ Network の枠の設定) を、WebSocket の輸送と同じく読み替えたもの。答えの `node` を `asterism_node` にして起動する。答えの形 `^<前置き><枠><席>$` と、枠がリンクの鍵と合うことを確認する (前置きは答えの ID から読むので、console の `ASTERISM_BROWSER_NODE_PREFIX` を変えても通る)。
  - 秘密は今の `fmrb_room_secret_<部屋の ID>` (localStorage) をそのまま使う。席を取るため以外には使わない。SHA-256 から ID を作る部分は外した (SHA-256 の関数も使わなくなったので消した)。
  - 席を取れないときは機械を起動せず、画面の中と状態の行に理由を出す: 409「Every seat in this room is taken.」(クリックで取り直す)、410「This room has ended or was closed. Ask for a new link.」、403「The room's door refused this link.」、429、404・405「The room's server is too old for this page (it has no seats).」、届かない「The room's router could not be reached.」(クリックで取り直す)。`https://` でないページ・手元のルータでは部屋を使えないと出す (輸送が door を通るのは `wss://` のときだけのため)。古いサーバへの戻り (秘密から ID を作る形) は作らない (決定)。
  - Network の枠: `node wasm-0103 (seat 03 in the room)`。起動前は `node: the room gives a seat at start`。2 つ目のタブは `(this tab only)`。
  - 4403 を受けたとき (`Module.zenohReseat`): 席を取り直す。同じ ID ならつなぎ直しを続ける。別の ID なら動いている機械はその ID を使えないので、つなぎ直しをやめて「the room's door no longer holds this page's seat. Reload the page to take a seat again.」と出す。取り直しの間は `Module.zenohSeatWait` で試しを止め、やめるときは `Module.zenohRoomStop`。
  - `/flash/etc/asterism_room_secret` への受け渡しを外した (卓のアプリのほかに使うものが無い)。
- `wasm/zenoh/zp_ws_emscripten.c`:
  - 部屋の WebSocket (`/room?t=` を付けたとき) は onopen で、状態を「開いた」にする前に Text `{"s":"<秘密>"}` を 1 通送る。秘密は `Module.zenohRoomSecret` から読み、URL にも記録にも出さない。
  - onclose で符号 4403 なら `Module.zenohReseat` を呼ぶ。1013・4408 は今どおりつなぎ直す。
  - door は 101 を返してから断るので、開いたが何も返ってこないうちに閉じた WebSocket を失敗の 1 回に数えるようにした (失敗の数を戻すのは最初の 1 通を受けたとき)。これが無いと、zenohd が居ない間は zenoh-pico が 1 秒ごとに試し続け、間隔 (1・2・4・… 30 秒) と 10 回で諦める決まりが効かなかった。
- `flash/app/game/mahjong/`: 部屋の `join` / `mine` に秘密を付けない (`load_room_secret` を外した)。家の LAN・永続の卓の道はもともと秘密を付けないので変わらない。`tools/mahjong_overflow/check.rb` の状態から `room_secret` を外した。
- `doc/ruby_asterism/browser.md` 5.2・5.4 を席の形に書き直した。

### 4.2 asterism-console の直し (41eafac)

部屋の WebSocket を door が 101 の後に断ると (4403・4408・4426・1013)、ページが秘密のすぐ後に送った最初の 1 まとまりが読まれないまま socket が閉じられ、TCP がリセットになっていた。nginx は upgrade した接続のリセットを error log に要求の行ごと書くので、部屋の鍵 (`/room?t=`) が残る。ブラウザ版の試験で zenohd を止めたときに 2 行出て分かった。door は close の 1 通の後、最大 2 秒 (0.3 秒何も来なければ終わり) ページから来るものを読んで捨ててから閉じるようにした (FIN で閉じる)。試験を 1 本足した (直す前はリセット、直した後は EOF。console の試験 415 件・rubocop 0 件)。直した後、同じ操作 (zenohd を 15 秒止める、1013 で 2 回断られる) で nginx の error log は増えなかった。

### 4.3 受け入れ (deploy/local、zenohd 1.10.1 mTLS、ブラウザ版のページ)

構成: asterism-console の deploy/local (nginx 1.27・door・signer・zenohd 1.10.1・console・bridge) を c9bd672 (直しの後は 41eafac) から作った image で動かし、`PLAY_DIST` にこのブランチの `rake wasm:dist` を置いた。部屋 01 (麻雀を開く) と 02 (オセロを開く) を作り、asterism-games 6644cf5 のホストを自分の証明書 `games` で手元から動かした (部屋 01 に卓 mahjong1)。

ブラウザ: web_* の道具は使えなかった。開発サーバのページは `http://localhost` で、輸送が door を通るのは `https://` のページからの `wss://` だけなので部屋に入れず、drive の命令はページと同じ origin の開発サーバへ取りに行くので `https://play.localtest` のページは動かせない。代わりに G2 Step 2 と同じく docker の Playwright 1.56 (Chromium) を rehearsal の web の網に置き、ブラウザの文脈 (別の localStorage) ごとに別のブラウザとして扱った。キーとクリックはページの canvas へ送った。

| 項目 | 結果 |
|---|---|
| 5.2-6 2 つのブラウザが同じ部屋のリンクで別の席 | 合格。`wasm-0101`・`wasm-0102` (後の回は `wasm-0103`・`wasm-0104`)。席の POST は `Content-Type: text/plain`、Referer なし |
| 5.2-6 読み直しで同じ席 | 合格。読み直した後も `wasm-0101` (`wasm-0103`) |
| 5.2-6 同じブラウザの 2 つ目のタブは別の席 | 合格。`wasm-0103` (`wasm-0105`)、`(this tab only)` |
| 5.2-6 door の起動し直しの後も同じ席 | 合格。開いていたページは 12 秒で同じ席 (seat 06) に戻った。最初の 1 通はどの回も Text |
| 最初の 1 通 | 部屋の WebSocket は毎回最初が Text (秘密)、URL に 32 桁の 16 進は 0 回 |
| 5.2-11 麻雀 (2 つのページ + CPU 2) | 合格。名前を入れて join (秘密なし、30 ms)、席 1・2 に着き、CPU を入れて開始、それぞれ 3 回ずつ打牌 (キーで)。卓の会員と席はページの席の ID (`wasm-0104`・`wasm-0105`) |
| 5.2-10 オセロ (部屋 02 の 2 つのページ) | 合格。誘って受け、8 手 (`d3c3b3b2b1a1c4c1`)、手の答え 53-55 ms。受けた側の記録の呼んだ側は相手の席の ID (`wasm-0201 played d3`) |
| 名乗りの偽り (席の答えを途中で書き換え、seat 08 の証明書で `wasm-0201` を名乗るページ) | 合格。相手 (`wasm-0206`) の一覧に出ず、偽のページから相手への呼び出しは全部 Timeout、誘いは届かない。本物の席のページ (`wasm-0207`) の誘いは届いた |
| 同じく、使用中の席 (`wasm-0204`) を名乗るページ | Asterism の重複の確認 (`wasm-0204/game is already on the network`) で起動しない |
| 409 (16 席が埋まった) | 合格。「Every seat in this room is taken. Click to try again.」、機械は起動しない |
| 410 (部屋を閉じた後) | 合格。「This room has ended or was closed. Ask for a new link.」 |
| 403 (署名を書き換えた鍵) | 合格。「The room's door refused this link. Ask for the link again.」 |
| 404・405 (答えを途中で差し替え) | 合格。「The room's server is too old for this page (it has no seats).」 |
| 1013 (zenohd を 12-15 秒止める) | 合格。door が 1013 で 2 回断り、間隔を空けて (5・8 秒) 試し、zenohd が戻ると同じ席で繋がった |
| 4403 (door の席の表を空にして起動し直す) | 合格。4403 → 席を取り直す → 別の席 (01) になったので、つなぎ直しをやめて「Reload the page to take a seat again.」。その後 WebSocket は開かない |
| 5.2-9 記録に秘密が無い | 合格。nginx (access・error)・door・console・bridge・zenohd・ゲームホストの記録に、使った秘密 13 個は 0 回、`"s":` も 0 回。部屋の鍵は error log に 2 行あり、どちらも直す前の 1013 のリセット (4.2) |

画面: `C:\Users\Public\asterism_deploy\screenshots\g2b\` (席・麻雀・オセロ・名乗りの偽り・断りの画面 23 枚)。

ほかの確認: fmruby-core `rake test` 合格 (はみ出しの検査の 426x240 の警告は前からのもの)。sim (TAB5、426x240) でエディタを開いて 1 文字打てた。卓のアプリは sim で起動し、卓の一覧の画面まで出た。

### 4.4 見立てと違った点

- 重複の確認があるので、使用中の席の ID を名乗る偽りはそもそも始まらない。ACL を確認するには、使われていない席の ID を名乗る形が要った (表の 2 行)。
- deploy/local で door の container を作り直すと、nginx が古い住所に繋ぎ続けて席の POST が 502 になった (nginx は upstream の名前を起動時に引く)。rehearsal の性質で、VPS では door は 127.0.0.1 の固定の port なので起きない。nginx を起動し直して続けた。

## 5. ユーザの VPS の手順

値は書かない (各自の `.env` のもの)。

1. **asterism-games を先に 0.5.0 へ**: games の置き場で `./update.sh main` (かタグ)。`credentials/games.tar` (行 `games`) が `ASTERISM_GAMES_PERMANENT_CREDENTIALS` に載っていること (部屋もこれを使う)。0.5.0 は console 0.2.x の間も今どおり動く。
2. **console の `.env`** に次を足す: `ASTERISM_ROOM_SEATS=16`、`ASTERISM_ROOM_HOSTS=games`、`WS_DOOR_SEAT_IDLE=1800`。`WS_DOOR_ROOM_LEGACY` は 0 のまま (古いページは入れない。決定)。
3. **nginx**: `deploy/vps/nginx/asterism-router.conf` の `location = /room/seat` を、今の router の server ブロックに足して `nginx -t && systemctl reload nginx`。
4. **console 0.3.0**: タグ v0.3.0 を作って image を publish した後 (part 2 の直し 41eafac を含めて)、`./update.sh 0.3.0`。setup が席の証明書 128 枚を作り、ルータを新しい設定で起動し直す (繋いでいる人は数秒で戻る)。
5. **新しいブラウザ版を /fmrb/ へ**: fmruby-core のこのブランチ (develop に入った後) で `rake wasm:dist` し、今の手順で `/fmrb/` に置く。console 0.3.0 と同時に入れる (古いブラウザ版は 0.3.0 の部屋に入れない: 4426)。
6. **確認**: `docker compose logs door | grep seats`、Admin > Relay の「And 128 room seats」、games のログの「its own certificate games.tar, 16 seats」。部屋を 1 つ作り、2 つのブラウザでリンクを開いて `wasm-0101`・`wasm-0102` のように別の席になること、読み直しで同じ席のままのこと。
7. (任意) 古いページの秘密の確認は要らなくなるので、games の `.env` に `ASTERISM_GAMES_ROOM_SECRET_IDS=0` を足してよい (`WS_DOOR_ROOM_LEGACY=0` なら古いページは入れないので、残しても害はない)。

## 6. 確認していないこと

- 4 つのブラウザで麻雀を 1 局最後まで (5.2-11 の元の形)。ブラウザ版は 2 つのページ + CPU 2 で数回の打牌まで。1 局を最後までは part 1 のボット (CRuby) で確認した。
- 30 分の席の渡し直しをブラウザで (door の待ち時間の試験は part 1 で 20 秒に縮めて確認した)。
- 公開の VPS・本物の nginx 1.18 での確認 (ユーザ)。
- Safari・Firefox での席の POST (CORS の事前確認は `text/plain` なので要らないはず。Chromium だけで確認した)。

## 7. 未決の点

- door が止まっている間 (起動し直しの数秒) に来た `/room?t=` の要求は、今どおり nginx の error log に鍵が残ることがある (part 1 の 4 章。0.2.x からある性質)。
- 4403 で別の席になったページは、読み直しを求めるだけで自分では読み直さない (動いている機械のノード ID は変えられないため)。自動で読み直すかは使ってみて決める。
- zenoh-ext の advanced subscriber が他人のキーの下に `@adv/sub/...` を出す形は、席の規則では落ちる (part 1 の 7 章。今の部屋のアプリは使っていない)。

## 8. 片付け

- deploy/local の stack を `VOLUMES=1 rehearse down` で止め、volume・network を消した。image `asterism-console-rehearsal:0.3.0` を消した。`deploy/local/.env` と `certs/` を消した。
- 手元のゲームホストを止めた。Playwright の container は `--rm` で残っていない。sim は `sim_down` で止めた。
- fmruby-core の build/ は Linux の標準構成 (x86-64) のまま。作業コピーは develop に戻した。
- VPS・実機には触れていない。試験の道具と結果は作業用の場所 (コミットしていない)。
