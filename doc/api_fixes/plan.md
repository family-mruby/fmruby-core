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

## 3. スコープ

- F1: A の 12 個。
- F2: B のうち、ユーザが直すと決めた 4 つ (上の「B の決定」)。
- B の残り (まだ聞いていない): `File.expand_path` の基準、`Dir.chdir` の後の相対パス、`Kernel.*` の task_id の上限、
  `Kernel::MSG_TASK_HOST`・`MSG_TASK_SYSTEM` の番号が本体と合わない、`I2C.new` の失敗の文、`JSON.generate` の NaN、
  `SmfPlayer#drop_queued` の共有の待ち行列、Spinel の基底の `app_settings`、枠なしの Window の定数 5 を sig に載せるか、
  `TableClient#leave` の戻り値、`ROS::Subscription#handler=` (A11)、書くだけの mode で `read` が "" を返す、
  `rescue Foo => e` の型。

## 4. 受け入れ条件

- A の各項目について、直す前の再現 (または再現しなかった記録) と、直した後の確認が報告にある。
- ホストの試験か回帰の場面が、直した動きを固定している (できる物)。
- `rake regress` が通る。sim のシェルで直した動きが見える。sig の説明が直した後の動きに合っている。

## 5. 未確定事項

- B の各項目 (ユーザの判断)。
- A3 の直し方 (本体のヘッダを使う形で直せるか。直せなければ止まる)。
