# T8c: 公開の後の使い勝手 (開き直し・ページの文字・サインイン)

> 状態: 実装済・検収待ち | 更新: 2026-11-10 | console から開くページはいつも `wasm-<handle>` になり、door は後から開いたページに名前と証明書を渡す (前のページの証明書の接続を閉じ、前のページは共有の接続でつなぎ直して「引き継がれた」と出す)。ブラウザ版のページの下の欄は本文 14px・注記 6.2:1 にし、Network の欄に確認済み (緑)・共有 (黄)・まだつながっていない (灰) の行を足した。端末の Network アプリにもルータの確認の 1 行を出す。console の番号 (TOTP) の画面に「このブラウザを 30 日覚える」を足し、アカウントの画面で一覧・消す・全部消すができる

指示は `../instruction_t8c.md`、決定は plan.md の「決定事項 (2026-11-10 ユーザ、T8 の公開の後)」。T8 は `t8.md`・`t8b.md`。

## 1. 作ったもの

| どこ | コミット | 何 |
|---|---|---|
| asterism-console (main) | `9476a2f` | door の引き継ぎ (`Relay::BrowserDoor`、`bin/ws_door`)、door への一言に `opened`、`Relay::BrowserNode.pick` はいつも自分の名前、覚えたブラウザ (`RememberedBrowser`、migration 1 つ、サインイン・アカウントの画面)、試験、docs (g1_5.md・g1_5b.md・files.md・install.md・README・CHANGELOG の Unreleased) |
| fmruby-core (develop) | この report と同じコミット | `wasm/web/index.html` (下の欄の CSS、`#door-state` の行)、`wasm/web/main.js` (`doorLine`、引き継がれた知らせ)、端末の Network アプリ (`system_desktop/network_dialog.rb`・`i18n.rb`) |

## 2. 開き直したページが名前を引き継ぐ (console)

- `Relay::BrowserNode.pick` は空きの枠 (`_2`〜`_9`) を探さず、いつも `wasm-<handle>` を返す。「Launch Web Studio」も「Make a pass」のリンクも同じ。画面の文言 (Make a pass・アカウント・Members > Browsers) も「後から開いたページが名前を引き継ぐ」に直した。
- **どちらが後か**: pass の確認の答え (door への一言) に `opened=<Unix ms>` を足した。値は `BrowserPass#used_at` (console がその pass のページを初めて通した時刻)。door は `[opened, pass]` の大きい方を「後から開いたページ」とする。pass の番号だけで比べると、先に作ってあとで開いたリンク (TTL 10 分の内) が負けるので時刻を使った。一言は署名つきのまま (欄が 1 つ増えただけ)。
- **door の動き** (`Relay::BrowserDoor#claim`): 名前ごとに「最新のページ」(pass・opened・証明書の接続の一覧) を覚える。
  - 同じ pass (同じページの 2 つ目のセッション・つなぎ直し) は今どおり証明書で。
  - 後から開いた pass は引き継ぐ: 前のページの証明書の接続すべてに close フレーム (4409、"taken over by a page opened later") を送って閉じ、自分は証明書で。ログは `browsers: wasm-alice: pass 2 (opened later) takes it over from pass 1; closing its 1 connection(s) with the certificate`。
  - 前の pass がつなぎ直してきたら共有の `ws/` へ。理由は `a page opened later from the console took over wasm-alice`、知らせに `"taken_over": true`。
  - 最新のページは接続が無くなっても覚えている。後のページが一時的に切れている間に前のページが名前を取り返すことはない (試験あり)。
- 部屋の席・pass の 1 回きり・ほかの利用者の名前の扱いは変えていない。名前は一言の中でその pass の利用者の `wasm-<handle>` のときだけ渡る (ほかの利用者の名前・古いリンクの `_2` は空、試験あり)。
- 前のページの中: zenoh-pico が自分でつなぎ直す (1 秒後)。そのとき door が共有へ通し、知らせで引き継がれたと分かる。ページの `/tmp/.asterism_door` は消え、転送のアプリの `certified?` は偽になる。

## 3. ブラウザ版のページの下の欄 (fmruby-core)

- 本文 12px → 14px (panel・ボタン・入力)、見出し 11px → 13px、`#status` 13px、「How to use it」14px (狭い画面 13px)。ラベルの幅 82px → 96px。テレビの形の画面 (`#frame`) の見た目は触っていない。
- 背景 `#1a1a22` との差 (WCAG の比): 注記 `.note`・`.meta` `#6f6f7c` (3.5:1) → `#9a9aa8` (6.2:1)、見出し `#7f7f8c` (4.4:1) → `#a0a0ad` (6.7:1)、ラベル `#8a8a97` (5.1:1) → `#9a9aa8`、本文 `#aaa` → `#bbb` (9.0:1)、使えないボタン `#666` → `#808088` (入力欄の `#111` に対し 4.8:1)。
- 見出しの右の `.meta` が長いとき (Your files の「kept in this browser, but ...」) は見出しの下の行へ回る (`flex-wrap`)。前は見出しの文字が「YOUR / FILES」と折れていた。
- Network の欄の見出しの下に 1 行 (`#door-state`、pass つきのページだけ):
  - 緑 (`ok`): `Checked by the router: this page is wasm-alice with your certificate (the console's door). Files can go.`
  - 黄 (`shared`): 共有の接続 (理由つき)・ほかの名前で通された・引き継がれた (`Another page opened later from the console has taken over wasm-alice. This page is on the shared connection now: no files. Open it again from the console to send from here.`)
  - 灰 (`wait`): `Router: not connected yet (an Asterism app connects it, e.g. Receiving)`、一度つながった後に全部閉じているときは `not connected now`。
  - 色と背景の比: 緑 9.1:1、黄 9.2:1。見出しの `node ...` には名前だけを出す (前は確認の文も同じ span に続けていた)。
- 端末の Network アプリ (ブラウザ版の画面): 2 行の下に、`/tmp/.asterism_door` があれば緑の札「Router checked」と名前、無ければ「Router check: none (no certificate)」(日本語は「ルータが確認」「ルータの確認: なし (証明書なし)」)。開いている間 1 秒ごとに読み直す。ほかの端末の表示は変えていない。

## 4. TOTP のブラウザを 30 日覚える (console)

- 番号の画面に「Remember this browser for 30 days (no code here; the password is still asked)」の選択 (既定は外れ)。選ぶと、ランダムな値の署名つき cookie `remembered_browser` (httponly・secure (本番と https)・same_site lax・30 日) と、`remembered_browsers` の行 (利用者・作った日時・期限・user agent・作った IP・最後に使った日時、cookie の値は SHA-256 だけ) を作る。
- 次のサインイン: パスワードが合い、その利用者の期限内の行に cookie が当たれば番号を聞かずに入る (`session: user N (...) signed in without the code (remembered browser M)`)。パスワードは今どおり毎回。セッションは今どおり 12 時間。
- アカウントの画面 (TOTP の節) に一覧 (覚えた日・期限・最後に使った日・user agent・「this browser」の印) と「Forget」「Forget them all」。
- 全部消える場合: パスワードの変更・TOTP の新しい秘密・オン・オフ (`User` の `after_update`。アカウントの画面・`console:user`・`console:otp_off` のどれから変えても)。番号を使った (`otp_last_step`) だけでは消えない。
- 管理者の TOTP の強制 (`CONSOLE_ADMIN_TOTP=required`) は変えていない。覚えるのは TOTP が有効な利用者の番号の段だけ。
- cookie を盗まれたときの範囲: パスワードも要り、その利用者だけ、期限まで、アカウントの画面で消せる。決定の範囲に収まり、決めることは出なかった。

## 5. 確認

| 何を | 結果 |
|---|---|
| console `bin/rails test` | 588 runs, 0 failures。足したもの: door の引き継ぎ 3 (後のページが取り前のページの 2 つの接続が 4409 で閉じ共有でつなぎ直して `taken_over`・後のページが切れている間に前のページは取り返さない・`opened` で順を決める)、pass の確認 (`opened`・ほかの利用者の名前は空)、BrowserNode、TOTP 6 (覚えたら番号なし・パスワードは聞く、選ばなければ覚えない、期限切れ・消した・ほかの利用者の行は聞く、パスワード・TOTP の変更で全部消える、アカウントの一覧と全部消す、選択は既定で外れ)。部屋・席の試験は変えずに通る |
| console `bin/rubocop` | 指摘なし |
| `rake regress` | host 23 suites 全部、描画 229 runs 全部通る |
| `rake regress:screens ONLY=web` | 6/6 通る (基準は変わらない。ページの下の欄はキャンバスの外なので場面に入らない) |
| `rake regress:transfer:quick WEB=1` | ok (2.4 分、api・Shell の `asend` を含む) |

### 手元の通し (2026-11-10)

console の `deploy/local/rehearse` を作業中の木から build、`PLAY_DIST` はこのブラウザ版の `rake wasm:dist`。利用者 alice (handle を付けて証明書、`bin/asterism setup --from-update` で browsers.json とルータの設定、zenohd を再起動)。ヘッドレスの Chromium (Playwright の container) で、別々の保存の 2 つの context から alice の pass のリンクを順に開いた (受け取り on)。

1. A: 最初「Router: not connected yet ...」(灰)、21 秒後に「Checked by the router ...」(緑)、`/tmp/.asterism_door` = `wasm-alice`。
2. B を開く: B が緑、同じ瞬間に A は「Another page opened later from the console has taken over wasm-alice ...」(黄)、A の door の印は消えた。15 秒後も同じ (取り返さない)。
3. door のログ:
   ```
   #1 open from <ip>: GET /, as wasm-alice with its certificate (pass 1), for 14399 s
   browsers: wasm-alice: pass 2 (opened later) takes it over from pass 1; closing its 1 connection(s) with the certificate
   #1 closed after 27.3 s (closed by zenohd)
   #2 open from <ip>: GET /, as wasm-alice with its certificate (pass 2), for 14400 s
   #3 open from <ip>: GET /, for 14372 s (shared: a page opened later from the console took over wasm-alice)
   ```
4. 端末の Network アプリ: 確認済みのページで緑の「Router checked wasm-alice」、引き継がれた後のページで「Router check: none (no certificate)」。
5. 起動したもの (rehearsal の container・volume・network・image、Playwright の container、開発サーバ、ヘッドレスの Chromium) は全部止めて消した。`deploy/local/.env`・`certs/` も消した。

撮った画面とログ (検収用、リポジトリには入れていない): scratchpad の `t8c/` (`panels_before.png` (変更前の下の欄)・`panels_after_pass.png`・`panels_after_narrow.png` (変更後、つながる前)、`a_checked.png`・`a_taken_over.png`・`b_checked.png`・`page_checked.png`・`page_checked_panels.png`、`dialog_checked.png`・`dialog_taken_over.png`、`scene.out`・`door.log`・`a.log`・`b.log`、`xfer_quick_web.log`、通しの台本は `t8c/drv/`)。

## 6. 見立てと違った点・撤回した仮説

- **「pass の番号で後先を決めればよい」**: 取らなかった。リンク (Make a pass) は作ってから 10 分の内に開けばよく、作った順と開いた順が入れ替わる。console が初めて通した時刻 (`used_at`) を一言に入れた。
- **「前のページの接続が無くなったら名前を手放せばよい」** (T8 の数え方): 引き継ぎでは足りない。後のページが一時的に切れた (ルータの再起動) 間に前のページがつなぎ直すと取り返し、後のページが戻るとまた取る、の行き来になる。最新のページを接続と別に覚える形にした。door を起こし直すとこの記録は消えるが、両方がつなぎ直せば `opened` で後のページが取るので、最後は同じ形に落ち着く。
- 前のページを「閉じる」(つなぎ直させない) 道もあったが、決定の「共有の接続でつなぎ直す (そのページにそう出す)」に合わせた。zenoh-pico は close フレームのあと自分でつなぎ直すので、ページの側に手は要らなかった。
- 下の欄を大きくすると Your files の見出しが「YOUR / FILES」と折れた (見出しと右の状態が 1 行を取り合う)。状態の方を次の行へ回すようにした。

## 7. 残件

1. 同じ利用者の 2 つのページは、グラフの上で同じノード ID (`wasm-alice`) を名乗る (前のページは共有の接続で、ルータは見分けない)。転送は後のページにしか届かない (ACL) が、ゲームなどの呼び出しは前のページも答えうる。決定どおり前のページは閉じずにつなぎ直す形にした。気になるなら「前のページは閉じる」に変えるかを親が決める。
2. 「Launch Web Studio」で開くページそのものは通しで押していない (通しは Make a pass のリンクで 2 回開いた。どちらも `pick` から同じ名前で、`open` の試験は名前が `wasm-pat` になることを見ている)。
3. 覚えたブラウザの行の数に上限は付けていない (期限切れの行は覚えるときにその利用者の分を消す)。
4. 前のフェーズの console の試験が残した `bin/ws_door` のプロセス 5 つ (13:15 起動、親なし) が手元に残っていたので止めた。今回の試験の分は残っていない。

## 8. VPS の更新に要ること

- `./update.sh <版>` (版を付けるのは親)。migration 1 つ (表 `remembered_browsers`、空)。nginx・`.env`・ロールは変えない。door と console は同じ版にする (door への一言の欄が 1 つ増えた。古い door は新しい一言を読めず、全部のページが共有の接続になる。同じ image なので `update.sh` なら揃う)。
- 更新の後に console から開くページはいつも `wasm-<handle>`。更新の前に作った `_2` のリンクは今までどおり共有の接続。
- 確認: 同じ利用者で 2 回開き、door のログに `pass N (opened later) takes it over from pass M`、前のページの Network の行が黄色で「taken over」。番号の画面に「Remember this browser for 30 days」。
- ブラウザ版の配置 (このブラウザ版を公開する) は別。古いブラウザ版のページでも引き継ぎは起きる (door の側の動き) が、Network の行の色と「引き継がれた」の文は新しいブラウザ版だけ。
- 詳細は console の docs/install.md「From 0.3.18 to the next version」と CHANGELOG の Unreleased。
