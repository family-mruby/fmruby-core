# W3 指示書: 透けている所のクリックを後ろの Window へ回す

> 状態: 実装中 | 更新: 2026-11-11 | 正は plan.md の「決定」(費用を測り、Modern で安くできるなら入れる。高ければ見送って report に書く)

## 背景

- 今は当たり判定が Window の四角。枠なしの Window の透けている所を押しても、そのアプリに届く (W0)。
- カーネル (input_router / window_manager の find_window_at) は画素を持たない。Modern (Tab5・ネイティブ版・ブラウザ版) は
  canvas が同じプロセスの display_local にあり、Retro (S3) は canvas が子マイコン (WROVER) にある。

## やること

1. 調べて測る: 押した点がそのアプリの canvas で透ける色かを、カーネルが知る道を比べる。
   - (a) Modern で canvas の画素を読む (display_local に 1 画素を問う口)。押下 1 回の費用 (時間・RAM)。
   - (b) アプリが当たりの形を申告する (例: 基底の口で矩形の一覧、または小さなマスク)。カーネルは申告だけを見る。
     Retro でも使える。アプリの書き方の負担。
   - Retro で (a) が成り立つか (子マイコンへの問い合わせの往復) も見立てを書く。
2. 安い方を入れる (Modern で押下 1 回あたり体感で分からない費用、内蔵 RAM の増分 0)。どちらも高い・複雑なら入れずに
   report に理由を書いて終える。(b) を入れるなら、申告の無い枠なしのアプリは今どおり四角で当たる (既定は変えない)。
3. 対象は枠なしの Window だけ。枠つきの Window の当たり判定は変えない。
4. 回したクリックは、後ろの Window (またはデスクトップ) にそのまま届き、焦点と前面化もその Window に移る。
   ドラッグの預かり (W2) とも矛盾しないこと (透けた所から始めたドラッグは後ろの Window のもの)。

## 確認

`rake regress`、`rake regress:screens`、`rake regress:size`。ネイティブ版・ブラウザ版 (と入れたなら sim の Retro) で、
枠なしの絵の透けた所を押すと後ろの Window・デスクトップのアイコンに届き、絵の上を押すとアプリに届く、を画面とログで。

## 触ってよい範囲

fmruby-core のカーネル・基底・display_local (Modern)。graphics-audio (Retro の子マイコン) を変える必要が出たら止まって返す。
コミットまで (develop)。push しない。

## report

`doc/frameless_window/report/w3.md`。
