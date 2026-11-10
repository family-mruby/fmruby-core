# W0 指示書: 今の口だけで枠なしの Window を試作して確かめる

> 状態: 実装中 | 更新: 2026-11-10 | 正は plan.md (「今の作りで分かっていること」「親の仮決め」)

## やること

1. 試作のアプリを 1 つ書く (リポジトリの外: scratchpad か、ネイティブ版の利用者の場所を scratchpad に分けたものの
   /home)。今の FmrbApp の口だけで:
   - 枠を描かず `@gfx.clear(TRANSPARENT_COLOR)`、Window 全体を `set_composite_regions` で `transparent: true` に。
   - 小さな絵 (Ruby で描く図形か、文字列に持ったドット絵) を描く。`self.closable = false`。
   - マウスの押下・移動・離しで、アプリ自身が `set_window_position` で自分を動かす (どこを掴んでも)。
   - `movable = true` にして、`amove` (ネイティブ版 2 つ、または native_app の move) で移した後に透けたままか。
2. 確かめること (画面とログ):
   - 後ろのデスクトップ・他の Window が透けて見えるか (ネイティブ版・ブラウザ版・sim の Retro と Modern)。
   - 上の 11 行を押したときにカーネルのドラッグが始まるか、右クリックで読み直しになるか。
   - アプリ自身のドラッグ: 速く動かしてカーソルが外へ出たとき途切れるか。sim_input / native_input の
     ドラッグの注入でどこまで再現できるかも書く。
   - 透けている所のクリックがどこへ届くか (後ろの Window には届かない見込み)。
   - 閉じ方: Ctrl+Q の終了の頼み、Shell の kill、ランチャーなど、今ある口で閉じられるか。
   - `amove` の後、基底が角の丸みの区画を掛け直して透けが消えるか (on_restore で掛け直すと直るか)。
   - 合成の速さ: Window 全体が透ける区画のとき、sim の Retro (S3 相当) で描画が遅くならないか
     (GFX STATS や present の間隔。測り方は report に)。実機が無いので sim での見立てでよい。
3. 結果から、W1・W2 の作り (基底とカーネルのどこを直すか) の見立てを report に書く。

## 触ってよい範囲

試作はリポジトリの外。リポジトリのコードは直さない (W0 は調べるだけ)。report と、要れば plan.md の
「今の作りで分かっていること」の事実の訂正は書いてよい (仮決めは変えない)。コミットは doc だけ (develop)。

## 確認

回帰は要らない (コードを変えないため)。ネイティブ版は利用者の場所を scratchpad に分け、
~/.local/share/family-mruby-dev を触らない。sim の /home に置いたものは片付ける。

## 止まる条件

仮決めが成り立たない事実 (例: 透けがどの構成でも出ない、Retro の合成が使えないほど遅い) が出たとき。

## report

`doc/frameless_window/report/w0.md`。画面は /tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/w0/ に置き、パスを書く。
