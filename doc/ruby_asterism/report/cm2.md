# CM2 報告: console に「部屋を作れる期限つきの招待リンク」

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | asterism-console 0.3.5 に入れた (`3e9d8b5`、0.3.5 はまだタグなし)。管理者が期限・同時の部屋の上限・選べるアプリつきの招待リンクを作り、開いた人はログインせずに「部屋を作るだけのページ」(`/invite`) で部屋を作る。部屋の長さはユーザの決定で 12 時間まで (既定 3 時間)。同じコミットで、ユーザの追加の決定により画面「Browser passes」を「Make a pass」、ボタン「Open the browser build」を「Launch Web Studio」に改めた

指示書: [instruction_cm2.md](../instruction_cm2.md)

## 0. 指示書からの読み替え (親から届いたユーザの決定)

- **部屋の長さの上限は 3 時間ではなく 12 時間** (既定は 3 時間)。1 章・2 章の「3 時間まで」を「12 時間まで」と読んだ。
- **0.3.5 に一緒に入れる**: 画面「Browser passes」→「Make a pass」、ボタン「Open the browser build」→「Launch Web Studio」(5 章)。

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-console | main | `3e9d8b5` | 招待リンク (モデル・移行・管理画面・招待のページ・nginx/Caddy の例・試験・docs・CHANGELOG 0.3.5)、Make a pass / Launch Web Studio |
| fmruby-core | (親がコミット) | - | この報告と画面 `report/cm2/` |

push・タグはしていない。コミットの前に差分から実在の IP・メールアドレス・端末 ID・鍵・試しの招待の秘密を検査して 0 件 (文書用の `192.0.2.20` と前からある `127.0.0.1` だけ)。

## 2. 画面

### 2.1 管理者の側 (Admin > Rooms の中)

- 部屋の画面に「Invite links」の欄を足した (`#invites`)。作る欄: 名前 (誰のためか)、Days (既定 7、1〜90)、Rooms at once (既定 1、1〜99)、選べるアプリ (`ASTERISM_ROOM_APPS` のものだけをチェックボックスで。1 つ以上)。`ASTERISM_ROOM_APPS` が無いときは「出せるアプリが無い」と出して作る欄を出さない。
- 一覧: 名前・作った時と人・期限 (無効にしたならその時)・状態 (open / expired / revoked)・今開いている部屋 (`1 of 1`)・作られた部屋の数・アプリ。
- 招待の画面 (`/admin/room-invites/:id`): 事実の一覧、作ったときだけ出るリンクの全文 (`https://<console>/invite#invite=<秘密>`)、その招待で作られた部屋の一覧 (いつ・どのスロット・閉じた理由)、「Revoke」(「Also close its open rooms now」を選べる。既定は部屋はそのまま期限まで続く)。
- 部屋の一覧の「Made」と部屋の画面に「invite link <名前>」(招待の画面へのリンク) を出す。これが「誰の招待で、いつ部屋が作られたか」の記録。Rails のログにも `rooms: invite link N (...) opened room ...` を出す。
- 画面: `report/cm2/admin_rooms.png` (招待の欄と部屋の一覧)、`admin_invite_made.png` (作った直後。秘密の部分は灰色で消した)、`admin_invite_show.png`。

### 2.2 部屋を作るだけのページ (`/invite`、ログイン不要)

- 専用の layout (`layouts/invite`): メニュー無し、コンソールのほかのページへのリンク無し。見出しは「Asterism rooms」。
- 招待の名前・期限・今開いている部屋の数 / 上限。作る欄: Opens (招待のアプリのうち、今も `ASTERISM_ROOM_APPS` にあるもの)、Hours (既定 3、0.25〜12)、Name (任意。空なら「招待の名前 + 時刻」)。上限に届いているとボタンは押せない。
- 作ると部屋のリンクを 1 回だけ出す (管理者の部屋の画面と同じ。`Cache-Control: no-store`)。下に「この招待で作った部屋」の一覧と、開いている部屋の「Close」。ほかの招待や管理者の部屋は出ない。
- 断るときは理由を出す: 招待が無効にされた / 期限が切れた / この招待の同時の部屋の上限 (「close one or wait until one ends」) / 全スロットが使用中 / 招待に無いアプリ / 12 時間を超える。
- 画面: `invite_room_made.png` (部屋を作った直後。部屋の鍵は灰色で消した。リンクが枠からはみ出していたので `a.wrap` の折り返しを CSS に足した。管理者の部屋の画面のリンクも同じ扱いになる)、`invite_limit.png` (上限)、`invite_revoked.png`、`invite_expired.png`、`web_studio_in_room.png` (出たリンクで開いたブラウザ版)。

## 3. 守りの形

- **秘密**: `SecureRandom.urlsafe_base64(32)` (43 文字)。DB には SHA-256 (`room_invites.token_digest`、unique) だけを置く。全文は作ったときの画面に 1 回だけ出す。
- **秘密はフラグメントで運ぶ**: リンクは `/invite#invite=<秘密>`。フラグメントはサーバに送られないので nginx のログにも Rails の `Started GET` の行にも出ない。ページの Stimulus (`invite_controller.js`) がアドレスバーからフラグメントを消し、フォームの本文で `POST /invite` に渡す (Rails は `token` を `[FILTERED]` にする)。以後はこのブラウザの Rails のセッション (暗号化された cookie) が招待の id を覚える。各要求のたびに招待の状態を見るので、無効にした・期限切れの招待は前に開いたブラウザでも即座に断られる。スクリプトの無いブラウザは、ページの欄にリンクを貼っても入れる。部屋のリンクと同じく「鍵をサーバのログに残さない」形にした (指示書の「部屋のリンクと同じ扱い」)。
- **ログイン扱いにしない**: 招待のセッションは `Authentication` と無関係で、ほかのページはどれも今どおりログインを求める。nginx の `/admin` のアドレス制限もそのまま効く。
- **回数制限** (door の作りに合わせた): 招待の秘密でない値を、アドレスごとに 10 分に 10 回送ると、その 10 分が過ぎるまでそのアドレスからの試しを 429 で断る (正しい秘密でも。door の `BAD_LIMIT` と同じ考え方)。加えて、試しそのものを 10 分 30 回、部屋の作成と閉じるを 10 分 20 回 (アドレスごと、`rate_limit`)。nginx の例では `/invite` に `asterism_signin` の zone (サインインと同じ、burst 10) を掛けた。
- **部屋の数の上限**: 招待ごとの上限は `Room.open!` の中の同じ Mutex (スロットを取る錠) で数えるので、同じ招待の 2 つのページが最後の 1 枠を同時に取ることはない。全体の上限はスロット (枠) のまま: 招待の部屋も空きスロットを取るので、管理者の部屋と合わせて枠を超えない。部屋の接続の上限は `ASTERISM_ROOM_MAX_CONNECTIONS`。
- **Referrer-Policy の罠**: 最初、招待のページを `no-referrer` にしたら、Chrome がフォームの POST を `Origin: null` で送り、Rails のなりすまし対策が 422 で断った (手元の構成で見つけた)。部屋の鍵も招待の秘密もフラグメントにあり referrer には載らないので、`same-origin` にした。
  - ついでに見えたこと (直していない): 管理者の部屋の画面 (`Admin::RoomsController#show_link`) は部屋のリンクを出すとき `Referrer-Policy: no-referrer` を付けている。同じ理由で、その画面から続けて「Make another link」「Close the room」を押すと 422 になるはず (試していない)。直すなら同じく `same-origin` に変える 1 行。

## 4. 12 時間について (ユーザの決定の確認)

12 時間に合わせて直す所は無かった。作りの大きな変更 (ACL・証明書) も要らない。

| 所 | 長さの決まり | 12 時間の部屋 |
|---|---|---|
| 部屋の期限 (`Room`) | `ASTERISM_ROOM_MAX_HOURS` (既定 24、最大 24) | もつ。招待の上限は `min(12, ASTERISM_ROOM_MAX_HOURS)` にした (管理者がそれより短く決めていればそちらに従う) |
| 署名 (signer) | 鍵の期限は 24 時間先まで | もつ |
| door の部屋の接続 | 鍵の期限まで (24 時間が上限) | もつ。`WS_DOOR_MAX_SECONDS` (4 時間) はパスの道 (`/`) だけで、部屋の道には掛からない |
| 席の割り当て | 鍵の期限まで (`seats.json` の `until`) | もつ |
| nginx の `proxy_read_timeout 1h` | 無通信のときだけ | 関係しない (Zenoh は無通信にならない) |
| ゲームホストの部屋の片付け | `rooms.json` の `until` を見る | もつ (asterism-games のコードは読んだだけ) |
| 席の証明書 | 365 日 | 部屋の長さと無関係 |

手元の構成で 12 時間の部屋そのものは作っていない (試験で `expires_at` が 12 時間先になること、12.5 時間は断ることを確認した)。

## 5. Make a pass / Launch Web Studio

- ナビゲーションの「Browser passes」→「Make a pass」、ページの題「Make a pass - Asterism Console」、見出しも「Make a pass」。
- ボタン「Open the browser build」→「Launch Web Studio」(ナビゲーションのボタン、パスのページの見出しとボタン)。アカウントのページの「to open the browser build」も「to launch Web Studio」に。
- 管理者の全パスの一覧 (Users から行く) の題と見出しが「Every browser pass」「Browser passes」だったので「Every pass」に揃えた。
- **URL は変えていない** (`/passes`、`/admin/passes` のまま) ので、転送は要らない。環境変数の名前 (`ASTERISM_BROWSER_PAGE` ほか) も変えていない。
- 文書: README、docs/install.md、deploy/local/README.md、deploy/vps/env.example の説明、コードのコメント。過去の段階の設計書 (docs/g1_5.md・g1_5b.md・g0_w4.md) と CHANGELOG の過去の版は、CM1 と同じく書いた当時のままにした。ページの本文の「a browser build (a page that joins the network from a browser)」のような仕組みの説明は残した (ボタンの名前ではないため)。
- 画面 `report/cm2/make_a_pass.png`。

## 6. nginx の扱い

- `deploy/vps/nginx/asterism-console.conf` に `location = /invite` と `location ^~ /invite/` を足した (`^~ /invite` 1 つにすると `/invitations/` にも当たるので 2 つに分けた)。どちらも `asterism-door-allow.conf` を include する: **既定は部屋の道 (`/room`) と同じアドレス**。部屋に入れないアドレスからは部屋も作れない、という考え。違う範囲にしたければこの 2 つに別の allow を書く、と docs とファイルの注釈に書いた。
- 手元の構成 (deploy/local) は door-allow が `allow all` なので、他人のアドレス (172.31.240.60) から招待のページが開け、`/admin` は nginx が 403 にした。`nginx -t` 相当 (起動) も通った。
- Caddy の例は `/room` にもアドレス制限を書いていないので、同じ形をコメントの塊 (`@invite_outside`) で置いた。
- **VPS の変更 (ユーザの作業)**: docs/install.md「From 0.3.4 to 0.3.5」に、`asterism-console.conf` を上書きするか 2 つの location を足して `nginx -t && reload`、と書いた。足さなければ招待のページは全アドレスに開く (リンクは要る)。VPS では `/room` を自宅だけに絞っているので、足せば招待のページも自宅からだけになる。外で招待を使うなら door-allow を広げるか、招待の 2 つの location に別の allow を書く判断が要る (7 章)。

## 7. 受け入れ条件の結果

| 条件 | 結果 |
|---|---|
| 1. console の試験全部と rubocop | `bin/rails test` **466 runs / 4761 assertions、0 failures、0 skips**。`bin/rubocop` 257 files、no offenses |
| 2. deploy/local で作成→ログインしていないブラウザで部屋→ブラウザ版が入る、上限・無効・期限切れ | 合格 (下記) |
| 3. 招待のページから管理画面・ほかの部屋が見えない | 合格 (下記と試験) |

### 7.1 試験 (`test/controllers/room_invites_test.rb`、14 件)

作成 (1 回だけ出る・ハッシュで保存・表に秘密が無い・一覧に出る)、`ASTERISM_ROOM_APPS` に無いアプリ・アプリ無し・91 日は断る、管理者でない人は作れない、招待のページで部屋を作る・リンクは 1 回だけ・一覧・閉じる・rooms.json から消える、貼ったリンクでも入れる・違う秘密は 404、12 時間まで (12.5 は断る)・招待に無いアプリは断る・`ASTERISM_ROOM_MAX_HOURS=6` なら 6、招待ごとの上限と全スロットの上限、無効・期限切れは前に開いたブラウザでも理由つきで断る、無効にするとき部屋も閉じられる、自分の招待の部屋だけ見え・閉じられる (ほかの招待と管理者の部屋は 404)、管理画面・トップ・管理者の部屋の作成に入れない (サインインへ)、秘密の推測の回数制限 (10 回で正しい秘密も 429)、試し全体の回数制限、部屋の記録に招待の名前。`authentication_test.rb` の「ログイン不要の道」に招待のページの 5 つを足した。`browser_page_open_test.rb` に新しい名前 (ナビゲーション・題・見出し、古い名前が無いこと)。

### 7.2 手元の構成

構成: console の deploy/local を作業コピーから `rehearse prepare` → `build` (image `asterism-console-rehearsal:0.3.5`) → `bootstrap` → `up`。`.env` は `CONSOLE_ADMIN_TOTP=optional` (Playwright のログインのため)、`ASTERISM_ROOM_APPS` は env.example のまま (Othello・Mahjong)。`PLAY_DIST` は fmruby-core の作業コピーの `wasm/build/dist` を scratchpad に**写して**使った (fmruby-core では何も作っていない)。ブラウザは docker の Playwright 1.56 (Chromium) を rehearsal の web の網に置いた。管理者は 172.31.240.50、招待を開く人は 172.31.240.60・61・62 (管理者でないアドレス、ログインなし)。

1. 管理者がログインして招待「Friday club」(7 日、同時 1 部屋、2 アプリ) を作った。リンクは `https://asterism.localtest/invite#invite=<43 文字>`。
2. ログインしていない別のブラウザ (.60) でリンクを開くと `/invite` に着き (アドレスバーから秘密が消えた)、Othello を選び名前「Othello by invite」で部屋を作った。スロット 02、3 時間後まで。
3. 出た部屋のリンク (`https://play.localtest/index.html?zenoh=...#room=<鍵>&app=/app/test/othello.app.rb`) をブラウザ版で開き、クリックで始めると「running in room 02」、node `wasm-0201` (席 01) で Othello (direct) が上がった (`othello: wasm-0201 up (ws/router.localtest:7443, 25 ms)`)。door のログ: `room ... slot 02 seat 01 (new): for a page from 172.31.240.60`、`open from 172.31.240.60 (1 in the room)`。合格。
4. **上限**: 同じページで 2 つ目を作ろうとするとボタンが押せず、押せるようにして送っても「this invite link may have 1 room open at once, and 1 is: close one or wait until one ends」。合格。
5. **管理画面が見えない**: 同じブラウザで `/admin/rooms`・`/admin/room-invites` は 403 (nginx のアドレス制限)、`/` はサインインのページへ。招待のページの HTML に `/admin` へのリンク 0、nav 0。一覧は自分の招待の部屋だけ。合格。
6. **無効**: 別のブラウザ (.61) が招待を開いたまま 2 秒ごとに `/invite` を読み直し、その間に管理者が Revoke → 次の読み直しで 403「This invite link was revoked by an admin.」。リンクを開き直しても同じ。合格。
7. **期限切れ**: 招待「Short club」を作り、rails runner で期限を 2 秒後にして待ってから、別のブラウザ (.62) でリンクを開くと「This invite link expired ...」。合格。
8. **ログに秘密が無い**: 2 つの招待の秘密で、console のログ (`docker logs`) と nginx の access・error のログを grep して 0 件。nginx の行は `"GET /invite HTTP/1.1"`、Rails は `"token"=>"[FILTERED]"`。
9. 管理者の部屋の画面に「invite link Friday club」、招待の画面に作られた部屋 2 つ (`admin_rooms.png`、`admin_invite_show.png`)。

## 8. 判断したこと・残ったこと

- **招待で作った部屋の扱い**は管理者の部屋と同じにした (同じスロット、同じ door、接続数の上限も同じ)。招待を無効にしても既に開いている部屋は期限まで続く (管理者が「Also close its open rooms now」を選べば閉じる)。1 章に無い新しい決まりは足していない。
- **招待のページで「もう 1 本の部屋のリンク」は出せない** (指示書の範囲どおり、一覧と閉じるだけ)。リンクをなくしたら閉じて作り直す。要るなら、管理者の「Make another link」と同じ操作を自分の招待の部屋に限って足すのは小さな変更。
- **VPS の判断 (ユーザ)**: nginx の 2 つの location を足すと、招待のページは今の door-allow (自宅だけ) に絞られる。外の人に招待を渡すなら、`/room` も外に開く必要がある (部屋に入れないため)。どちらを開くかは VPS の作業のときに決める。選択肢:
  1. **`/room` と招待のページを同じ範囲で広げる (door-allow を変える)**。既定の考え (同じ扱い) のまま。推奨。
  2. 招待のページだけ自宅に残し、`/room` を広げる (招待のリンクを作った人が自宅で部屋を作ってリンクを外に渡す使い方)。2 つの location に別の allow を書く。
  3. 今のまま (どちらも自宅だけ)。
- **管理者の部屋の画面の `no-referrer`** (3 章): 続けてボタンを押すと 422 になるはずの既存の穴。直していない。
- 開発用 DB (`storage/` の development、git の外) に移行を当てた (schema.rb を作るため)。同時に書き換わった `db/cable_schema.rb` は元に戻した。
- 後片付け: deploy/local を `VOLUMES=1 rehearse down` で volume ごと消し、`deploy/local/.env` と `certs/` を消した。作った image `asterism-console-rehearsal:0.3.5` を消した。前からある image (`asterism-console-rehearsal:0.3.2`、Playwright、nginx ほか) は残した。scratchpad の写したブラウザ版・管理者のパスワード・招待のリンク・Playwright の台本と node_modules・画面の元を消した。docker の prune はしていない。Tab5・シリアル・sim・web には触っていない。fmruby-core では git の操作をしていない (この報告と `report/cm2/` の画面を書いただけ)。
