# S8 指示書: エディタの型エンジンが入れ子のクラス (`A::B`) を受け手に解けるようにする

> 2026-11-25 | 親セッションからサブへの指示。決定 28 (plan.md 7 章)。現象の記録は doc/reference/TODO.md
> (「エディタの型支援が、入れ子のクラスを受け手にした呼び出しを解けない」)。型エンジンの設計は doc/editor_ti/。

## 現象と原因 (親が確かめたこと)

- sim のエディタで `l = FmrbAsterism::Link.new(self, "y")` の `l` に型が付かない (Ctrl+T は "No type here")。その先の
  補完 (Tab)・検査 (Ctrl+E) が働かない。`FmrbMidi::MmlPlayer.new(1)`、`Asterism::Types.number(:a, 1)` (モジュールの
  関数。引数が多くても指摘されない)、`JSON::Digger.new(...)` も同じ。
- 最上位のクラス (`FmrbUI.new`・`TileSheet.new`・`ArgumentError.new("x")`)、`Klass::CONST` の定数、戻り値が入れ子の
  クラスのメソッド (`FmrbMidi.device(self)` → `MIDI::Device`) は解ける。
- `vendor/picoruby-ti/src/eval/picoruby_ti_eval_handlers.c` の `ti_handle_constant_path` が「`Klass::CONST` で Klass が
  データベースのクラス」の形だけを扱い、`A::B` (と `A::B::C`) をクラスの名前として引かない。
- sig のクラスは 142 あり、入れ子のものが多い (Asterism・`Asterism::ROS::*`・`FmrbUI::*`・`FmrbMidi::*`・`JSON::*`・
  `Fmrb::Fft`・`FmrbAsterism::*`・`Digest::SHA256` ほか)。データベースでの入れ子のクラスの名前の持ち方
  (`"A::B"` の文字列か) は `vendor/picoruby-ti/tidbgen/` と生成物で確かめる。

## やること

1. **型エンジン (fork `vendor/picoruby-ti`、ブランチ `fmrb-dev`) を直す**:
   - 定数のパス (`A::B`、`A::B::C`、頭の `::A::B`) がデータベースのクラスの名前に当たれば、そのクラス (静的。
     `Foo` 単独を読んだときと同じ扱い) を返す。当たらなければ今までどおり `Klass::CONST` の定数を引く。
   - これで `A::B.new(...)`・`A::B.func(...)` の戻り値、引数の検査、`A::B.` の後の補完 (クラスメソッド)、
     `A::` の後の補完 (入れ子のクラスと定数。今どう動くかを先に確かめ、無理なく足せる範囲で)、
     `rescue A::B => e` の `e` (今 `rescue Foo => e` に型が付くなら同じに)、`x = A::B` の後の `x.new` が解ける。
   - ユーザのソースの中のクラス (`class Foo::Bar` や `module M; class C`) の扱いは、今の動きを変えない。
   - **内蔵 RAM とヒープを増やさない**: パスを組み立てるのにヒープを取らない (スタックの固定長の小さな配列か、
     節をたどって比較する)。長すぎるパスは今までどおり型なしにする。再帰の深さに上限を置く。
   - host の試験 (`vendor/picoruby-ti/host_test/`) に場面を足す: 2 段・3 段のパス、`.new` の戻り値、クラスメソッドの
     引数の検査、補完、当たらないパス (型なしのまま・落ちない)、`Klass::CONST` が今までどおり、長すぎるパス。
     試験用の sig (`tools/ti/test_sig/` か host_test の見本) が要るなら足す。
2. **fmruby-core 側**: fork にコミットし (ブランチ `fmrb-dev`、**push しない**)、`lib/upstream/PICORUBY_TI_PIN` の
   commit を新しいハッシュにする (ブランチ `ti-nested-class`)。push していない間は `rake ti:setup` が手元の
   checkout をそのまま使う (HEAD が PIN と同じなら取りに行かない) ことを確かめる。
   - ブラウザ版の型エンジン (wasm。`rakelib/ti.rake` の wasm の所) も同じソースから作られるなら、作り直しが
     要るかを確かめて報告に書く (作り直しは親が行う)。
3. **測る**: `rake ti:probe` (補完 1 回のメモリの頂点と時間) を直す前と後で。エンジンのコードの大きさの差
   (host の .o の大きさでよい)。
4. **確かめる**: `rake ti:test`、`rake regress`。その後 `rake clean` → `rake build:linux` をして、sim のエディタで
   (MCP の sim_up / sim_app (default/editor) / sim_input / sim_screenshot / sim_down。使い方はリポジトリ直下の
   CLAUDE.md と skill sim-verify):
   - `l = FmrbAsterism::Link.new(self, "y")` の `l` に Ctrl+T で型が出る。
   - `l.update(1)` が Ctrl+E で指摘される (引数が多い)。`l.update` は指摘されない。
   - `Asterism::Types.number(:a, 1)` が指摘される。`m = FmrbMidi::MmlPlayer.new(1)` の後 `m.` で補完が出る。
   - `e = ArgumentError.new("x")` と `"a".byteslice(1, 2, 3)` の指摘が今までどおり。
   - エディタを 1 回起動して 1 打鍵する所まで (標準構成の決まり) を含む。画面は scratchpad に置く。

## 報告 (doc/archive/sig_coverage/report/s8.md)

1. 直した所 (関数ごと) と、データベースでの入れ子のクラスの持ち方。
2. 足した試験の場面。`rake ti:test`・`rake regress` の結果。
3. 計測 (probe の前後、コードの大きさ)。
4. sim で見た結果 (上の項目ごと。画像の絶対パス)。
5. 直らなかった形・手を付けなかった形 (あれば、なぜ)。確かめていないこと。
6. 上流 (picoruby-ti) に出せる形か (fork の独自の拡張に依るか) の見立て。
7. コミット (fork と fmruby-core)。

## 範囲と止まる条件

- 触ってよい: `vendor/picoruby-ti/` (fork。`src/`・`host_test/`・要れば `tidbgen/`)、fmruby-core の
  `lib/upstream/PICORUBY_TI_PIN`・`tools/ti/test_sig/`・`doc/archive/sig_coverage/report/s8.md`・`doc/README.md`。
- 変えない: `sig/`・アプリ・エディタの Ruby (`main/ruby/default_app/editor/`)・エディタの C の橋。そこを変えないと
  直らないと分かったら、止まって報告する。データベースの形 (行の大きさ・領域) を変える必要が出たときも止まる。
- ESP32 のビルドはしない (`build/` に触らない)。sim のビルドはしてよい。ブラウザ版は作らない。
- **push しない** (fork も fmruby-core も)。マージしない。ユーザに質問しない。
- 利用者から見た動きが、上に書いた「解ける形が増える」以外に変わる直し方 (既にある補完の候補が減る・検査の
  指摘が増減する) が要るときは、止まって報告する。
- 終わったら sim を止める (sim_down)。コメントとコミットは英語、報告は日本語 (常体)。
