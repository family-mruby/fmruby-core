# 動作リスト

FMRuby BASIC で動かしたプログラムの互換状況。区分は 3 つ:

- **動く** … 最後まで意図どおりに動作する
- **制限付き** … 動くが差がある (内容を明記)
- **非対応** … 動かない (理由を明記)

作品のコードや出典は載せない。タイトルと互換状況だけを記録する。

## 自作サンプル (コーパス)

ターゲット作品リストが確定するまでの代替コーパス。
`components/basic/test/samples/` にある。検証は Linux シミュレーション。

このうち 3 本だけを `rake basic:samples` で `flash/app/basic/` に入れ、
ランチャーに出している。**面 (スプライト / テキスト / 音とグラフィック)
ごとに 1 本**という選び方で、重複を減らして機能を一通り見せる。3 本とも
キーボードだけで遊べる (ヤジルシと Z。ファームが STICK / STRIG に
割り当てている。`components/basic/fmrb_basic.cpp`)。パッドがあれば
そちらでも動く。

| サンプル | 使う機能 | 状況 |
|---|---|---|
| **Shoot** (`sample_11_shoot`) | 3 つの MOVE、DEF MOVE 8 方向、STICK / STRIG、CRASH 両方向、ERA / CAN、XPOS/YPOS、PALET、PLAY、BEEP | 動く |
| **Maze** (`sample_12_maze`) | 穴掘り法 (再帰的バックトラック) による完全迷路、SCR$ を訪問済み地図に使う、COLOR、DIM、RND、STICK 歩行、INKEY$ のカナ入力、`_SPEED` | 動く |
| **Music** (`sample_13_music`) | PLAY 3 声、非同期再生、CLICK、BEEP、fmruby 拡張の CIRCLE / PRESENT | 動く (音の官能確認は実機で) |
| Kana (`sample_04_screen_kana`) | カナ PRINT、COLOR、LOCATE、`INKEY$(0)`、カナ入力 | 動く (ランチャーには出さない)。Enter が CHR$(10) で届く件を修正済み (同 2.1) |
| Dodge (`sample_10_dodge`) | DEF MOVE 8 方向、STICK、CRASH、ERA、XPOS/YPOS、PAUSE | **制限付き**: 走るが自機が動かない (毎フレーム POSITION+DEF MOVE+MOVE を出し直すと step_counter が戻り続けるため。reports/demo_curation.md 2.3)。8 方向の書き方は Shoot に取り込み済み |
| Hit (`sample_14_hit`) | RND、PAUSE によるタイムアウト、INKEY$、スコア | 動く (同上) |

ベンチマーク (`bench_01`-`bench_05`) は速度計測用で、作品ではない。
言語そのものの見本 (`flash/app/basic/basic.app.bas`、ランチャー名
「BASICデモ」) は PRINT / FOR / IF / GOSUB だけの入力なしプログラムで、
テンプレートを兼ねている。

## ターゲット作品

**未着手**。作品リストがユーザから提供され次第ここに追加する
(00_common のユーザ判断待ち事項 1)。

判定の目安:

- **非対応になるもの**: マシン語を `CALL` する作品、`POKE` で
  対応していないアドレス (スプライト属性の直接操作など) に依存する作品。
  対応表は `virtual_memory_map.md`、未対応アドレスへの POKE はログに
  `POKEW|unmapped $XXXX` が 1 度出るので機械的に洗い出せる。
- **制限付きになりやすいもの**: 実行速度に依存する作品 (`known_differences.md`
  の「実行速度」の行)、内蔵キャラクタの見た目を前提にした作品 (絵柄は
  本プロジェクトの自作なので動作は同じだが見た目が違う)、テープ入出力
  (`LOAD` / `LOADS` / `SAVES`) を使う作品。
