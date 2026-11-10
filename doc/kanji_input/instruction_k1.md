# K1 指示書: 辞書の生成の道具

> 状態: 完了 | 更新: 2026-11-11 | 正は plan.md (方針・仮決め・K0 の後の親の判断)、report/k0.md

## 作るもの

- `tools/kanji/` に辞書を作る道具 (Ruby、標準ライブラリだけ)。`rake kanji:dict` から呼べるようにする。
  - 入力: K0 で固定した mozc (dictionary_oss のコミット a4541df29db443b49f81cf54665873b68fde9efa) の辞書と single_kanji・
    symbol、Unihan 17.0.0 の読み。道具が版を固定して取る (取得先はスクリプトの設定に。doc には URL を書かない)。
    取ったものは build の外の作業の場所 (例: `tools/kanji/work/`、gitignore) に置き、リポジトリに入れない。
  - 基準のフォント (misaki 8、`tools/kanji/font_charset.rb` が数える) で描ける字だけからなる語を選ぶ。
  - 今のかなの合成で打てない読みの語は外す (K0 の 3.6% のうち っ 終わり以外)。っ 終わりの語は残す (K2 で最長一致)。
  - mozc のコストの順に、大きさの上限 (既定 1 MB、`SIZE=` で変えられる) まで入れる。1 字の表 (基準のフォントの漢字
    すべて。漏れ 0 を道具が数えて示す) と字の表は必ず入れる。
  - 出力: 1 ファイル (例: `fmrb_kanji.dic`) と、横にライセンスの表示 (`LICENSE` の文: mozc の BSD、IPAdic の NAIST の
    著作権表示と無保証、ICOT の NO WARRANTY、Unihan の Unicode License v3、沖縄辞書)。出力は scratchpad か
    `tools/kanji/work/out/` (gitignore)。
  - 形: K0 の案 (4 KB の塊、塊の最初の読みの表を先頭に、読みの順)。版の番号と、引く側が確かめる印 (マジック・
    字の表の大きさ・塊の数) をヘッダに。文字の番号付けは K0 の数え方 (かな 1 バイト、ほか 2 バイト)。
    形を `doc/kanji_input/dict_format.md` (参照資料ではなく企画の中の仕様として) に書く。
- ホストの Ruby で辞書を引く小さな参照実装 (`tools/kanji/lookup.rb 読み`) を作り、K2 の C の実装と同じ答えを出す基準にする。
  読みの先頭からの最長一致も。

## 確認

- `rake kanji:dict` が通り、大きさ・語の数・1 字の表の漏れ 0・描けない字 0 を出す。
- `tools/kanji/lookup.rb` で日常の語 (K0 の 31 語ほか) の候補が出る。
- `rake regress` (host の試験に辞書の形の試験を足すなら、元データを取らずに済む小さな試験用の辞書で)。
- ビルド・sim・ネイティブ版・ブラウザ版は動かさない (ほかのサブが使っている)。

## 触ってよい範囲

tools/kanji/、rakelib の kanji の task、.gitignore、doc/kanji_input/。コミットはパスを明示して develop へ。push しない。

## report

`doc/kanji_input/report/k1.md` (常体)。
