# sig の作業で見つかった API の不具合を直す

> 状態: 進行中 | 更新: 2026-11-25 | sig を書くために実装を読んで見つけた不具合の疑い (doc/reference/TODO.md) のうち、書いてある意図と動きが食い違っている物を直す (F1)。利用者から見た動きを決め直す物はユーザに選択肢で聞く (2 章の B)

## 1. 目的

doc/archive/sig_coverage (S2〜S5) で、API の型を書くために実装を読んだとき、不具合の疑いが 30 ほど見つかった。
記録は doc/reference/TODO.md。このうち「実装のコメント・文書に書いてある意図と、動きが食い違う物」を直し、
sig の説明を直した後の動きに合わせる。

## 2. 方針

- **A (直す)**: 意図がはっきりしていて、直すと「書いてあるとおりになる」物。直す前に sim かホストで再現し、
  再現しなかった物は直さず記録する。
- **B (ユーザが決める)**: 動きを決め直すことになる物。直さず、選択肢を用意して聞く。
- **C (実機が要る)**: ESP32 の `Machine.delay_ms` (実機で測ってから。ユーザ決定)、ESP32 のファイル HAL の経路の表。
- 上流 (PicoRuby) の gem の中の物は、`components/` を直接直さず `lib/upstream/` の差し込みで直す (CLAUDE.md)。
  上流に出せる形なら doc/reference/picoruby_upstream_pr_candidates.md に候補として記録する。

### A の一覧 (F1)

| # | 物 | 記録にある疑い |
|---|---|---|
| A1 | `File.path("x")` (クラスメソッド) | nil を返す (sim で確認済み) |
| A2 | `File.open` の mode | `"a+"` が追記にならず先頭から上書き (sim で確認済み)。`"rb+"` が読み取り専用になる読み |
| A3 | `Kernel.send_message`・`receive_message`・`broadcast_message` | メッセージの構造体が本体と違い、スタックを壊す見込み。直したら sig に戻す (決定 25 の続き) |
| A4 | `JSON::Digger#dig` | 範囲の外の位置で例外が出ず別の要素が返る (sim で確認済み)。冒頭のコメントは例外と書く |
| A5 | `JSON.parse` の `\uXXXX` | ASCII の外 (日本語) が 1 バイトに丸められる読み |
| A6 | `MIDI::Device#pitch_bend` | この mruby に無い `Integer#clamp` を使う読み |
| A7 | `SmfPlayer#transpose=` | 鳴らしている間に変えると note off が別の高さに行き音が残る疑い |
| A8 | `MmlPlayer#load_text` | 空のパートの後に鳴るパートが来ると `error` が残る |
| A9 | `GPIO#read` の失敗 (-1) | `high?` が true になる |
| A10 | `gpio_viewer.app.rb` | `PIN_COUNT = 49` の直書き (P4 は 55)。`FmrbHw.pin_count` を使う |
| A11 | `Asterism::ROS::Subscription#handler=` | `node.subscription` に後から付けても `Node#poll` が呼ばない (Asterism の gem。直す場所を確かめる) |
| A12 | `FmrbI18n.add` の長文 | 「Existing keys are left alone」が実装 (上書きする) と合わない (sig の文の直し) |

### B の一覧 (ユーザに聞く。F1 では直さない)

`IO#puts` が配列を展開しない、`File.expand_path` の基準 (`ENV["PWD"]`)、`Dir.chdir` の後の相対パス、
`FmrbUI#bg=` が既にある部品の色を変えない、`Kernel.*` の task_id の検査 (0〜15) と本体の上限 (9)、
`GPIO.new` がピンの台帳を見ない、`I2C.new` の失敗の文、`JSON.generate` の NaN、`SmfPlayer#drop_queued` が共有の
待ち行列を空にする、Spinel の基底に `app_settings` 系が無い、アプリが `_` で始まるメソッドを呼んでいる
(公開の名前を用意するか)、`FmrbGfx#draw_text_mixed` と枠なしの Window の定数 5 を sig に載せるか、
`TableClient#leave` の戻り値、`rescue Foo => e` の型 (型エンジンの機能の追加)。

### F1 の結果 (2026-11-25、report/f1.md)

A1〜A8・A10 を直し、A12 は sig の文を直した。A9 (`GPIO#read` の失敗) は意図が実装から読めず B へ、A11
(`ROS::Subscription#handler=`) は Asterism の gem の元のリポジトリの物なので止めた (直し方の案は報告の 2 章)。
親の検収: 差分を読み、sim で `a+` の追記・`File.path`・`Digger` の DiggerError・`pitch_bend`・`Kernel.send_message` と
`receive_message` (落ちない。自分の番号では HID の伝言が先に来る) を再現、`rake regress:size` で S3 と P4 のビルドが通る
(S3 のファームの区画の空きは 269 KB = 4%)。ホストの試験 `rake api_fixes:test` が `rake test` に入った。
未確認: ESP32 の実機での `a+`、P4 の gpio_viewer。

### B の決定 (2026-11-25 ユーザ) -- F2 で直す

1. **`IO#puts` は mruby に合わせる**: mruby 本家 (mruby-io の `io_puts`) と同じく、配列は要素ごとに 1 行 (入れ子も)、
   引数なしは改行だけ、末尾が改行の文字列には改行を足さない。
2. **`FmrbUI#bg=` は既にある部品にも反映する**。
3. **GPIO は両方とも例外にする**: システムが使っているピン (ピンの台帳で取れないピン) の `GPIO.new` と、`read` の失敗。
   同梱のアプリを調べてから直す。実機の確認が要る (C)。
4. **直前のエラー文を読む公開の名前を作る** (`FmrbApp._get_last_error` → 公開の名前。sig に書き、AI ヘルパーを
   切り替え、古い名前は消す)。midi_bench の 3 つは試験用なのでそのまま。

### F2 の結果 (2026-11-25、report/f2.md)

B1 (`IO#puts`。Kernel の `puts` も `$stdout.puts` を呼ぶだけなので一緒に変わる)・B2 (`FmrbUI#bg=`。部品の ivar の数は
変えていない)・B4 (`FmrbApp.last_error`) を直した。親の検収: 差分を読み、sim で `puts [1, [2, 3]], "a\n", 4` が
5 行、`FmrbApp.last_error` が nil・古い名前が無いことを再現。
**B3 (GPIO) は止めた**: 同梱の `flash/app/test/step_sensor.app.rb` が StickS3 の KEY1 (GPIO 11。システムのピン) を
`GPIO.new(11, GPIO::IN)` で読んでいて、例外にすると KEY1 で歩数を 0 にする機能が無くなる (設計どおりの使い方。
doc/headless_s3/report/m3s.md)。直し方の候補: (a) 台帳に「アプリが入力として読んでよいシステムのピン」の区別を足し
IN だけ通す、(b) 例外にして step_sensor を別の読み方に変える、(c) システムのピンは今のまま通し、ほかのアプリが
使用中のピンと read の失敗だけ例外にする。**ユーザの判断待ち**。あわせて見つかった物: 台帳で断られた `GPIO.new` も
続く `set_dir_at` でシステムのピンの向きを変えてしまう、`*_at` のクラスメソッドも台帳を通らない。

### C: `Machine.delay_ms` (2026-11-25、実機で測って直した)

P4-Nano で測った (計測アプリを WiFi で送って起動): `delay_ms(300)` = 30.4 ms、`(100)` = 10.5 ms、`(10)` = 1.3 ms、
`(5)` 以下は約 0.05 ms (眠らない)。読みどおり `vTaskDelay(ms / 10)` が 1 ms の tick で 1/10 になっていた。
`vTaskDelay(pdMS_TO_TICKS(ms))` に直した (`lib/upstream/replace/picoruby-machine/ports/esp32/machine.c`)。
直した後の実機の確認 (同じ計測、起動ログ、Asterism の待ち、ランチャー) は F3 の後にまとめて焼いて行う。

### B の決定 2 回目 (2026-11-25 ユーザ) -- F3 で直す

5. **`Kernel::MSG_TASK_HOST`・`MSG_TASK_SYSTEM` を本体の番号に合わせる** (本体の定義から取る。カーネルの番号の定数も
   足す。受け付ける番号の上限も本体の上限に合わせる)。
6. **`SmfPlayer#drop_queued` は調べてから直す**: ホストの試験で再現し、再現したら「自分が予約した分だけ消す」形に。
   予約 1 件あたりのメモリの増分を測る。
7. **`rescue Foo => e` の `e` に型を付ける** (型エンジン。クラスを省いた rescue は StandardError、複数ならそのどれか)。
8. **定数は `TRANSPARENT_COLOR` と `FmrbGfx::FONT_METRICS` だけ sig に載せる**。`CLOSE_BTN_CX_OFFSET`・`CLOSE_BTN_CY`・
   `CLOSE_BTN_HIT_R` は載せない一覧へ (見本がそれを読んでいる点は課題に残す)。

### F3 の結果 (2026-11-25、report/f3.md)

4 つとも直した。B5: 定数は本体の `PROC_ID_*` から (KERNEL 0・HOST 1・SYSTEM 2)、番号の上限は `PROC_ID_MAX`。
B6: 共有だったのは「1 つのアプリの中の 2 つのプレーヤー」(アプリの間は前から分かれていた)。sim で再現し、予約に
持ち主の番号を付けて自分の分だけ消す形に。予約 1 件は 16 バイトのまま (増分 0)。B7: 型エンジン (fork `5c5a04c`、
未 push) が `rescue Foo => e`・`rescue A, B => e` (union)・`rescue => e` (StandardError) を解く。メソッドの本体の
rescue が評価されず戻り値が nil になっていた前からの不具合も直った。probe のメモリは前後同じ、コード +978 B。
B8: `TRANSPARENT_COLOR`・`FmrbGfx::FONT_METRICS` を sig に、`CLOSE_BTN_*` は載せない一覧へ。
親の検収: 差分 (MIDI の待ち行列の C を含む) を読み、sim のエディタで `rescue JSON::ParserError => e` の後の
`e.message(1)` と `rescue => f` の後の `f.backtrace(2)` が指摘されるのを再現。
見つかった別の物 (直していない): MIDI の `_depth`・`_free` が待ち行列の全体を数えるので、ほかのプレーヤーやアプリが
鳴らしている間 `playing?` が true のまま残る見込み (読みだけ)。

### 実機 (P4-Nano) とまとめの回帰 (2026-11-25)

- **P4-Nano に焼いて確かめた** (develop `1e89a5f6`、`FMRB_HW_TARGET=NARYAv4` で作り `rake flash:app_sync`):
  起動ログに crash 0、周期ダンプ 10 秒ごと、内蔵 RAM の空きは直す前と同じ水準 (起動後 208 KB 台)。
  `delay_ms`: 300 → 300.5 ms、100 → 100.3 ms、10 → 10.0 ms、5 → 5.3 ms、2 → 2.5 ms、1 → 1.5 ms (直す前は約 1/10、
  10 ms 未満は眠らない)。確認用のアプリを WiFi で送って: `a+` の追記と無いファイルの作成、`rb+`、`File.path`、
  Digger の DiggerError、`\u3042` が UTF-8 の 3 バイト、Kernel の定数 0・1・2 と番号 9 の ArgumentError、
  `FmrbApp.last_error`、`pitch_bend`、`puts` の配列が 1 要素 1 行、がどれも意図どおり。
  麻雀 (スタック 24 KB): ロビー → 卓 → 対局 5 巡でスタックの余り 9,784 バイト (和了・結果の画面は未確認)。
  未確認: S3 (Retro)・StickS3 の実機、GPIO、Asterism の待ちの間の CPU の計測、ランチャーを開く時間の比較。
- **回帰**: `rake regress`・`regress:live` (17/17)・`regress:screens` (28/28)・`regress:size` (S3・P4 のビルド) は通過。
  **`regress:run` (VM プール) は基準 (2026-11-21) から外れた**: system_desktop +1.7〜2.1 KB、FlappyDemo +768 B。
  直しの前の版 (`521eb2e7`) で流すと system_desktop は既に +1.9 KB (2026-11-24 の起動失敗のエラーメッセージの分)。
  F1〜F3 の分は system_desktop +約 0.2 KB、アプリ 1 つあたり +約 0.8 KB (FlappyDemo)。`IO#puts` の補助のメソッドを
  1 つにまとめる形を試したが減らなかった (揺れの幅の内) ので戻した。mrblib に足したコードの分と見ている (切り分けは
  そこまで)。**基準は更新していない** (ユーザの確認待ち)。

### 決定 3 回目 (2026-11-25 ユーザ)

9. **GPIO は (a)**: 台帳に「アプリが入力として読んでよいシステムのピン (本体ボタン)」の区別を足す。ボタンは入力としてだけ
   通し、出力にすることと、画面・SD・USB などのピンの `GPIO.new` は例外。`read` の失敗も例外 (F4)。
10. **VM プールの増分は受け入れ、基準を更新する** (system_desktop +約 2 KB、アプリ 1 つ +約 0.8 KB)。更新済み。
11. B の残り 9 個は 1 つずつ聞く。

## 3. スコープ

- F1: A の 12 個。
- F2: B のうち、ユーザが直すと決めた 4 つ (上の「B の決定」)。
- F3: 上の「B の決定 2 回目」の 4 つ。
- B の残り (まだ聞いていない): GPIO の直し方 (F2 で止めた)、`File.expand_path` の基準、`Dir.chdir` の後の相対パス、
  `I2C.new` の失敗の文、`JSON.generate` の NaN、Spinel の基底の `app_settings`、`TableClient#leave` の戻り値、
  `ROS::Subscription#handler=` (A11)、書くだけの mode で `read` が "" を返す。

## 4. 受け入れ条件

- A の各項目について、直す前の再現 (または再現しなかった記録) と、直した後の確認が報告にある。
- ホストの試験か回帰の場面が、直した動きを固定している (できる物)。
- `rake regress` が通る。sim のシェルで直した動きが見える。sig の説明が直した後の動きに合っている。

## 5. 未確定事項

- B の各項目 (ユーザの判断)。
- A3 の直し方 (本体のヘッダを使う形で直せるか。直せなければ止まる)。
