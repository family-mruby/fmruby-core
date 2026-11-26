# U1・U2 の指示: UART の API と、シリアルのターミナルのアプリ

先に読む: `doc/app_uart/plan.md` (4 章が決定)、`report/u0.md`、`doc/hw_resources/plan.md` (方針の元)、
`doc/app_boundary/` (境界の関数の書き方)、`doc/reference/sig_writing_and_coverage.md` (sig の手順)。
上流のクラスは `components/picoruby-esp32/picoruby/mrbgems/picoruby-uart` (読むだけ。ビルドには入れない)。

## U1: API

1. **HAL** (`components/fmrb_hal/fmrb_hal_uart.h` と ESP32・POSIX の実装): 形式 (データ・ストップ・パリティ)、受信の大きさ、
   受信量の問い合わせ、受信の消去、送信の待ち、break、速さの変更を足す。今の使い手 (シリアルの MIDI 出力) の動きは
   変えない。POSIX は sim 用の偽物: 既定はループバック (書いた物が読める)、`uart<N>_in.fifo`・`uart<N>_out.fifo` が
   プロジェクトの直下にあればそちら (MIDI の FIFO と同じ作り)。
2. **ピンの台帳**: UART 用の取得 (`claim_uart` の形。GPIO と同じシステムのピンの表で断る、入力だけの線は RX だけ、
   持ち主はアプリの単位)、本体が使う UART の番号の判定 (機種ごと。plan.md 4 章 6)。断る理由は GPIO と同じく番号で返し、
   文にするのは上の層。
3. **境界の関数** (`main/app/fmrb_boundary_uart.c` 新規、`sig/uart.rbs` 新規): モジュール `FmrbUart` に、開く・読む・
   行を読む・書く・受信量・戻す (unget)・送り切る・消す・break・速さ・形式・閉じる・捨てた数。番号ごとの表
   (持ち主・HAL のハンドル・PSRAM のリング) を持ち、毎回呼び出し元が持ち主かを確かめる。ドライバから PSRAM のリングへの
   汲み上げは、アプリが呼んだとき。1 回の上限は読む側がリングの大きさ、書く側が 4096 (包みが分けて送る)。
   アプリが終わったときの片付け (`hw_proxy_release_resources` の経路。sim でも走るようにする) に UART を足す。
   ブラウザ版は「この環境では使えない」(今ある例に倣う)。
4. **Ruby の包み** `UART` (新しい gem `lib/gems/picoruby-fmrb-uart`、または今ある fmrb の gem の中。境界の関数を呼ぶ
   薄いクラス): plan.md 4 章 1〜5 のとおり。例外の種類と理由の文は 4 章 5。
5. **sig と文書**: `sig/uart.rbs` に `UART` と `FmrbUart` (内側の物は `ignore.yml` か `@api private` の扱いに合わせる) を、
   日英の要約・長文・例つきで書く。例のピンの番号は説明用と分かる書き方にし、機種ごとの番号は長文に表で書く。
   `rake sig:coverage` の doc gaps 0、`rake ti:help`、`tools/ti/gen_site.rb` の `AREAS` に足す。
6. **ほかの言語**: wasm と MicroPython は生成で出る (SDK・qstr の表を作り直す)。Lua は 1 行足す。動作の確認は Ruby だけで
   よい (ほかの言語はビルドが通る所まで)。
7. **試験**: ホストの試験 (判定の表・リング・溢れ・待たない読み方・`gets` の行の切れ目)、上流の `test/uart_test.rb` を
   unit の名前を置き換えて流せる形にした物。`rake test` に入れる。

## U2: 見本のアプリと文書

1. **シリアルのターミナル** (`flash/app/tool/` に `.app.rb` と `.app.toml`。名前は Serial Terminal / シリアルターミナル):
   番号・TX・RX・速さを選んで Open、届いた文字を並べ (改行で行を分け、表示できないバイトは `.` か 16 進)、下の欄に
   打った 1 行を Enter で送る (行末は LF・CR+LF・無しから選ぶ)。Close で閉じる。開けなかったときは理由を出す。
   選んだ設定は次に開いたときも残す (アプリの設定の保存の今ある形に合わせる)。部品は FmrbUI。手本は
   `flash/app/demo/talk_client.app.rb`。Retro の 320x240 に収める。受信が多くても画面の更新が固まらないこと
   (1 回の `on_update` で読む量と描く行数に上限)。ブラウザ版では「この環境では使えない」と出す。
   ソースは、アプリを書く人の手本になるように短く素直に書く。
2. **利用者向けの説明** `flash/usr/share/doc/uart.md`・`uart_ja.md` (今ある文書の形に合わせ、`home*.md` の一覧に足す):
   つなぎ方 (TX と RX は相手と交差、GND、3.3 V の信号)、機種ごとの番号と端子 (Tab5 の Grove は G53・G54)、
   アプリの書き方の短い例、MIDI のシリアル出力と同時に使えないこと、うまくいかないとき。

## 触ってよい範囲

fmruby-core の上の物と、その試験・sig・生成物。`vendor/`・`lib/upstream/` の上流の gem の写し・sdkconfig・シェル・
ほかのリポジトリは触らない (上流の gem のビルドの設定のコメントアウトは外さない)。シリアルの MIDI 出力の動きを変えない。
ブランチは今の `app-uart`。コミットしてよい (件名は `<種類>(<場所>): <要約>`、英語)。push しない。

## 確認

- `rake regress` (落ちたものだけ見る。描画の記録の基準が変わるなら理由を report に書いて別コミット)、足した試験、
  `rake sig:coverage`。
- sim: `rake clean` → `rake build:linux` → ターミナルのアプリを開き、ループバックで打った行が戻って出ること、FIFO を
  置いて外から流した文字 (ASCII・かな・長い行・1 KB を続けて) が出ること、送った行が FIFO に出ること、を画面と
  ファイルで確かめる (画面を撮る)。Open したままアプリを閉じ、開き直して同じ番号を Open できること。本体の番号・
  システムのピンを選んだときの理由の文。2 つ目のアプリから同じ番号を開いたときの断り。Retro と Modern の両方。
- 内蔵 RAM の静的な増分が無いこと (足した大域の変数を数えて report に書く。`regress:size` は親が流す)。
- **ESP32 のビルド・実機・シリアルは触らない** (親がする)。重い道具 (`regress:run`・`size`・`screens`) も流さない。

## 止まる条件 (止まったら report に書いて返す)

- 利用者から見た動き (仕様) が変わる選択が要るとき。plan.md 4 章と食い違う事実が出たとき。
- 触ってよい範囲の外を変える必要が出たとき (とくに MIDI・I2C・GPIO の今の動き)。
- 境界の生成の仕組みを作り変える必要が出たとき、内蔵 RAM の静的な増分が避けられないとき。

## report (`doc/app_uart/report/u1.md`)

メソッドごとの動き (上流との違いを含む)、断る理由の一覧、機種ごとの番号、測った数、撮った画面の置き場
(リポジトリの外の作業用の場所)、見立てと違った点、撤回した仮説、残件 (U3 の実機で見る事)。実在の値を書かない。
終わったら sim を止め、置いた FIFO と試験用のファイルを消す。
