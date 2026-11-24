# S0 指示書: sig の抜けを数える道具

> 2026-11-24 | 親セッションからサブ (Opus) への指示。計画は plan.md (2 章の調査、5 章の S0、7 章の決定)。

## 目的

`sig/*.rbs` に何が足りないかを、毎回同じ数え方で出す道具を作る。足す順番と「終わったか」の判定に使う。

## 作るもの (fmruby-core、ブランチ `sig-coverage`。develop から切る)

`rake sig:coverage` (本体は `tools/sig_coverage/` の Ruby。標準ライブラリと、型の生成に既に使っている rbs gem
だけ)。次の 3 つを数えて、要約 (20 行ほど) を標準出力に、明細を `build-linux` などのビルドの置き場ではない
作業用の場所 (例: `tmp/sig_coverage/report.md`、git に入れない) に出す。

1. **アプリが使っているのに sig に無いもの**: `flash/app/` (`flash/app/test/` を除く)・`flash/lib/templates/`・
   `flash/usr/share/samples/` の `.rb` が呼ぶクラス (定数) とメソッド。アプリ自身が定義したクラス・メソッド・
   定数の表 (`TUNES` など) は除く。クラスごと・メソッドごとに回数と、使っているファイルの例を出す。
   - 正規表現の粗い数え方 (plan.md 2 章) は誤検知が多い。Ruby の構文解析 (Prism。tools の他の道具が使っているものに
     合わせる) で、受け手が定数の呼び出し (`Machine.board_millis`)・受け手の型が決まらない呼び出し (`.byteslice`) を
     分けて数える。後者は「どのクラスの sig にもその名前が無い」ものだけを挙げる。
2. **実装にあって sig に無いもの**: (a) `lib/gems/picoruby-fmrb-*/mrblib/` と `vendor/asterism/mrblib/` の公開メソッド
   (`private` の後と `_` で始まるものを除く)、(b) C で定義したメソッド (`mrb_define_method`・
   `mrb_define_class_method`・`mrb_define_module_function` など。`lib/gems/*/ports/`・`lib/gems/*/src/` と、
   アプリから見える picoruby の gem のうち `Machine`・`Math`・`GC`・`Dir`・`File`・`IO`・`Time`・`JSON`・
   `MessagePack`・`I2C`・`GPIO` に当たるもの)。クラスごとに「実装の数・sig にある数・無い名前」を出す。
3. **書き方の欠け**: Family mruby の分 (`fmrb_*.rbs`・`enumerable.rbs`・`gpio.rbs`) の各メソッドについて、
   要約が無い・要約が 112 バイトを超える・英語 (`<<en>>`) が無い・長文が無い・例 (` ```ruby `) が無い。
   sig の読み取りは、生成器 (picoruby-ti の tidbgen) と同じ rbs gem で行い、コメントの扱いを生成器に合わせる。

**わざと載せない一覧**: `tools/sig_coverage/ignore.yml` に、クラス名かクラス名#メソッド名と理由を書く。
ここにあるものは 1〜3 の数から除き、要約には「除いた数」だけを出す。最初の中身は、明らかに内部用のもの
(描画の記録用の `Recorder`、エディタの内側の `EditorCore`、Asterism の内側の `Codec`・`Reader` など) を
実装を読んで判断して入れる。**迷うものは入れずに、報告に「判断が要る」として並べる** (アプリ向けかどうかは
親とユーザが決める)。

**基準**: 今の数を `tools/sig_coverage/baseline.json` に記録し、`rake sig:coverage` は基準との差も出す
(増えた・減った)。`rake regress` には入れない (決定 3)。道具自身の試験 (小さな見本の sig とアプリで、数え方が
合っていること) を `rake test` に足す。

## 確認

- `rake sig:coverage` が通り、要約が読める。plan.md 2 章の数と大きく違う所は、理由を報告に書く。
- `rake regress` が通る (道具の試験を含む)。
- **ビルドはしない** (sim・ESP32・ブラウザ版とも)。sig・実装・アプリは変えない。

## 範囲と止まる条件

- 触ってよい: `tools/sig_coverage/`、`rakelib/` (タスクの追加)、`.gitignore` (明細の置き場)、`doc/sig_coverage/report/s0.md`、
  `rake docs:index` による `doc/README.md`。
- sig・実装・アプリ・`sig/README.md`・plan.md は変えない (plan への反映は親)。
- push しない・マージしない。ユーザに質問しない。範囲の外の変更が要る・見立てと食い違う事実がある・
  仕様 (何を載せるか) の判断が要るときは、止まって報告に書く。
- 実在の値 (ドメイン・IP・ノード名・鍵) を書かない。コメントとコミットは英語、報告は日本語 (常体)。
  AI ホストの知識のまとまりは「バンドル」と書く (「束」と書かない)。

## 報告 (doc/sig_coverage/report/s0.md) に書くこと

1. 数え方 (何を使われていると見なし、何を除いたか) と、今の数 (1〜3)。plan.md 2 章との違い。
2. 「アプリが使っているのに無い」の上位 30 と、S2〜S5 のどの段に入るかの案。
3. 「わざと載せない」に入れたものと理由、**判断が要るものの一覧**。
4. 数え切れなかったもの (動的な呼び出し、`send`、method_missing など) と、その大きさの見立て。
5. 見立てと違った点、残件。
