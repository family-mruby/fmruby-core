# Phase 4: ESP32 ビルド・資源実測・制限事項文書

## 目的

実機ターゲット (esp32s3 Retro / esp32p4 Modern) でビルドを通し、
フラッシュ・RAM のコストを実測して採用可否の最終判断材料を揃える。
実機での動作確認はユーザが行う。

## 作業項目

1. **esp32 ビルド**: rake clean_all の後 rake build:esp32 (S3)。
   Xtensa 特有のビルドエラー (アライメント、setjmp、int サイズ前提) を潰す。
   修正はすべて port/ 側 (mpconfigport.h やラッパ) で行い、submodule と
   mp_embed 生成物への手編集はしない (生成物の修正が要る場合は
   mpconfigport.h 経由で解決するか、報告して相談)。
   P4 (Modern) 側もビルドが通ることを確認する (.env の FMRB_HW_TARGET に注意)。

2. **サイズ実測**: MicroPython コンポーネント有り/無し (git stash 等ではなく
   直前コミットとの比較でよい) で以下を記録し、本ファイルに表で追記する。
   - フラッシュ: fmruby-core.bin のサイズ差分、パーティション残量
   - 静的 RAM: idf.py size 相当の bss/data 差分
   比較は必ず同一コミット同士のビルドで行う (別日の記録と突き合わせない)。

3. **ヒープ配置の確認**: GC ヒープの元になるアプリ用プールが PSRAM 側に
   置かれていることを確認する。内蔵 RAM から取られている場合は、
   ヒープサイズ (FMRB_MP_HEAP_SIZE) を内蔵 RAM 予算と相談して決め直す。

4. **タスクスタックの再確認**: S3 実機ビルド構成で phase2 と同じ
   stack_high_water 確認を行う (linux と Xtensa でスタック消費が違うため)。

5. **実機確認 (ユーザ依頼)**: python.app の起動・描画・終了・排他を実機で
   確認してもらう。書き込み手順と確認項目を依頼時に明示する。
   実機で NG が出た場合はログを回収して原因を特定してから直す
   (推測での修正を繰り返さない)。

6. **制限事項文書**: doc/micropython/known_limitations.md を作成する。内容:
   - 同時実行 1 本 (理由: VM 状態がグローバル)
   - import は組み込みモジュールのみ
   - REPL なし / スレッドなし
   - 対応 API の一覧 (app / gfx) と Lua 版との差分 (無いのが理想)
   - GC ヒープサイズと超過時の挙動 (MemoryError)
   - 停止要求からの脱出タイミング (フック粒度)

7. **README 更新**: 本計画 README の「リスク・実測で確定する項目」に
   実測値を反映し、残課題 (ファイルシステム import、アイコン、REPL など) を
   将来課題として列挙する。

## サイズ実測 (2026-07-31, commit 81a29d4 + GCREGS 修正)

コンポーネント単位の寄与 (`idf.py size-components`)。全体バイナリの差分より、
MicroPython だけの取り分がそのまま出るぶん正確。比較用に Lua を併記する。

### ESP32-S3 (NARYAv3 / N16R8, Xtensa)

| アーカイブ | 合計 | Flash .text | Flash .rodata | 内蔵 RAM (.bss) |
|---|---|---|---|---|
| libmicropython.a | 127,912 B | 102,146 B | 25,321 B | 445 B |
| liblua.a (参考) | 119,100 B | 115,809 B | 3,291 B | 0 B |

| 項目 | 値 |
|---|---|
| fmruby-core.bin | 0x256600 (2,451,968 B) |
| アプリパーティション | 0x300000 (3 MB) |
| 残量 | 0xa9a00 (694,272 B / 22%) |

### ESP32-P4 (TAB5 / Modern, RISC-V)

| アーカイブ | 合計 | Flash | 内蔵 RAM (.bss) |
|---|---|---|---|
| libmicropython.a | 149,992 B | 149,547 B | 445 B |
| liblua.a (参考) | 147,789 B | 147,789 B | 0 B |

| 項目 | 値 |
|---|---|
| fmruby-core.bin | 0x3dc4b0 (4,048,048 B) |
| アプリパーティション | 0x600000 (6 MB) |
| 残量 | 0x223b50 (2,244,432 B / 36%) |

**当初見込み (フラッシュ +200-300KB) を下回った** (S3 で 128KB、P4 で 150KB)。
Lua とほぼ同じ規模で、残量は両ターゲットとも十分。

### ヒープ配置

アプリ用メモリプールは全て `EXT_RAM_BSS_ATTR` = PSRAM
(components/fmrb_mem/fmrb_mempool.c)。GC ヒープ 256KB はそこから取るので
**内蔵 RAM 予算には効かない**。MicroPython が内蔵 RAM に置くのは
445 B の .bss のみ。`FMRB_MP_HEAP_SIZE` の見直しは不要。

## 完了条件

- S3 / P4 の両ターゲットで rake build:esp32 が成功する。
- サイズ実測表が本ファイルに追記され、フラッシュ残量が許容内であることが
  数字で示されている。
- known_limitations.md が作成済み。
- 実機確認の結果 (ユーザ報告) が本ファイルに記録されている。
  実機確認だけが残って他が完了している状態なら、フェーズは
  「実機待ち」として扱ってよい。

判定結果・実装中の気づき・**実機確認の依頼手順**は
[report/phase4.md](report/phase4.md)。現状は「実機待ち」。

## 実機確認の結果 (ユーザ報告後に追記)

未実施。
