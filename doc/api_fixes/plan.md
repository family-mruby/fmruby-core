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

### 決定 4 回目 (2026-11-25 ユーザ。B の残り 9 個)

12. **MIDI の `playing?`** (ほかのプレーヤー・アプリの予約も数える見込み): 再現して直す (自分が予約した分だけ数える)。
13. **`File.expand_path` の基準と `Dir.chdir` の後の相対パス**: P4-Nano と sim で実際の動きを表にして報告する。直さない。
14. **書くだけの mode で `read`**: 例外 (IOError) にする。逆向き (読むだけの mode で `write`) も調べてそろえる。
15. **`JSON.generate` の NaN・Infinity**: `JSON::GeneratorError` にする。
16. **`I2C.new` の失敗の文**: 原因ごとの文にする (例外のクラスは変えない)。GPIO と一緒に。
17. **`TableClient#leave`**: nil を返す。
18. **Spinel の基底の `app_settings`**: 記録だけ (Spinel で設定が要るアプリを作るときに足す)。
19. **`ROS::Subscription#handler=`**: Asterism の gem (元のリポジトリ) を直す。リリースと取り込み直しが要る (F5)。

### F4 の結果 (2026-11-25、report/f4.md)

C1〜C6 を直し、C7 は調べた。親の検収: P4-Nano 用にビルドして焼き (`45af62d0`)、確認用のアプリで: 空きのピン 48 は
取れて読める・台帳で「アプリが使用中」、システムのピン 35・37・38 の `GPIO.new`・`set_dir_at` は IOError
("is used by the system")、`read_at(35)` は読める、無い番号 60 は ArgumentError、書くだけの mode の `read` と
読むだけの mode の `write` は IOError、`JSON.generate` の NaN は GeneratorError。crash 0。
回帰: `rake regress`・`regress:size ALL=1` (5 端末のビルド)・`regress:live` 17/17・`regress:run` が通過。
未確認: I2C の文、別のアプリとのピンの取り合い、gpio_viewer の表示、SD (P4-Nano にカードが入っていない)、
StickS3 のボタン (KEY1) と step_sensor、NARYA v3 で step_sensor が IOError を rescue して続くこと。

相対パス (C7) の P4-Nano での実際: `Dir.getwd` は "/"、`Dir.chdir` は RuntimeError、chdir の後も "/"、相対パスは
ルートからの意味 (サブの読みどおり)。sim は chdir が成功し、その後は全アプリの全てのパスが外れる (不具合)。

### 決定 5 回目 (2026-11-25 ユーザ)

20. **GPIO の台帳の穴は (b)**: GPIO だけが見る「アプリが触ってはいけないピン」の表を機種ごとに持つ (台帳は変えない。
    `fmrb_pin_assign.h` から作るビルド時の定数。読むだけは今までどおり通す) (F6)。
21. **相対パスは (b)**: アプリごとに「今いる場所」を持ち、`Dir.chdir` を実機でも使えるようにする。ファイルの HAL と
    `File.expand_path` が相対パスにそれを付ける。sim の不具合 (chdir の後に全てのパスが外れる) もこれで消す (F7)。
22. **zenohd を 1.10.2 に上げる** (手元で確かめてから。console の compose・VPS・asterism-watch の `running`)。

### 見つかったこと: StickS3 と AtomS3R のファームの区画の空きがほぼ無い (2026-11-25)

`regress:size ALL=1` で、STICKS3 の空きが 5,344 バイト、ATOMS3R が 20,128 バイト (どちらも区画 5,767,168 バイトの 0%)。
基準 (11-07) から +738 KB で、大きいのは sig の型データベース (どの端末にも入る。`family_mruby.gembox` の
`picoruby-ti`)。F6・F7 でコードが増えると、この 2 つはビルドが通らなくなる。

23. **決定 (2026-11-25 ユーザ): AtomS3R と StickS3 のビルドから、エディタと型まわり (型エンジン・型データベース・
    エディタの C の部品) を外す**。この 2 つは単一のアプリを動かす Asterism のノードで、キーボードが無く、F1 のヘルプと
    見本は既に storage から外してある。区画は変えない (F8)。

### F8 の結果 (2026-11-25、report/f8.md)

`FMRB_EDITOR` (既定 1、`MINI_BUILD_DEFAULTS` で 0。ESP32 のファームだけが読む) で、AtomS3R と StickS3 からエディタの
アプリ・editor-core・型エンジンと db・fmrb-debug を外した。ファームは約 517 KB 小さくなり、区画の空きは STICKS3
5,344 → 522,752 バイト、ATOMS3R 20,128 → 537,552 バイト。NARYAv3・TAB5 は大きさも内蔵 RAM も差なし。エディタを
開こうとすると `FMRB_ERR_NOT_SUPPORTED` で断る。checkpoint の表は名前が 80 減るが、移動は名前で引き当てるので
壊れない (サブの判断。`regress:checkpoint` は未実行)。親の検収: 差分を読み、大きさの記録を照合、`rake regress` 通過。
未確認: 2 つの端末の実機、移動の試験、NARYAv4 の大きさ。
見つかった別の物 (直していない): 環境変数で選ぶほかのビルドの選択 (`FMRB_ASTERISM`・`FMRB_FONT_JA`・`FMRB_IMU` ほか) も、
同じ build ディレクトリで値を変えると CMake が構成し直さず食い違う。守りは `FMRB_EDITOR` にだけ付いた。

### F6 の結果 (2026-11-25、report/f6.md)

機種ごとの「アプリが GPIO として設定してはいけないピン」を 64 ビットの定数にし (`GPIO_RESERVED`。RAM は使わない)、
`fmrb_pin_manager_claim_gpio` が台帳より先に見る。読むだけは通す。`FmrbHw.pin_status`・`pin_available?` は表のピンを
「システム」「取れない」と答える。台帳・I2C・RMT は変えていない。P4 の 2 機種の SDIO と子チップのリセットは、ビルドの
ときに sdkconfig と `_Static_assert` で照合する。

親の検収 (ピンの根拠の確認を含む。ユーザの依頼):
- 全 5 機種で、ヘッダの全ピンの定義と表を機械的に突き合わせた: 表のピンは全部名前がある。名前があって表に無いのは
  ボタン・Grove (`I2C2`)・AtomS3R の IMU の割り込み 16・StickS3 の PM1 の割り込み 13 だけ。
- チップ内部のピンを ESP-IDF のヘッダで確かめた: S3 の MSPI は 26〜32 (flash) と 33〜37 (8 線のデータ)、S3 の UART0 は
  43・44、P4 の UART0 は 37・38。**サブの表は S3 の 35〜37 しか持っていなかったので、26〜34 を親が足した** (`e5a54727`)。
- P4-Nano に焼いて (`e5a54727`): SD 39・SDIO 14・I2S 11・I2C 7・アンプ 53・Ethernet 28 の設定は IOError、`read_at(39)` は
  読める、48 は取れる、`pin_status(39)` は 1、`pin_available?(14)` は false・`(20)` は true。WiFi は生きている。crash 0。
- `rake regress`・`regress:size ALL=1` (5 端末のビルド) 通過。空きは STICKS3 522,496・ATOMS3R 537,312 バイト。
未確認: Tab5・AtomS3R・StickS3・NARYA v3 の基板の配線 (ヘッダの値を正とした。回路図とは照らしていない) と実機、
P4-Nano の 24・25 (文書に無く表に入れていない)、gpio_viewer の表示。
NARYA v3 のシリアルのログのピン (UART0 の 43・44) は、ユーザ決定 (2026-11-25) で表に足した
(`FMRB_PIN_CONSOLE_TX`・`_RX`。アプリ用には出していない)。

### F7 の設計 (決定 21: アプリごとに「今いる場所」を持つ)

- 今: `Dir.chdir` は HAL の `fmrb_hal_file_chdir` がプロセスの `chdir()` を呼ぶ。ESP32 では必ず失敗し (ESP-IDF の chdir は
  ENOSYS)、`Dir.getwd` は "/" のまま。sim では成功してプロセス全体の場所が変わり、`fmrb_hal_file_resolve_path` の起点が
  ずれて全アプリの全てのパスが外れる。
- 直した後:
  1. **今いる場所はアプリ (タスク) ごと**。最初は "/"。アプリが終わると消える。ほかのアプリには見えず、引き継がない。
     アプリでないタスク (カーネル・ホストの C のタスク) は常に "/"。
  2. `Dir.chdir(path)` は、在るフォルダなら成功 (どの端末でも同じ)。無い・フォルダでない・長すぎるは例外 (今の例外の
     クラスに合わせる)。相対パスの chdir は今いる場所から。`..` と `.` をまとめる。ルートより上には出ない。
     ブロックの形 (`Dir.chdir(path) { }`。終わったら元に戻る。例外でも戻る) を使えるようにする。
  3. アプリが渡すパスを実際のパスに直す所 (`fmrb_hal_file_resolve_path`) で、相対パスに今いる場所を付ける。これで
     `File.open`・`File.exist?`・`File.delete`・`Dir.entries`・`Dir.mkdir`・`File.rename` ほか HAL を通る全部がそろう。
     絶対パスの動きは変えない。
  4. `Dir.getwd` はその場所を返す。`File.expand_path` は `ENV["PWD"]` でなく `Dir.getwd` を基準にする。
  5. posix (sim・ネイティブ版・ブラウザ版) はプロセスの `chdir()` を呼ばない (全アプリのパスが外れる不具合が消える)。
  6. シェルの `cd` (自前で場所を持つ) は変えない。シェルから起動したアプリに場所を渡すこともしない (今までと同じ)。
- メモリ: 場所の文字列をアプリの枠の数だけ持つ。内蔵 RAM でなく PSRAM (か、最初の chdir のときに確保)。増分を測る。

### F7 の結果 (2026-11-25、report/f7.md)

設計の 1〜6 のとおり。場所はファイルの HAL の中の表 (`fmrb_hal_file_cwd.c`。アプリの枠 × 128 バイト、ESP32 では PSRAM、
1,156 バイト)。アプリの層が起動時に「今のタスクの枠の番号」を返す関数を HAL に入れる。アプリの終わりと kill で消す。
ESP32 の名前の解決は `fmrb_hal_file_path_esp32.c` に移った (ホストの試験で組める)。絶対パスの解決は直す前と同じ
(41 通りの表で照合)。ホストの試験は posix 570・ESP32 217。同梱のコードに相対パスを使う所は無い。
親の検収: ESP32 のビルドが checkpoint の表の段で落ちたのを直した (`0cf10941`。サブがネイティブ版で直したのと同じ原因)。
P4-Nano に焼いて (`0cf10941`) 確認用のアプリで 18 項目 (getwd・chdir・相対パスの読み書き・expand_path・ブロックの形・
無い場所・`..`・ルートより上・chdir の後の絶対パス) が期待どおり、ファイルマネージャと麻雀が普通に動く、crash 0、
内蔵 RAM の空きは前と同じ水準。回帰: `rake regress`・`regress:size ALL=1` (DIRAM は 5 端末とも前と同じ)・`regress:run`・
`regress:live` 17/17・`regress:screens` 28/28 が通過。
未確認: SD への chdir (P4-Nano にカードが無い)、Spinel のアプリからの `Dir.chdir`、S3 の実機。

### F5 の結果 (2026-11-25)

Asterism の gem (ruby-asterism/asterism) を 0.14.1 にした (`a1019df`、タグ v0.14.1、push 済み、CI 成功):
`ROS::Subscription#handler=` が自分の node に登録し、`node.subscription` で作った物に後から付けても `node.poll` が呼ぶ。
nil で止まる。試験 (後から付ける・nil・2 回付けても 2 重に届かない) を足し、直しを外すと落ちることを確かめた。
fmruby-core は `ASTERISM_PIN` を上げ、sig の `handler=` の説明を直した。`rake regress`・`regress:live` 17/17 通過。
未確認: mruby の上での動き (直したファイルは CRuby と同じ `mrblib/ros.rb`。CRuby の試験で確かめた)。
rubygems.org への公開はしていない (ユーザ判断)。

## 3. スコープ

- F1: A の 12 個。
- F2: B のうち、ユーザが直すと決めた 4 つ (上の「B の決定」)。
- F3: 上の「B の決定 2 回目」の 4 つ。
- F4: 決定 9 (GPIO の (a))・12〜17。
- F5: 決定 19 (Asterism の gem の `handler=`)。
- F6: 決定 20 (GPIO 用の表)。F7: 決定 21 (アプリごとの今いる場所)。
- F8: 決定 23 (小さい端末からエディタと型まわりを外す)。F6・F7 より先に行う。

## 4. 受け入れ条件

- A の各項目について、直す前の再現 (または再現しなかった記録) と、直した後の確認が報告にある。
- ホストの試験か回帰の場面が、直した動きを固定している (できる物)。
- `rake regress` が通る。sim のシェルで直した動きが見える。sig の説明が直した後の動きに合っている。

## 5. 未確定事項

- B の各項目 (ユーザの判断)。
- A3 の直し方 (本体のヘッダを使う形で直せるか。直せなければ止まる)。
