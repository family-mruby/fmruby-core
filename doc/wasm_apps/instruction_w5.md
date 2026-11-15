# W5 指示書: SDK の仕上げ (つなぎの生成・版の照合・配り方) と、手順書の「ブラウザで試す」

> 親: plan.md の段 W5 と決定 13・14・19・20・21。前段は report/w1b.md・w1b_2.md・w1c.md。境界の生成の道具は
> `tools/boundary/` (doc/app_boundary)。SDK は family-mruby-sdk (公開、MIT)。時間の目安は doc/regression/usage.md 0 章。

## 作業場所 (並行のため)

本体の checkout では doc/flash_layout の F0・F1 のサブが sim を使う。この段は worktree で行う:

```
git -C /home/kishima/fmrb/family-mruby/fmruby-core worktree add /home/kishima/fmrb/wt/w5-core -b wt/w5 develop
```

submodule・`.env`・`config/wifi.toml`・`flash_local/`・`hosted/vendor/`・`vendor/` (`vendor/wamr` を含む)・
`tools/wamr_poc/vendor/`・`wasm/vendor/` を本体の checkout から rsync で写す (`.git` を除き、入れ子の build を外さない
よう `/build/` で除く)。本体の checkout・sim・シリアル・flash には触らない。ネイティブ版・ブラウザ版の確認は、
worktree の中で道具を直接動かす (W1c と同じやり方、report/w1c.md)。

## やること

1. **つなぎの生成 (TinyGo)**: 境界の型定義 (sig) か生成した C のヘッダから、TinyGo のつなぎ (`//go:wasmimport` の宣言と、
   イベントの記録・選択肢の定数・記録の型) を生成する。生成の道具は fmruby-core の `tools/boundary/` に型紙を足す形か、
   SDK の側に置くかを決めて書く (SDK の利用者が fmruby-core 無しで使えること)。手書きの包み (Window・App・Audio・
   File などの使いやすい形) は残し、生成した低い層の上に載せる。生成物は SDK にコミットし、`check_bindings.rb` を
   「生成物と型定義が一致するか」の照合に置き換える。
2. **版の照合 (決定 13・14)**: `.app.toml` に SDK の版 (と使う関数の一覧か、その版で足りるか) を書き、本体は起動の前に、
   自分に無い関数を import するアプリを理由つきで断る (WAMR の読み込みの時点で分かる import を照合)。SDK の版の
   付け方 (大きい番号・小さい番号を、前回のリリースの型定義との比較で決める) を道具に。
3. **配り方**: SDK のリポジトリの形 (言語ごとのディレクトリ、Go のモジュールの path、タグの付け方) を README に。
   リリースの手順は書くだけ (タグは打たない)。
4. **手順書に「ブラウザで試す」(決定 21)**: 公開のブラウザ版 (https://family-mruby.github.io/studio/) に TinyGo の
   bounce と hello が入った。自分で作った `.wasm` をブラウザ版に入れて動かす方法 (ブラウザ版のファイルの入れ方を
   調べて) を英語版と日本語版の手順書に足す。
5. 作り直した bounce・hello・c_check が今までどおり動くこと (ネイティブ版・ブラウザ版、worktree で)。

## 決まり

- SDK の中は README.ja.md と docs/getting_started.ja.md 以外 ASCII のみ (英語のコメント)。日本語の文書はエンジニアに
  通じるカタカナ語、Window・端末・ESP32 の言い方、常体。実在の IP・ホスト名・個人の名前やパスを書かない。

## 確かめること

- `rake regress`・`rake regress:spinel`・`ruby tools/wamr_poc/run.rb boundary`・`boundary:check`。
- ネイティブ版とブラウザ版で、作り直したアプリと、わざと無い関数を import したアプリ (断られること)。
- SDK の照合の道具。

## 触ってよい範囲

- worktree の fmruby-core と family-mruby-sdk。graphics-audio は変えない。境界の規則・sig は変えない。
- コミットは fmruby-core は `wt/w5`、SDK は main (ローカル)。件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、
  pathspec を明示。push しない (親がする)。

## 止まる条件

- 境界や殻の形を変える必要。Ruby のアプリの動きが変わる。仕様が変わる選択 (版の付け方の大きな方針など、決定に
  無いもの)。ユーザの作業が要るとき。

## report (report/w5.md)

生成の道具の置き場と型紙、版の照合の作り、配り方、手順書の追加、確かめた結果、Rust・Zig を足すときの手順。開始と
終了の時刻は `date` で書く。終わったら worktree で `rake clean_all`。
