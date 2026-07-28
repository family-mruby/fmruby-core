# 動作リスト

FMRuby BASIC で動かしたプログラムの互換状況。区分は 3 つ:

- **動く** … 最後まで意図どおりに動作する
- **制限付き** … 動くが差がある (内容を明記)
- **非対応** … 動かない (理由を明記)

作品のコードや出典は載せない。タイトルと互換状況だけを記録する。

## 自作サンプル (コーパス)

ターゲット作品リストが確定するまでの代替コーパス。
`components/basic/test/samples/` にあり、`flash/app/basic/` から
ランチャーで起動できる。検証は Linux シミュレーション。

| サンプル | 使う機能 | 状況 |
|---|---|---|
| Kana (`sample_04_screen_kana`) | カナ PRINT、COLOR、LOCATE、`INKEY$(0)`、カナ入力 | 動く |
| Dodge (`sample_10_dodge`) | DEF MOVE 8 方向、STICK、CRASH、ERA、XPOS/YPOS、PAUSE | 動く |
| Shoot (`sample_11_shoot`) | 3 つの MOVE、STRIG、CRASH ペア、CAN、PLAY、BEEP | 動く |
| Maze (`sample_12_maze`) | SCR$ による当たり判定、COLOR、STICK / IJKM、RND | 動く |
| Music (`sample_13_music`) | PLAY 3 声、非同期再生、CLICK、BEEP | 動く (音の官能確認は実機で) |
| Hit (`sample_14_hit`) | RND、PAUSE によるタイムアウト、INKEY$、スコア | 動く |

ベンチマーク (`bench_01`-`bench_05`) は速度計測用で、作品ではない。

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
