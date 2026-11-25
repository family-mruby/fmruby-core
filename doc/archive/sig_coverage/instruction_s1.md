# S1 指示書: 書き方の型紙と、上流由来のファイルを直接直す決まり

> 2026-11-24 | 親セッションからサブ (Opus) への指示。計画は plan.md (5 章の S1、7 章の決定 1・2・17)。S0 の結果は report/s0.md。

## 目的

S2 以降で大量に書く前に、(1) 書き方を 1 つに揃える型紙を作り、(2) 上流由来の 15 ファイルに直接足す道を 1 件で
通しておく。足したものが、3 つの読み手 (エディタの補完・F1 のヘルプ・AI ホストの検索) に届くことを確かめる。

## やること (fmruby-core、ブランチ `sig-s1`。develop から切ってある)

### 1. `sig/README.md` を直す
- **つまずく API の型紙**を足す。手本は `sig/fmrb_asterism.rbs` (A6b)。長文に書く順番を決める:
  何をするか (1〜2 文) → 引数の意味 (シグネチャで分からないものだけ) → 戻り値と、失敗のしかた (例外の名前・nil・
  false・何も起きない) → 使う順番や前提 (先に呼ぶもの、呼べる場面) → 短い例。日本語、`<<en>>` の行、英語の順。
  素直な API は「例だけ」の今の決まりのまま。どちらに当たるかの判断の目安 (実装を読まないと分からない振る舞いが
  あるか) も書く。
- **キーワード引数・省略できる引数の書き方** (決定 2): シグネチャはいちばん多い呼び方を必須引数の形で書き、
  キーワードと省略時の値は長文に「キーワード: `name:` (省略すると…)」の形で並べる。A6b の書き方に合わせて例を 1 つ載せる。
- **「上流由来の 15 ファイルは変更禁止」を改める** (決定 1): 足りないメソッドは直接足してよい。足すときの決まりを書く:
  既にある宣言の順番と文を変えない (足すだけ)、足した分は Family mruby の書き方 (要約 `日本語 <<en>> English`・
  長文・例) で書く、型エンジンの試験が文字列の一致で見ている所が落ちたら試験の期待値も直す、上流
  (picoruby-ti の example/rbs) の更新を取り込むときは差分を手で合わせる。既にある英語だけの要約を書き直すことは
  この企画ではしない (S2 以降でも、足すだけ)。
- `gpio.rbs` は `sig/` に無い (`tools/ti/test_sig/` にある)。README の記述を実態に合わせる。
- 要約の上限は 111 バイト (直してある。型紙の中でもこの数にする)。

### 2. `String#byteslice` を `sig/string.rbs` に足す (1 件で通す)
- 実装 (mruby の `String#byteslice`、Spinel の側の扱い) を読み、端末で実際に使える形 (引数 1 つ・2 つ・Range が
  使えるか、範囲の外で nil か) を確かめて書く。アプリでの使われ方 (`flash/app/` に 34 回) に合わせて、いちばん多い
  呼び方をシグネチャにする。型紙のとおり、要約・長文・例・英語をそろえる。
- 文字単位の `[]` との違い (日本語の入った文字列でずれる) は、つまずく所なので長文に書く。

### 3. 3 つの読み手で確かめる
- **型エンジン**: `rake ti:test` が通る。落ちたら、文字列の一致で見ている期待値を直す (何を直したかを報告に)。
- **エディタ**: `rake clean` → `rake build:linux` → sim (MCP の sim_up・sim_app・sim_input・sim_screenshot。使えなければ
  `tools/dev_run_check.sh` と `tools/fmrb_input.rb`・`tools/fmrb_screenshot.py`) でエディタを起こし、
  `s = "abc"` の次の行で `s.bytes` まで打って Tab を押し、候補に `byteslice` が出て、状態行に要約が出ること。
  F1 で長文と例が出ること。**画面の写しを scratch に残し、報告にパスを書く**。確認が済んだら sim を止める。
- **F1 のヘルプのファイル**: `rake ti:help` が `flash/help/String/byteslice.md` を作ること (中身に日本語側・`<<en>>`・
  英語側がある)。`flash/help/` が git でどう扱われているか (追跡か生成物か) に合わせる。
- **AI ホストの検索**: `/home/kishima/fmrb/family-mruby/asterism-tutor` の `Tutor::Lookup` を手元の fmruby-core の
  木に向けて (LLM は呼ばない。`ruby -Ilib -e` で `Tutor::Lookup.new(<fmruby-core の絶対パス>, Tutor::Lookup::SCOPE)` を
  作り `search("byteslice", path: "sig/")` と `read`)、足した型の要約・長文・例が読めること。asterism-tutor は変えない。
- **数える道具**: `rake sig:coverage` で `byteslice` の 34 回が「無い」から消える。`rake sig:coverage:update` で基準を
  更新する (変化を報告に書き、別コミットにする)。
- `rake regress` が通る (境界の検査 `boundary:check` が落ちたら `ruby tools/boundary/gen.rb` で作り直して一緒にコミット)。

## 範囲と止まる条件
- 触ってよい: `sig/README.md`・`sig/string.rbs`・型エンジンの試験の期待値 (`tools/ti/` の下。落ちたときだけ)・
  `flash/help/` (追跡されているなら生成の結果)・`sdk/` (境界の作り直しで変わるなら)・`tools/sig_coverage/baseline.json`・
  `doc/archive/sig_coverage/report/s1.md`・`rake docs:index` による `doc/README.md`。
- 実装 (lib/gems・main・components・vendor)・ほかの sig・アプリ・plan.md・asterism-tutor は変えない。
- **ビルドは sim (`build-linux/`) だけ**。ESP32 のビルドをしない (`build/` は Tab5 用が残っている。触らない)。
  ブラウザ版・ネイティブ版もビルドしない。
- 止まる条件: 上流由来のファイルに足すと型エンジンの試験が期待値の直しでは済まない形で壊れる / 生成器が足した
  宣言を読めない / 型紙どおりに書くと 3 つの読み手のどれかに届かない / 範囲の外の変更が要る。止まったら報告に書く。
- push しない・マージしない。ユーザに質問しない。実在の値を書かない。コメントとコミットは英語、報告は日本語 (常体)。
  AI ホストの知識のまとまりは「バンドル」と書く。

## 報告 (doc/archive/sig_coverage/report/s1.md)
1. 型紙の要点 (README のどこに何を足したか) と、決めかねた所。
2. `String#byteslice` の実装で確かめたこと (使える形・失敗のしかた・エンジンごとの違いがあれば)。
3. 3 つの読み手それぞれの確認の結果 (画面の写しのパス、ヘルプのファイル、検索の出力の要点)。
4. 型エンジンの試験で直した期待値。型データベースの各領域の大きさの変化。
5. S2 で同じ手順を大量に回すときの見積もり (1 メソッドあたりの手間、まとめて確認できる所) と気づき。
