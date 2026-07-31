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

## 実機確認の結果

### ESP32-S3 (NARYAv3 / N16R8) — 2026-07-31, 1215f2a

**MicroPython 固有の問題はゼロ**。

| 項目 | 結果 |
|---|---|
| ファームウェア起動 | 正常 (`fmrb_mp: MicroPython subsystem initialized`) |
| ランチャーへの表示 | 再スキャン後 `Found app: Python (/app/demo/python.app.py)` (35 -> 36) |
| ランチャーからの起動 | 成功。`Detected Python script` -> vm_type=4 -> プール 4 割当 |
| キャンバス生成 | `Created canvas 3 (160x190) for app Python` |
| アプリ基盤の読み込み | 63-118 ms (同時に動くアプリの数で変わる) |
| on_create の実行 | `Python demo started on esp32` |
| 画面 | **Linux シミュレーションと同じ見た目** (ウィンドウ枠・閉じるボタン・角丸枠と Shapes ページの図形) |
| ページ切替 | OK。ユーザ領域のクリックで Shapes -> Lines -> Text と 3 ページ巡回 (on_event が効いている) |
| 排他 | OK。実行中に 2 本目を起動すると `Python app 'Python' is already running; only one at a time` -> エラーダイアログ -> 2 本目のタスクは正常終了して回収 |
| 閉じるボタンでの終了 | OK。`App Python received stop command` -> `_cleanup` -> `Canvas delete queued` -> `Python script executed successfully` -> プール破棄 -> Reaped |
| 終了後の再起動 | OK (排他が解放されている) |
| `_spin` 待機中の停止 | OK。**20 ms で応答** (`on_update` が 5000ms を返すアプリで、turn の 3.9 秒後に閉じるボタン -> 同じ 20ms 以内に `on_destroy`) |
| **GC のレジスタ退避** | **OK。`pytest: gc ok=True free 246176 -> 248256`** |

`gc ok=True` は `MICROPY_GCREGS_SETJMP` が Xtensa で正しく動いていることの
確認。ローカル変数からしか届かないオブジェクトを作って collect し、
読み戻して内容が一致することを見ている。**これが通らないと後になって
壊れる**類の問題なので、実機で確かめる価値が一番高い項目だった。

未確認 (次の実機確認に持ち越し): ビジーループ中の停止 (VM フック経由の
abort)、.rb/.lua/.bas の退行。どちらも Linux シミュレーションでは確認済みで、
実機固有の要素が薄い項目。

### 実機の実測値

**GC ヒープ** (確保 262,144 B)

| 項目 | S3 実機 | Linux 参考 |
|---|---|---|
| GC が管理する総量 | 258,048 B | 259,968 B |
| 起動直後の使用量 | 96 B | 192 B |
| アプリ基盤の消費 | 8,496 B | 12,672 B |
| ユーザスクリプトが使える目安 | 約 244 KB | 約 241 KB |

32bit なのでオブジェクトが小さく、Linux (64bit) より消費が少ない。

**アプリ基盤の読み込み時間**: **63-118 ms** (Linux は 0-3 ms)。
デスクトップ単独のときが 63ms、シェルも動いている状態で 118ms。
アプリ起動のたびに乗るが、ランチャーのスキャンが秒単位かかることを思えば
体感には出ない。frozen bytecode を入れる判断はこの値を基準にする。

**タスクスタック** (アプリタスク 16,384 B)

| 項目 | 値 |
|---|---|
| fmrb_mp_start 時点の空き | 14,128-14,140 B |
| 設定されたスタック上限 | 12,080-12,092 B (空き - 予備 2,048 B) |
| 実行中の最小空き | 11,664-11,724 B |
| ピーク使用量 | 約 4,720 B (16KB の 29%) |

上限は起動時の残量から決まるので、呼び出し時のフレーム深さでわずかに動く。
そのうち MicroPython 自身の消費は約 2,460 B (上限 12,080 B に対して 20%)。

**16KB のままで十分**。MicroPython のときだけスタックを増やす必要はない。
上限 12,092 B に対してピークが 4,660 B なので、もっと深い再帰をする
スクリプトにも余地がある。

**内蔵 RAM**: Python アプリ実行中は IRAM 空きが 81,920 -> 58,852 B に減る。
これはアプリタスクのスタック 16KB + TCB + メッセージキューで、**どの
ユーザアプリでも同じ**。MicroPython 固有の増加ではない
(コンポーネントの静的 RAM は 445 B のまま)。
