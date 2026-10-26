# R5 指示書: 画面の小物 (文字の幅・詰める・折り返す・ボタン) を共通にする

> 状態: 指示 | 更新: 2026-10-25 | plan.md の R5。オセロの卓 (text_w・fit・fit_dots・wrap2・text_lines・button)、お絵かきボード (text_w・fit・button)、麻雀 (fit・button)、walk_scroller (fit) が、同じ役の小物をそれぞれ書いている。既にある部品 (FmrbGfx#text_width と FONT_METRICS (`lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb`)、FmrbUI (`fmrb-ui.rb`)) に寄せられるものは寄せ、足りないものは同じ gem に足す。見た目は変えない。**実機は使わない** (Tab5 は使わない。StickS3 は R5 に関係しないので触らない)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- 4 つのアプリの小物の中身と違い (全角を 2 桁と数えるか、px で測るか、「…」か「..」か、ボタンの形・押せる範囲・色)。表にする。
- `FmrbGfx#text_width` (FONT_METRICS による幅)、`FmrbUI` の今の口 (ボタン・スクロールバーなど、既存の利用者)。
- picoruby・Spinel の罠 (memory の Ruby の節、.claude/skills/fmrb-app-new)。卓のアプリと walk_scroller は mruby の VM。FmrbUI・FmrbGfx を Spinel のプログラム (デスクトップ・エディタ) も使っているなら、変えると Spinel 側にも効くので、既存の口の振る舞いは変えない。

## 2. 作るもの

1. 幅に詰める (末尾に印を付ける・付けない)、2 行までに折り返す (空白で割れるなら空白で)、複数行を描いて次の y を返す、を共通の口にする (置き場は FmrbGfx か FmrbUI か小さなモジュール、読んでから決める)。幅は px で測る (FONT_METRICS)。今の「全角 = 2 桁」の数え方と結果が違うなら、どちらが正しく見えるかを画面で確かめて決め、見た目が変わらない方にする。
2. ボタン: 4 つのアプリのボタンが同じ形なら共通の口にし、FmrbUI に既にあるボタンと形・押せる範囲がそろうなら寄せる (違うなら、アプリのボタンの形のまま共通にする)。
3. 4 つのアプリを載せ替える。行数の前後。

## 3. 確認

- sim の Modern (麻雀は 640x360 の高解像度の全画面) と Retro (オセロ・ボード)、ブラウザ版で、前後の画面を並べる (手元のゲームホストを使う、VPS には繋がない)。オセロの右の欄 (OP1 で直した詰めと折り返し: 長い名前で「…」、案内の 2 行)、ボードのボタンと案内、麻雀のボタン (鳴き・パスなど 640 と 1280)、walk_scroller の文字。差が出たところは画面を report に。
- `rake test`、`rake mahjong:overflow` (麻雀の文字のはみ出しの検査、前と同じ結果)、標準構成の sim でエディタを 1 回起動して 1 打鍵。
- 内蔵 RAM: Tab5・Retro・StickS3 の DIRAM が増えない (gem を変えたら前後で比べる)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の picoruby-fmrb-app の gem (fmrb-gfx.rb・fmrb-ui.rb ほか、既存の口の振る舞いは変えない)、4 つのアプリ、試験、report。lib/ を触ったら `rake clean`、ターゲットの切り替えは `rake clean_all`。
- **触らないもの**: 実機、asterism-games・asterism の gem 本体・console、sdkconfig、VPS、`~/asterism_keys/`、麻雀の手描きの牌の絵。
- push・タグはしない (親がする)。`git commit -a` は使わない。件名 `app:` `game:` `mahjong:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc (この指示書・report) はコミットしない。
- 終わったら sim・web・手元のゲームホストを片付ける (前から動いていたものは残す)。
- **止まる条件**: 見た目を変えないと共通にできない (どちらに合わせるか選択肢で返す)、既存の口を変えると Spinel のプログラムの振る舞いが変わる、内蔵 RAM が増える。
- **report**: `doc/archive/app_refactor/report/r5.md` (常体): 違いの表、共通の口の設計、載せ替えの前後の行数、画面の比較、RAM、残件、コミット。
