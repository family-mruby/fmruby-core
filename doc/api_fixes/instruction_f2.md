# F2 指示書: ユーザが決めた 4 つの動きの直し (IO#puts・FmrbUI#bg=・GPIO・直前のエラー文)

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (2 章の「B の決定」)。進め方と報告の形は F1
> (instruction_f1.md・report/f1.md) と同じ: 再現 → 直す → 試験で固定 → sig を合わせる。

## 項目

### B1. `IO#puts` を mruby に合わせる

- 今: `$stdout.puts([1, 2])`・`file.puts([1, 2])` は `[1, 2]` の 1 行 (sim で確認済み)。
- 直した後: mruby 本家の `io_puts`
  (`components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-io/src/io.c` の
  `io_puts`・`io_puts_ary`・`io_puts_str`。読むだけ) と同じ動き。配列は要素ごとに 1 行 (入れ子も展開)、引数なしは
  改行だけ、複数の引数はそれぞれ 1 行、末尾が改行の文字列には改行を足さない、文字列でない物は to_s。
  入れ子の深さの上限・自分自身を含む配列の扱いも本家に合わせる。
- うちの `IO#puts` がどこにあるか (Family mruby の gem か上流の gem か) を確かめ、F1 と同じ決まりで直す。
  Kernel の `puts` (ログに出る方) が別の実装なら、その今の動きを表に書き、**変えない** (聞かれていない)。
  ただし本家と違う点があれば報告に書く。
- 要素ごとの一時的な文字列でメモリを余分に使わない形にする (1 つの大きな文字列に連結しない)。

### B2. `FmrbUI#bg=` を既にある部品にも反映する

- 今: 部品 (label・button・toggle・stepper・text_field・スクロールバーほか) は作るときに色を写して持つので、
  後から `ui.bg = 色` をしても既にある部品の背景が変わらない (読み。まず sim で再現する)。
- 直した後: `bg=` で、既にある部品の「入れ物の背景から写した色」も新しい色になり、次の描画で反映される。
  **部品ごとに明示して渡した色は変えない** (入れ物の色を写しただけの物と区別できる形にする。区別できない作りなら、
  どう区別するかの案を 2 つ書いて止まる)。
- FmrbUI は Spinel のプログラム (デスクトップ・エディタ) にも取り込まれる (`tools/spinel/gen_app_combined.rb`)。
  Spinel の型の制約 (ブロックなし・Hash なし・ivar の並びを親子で食い違わせない。
  doc/spinel_aot/reports/editor_ivar_layout_bug.md) を守り、`rake spinel:gen` とエディタの起動 + 1 打鍵まで確かめる。
- ivar を増やすなら、部品 1 つあたりの増分 (VM プール) を報告に書く。`rake regress:run` は親が流す。

### B3. GPIO: システムが使っているピンの `GPIO.new` と、`read` の失敗を例外にする

- 今 (読み): `GPIO.new(pin, ...)` はピンの台帳 (`fmrb_pin` の取得。`main/` の pin manager と `fmrb_pin_assign.h`) への
  取得の失敗を見ない。`GPIO#read` は失敗で -1 を返し、`high?` が true になる。
- 直した後: 台帳で取れないピン (システム専用・ほかが使用中) の `GPIO.new` は例外 (今ある `GPIO::Error` があればそれ、
  無ければ ArgumentError か RuntimeError のうち、今の gem の流儀に合う物。文にピンの番号と理由)。`read` の失敗も例外。
  `high?`・`low?` は `read` を通るなら同じく例外。
- 実装は上流の gem (`picoruby-gpio`) と Family mruby の port (`lib/gems/` か `components/picoruby-esp32/` の
  CMake の `PICORUBY_SRCS` で管理している port) のどちらにあるかを確かめる。submodule の中は直接編集しない
  (`lib/upstream/` の差し込み)。
- **同梱のアプリと見本を全部調べる** (`flash/app`・`flash/usr/share/samples`・`flash/lib`): `GPIO.new` に渡している
  ピンが、各端末 (S3・P4 の各ボード。`fmrb_pin_assign.h`) の台帳で取れるピンか。**今動いているアプリが例外で
  動かなくなる組み合わせが 1 つでもあれば、直さずに一覧を書いて止まる。**
- アプリが終わったときにピンを台帳へ返す道 (close・アプリの終了時) があるかを確かめる。無いと、アプリを 2 回目に
  起動したとき自分のピンが「使用中」で例外になる。無ければ、それも直しの範囲に入れるか止まって報告する。
- sim に GPIO は無い。ホストで確かめられる範囲 (台帳の判定の試験) と、実機で親・ユーザが確かめる手順
  (どのボードで何を打つと何が出るか) を報告に書く。ESP32 のコンパイルは親が `rake regress:size` で確かめる。

### B4. 直前に落ちたアプリのエラー文を読む公開の名前

- 今: AI ヘルパー (`flash/app/modern/ai_helper/editor.rb`) が `FmrbApp._get_last_error` を呼んでいる。
- 直した後: 公開の名前 **`FmrbApp.last_error`** (クラスメソッド。戻り値と中身は今の `_get_last_error` と同じ) を
  足し、sig (`sig/fmrb_app.rbs`。要約と長文と例。いつ値が入り、いつ消えるか・どのアプリのエラーか・無いときの値を
  実装から読んで書く) に書き、AI ヘルパーとほかの呼び手を切り替え、`_get_last_error` は消す
  (C の定義なら名前の付け替え。Spinel の側・エディタ・カーネルが呼んでいないかを grep)。
- `tools/sig_coverage` の数える道具で `FmrbApp._get_last_error` が消え、`last_error` が「sig にある」になる。
- midi_bench の `_clear`・`_stats`・`_audio_note_bytes` は変えない。

## 確かめる

- 項目ごとに sim かホストで、直す前と後。最後に `rake regress`・`rake sig:coverage` (「3. doc gaps」が 0 のまま)、
  `rake clean` → `rake build:linux` → sim でシェルの irb、エディタを起動して 1 打鍵、UI の部品を使うアプリを 1 つ
  (Config か部品の見本) 開いて表示が前と同じこと。
- ESP32 のビルドはしない。ブラウザ版も作らない。

## 報告 (doc/api_fixes/report/f2.md)

F1 と同じ章立て (項目ごとの表 / 直さなかった物と理由 / 動きが変わる所と同梱のアプリへの影響 / 上流に出せる直し /
途中で見つけた別の不具合・確かめていないこと / 確認の結果 / コミット)。B3 は「実機で確かめる手順」を足す。

## 範囲と止まる条件

- 触ってよい: 4 項目の実装と試験、`flash/app/modern/ai_helper/` (B4 の呼び手)、`sig/` の該当の説明、
  `tools/sig_coverage/baseline.json`、`sdk/` (境界の作り直し)、`doc/api_fixes/report/f2.md`、`doc/README.md`。
  F1 と同じく、直しに要る HAL・port のファイルは触ってよい (報告に並べる)。
- 変えない: plan.md の「B の残り」の動き、`components/` の submodule の中、`vendor/asterism`、sdkconfig。
- 各項目の「止まる」の条件に当たったら、その項目は直さず、ほかの項目を進めて報告する。
- 利用者から見た動きが、上に書いた以外に変わる直し方が要るときは止まって報告する。
- push しない・マージしない (ブランチ `api-fixes-f2` にコミットまで)。ユーザに質問しない。終わったら sim を止める。
  コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
