# L7d 指示書: File Manager と /mnt の小さな直し

> 計画: doc/linux_native/plan.md (「後で直すもの」の L7c・L7 の分)。
> 前段の結果: report/l7.md、report/l7c.md

## ユーザの決定 (2026-10-28、変えない)

- 「後で直すもの」の L7c・L7 の分を直す。
- **特殊なファイル** (`/dev` の中の装置など) は、一覧に**出すが開かない**。
  印で特殊なファイルと分かるようにし、開こうとすると「開けない」と出す。
- 以前に配列で jp を選んだ人は auto に**変えない** (このフェーズでは触らない)。

## 作業の内容

1. File Manager の削除に失敗したとき、画面にエラーを出す (今はログだけ)。
2. 削除の確認が出ている間に File Manager の外を押すと、File Manager まで閉じる。
   確認だけを閉じる。
3. ファイルの選択などのダイアログを File Manager の上で閉じると、kernel に知らせる
   重なりの範囲が「無し」に戻る。開いたままの File Manager の範囲に戻す
   (L7c で削除の確認だけ直した形を、ほかのダイアログにも広げる)。
4. keys のデスクトップの確認の行が、一覧の最後の 2 行ほどに重なる。重ならない
   置き方にする。
5. `/mnt/root` の下の削除は、確認の前に「読むだけ」と分かるようにする (Ruby から
   読み出し専用かを問う口を足す。端末・sim・ブラウザ版では「読み出し専用でない」
   か今の振り分けの答えを返す)。
6. 書き込みの判定から実際に開くまでの間にリンクを差し替えられる隙を閉じる
   (`openat` と `O_NOFOLLOW` で 1 段ずつ辿るなど)。
7. `/proc/kcore` のような大きなファイルの大きさが表の桁からはみ出す。桁に収める
   表し方 (K・M・G・T など) にする。
8. 特殊なファイルに印を付け、開こうとしたら「開けない」と出す。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/kernel/` (デスクトップ・File Manager・確認の
  ダイアログ・keys のデスクトップ)、`components/fmrb_hal/` と `hosted/` の file HAL、
  `native/`、`lib/add/` の該当箇所、`test/`、`doc/linux_native/report/`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/`、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- native は HOME を一時ディレクトリにし、利用者のホームに書かない。
- kernel・デスクトップの Ruby を変えたら、標準 (Spinel) と全部 mruby の両方で
  確かめる。

## 受け入れ条件

1. 1〜8 をそれぞれ native (`sdl`、必要なら `sdl_keys`) で確かめ、確かめ方と結果を
   report に書く。自動にできるもの (5〜8 の C の部分) は host 試験にする。
2. 端末・sim・ブラウザ版で変わる所 (1〜4 はデスクトップの共通の Ruby) は、
   sim の画面で確かめ、`rake regress:screens` が通る。基準の絵が変わるなら変化を
   report に書き、基準の更新は別のコミットにする。
3. `rake regress`、`rake regress:size` (内蔵 RAM +0)、`rake native:test` が通る。
4. 1・2 を標準と全部 mruby の両方で。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- 端末の動きを、直す対象以外で変えないとできないとき、内蔵 RAM が増える作りしか
  無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l7d.md) に書くこと

- 各直しの原因と直し方、確かめ方
- 受け入れ条件ごとの結果と数値 (内蔵 RAM・flash の差)、残件
