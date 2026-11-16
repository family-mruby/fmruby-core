# 指示書: アプリを何回も開いて閉じてメモリが漏れないかを見る道具 (regress:soak)

> 親: doc/reference/TODO.md の「アプリを何回も開いて閉じてメモリが漏れないかを見る」(2026-11-16、ユーザの希望)。
> 回帰の道具の作りは doc/regression/usage.md と report/hardening.md。時間の目安は usage.md 0 章。

## 目的

regress:run を代表の組 (9 本) にしたら、全部閉じた後のカーネルの VM プール (PSRAM) が前の基準より 17 KB 多かった。
アプリの出入りが増えた分の表の広がりなどと見ているが、漏れと区別していない。同じ組を何回も開いて閉じ、値が
落ち着くか増え続けるかで見分ける道具を作る。

## やること

1. **`rake regress:soak`** (sim): regress:run の代表の組を N 回 (既定 10、`ROUNDS=`) 続けて開いて閉じ、回ごとに
   全部閉じた時点の各 VM プール (カーネル・デスクトップ・サービスホスト)・内蔵 RAM・PSRAM の空きを記録する。
   最初の数回を慣らしとし、その後の回で単調に増え続けるか (回あたりの増え方) を判定して、要約に「落ち着いた /
   増え続ける (何が・回あたり何 B)」を出す。GC を回してから測る (regress:run と同じ扱い)。Retro と Modern。
   Modern は wasm のアプリも含む (共通のプールの使用も記録する: `FmrbApp.wasm_stats`)。
2. **実機でも流せる形**: 同じ台本を、WiFi の devctl (`/app/launch`・`/app/kill`・`/app/list`) で起動と終了を行い、
   シリアルの 10 秒ごとの行 (`IRAM free:`・`fmrb_app:`・`fmrb_wasm:`) を読んで記録する (`rake regress:soak BOARD=`、
   シリアルと WiFi の両方が要る。MCP の serial_* は使わず、道具が自分で capture を読むか、親が serial_start した
   capture のファイル `~/.fmrb_mcp/current.log` を読む形にし、どちらにするか決めて書く)。この段では作るところまで。
   実機で流すのは親。
3. **今の 17 KB の正体**: sim で流して、カーネルの値が落ち着くか増え続けるかを報告する。増え続けるなら、どの
   アプリの出入りで増えるかを切り分け (組ごとに N 回など)、原因の見当まで (直しは別の段、ユーザに相談)。
4. usage.md に章を足し、0 章の表に時間の目安を入れる。

## 確かめること

- `rake regress`・`rake regress:soak` (sim、Retro と Modern)。ほかの道具の結果が変わらないこと (`regress:run` を 1 回)。

## 触ってよい範囲

- fmruby-core (develop) の tools/regress・rakelib・test/regress・doc/regression。本体のコードは変えない (計るための
  ログが足りなければ止まって返す)。シリアル・flash・/dev/tty* は使わない。既存の未コミット (components/ の
  サブモジュール 2 つ、wasm/vendor/) には触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。

## report (doc/regression/report/soak.md)

道具の作り、sim の結果 (回ごとの表)、17 KB の正体、実機で流すときの手順、所要時間。
