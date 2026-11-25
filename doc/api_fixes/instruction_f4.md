# F4 指示書: GPIO の台帳の区別・I2C の文・MIDI の playing?・mode と read・JSON の NaN・leave・相対パスの調査

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (2 章の決定 9・12〜17)。進め方と報告の形は F1〜F3 と同じ
> (再現 → 直す → 試験で固定 → sig を合わせる)。GPIO の事実は report/f2.md の 2 章・4 章・6 章にある。

## 項目

### C1. GPIO: 入力として読んでよいシステムのピンを台帳で区別する (決定 9)

- 今 (report/f2.md): `GPIO.new(pin, ...)` はピンの台帳への取得の失敗を見ない。断られても続く `set_dir_at` で
  システムのピンの向きを変えてしまう。`*_at` のクラスメソッドも台帳を通らない。`read` の失敗は -1 で `high?` が true。
- 直した後:
  1. 台帳 (`main/` の pin manager と `fmrb_pin_assign.h`) に「アプリが入力として読んでよいシステムのピン」の区別を足す。
     対象は**本体のボタン** (各ボードの `FMRB_PIN_BUTTON_*` に当たるピン。StickS3 の KEY1 = GPIO 11 など。ボードごとに
     `fmrb_pin_assign.h` を読んで決め、一覧を報告に書く)。ボタン以外のシステムのピンで、今アプリ・見本が読んでいる物が
     あれば一覧にして止まる。
  2. `GPIO.new(pin, GPIO::IN ...)` (入力。プルアップ・プルダウンつきを含む) は、その区別のあるピンなら通す。向きを
     出力にする指定 (`GPIO::OUT` ほか、後からの `set_dir`・`write`) は例外。
  3. それ以外のシステムのピン (画面・SD・USB・I2S・リンクほか) と、ほかのアプリが使用中のピンの `GPIO.new` は例外。
     **断られたときにピンの設定 (向き・プル) を変えない**。
  4. `*_at` のクラスメソッド (`GPIO.read_at`・`high_at?`・`set_dir_at` ほか) も同じ判定を通す。読むだけの物
     (`read_at`・`high_at?`・`low_at?`) を、システムのピンに対してどう扱うかは、今の使われ方 (gpio_viewer は全ピンを
     読んで表示する) を調べ、**gpio_viewer が今までどおり全ピンの状態を表示できる**形にする (読むだけは通す、など)。
     どうしたかを報告に書く。
  5. `read` の失敗 (-1) は例外。`high?`・`low?` も同じ。
  6. 例外のクラスは今の gem の流儀に合わせる (report/f2.md: `GPIO::Error` は例外のクラスではない。IOError か
     ArgumentError のどちらが合うかを実装から決め、文にピンの番号と理由を入れる)。
- 実装の置き場所: 上流 `picoruby-gpio` の `src/mruby/gpio.c`・`include/gpio.h` は `lib/upstream/patch/` に置く
  (picoruby-i2c と同じ形。report/f2.md)。port と台帳は Family mruby の側。
- 同梱のアプリと見本 (`flash/app`・`flash/usr/share/samples`・`flash/lib`) の `GPIO.new`・`*_at` を全部調べ、各ボードで
  動きが変わる物の一覧を報告に書く。**今動いている物が動かなくなる組み合わせが残るなら、そこは直さず止まる。**
- ホストの試験 (台帳の判定) を足す。sim に GPIO は無い。**実機で親が確かめる手順** (P4-Nano。空いているピンの
  `GPIO.new`・システムのピンの `GPIO.new` が例外で、画面と SD が無事なこと・gpio_viewer の表示) を報告に書く。

### C2. `I2C.new` の失敗の文を原因ごとに (決定 16)

- 今: 失敗の文が 1 種類 (report/s5a.md)。直した後: 原因 (ピンが使用中・その端末に無い・周波数ほか、内側のエラーコード) が
  文で分かる。例外のクラスは変えない。置き場所は今ある `lib/upstream/patch/picoruby-i2c`。sig の説明を合わせる。

### C3. MIDI の `playing?` (決定 12)

- 疑い (report/f3.md 7 章): `_depth`・`_free` が待ち行列の全体 (全アプリ・全プレーヤー) を数え、鳴り終わったかの判定が
  それを使うので、ほかのプレーヤーやアプリが鳴らしている間 `playing?` が true のまま残る。
- ホストの試験と sim (F3 の 2 つのプレーヤーの試験アプリの形。scratchpad の `f3/f3_two_players.app.rb`) で再現する。
  **再現しなければ直さない**。再現したら、自分 (持ち主の番号) が予約した分だけを数える形にする。空きの数 (`_free`:
  予約できる残り) は全体のままでよいはずだが、使われ方を読んで決める。予約 1 件の大きさ (16 バイト) は変えない。
- `MmlPlayer`・`SmfPlayer` の両方。sig の説明を合わせる。

### C4. 書くだけの mode で `read` (決定 14)

- 今: `"w"`・`"a"` で開いたファイルの `read` が "" を返す (sim で確認済み)。直した後: IOError
  (この mruby に IOError があるかを確かめる。無ければ今の IO の gem が使っている例外に合わせ、報告に書く)。
  `gets`・`each_line`・`getc` など読む系も同じ扱いに。逆向き (`"r"` で `write`・`puts`・`print`) の今の動きを表にし、
  同じ例外にそろえる。`"r+"`・`"w+"`・`"a+"` は両方できるまま。
- 同梱のアプリ・シェル・エディタ・カーネルが、この使い方に頼っていないことを grep と sim で確かめる
  (頼っている所があれば止まる)。sig (`sig/fmrb_file.rbs`・`sig/io.rbs`) を合わせる。

### C5. `JSON.generate` の NaN・Infinity (決定 15)

- 今: そのまま `NaN`・`Infinity` と出す (読み。まず sim で再現)。直した後: `JSON::GeneratorError`。入れ子の中でも。
  `lib/upstream/patch/picoruby-json/mrblib/json.rb`。ホストの試験、sig、上流の PR 候補の台帳
  (doc/reference/picoruby_upstream_pr_candidates.md の 2026-11-25 の節) に 1 行足す。

### C6. `TableClient#leave` (決定 17)

- nil を返す。sig は void のまま。ホストで確かめる。

### C7. 相対パスの調査 (決定 13。**直さない**)

- sim で、`Dir.getwd`・`Dir.chdir("/tmp")` の後の `Dir.getwd`・`File.open("a.txt", "w")` がどこに出来るか・
  `File.expand_path("a.txt")`・`ENV["PWD"]`・`File.exist?("a.txt")` を表にする。実装 (posix と esp32 の HAL、
  `file.rb`・`dir.rb`) を読み、ESP32 でどうなるはずかを同じ表に「読み」として並べる。
- **実機では流さない** (親が P4-Nano で同じ式を流す)。親が流す式の一覧 (1 行 60 文字以内、/tmp だけを使う) を報告に書く。
- 食い違いがあれば、そろえ方の案を 2〜3 個、利用者から見た違いと一緒に書く。

## 確かめる

- 項目ごとに sim かホストで。最後に `rake regress`・`rake sig:coverage` (「3. doc gaps」が 0 のまま)、
  `rake clean` → `rake build:linux` → sim でシェルの irb とエディタの起動 + 1 打鍵、gpio_viewer を開いて落ちないこと。
- ESP32 のビルドはしない (親が P4-Nano 用に作って焼く)。ブラウザ版も作らない。

## 報告 (doc/api_fixes/report/f4.md)

F1〜F3 と同じ章立て。C1 は「ボードごとの、入力として読んでよいピンの一覧」「同梱のアプリへの影響の表」
「実機で確かめる手順」を足す。C7 は表と案。

## 範囲と止まる条件

- 触ってよい: 各項目の実装と試験、`lib/upstream/patch/` (picoruby-gpio・picoruby-i2c・picoruby-json)、台帳と
  `fmrb_pin_assign.h` (区別を足す分)、`sig/` の該当、`tools/sig_coverage/baseline.json`、`sdk/`、
  `doc/reference/picoruby_upstream_pr_candidates.md` (C5 の 1 行)、`doc/api_fixes/report/f4.md`、`doc/README.md`。
- 変えない: `components/` の submodule の中、`vendor/asterism`、sdkconfig、ピンの割り当てそのもの (番号)、
  plan.md の決定 18・19 の物。
- 各項目の止まる条件に当たったら、その項目は直さず、ほかの項目を進めて報告する。
- 利用者から見た動きが、上に書いた以外に変わる直し方が要るときは止まって報告する。
- 内蔵 RAM を増やさない (台帳に区別を足すときの増分をバイトで報告する)。
- push しない・マージしない (ブランチ `api-fixes-f4` にコミットまで)。ユーザに質問しない。終わったら sim を止める。
- **シリアルのポートと実機には触らない。** コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
