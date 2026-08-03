# MIDI 対応 作業指示書 (P0 / P1)

対象: 実装セッション (Opus)
ブランチ: `feature/midi`
前提文書: [README.md](README.md) (方針。**先に読むこと**)

この指示書は README の「4. 進め方の案」のうち **P0 と P1** を対象にする。
P2 以降は P0 の結果で実装深度が変わるので、この指示書には含めない。

## 0. この段階のゴール

1. **P0**: 後続の設計判断に必要な数値と事実を採る。**コードを書くのが目的ではない**。
2. **P1**: SMF (.mid) を FMSQ に変換するツールを作る。ハードウェアに依存せず、
   単体で成果になり、以降のテスト素材にもなる。

P1 が動けば「手持ちの .mid が Family mruby で鳴る」が成立する。P0 はその次
(P2: MIDI 中核層 + APU transport) を正しく設計するための材料集めである。

## 1. 先に読むもの

| 対象 | 位置 | 何が分かるか |
|---|---|---|
| 方針 | `doc/midi/README.md` | 全体設計、決定済み事項、未決事項 |
| FMSQ 仕様 | `../fmrb-audio-tools/doc/fmsq_format.md` | 変換先のフォーマット (これが正) |
| FMSQ 定義 | `components/apu_emu/include/fmsq_format.h` | ファーム側の実装 (仕様と突き合わせる) |
| 生成の実例 | `flash/app/game/rpg_demo/generate_bgm.rb` | Ruby で FMSQ を書く既存例。**雛形にできる** |
| APU レジスタ | 同上 + `components/apu_emu/` | REG_WRITE で叩くレジスタの意味 |
| BASIC の MML | `components/basic/core/basic_mml.cpp` | 音高・テンポの既存の対応表 (P1 で流用) |
| 音の検証 | `../fmrb-audio-tools/README.md` | `bin/fmsq_player <f.fmsq> -o out.wav` で WAV 化 |
| **Midori 本体** | `../tmp/midori/` (clone 済み) | 下表のとおり |

### Midori のどこを見るか

`family-mruby/tmp/midori/` に clone 済み (2026-08-03 時点で `673257d`)。

| 見る場所 | 何が分かるか |
|---|---|
| `mrbgems/picoruby-midi/include/midi_transport.h` | transport 抽象の関数 5 つ。APU transport はこれを実装する |
| `mrbgems/picoruby-midi/include/` の他 | `midi_parser.h` / `midi_scheduler.h` / `midi_clock_gen.h` |
| `mrbgems/picoruby-uart_midi/` | UART transport 本体。ports は esp32 のみ |
| `mrbgems/picoruby-sam2695/` | **uart_midi の薄いラッパ (Ruby 43 行)**。Unit MIDI 用 |
| `mrbgems/picoruby-midi-mml/mrblib/` | MML 解析 (`midi_mml.rb`) と演奏 (`midi_mml_player.rb`)、各 345/347 行 |
| `docs/MML_DESIGN.md` | **MML 方言の仕様書**。P0-5 の比較対象はこれ |
| `docs/PICORUBY_MIDI_STANDARDIZATION.md` | upstream 標準化の状況 |
| `docs/MIDI_DEVICES.md` | transport ごとのデバイス扱い |

**tmp/ 以下は Family mruby のリポジトリではない**。参照専用で、変更しないこと。

## 2. P0: 計測と調査

結果は `doc/midi/report/p0.md` に書く (数値・手順・結論)。
**測る前に「何を判断するための数字か」を書いてから測ること**。

### P0-1: APU 発音の遅延とスループット (最重要)

方向B (APU transport) が実時間演奏に耐えるかを決める。

- **遅延**: Ruby で `FmrbAudio#note_on` を呼んでから音が出るまで。
  経路は Ruby -> kernel メッセージ -> UART link (921600) -> WROVER -> APU。
  ホスト側で時刻を取れる地点を選んで区間ごとに測る (往復が取れるなら往復で)。
- **スループット**: **こちらが本命**。MIDI は和音で 3 バイトのイベントが束で来る。
  「16 分音符の和音を連打して詰まるか」を測る。同じ host メッセージキュー
  (長さ 128、格納域は PSRAM) を GFX も通っており、実測で 160 cmds/s 級は
  流れている。**何イベント/秒までなら落ちないか**を出す。
- 実機 (S3) と Linux sim の両方で測り、差を記録する。

判断に使う: P2 で SMF プレーヤを C に置くか Ruby に置くか。

### P0-2: FMSQ 再生中の note_on の挙動

**BGM を鳴らしながら MIDI で発音できるか**を確定させる。

`play_slot` は instance を持つ (0=MAIN は NSF 再生と、1=SUB は note_on/off の
効果音と、それぞれインスタンスを共有する。
`../fmruby-graphics-audio/main/common/audio_commands.h`)。

- FMSQ を再生しながら `note_on` を撃つと何が起きるか (無視される / 声を奪う /
  別インスタンスで両立する)。
- 4 声の割り当てはインスタンス単位なのか全体なのか。

判断に使う: 方向B のチャンネル割り当て表。ここが「両立しない」なら、
BGM と実時間演奏の同居は設計から外す必要がある。

### P0-3: S3 の空き UART ユニット確認

- UART0 = コンソール、UART1 = graphics-audio (`fmrb_hal_link_uart_esp32.c` の
  `UART_PORT_NUM`)。**UART2 が空いているか**を実機で確認する。
- 確認方法は「UART2 を 31250 bps で開いて成功するか」で足りる。
  GPIO47/48 に割り当てて送信できるところまで見られるとなお良い
  (Unit MIDI が手元にあれば実際に鳴らしてもよいが、それは P5 の範囲)。

### P0-4: gem 追加後の flash サイズ見積り

**S3 の app パーティションは 4M で残り 24% (約 1MB)** しかない
(`doc/internal_ram_budget.md` の 2026-08-03 節)。

- P2 で入れる予定の gem 群 (`picoruby-midi` ほか) を仮に足したとき、
  どれくらい増えるかを見積もる。**実際に入れる必要はない**。
  Midori のリポジトリサイズや、既存 gem の追加時の増分から概算でよい。
- 足りない見込みなら、パーティションを更に広げる案を報告に書く
  (16MB のうち使用は約 9MB なので余地はある。ただしフル書き込みが要る)。

### P0-5: MML 方言の差分表

方向D (MML 共通化) の前提。**表を作るところまで**が P0 の範囲で、
共通化の実装は P6。

- Family mruby BASIC の MML (`components/basic/core/basic_mml.cpp`) と、
  Midori の MML を比較する。Midori 側は `docs/MML_DESIGN.md` に仕様が
  書かれており、実装は `mrbgems/picoruby-midi-mml/mrblib/midi_mml.rb`。
  **両方を突き合わせて表にする** (仕様書と実装がずれている場合は実装を正とし、
  ずれ自体も記録する)。
- 比較軸: テンポの基準 (BASIC は T4 = 4 分音符 0.5s、実測済み。Midori は BPM)、
  オクターブの基準 (BASIC は O3 の C = 中央ハ = 261.4Hz 実測。Midori は o4 が既定)、
  音長・付点・タイ・休符・音色指定の記法。
- Midori 側にあって BASIC に無い記法 (ループ `[cdef]4` のネスト、複付点 `c4..`、
  ベロシティ `v100`) をどう扱うかは P6 の論点。**表に載せるところまで**でよい。

## 3. P1: SMF -> FMSQ 変換ツール

### 3.1 何を作るか

`tool/midi/smf2fmsq.rb` (Ruby、標準ライブラリのみ)。

```
ruby tool/midi/smf2fmsq.rb input.mid [-o out.fmsq] [--track N] [--map ...]
```

- SMF format 0 / 1 を読む (format 2 は対象外でよい。その旨をエラーで出す)。
- 出力は FMSQ v1。**仕様は `../fmrb-audio-tools/doc/fmsq_format.md` が正**。
- `flash/app/game/rpg_demo/generate_bgm.rb` が Ruby で FMSQ を書く既存例なので、
  ヘッダ組み立てとレジスタ書き込みの作法はそこに倣う。

**Ruby で書くこと** (ルート `CLAUDE.md` の「周辺ツールの言語」)。gem は使わず
標準ライブラリだけで書く (SMF のパースは自前で書ける規模)。

### 3.2 設計上の判断が要る点

以下は**指示ではなく検討事項**。実装前に短く方針を決めて、報告に理由を書くこと。

1. **チャンネル割り当て**。MIDI 16ch を APU 4 声にどう落とすか。
   既定案は README 方向B のとおり (ch1,2 -> 矩形波、ch3 -> 三角、
   ch10 -> ノイズ)。`--map` で上書きできる形にしておくと実用的。
2. **声を超えた和音の扱い**。4 声を超えたときに何を捨てるか
   (後着優先 / 音高が高い方優先 / 先着優先)。**捨てる方針を決めて明示する**。
   ここが聞こえ方を最も左右する。
3. **時間解像度**。FMSQ は 60Hz フレーム、SMF は tick + テンポマップ。
   丸め方 (最近傍 / 切り捨て) と、テンポ変更 (Set Tempo メタイベント) への
   追従をどうするか。**テンポ変更が曲中にある SMF は珍しくない**ので、
   最低限追従はしたい。
4. **ベロシティ -> 音量**。APU の音量は 0-15。線形か対数か。
5. **ドラム (ch10)** をノイズにどう割り当てるか。音程が無いので、
   ノート番号ごとにノイズ周期を割り当てる表が要る。最初は数種類でよい。

### 3.3 完了の条件

- 手持ちの .mid (無ければ簡単なものを自分で生成する) を変換して `.fmsq` が出る。
- **`bin/fmsq_player out.fmsq -o out.wav` で WAV にして、音高と長さを数値で検証する**
  (`../fmrb-audio-tools` を `rake build` して使う)。耳で聞く確認はユーザが行う。
  検証例: 単音のスケールを変換 -> WAV を FFT -> 期待周波数と一致するか。
  和音は最強ピークしか出ないので、声ごとに分けて測る。
- 実機/sim で `FmrbAudio#load_fmsq_file` + `play_slot` から再生できることを、
  少なくとも sim で確認する (sim で音を出すには **通常の `docker compose up`**
  が要る。`dev_run_check.sh` は SDL dummy なので無音)。
- テスト用の .mid と期待値をどこかに残す (`tool/midi/test/` など)。

## 4. 守ること

- **`doc/midi/README.md` は勝手に書き換えない**。P0 で前提が覆ったら、
  報告書 (`doc/midi/report/p0.md`) に「README のここが違う」と書く。
  README の更新はレビュー後にまとめて行う。
- 実装中の気づき・実測値・申し送りは `doc/midi/report/` に書く
  (計画文書には未確定事項の確定結果だけ入れる)。
- コード中のコメントは英語。コミットログは英文。ASCII 以外の記号は使わない。
- `lib/` 以下や submodule を触る場合の作法、ビルド前の `rake clean` /
  `rake clean_all` は `fmruby-core/CLAUDE.md` に従う。
- **sdkconfig 系は編集禁止** (必要なら提案)。
- 実機の flash とログ採取は自律的に行える。手順はルート `CLAUDE.md` の
  「ESP32-S3 実機の自律検証」節。ポートは排他なので、ログ採取中は flash できない。
- **音の最終確認はユーザが行う**。数値検証まではこちらで詰める。

## 5. 進め方の提案

P1 -> P0 の順でも構わない (P1 はハードウェア不要で、先に成果が出る)。
ただし **P0-2 (FMSQ と note_on の同居) だけは早めに**確認したい。
ここの結果次第で P2 の設計が変わるため。

完了したら、報告書と差分をレビューに回すこと。
