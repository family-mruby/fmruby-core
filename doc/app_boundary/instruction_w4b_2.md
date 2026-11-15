# W4b-2 指示書: 音・時刻とログとクリップボード・ファイルを境界に載せる

> 親: plan.md の作るもの 2・段 W4b (機能のまとまり 7 つ。決定 6)、「W4a で決めたこと」「W4b-1 の後に決めたこと」。
> 前段は report/w4a.md (道具の規則)・w4b_1.md・w4b_1b.md。時間の目安は doc/regression/usage.md 0 章。

## 範囲

W4b の 7 つのまとまりのうち、**音 (FmrbAudio)**、**時刻・ログ・クリップボード**、**ファイル** の 3 つ。描画の 3 つ
(図形・文字・画像とスプライト) は W4a・W4b-1 で済み。**アプリと Window (FmrbApp) は次の段 (W4b-3)**。どのクラス・
メソッドがどのまとまりに入るかは、sig/ (fmrb_audio.rbs・fmrb_system.rbs・fmrb_misc.rbs・fmrb_app.rbs のうち該当
するもの) と実装を読んで決め、最初に表にする。

## やること

1. **調べて表にする** (report の最初の章): まとまりごとのメソッド、今の実装 (C のメソッド・mrblib の包み・カーネルへの
   メッセージ)、各エンジン (mruby・Spinel・Lua・MicroPython) での今の呼び方、境界に載せる形 (そのまま・`_raw`・
   載せない理由)。**アプリの書き方が変わる所は、変える前に一覧にして止まって返す** (仕様の変更はユーザが決める)。
   FmrbAudio のようにカーネルへのメッセージで動くものは、境界の関数がメッセージを組む形になるか、C で直接
   送る形になるかを書く (Hash を作る回数が増えないこと)。
2. **型定義に印と型を付けて生成に切り替える**: クラスに `%a{fmrb.boundary}`、載せないものに `%a{fmrb.internal}`、
   包みを持つものは `_raw`。本体の C の実体は `fmrb_bnd_<クラス>_<メソッド>`、アプリから見える C の名前は
   `fmrb_<クラス>_<メソッド>`。「本体からバイト列を返す形」の規則 (ファイルの読み込み・クリップボード) は
   W4a の道具の規則 (呼ぶ側のバッファ、全体の長さを返す) を使う。道具に足りない規則があれば足し、扱えない型は
   名前を出して止まる今の作りのまま。
3. **mruby と Spinel の包みを揃え、mruby の gem のつなぎを生成に**。Lua と MicroPython は生成で使えるようになる
   (前置き・既定値は FmrbGfx と同じ作り)。
4. **生成物の古さの検出** (`boundary:check`) と sig/README.md を新しいクラスに合わせる。MicroPython の qstr の表を
   作り直してコミット。
5. 毎フレーム・毎回呼ぶもの (音の note_on など) の mruby の命令数を前後で測る (W4b-1b と同じ測り方)。増えたら
   理由を書き、FmrbGfx の `present` と同じく C に戻すべきものは report に挙げる (戻すかは親が決める)。

## 確かめること (これだけ)

- `rake regress` (35 秒)。sig を直したら `rake clean` してからビルド。`rake regress:spinel` (20 秒)。
- `ruby tools/wamr_poc/run.rb boundary` (新しい関数を C のアプリから呼べる。ABI のハッシュが変わったら新しい値を書く)。
- 音: `rake regress:live` (音・MIDI の場面を含む、2.5 分) と、起動の和音 (FMSQ) が鳴ること
  (`tools/fmrb_audio_probe.rb`、sim の音は外に出さない)。
- 最後に 1 回: `rake regress:screens`・`rake regress:size` (内蔵 RAM の静的な増分 0)・`rake regress:run`
  (VM プールが揺れの幅の中。KB 単位で増えたら理由を書く)。
- 速さの確認は Ruby のエンジン (mruby・Spinel) だけでよい。ほかの言語は致命的でなければ多少遅くてよい (ユーザ)。
- 直しのやり直しでは、変えた所が影響する道具だけを流し直す。

## 触ってよい範囲

- fmruby-core (develop)。graphics-audio は変えない (要るなら止まって返す)。既存の未コミット (components/ の
  サブモジュール 2 つ、wasm/vendor/) には触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。
- 終わったら Linux のビルド (x86-64)、sim を片付け、裏の処理を残さない。

## 止まる条件

- アプリの書き方・名前・既定値が変わる選択 (一覧にして返す)。内蔵 RAM の増分。VM プールの KB 単位の増分で理由が
  説明できないもの。graphics-audio の変更が要るとき。ユーザの作業が要るとき。

## report (report/w4b_2.md)

調べた表、載せた関数と載せなかった関数 (理由)、道具に足した規則、各言語の変化、命令数の前後、確かめた結果と数字、
W4b-3 (FmrbApp) への注意。開始と終了の時刻は `date` で書く。
