# 付属デモの選定とキーボード化 (2026-08-15)

ランチャーに並ぶ BASIC デモを 7 本から 3 本に絞り、パッド前提の見せ方を
キーボード前提に直した作業の記録。決まったことは `compat_list.md` と
`usage.md`、`components/basic/test/samples/README.md` に反映済み。ここには
**作業中に分かった実装側の事実**を残す。

## 1. 選定

| 残した | 理由 |
|---|---|
| Shoot (`sample_11`) | スプライト面を 1 本で見せる。Dodge (`sample_10`) の 8 方向 DEF MOVE と箱内クランプを取り込み、Dodge は非表示に |
| Maze (`sample_12`) | テキスト面 (LOCATE / COLOR / SCR$) と、カナ表示・カナ入力。Kana (`sample_04`) と Hit (`sample_14`) の守備範囲を吸収 |
| Music (`sample_13`) | 音 (PLAY 3 声・CLICK・BEEP) と fmruby 拡張の CIRCLE / PRESENT。bounce (`flash/app/basic/bounce.app.bas`) を統合し、bounce は削除 |

`basic.app.bas` (「BASICデモ」) は入力なしの言語見本兼テンプレートとして
残し、PAUSE を詰めて短縮した。外した 3 本は `test/samples/` に残っており、
`rakelib/basic.rake` の `BASIC_SAMPLE_APPS` に足せばいつでも戻せる。

キーボード対応そのものは**もともと動いていた**。`fmrb_basic.cpp` の
`apply_key_to_pad()` が矢印を STICK に、Z/X/Enter/Space を STRIG に割り当て
ている。直したのは画面とコメントの文言、および Maze の `IJKM` 併記を矢印に
一本化した点。なお**矢印キーは INKEY$ では読めない** (HID イベントの
`character` が 0 なので `push_key` されない) ので、方向入力を INKEY$ に
書き換えるのは劣化になる。STICK が正しい経路。

## 2. 分かった実装側の事実 (3 件)

### 2.1 Enter は INKEY$ に CHR$(10) で届く (CHR$(13) ではない)

`main/drivers/usb/fmrb_keymap.c:52` が Enter を `'\n'` に変換しているため、
`INKEY$` が返すのは 10。実機の Family BASIC は CR (13) なので差異になる。

- 影響: `IF A$=CHR$(13)` で入力終了を判定するプログラムは**永久に終わらない**。
  旧 `sample_04_screen_kana` がまさにこれで、Enter を押しても終了しなかった
  (sim で確認)。両方を見るように直した。
- 対処の選択肢: (a) 各プログラムで 10 と 13 の両方を見る (現状)、
  (b) キーマップを 13 に変える、(c) `unicode_to_fbcode` で 10 を 13 に畳む。
  (b)/(c) はエディタなど他の利用者に影響するので未着手。

### 2.2 ブロッキング `INKEY$(0)` は待っている間に画面を present しない

`components/basic/core/basic_expr.cpp:426-434` の待機ループは `on_tick` と
`sleep_ms` は呼ぶが `frame_tick()` (present) を呼ばない。このため
**直前に PRINT した文字が画面に出ないまま入力待ちに入る**。sim では
ゴール画面が真っ黒のまま止まって見えた。

- 回避: デモ側は `PAUSE 1` + 非ブロッキング `INKEY$` のループにした。
  PAUSE はフレームを進めるので画面が出る。
- 直すなら待機ループに `service_frames()` 相当を入れるのが素直。

### 2.3 毎フレーム MOVE を出し直すとスプライトは 1 ドットも動かない

`advance_moves()` は `speed` から周期 (C=1 なら 2 フレーム) を求め、
`step_counter` がそこに達したときだけ座標を進める。ところが `POSITION` も
`DEF MOVE` も `step_counter` を 0 に戻すので、**毎フレーム
`POSITION`+`DEF MOVE`+`MOVE` を出し直す書き方だとカウンタが周期に届かず、
永久に静止する**。

- `sample_10_dodge` の自機操作がこの書き方で、実際に自機が動かない
  (ランチャーから外したのでそのままにしてある)。
- `sample_11_shoot` は自機だけ `POSITION` で手動に進める形に変えた。移動
  エンジンは敵と弾が使っている。DEF MOVE は向きを変えるためだけに呼ぶ
  (DEF MOVE は位置と visible を保つので、これで消えたりはしない)。

## 3. その他

- **RND の種は固定** (`basic_core.cpp:182`。ゴールデンテストの再現性のため)。
  そのままだと迷路が毎回同じになるので、Maze はタイトルで開始キーを待つ間に
  `RND` を空回しし、押した時刻を種の代わりにしている。
- 迷路生成 (13x10 セル、穴掘り法) は既定の実行ペース (60 文/フレーム) だと
  6-10 秒かかった。`_SPEED 600` で 2-3 秒に縮めて元に戻している。
- **`STICK(0)` は押しっぱなしの状態を返す**ので、数フレームおきに読んで
  そのまま歩かせると 1 回のタップで数マス進む。Maze はキーボードの
  オートリピートと同じ形にした: 新しく見えた方向はその場で 1 マス、
  押し続けたときだけ 20 フレーム待ってから 7 フレーム間隔で繰り返す
  (`RD` / `RI`)。壁のブザーも最初の 1 回だけ鳴らす。
- 検証は Linux sim (headless) で実施。3 本とも起動・キー操作・画面・音
  (`fmrb_audio_probe.rb` で peak=12800 / 全窓発音) を確認した。
