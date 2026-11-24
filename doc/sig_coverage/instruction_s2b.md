# S2b 指示書: Machine・GC・乱数と、上流由来のクラスの足りないメソッドを sig に書く

> 2026-11-24 | 親セッションからサブ (Sonnet) への指示。計画は plan.md (5 章の S2、7 章の決定)。書き方の正は
> `sig/README.md`。前の一括は report/s2a.md (同じ進め方。報告の形はこれに合わせる)。

## 目的

同梱のアプリがよく使うのに sig に無いもののうち、次を埋める。

1. **`Machine`** (時刻と待ち。`board_millis` 115 回・`uptime_us` 14 回・`delay_ms` ほか)。
2. **`GC`** (`start`・`stat` ほか。21 回)。
3. **乱数** (アプリが使っている呼び方を `tmp/sig_coverage/report.md` と `flash/app/` から拾う。`rand`・`srand`・
   `random_int` など、実際に使われていて実装があるもの)。
4. **上流由来のクラスの足りないメソッド** (`String`・`Array`・`Hash`・`Integer`・`Float`・`Object`・`Kernel`・`Symbol`・
   `Range`・`Proc`・`NilClass`): **同梱のアプリが呼んでいて、端末の実装にあり、sig に無いもの**。report.md の
   「受け手の型が決まらない呼び出しで、どのクラスにも無い名前」(`round`・`freeze`・`pack`・`key?`・`rindex`・
   `equal?` など) と「受け手なしの呼び出し」から、これらのクラスのメソッドに当たるものを拾う。アプリ自身や
   ほかの gem のメソッド (`table`・`token`・`slides` など) はこの一括の対象外。

`File`・`Dir`・`Time`・`IO` は次の一括 (S2c) なので書かない。

## やること (fmruby-core、ブランチ `sig-s2b`)

1. `rake sig:coverage` を流し、`tmp/sig_coverage/report.md` から上の 1〜4 に当たる行を控える (前の数)。
2. **実装を読む** (推測で書かない)。端末のビルドに入っている定義を突き止める。
   - `Machine`: picoruby の `picoruby-machine` と、Family mruby の側の差し替え (`lib/upstream/`・`lib/gems/`) の両方。
     **機種やビルドで無いメソッド** (sim・ブラウザ版・ネイティブ版で使えないもの) は、長文にそう書く。単位 (ms か µs か)、
     戻り値の型、桁あふれ (何日で一周するか) は、つまずく所なので実装を読んで書く。`sleep_ms` と `delay_ms` の違い
     (アプリの中で止まるかどうか) のように、名前が似ていて振る舞いが違うものも同じ。
   - `GC`: mruby 本体の gc と、Family mruby が足したもの。`stat` の戻り値のキーは実装のとおりに書く。
   - 乱数: どの gem のどの関数か。種を入れないと毎回同じ並びになるか、範囲の端を含むか。
   - 上流由来のクラス: mruby 本体 (`src/`)、`mrblib/`、ビルドに入っている mruby の gem (`mruby-string-ext` など)。
     **ビルドに入っている gem だけ**が対象 (どの gem が入るかは `lib/upstream/` の build config と gembox で確かめる)。
     CRuby にあって端末に無いものは書かない。アプリが呼んでいるのに実装に見つからないものは、書かずに報告に並べる
     (アプリの側の不具合かもしれない)。
3. **sig に書く**。`Machine` は `sig/machine.rbs`、`GC` は `sig/gc.rbs` (新しいファイル)。上流由来のクラスは、その
   クラスのファイルに直接足す (`sig/README.md` の「上流由来の 15 ファイル」の決まり: 足すだけ、既にある宣言と
   要約は変えない、近い名前の隣に置く)。
   - 要約は `日本語 <<en>> English` で 111 バイトまで。素直なものは「例だけ」、つまずくものは型紙の順。
   - 省略できる引数は `?`、キーワードは `name:`、形が複数あるものは `|` で重ねて、実装のとおりに書く。
   - 例は同梱のアプリでの実際の使い方に合わせる (39 桁以内)。強調 `**` は 1 行の中で閉じる。
4. **確かめる**: `rake ti:test`、`rake regress`、`rake sig:coverage` (対象の「無い」が、書かなかった理由のあるもの
   だけになる)。`rake sig:coverage:update` で基準を更新し、別コミットにする。型データベースの各領域の大きさの
   前後を控える。**ビルドと sim の確認はしない** (親が検収でまとめて行う)。

## 報告に必ず書くこと (doc/sig_coverage/report/s2b.md)

1. **根拠の表**: 書いたメソッドごと (上流由来のクラスは 1 つずつ、`Machine`・`GC` は仲間ごとでよい) に、読んだ実装の
   ファイルと行 (`パス:行`) と、どの gem・どのビルド設定に入っているか。
2. 書いた数、素直 / つまずくの内訳、つまずくと判断したものと理由。
3. 実装にあるが書かなかったものと理由 / 判断が要るもの / **アプリが使っているのに実装に見つからなかったもの**。
4. 数える道具の前後の数、型データベースの大きさの前後。
5. **確かめていないこと** (動かして確かめていない振る舞い、機種ごとの違いで読んでいない所) を、確かめたことと
   分けて書く。
6. 迷った所・型紙で足りなかった所。

## 範囲と止まる条件

- 触ってよい: `sig/machine.rbs`・`sig/gc.rbs` (新規)、上流由来の sig ファイルへの追加 (`string.rbs`・`array.rbs`・
  `hash.rbs`・`integer.rbs`・`float.rbs`・`object.rbs`・`kernel.rbs`・`symbol.rbs`・`range.rbs`・`proc.rbs`・`nil_class.rbs`)、
  `sdk/` (境界の検査が落ちて作り直しが要るときだけ。`ruby tools/boundary/gen.rb`)、`tools/sig_coverage/baseline.json`、
  `doc/sig_coverage/report/s2b.md`、`rake docs:index` による `doc/README.md`。
- 実装・アプリ・sig の既にある宣言と要約・`sig/README.md`・plan.md・`tools/sig_coverage/` の道具と ignore.yml・
  型エンジンの試験の期待値は変えない (試験が落ちたら止まって報告)。
- ビルドをしない (sim・ESP32・ブラウザ版・ネイティブ版とも。`rake clean`・`rake build:*` を流さない)。
- 止まる条件: 定義が見つからない仲間がまとまってある / 型エンジンの試験が sig の追加で落ちる / 範囲の外の変更が要る。
  止まったら、そこまでをコミットして報告に書く。
- push しない・マージしない。ユーザに質問しない。実在の値 (ドメイン・IP・ノード名・鍵) を書かない。
  コメントとコミットは英語 (件名は `feat(sig): ...`・`test(sig_coverage): ...`・`docs(sig_coverage): ...`)、報告は日本語 (常体)。
