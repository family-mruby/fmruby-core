# VT1・VT2: console の Terminal (読むコマンド → call・権限・記録)

> 状態: 実装済・検収待ち | 更新: 2026-11-08 | asterism-console に Terminal (`/terminal`、xterm.js 6.0.0 を vendor に同梱) を置いた。asterism gem の CLI を bridge の中で bridge の接続のまま動かし (gem に `CLI.new(net:, stop:, permit:)` を足した)、出力はそのページだけに流す。読むコマンドはサインインした全員、`call` と `ping --call` は Call permissions の表、それ以外 (OS のコマンド・mcp・completion・接続を選ぶオプション) は理由つきで断る。打った行は断ったものも含めて Call log の「Terminal commands」に残る。rails test 549 件・gem の全試験が通り、ヘッドレスの Chromium で読む・Tab・履歴・watch と Ctrl-C・許した call と断る call・Call log・ページを閉じて止まる、を確認した。push・タグ・公開・VPS はしていない

## コミット

| リポジトリ | コミット | 内容 |
|---|---|---|
| asterism (main) | `b1f4c64` cli: run inside another program on its connection (net:, stop:, permit:), the command table alone | gem 側。CHANGELOG の `## Unreleased` に記載。版は上げていない |
| asterism-console (main) | `9d03e11` test: a migration test quiets its own migration only | 既存の試験の不安定 (下の「気づき」) の直し。試験だけ |
| asterism-console (main) | `2d0fd0f` terminal: the asterism command line in a page, ... (VT1, VT2) | 本体。CHANGELOG `## Unreleased (0.3.15)`、docs/install.md に「From 0.3.14 to 0.3.15」、docs/terminal.md。VERSION は 0.3.14 のまま |

## 作り

### gem (asterism)

- `lib/asterism/cli/commands.rb` (新規): コマンドの表 (`NOUNS`・`COMMANDS`・`ALIASES`・`VALUE_OPTIONS`) を cli.rb から移し、`CONNECTION_OPTIONS`・`LOCAL` (mcp・completion)・`CLI.kind(key, call:)`・`CLI::Plan` を足した。何も require しないので、Zenoh を読まない Puma でも表だけ読める (`require "asterism/cli/commands"`)。
- `Asterism::CLI.new(net:, stop:, permit:)`:
  - `net:` 渡された `Asterism::Net` の上で動く (start も close もしない)。コマンドが subscribe したもの・on_join/on_leave に掛けたものは終わるときに外す (`hold` / `release_held`。単体の CLI でも同じく外すようになった。出力は変わらない)。
  - `stop:` watch・echo・hz・object get・ping の待ちの中で見て、true なら Ctrl-C と同じく終わる。
  - `permit:` 語を OptionParser で読んだ後、接続する前に `Plan` (コマンド・`:read`/`:call`/`:local`・call の object と method・引数・オプション) で問い合わせる。理由を返すと実行せずに表示し、`CLI#refused` に残す。
  - 渡された接続で動くときは: 接続オプションを受けない、mcp・completion を動かさない、長いオプションは全部書いたものだけ (`require_exact`。`--cal` が `--call` と読まれて「読むだけ」に見える穴を塞ぐ)、OptionParser 自身の `--help`/`--version`/補完スクリプト (どれも $stdout に出して exit する) を外して `--help` は out に出す、`__complete` はキャッシュを書かず `Process.exit!` もしない。
- 試験: `test/test_cli.rb` の `test_on_a_given_connection` (別プロセスの CRuby ノードに peer で繋いだ接続の上で、一覧・tree・call・whoami が接続のノード名・permit の Plan と断り・`--cal` を断る・接続オプション・mcp・completion・`--help`・`__complete`・stop で status echo と watch が終わり hold が空・on_join が外れ・接続は残る)。

### console (asterism-console)

- 流れ: ページ (xterm.js) → `POST /terminal/commands` → Rails が `Terminal::Line` で読み、`TerminalCommand` の行を書く (pending、または denied と理由) → bridge (`Bridge::Terminal`) が毎 tick (0.1 秒) で拾い、`Terminal::Line` で読み直し、スレッドで `Asterism::CLI.new(out:, err:, env: {}, net: Asterism.net, stop:, permit:).run(語)` → 出力はバッファ (`Bridge::Terminal::Output`、tty? と winsize でページの幅の端末に見せる) に溜め、bridge の主スレッドが Action Cable の `console:terminal:<ページの token>` に送る → 終わったら主スレッドが行に結果を書き、`done` を送る。
- ページの token: `terminal_sessions` (ページごとの乱数、10 秒ごとに借り直し、30 秒で切れる、pagehide で sendBeacon の release)。他人の token は lease も stream も断る (`ConsoleChannel` の `stream: "terminal"`)。
- コマンドのスレッドは DB に触らない (出力・結果は主スレッドが書く)。Call permissions の行は起動時に主スレッドで読んで渡す。
- Ctrl-C: `POST /terminal/commands/:id/stop` → pending ならその場で stopped、running なら stopping → bridge が 0.5 秒ごとに見て `stop:` を立てる。ページを閉じた・lease が切れた → bridge が止める ("stopped: the page was closed")。bridge が止まる・router を失う → 全部止めて行を書く。起動した bridge は前の bridge が残した running を stopped にする。bridge が動いていないときは Rails がそのページの running を stopped にしてページが詰まらないようにする。
- Tab: `POST /terminal/complete` (kind "complete" の行) → bridge が `Complete.split_line("asterism " + 行)` の語で `__complete -- 語...` を動かし、候補をページへ送って行を消す。ページは 1 つなら入れる (`/` で終わらなければ空白を足す)、複数なら共通部分まで伸ばす、伸ばせなければ並べて出す。
- 行の編集はページ側: ←→、Home/End (Ctrl-A/E)、Backspace/Delete、Ctrl-U/K、Ctrl-L、↑↓ の履歴 (ブラウザの localStorage にユーザごと 200 行)、折り返す長い行・全角の幅も扱う。xterm.js はページを開いたときに動的 import (他のページでは読まない、`preload: false`)。
- CSP: xterm.js は自分で `<style>` 要素を足し、nonce を付けられないので、Terminal のページだけ `style-src 'self' 'unsafe-inline'` (script は nonce のまま)。Turbo で差し替わると前のページの CSP のままになるので、メニューのリンクは `data-turbo=false`、ページに `turbo-visit-control: reload`。
- メニュー: Watch に「Terminal」。Call log に「Terminal commands」の節。
- gem が古い (CLI.new(net:) が無い) と、ページは「asterism gem 0.13.0 以降が要る」と出し、bridge はログに出して Terminal だけ起こさない。ほかは今までどおり。

## コマンドの分類と上限

| コマンド | 種類 | 誰が |
|---|---|---|
| `node list/info`、`app list/info`、`object list/info/find/get/echo`、`status get/echo/hz`、`tree`、`watch`、`ping PATH`、旧名の `nodes`・`objects`・`echo`、`help`、`version` | read | サインインした全員 |
| `call PATH METHOD [ARG...]`、`ping PATH --call METHOD [ARG...]` | call | Call permissions の行がその object と method を許すとき (ページからの呼び出しと同じ表。Rails・bridge・CLI 自身の parse の 3 回確かめる) |
| `mcp`、`completion` | local | 断る |
| それ以外 (`ls` など)、手で打った `__complete` | なし | 断る ("is not an asterism command") |

- 分類は gem の `CLI.kind` (help の「they only read, except call」と同じ。ping --call も call)。
- ほかに断るもの: `--router`・`--credentials`・`--peer`・`--node`・`--app` (console の接続で動くため)、`--timeout` が 30 秒を超えるもの (bridge のスレッドを待たせる上限。ページの呼び出しの `BridgeRequest::MAX_TIMEOUT` と同じ)、長い省略形 (`--cal`: CLI 側で invalid option)、2000 バイトを超える行・64 語を超える行・制御文字。
- シェルは出さない: 行は Ruby の `Shellwords` で語に分けて CLI の `run` に渡すだけ。`system` もシェルも呼ばない。
- 上限: 1 ページ 1 つずつ、1 人 `ASTERISM_TERMINAL_PER_USER` (3、Rails が理由つきで断り、bridge も超えて起こさない)、全体 `ASTERISM_TERMINAL_MAX` (12、超えた分は待ち、30 秒拾われなければ expired)、出力 `ASTERISM_TERMINAL_MAX_OUTPUT` (4 MB で止める)、ページへは 0.1 秒ごとに 64 KB まで、Tab は同時 4 つ。
- ファイルを送るコマンドは gem 0.12.0 の CLI に無いので、Members の印 (T4b) を当てる所はまだ無い。

## 記録 (Call log)

- `terminal_commands` の kind "run" の行が記録: だれ (user)・いつ・打った行・種類 (read/call、call なら object と method。CLI 自身が parse した値で上書きする)・どう終わったか (ok・failed・denied・stopped・expired)・exit code・理由・時間・出力の先頭 2000 バイト (色の符号は除く)。断った行も denied で残す (ページの呼び出しが denied を残すのと同じ)。
- 「Call log」の「Terminal commands」の節に、本人の分 (管理者は全員の分) を新しい順に 200 行。消すページも経路も無い。保存期間は Call log (bridge_requests) と同じで、どちらも消す仕組みが無い (期限なし)。ユーザを消すと user は null になり行は残る (file_transfers と同じ)。
- Tab は記録しない (名前を読むだけ。答えたら行を消す)。

## 確認の結果

- `bin/rails test`: 549 runs, 5902 assertions, 0 failures (2 回続けて)。追加: `test/lib/terminal/line_test.rb`、`test/controllers/terminals_controller_test.rb`、`test/lib/bridge/terminal_test.rb` (CLI の代役)、`test/lib/bridge/terminal_live_test.rb` (本物の CLI を本物の接続で、別プロセスの CRuby ノードに peer で)、`test/channels/console_channel_test.rb` に 1 件、`w4_guards_test.rb` のメニュー数。rubocop は変更ファイルで 0 件、brakeman に Terminal の指摘なし。
- gem: `env -u ASTERISM_CREDENTIALS rake test` 全部通過 (test:cli 27 runs 516 assertions を含む。msgs・transfer・objects・api・protocol も 0 failures)。
- 手元 (WSL): zenohd 1.10.1 (docker、127.0.0.1:17447)、CRuby の小さな相手 `calcnode/calc/calc` (add・sub、2 秒ごとに status が変わる)、`bin/rails server -p 3300` と `bin/bridge` (router watch・files は切った)、管理者でないユーザ `vt1check@example.org`、一時の call permission `calcnode/calc/calc add` (確認後に消した)。Playwright の Chromium (docker) で操作した画面 (`asterism-console/script/headless/`、gitignore 済み):
  - `vt_1_reads.png`: `node list`・`tree --methods`・`object info calcnode/calc/calc`・`status get calcnode/calc` が手元の CLI と同じ出力・色で出る。
  - `vt_2a_tab_parts.png`・`vt_2b_tab_methods.png`: `object info cal` で Tab → `calcnode/` → `calcnode/calc/` (→ `calcnode/calc/calc `)。`call calcnode/calc/calc ` で Tab → `add  sub` を並べる。
  - `vt_3_history.png`: ↑ で前の行が戻る。
  - `vt_4_watch_ctrl_c.png`: `watch` と `status echo` が流れ、Ctrl-C で止まり "(stopped (Ctrl-C))"。
  - `vt_5_calls_and_refusals.png`: `call ... add 2 3` → `5`、`call ... sub 5 1` → 権限が無いと理由つきで断る、`--cal` → invalid option、`ls -la /`・`mcp`・`--router` → 理由つきで断る。
  - `vt_6_call_log.png`・`vt_6b_call_log_terminal.png`: Call log の「Terminal commands」に、だれ・いつ・行・種類・結果・出力の先頭が並ぶ。
  - `vt_7_phone.png`: 幅 390 (スマホ) でも使える。
  - ページを閉じたとき: `watch` が走っている間にページを閉じると、0.4 秒後に bridge が止め、行は "stopped: the page was closed"。
- 片付け: bridge・rails server・相手・zenohd のコンテナは止めた。開発 DB にはマイグレーションを当て、`vt1check@example.org` と Terminal の行が残る (s1check などこれまでの確認用ユーザと同じ扱い)。一時の call permission は消した。

## VPS に要る設定 (0.3.15)

- **gem を先に出す**: image は gem を rubygems.org から取る (`ASTERISM_GEMS=rubygems`、Gemfile は `~> 0.12`)。Terminal には今回の gem の変更 (0.12.0 の次の版) が要る。gem を出さずに console 0.3.15 を出すと、Terminal のページが「gem が要る」と出るだけで残りは動く。Gemfile の `~> 0.12` を次の版に上げるかは親が決める (下)。
- マイグレーション 1 つ (`terminal_sessions`・`terminal_commands` の新しい表)。update.sh のバックアップで戻せる。
- nginx・router の ACL・ロールの変更は要らない (ページと WebSocket は `location /` を通る。Terminal は console の証明書が読めるものを読み、Call permissions が許すものを呼ぶ)。
- `.env` は任意: `ASTERISM_TERMINAL_PER_USER` (3)、`ASTERISM_TERMINAL_MAX` (12)、`ASTERISM_TERMINAL_MAX_OUTPUT` (4194304)。env.example に注釈つきの行を足した。

## 親が決めること

1. **gem の版と出す順**: gem の変更は `## Unreleased` に入れた (版は上げていない)。console の文言は「0.12.0 の次の版」「0.13.0 以降」(ページの表示と `Terminal::Line` の理由) と書いた。版の名前が 0.13.0 でなければ、その 2 か所 (`app/javascript/controllers/terminal_controller.js`・`lib/terminal/line.rb`・`lib/bridge/runner.rb` のログ) を直す。Gemfile の rubygems 側を `~> 0.13` に上げるか (上げると gem を出す前の image のビルドが落ちる)、`~> 0.12` のまま (古い gem では Terminal だけ無効) にするか。
2. **Call log の形**: Terminal の行は Call log のページの別の節 (「Terminal commands」) に出した (Playbacks の節と同じ並べ方)。ページからの呼び出しの表 (Calls) と 1 つの表に混ぜてはいない。同じ表にまとめたいなら作りを変える。
3. **読むコマンドの範囲**: 決定事項 4 のとおり読むコマンドは全員に開けた。そのため `object info`・`tree --methods` は、Call permissions で覆われていない object の method の一覧も見せる (グラフのページは覆われた object の meta しか読ませない)。見せる範囲を Call permissions に合わせるなら仕様の変更になる。
4. **時刻**: Terminal の出力の時刻 (watch など) は bridge のプロセスの時間帯 (CLI の `Time.now`)、Call log の時刻は `CONSOLE_TIME_ZONE`。VPS のコンテナが UTC なら Terminal の出力は UTC になる。揃えるなら bridge の TZ を設定で合わせる (コードの変更ではない)。

## 気づき・残件

- 既存の試験の不安定: `room_invite_records_group_test` がインスタンスの `verbose = false` でクラス全体の設定を書き換え、同じワーカーで後から走る `member_file_marks_test` がマイグレーションの出力を読めず落ちることがあった (試験の配り方の順で出たり出なかったりする)。`suppress_messages` に変えた (`9d03e11`)。
- 拒否された行の POST は 403 を返すので、ブラウザのコンソールに "Failed to load resource: 403" が出る (ページの呼び出しの denied と同じ作り)。表示には影響しない。
- 長い出力の行は CLI が幅で折り返すので、ページの幅を後から変えても前の出力は折り返し直さない (端末と同じ)。
- 複数行を貼り付けると最初の行だけを送る。
- VT3 (図とのつながり) は未着手。
