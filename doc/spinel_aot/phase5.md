# Phase 5 指示書: ESP32-S3 ポート

前提: **Phase 3.5 完了は必須** (カーネルのみ先行移植する場合でも)。
ESP32 では素の malloc が禁止で全確保を estalloc プールに向ける必要が
あり、そのフック機構 (SP_MULTI_CTX + sp_mem_override.h) は Phase 3.5 の
成果だからである。つまり **ESP32 ビルドは常に SP_MULTI_CTX 構成**
(インスタンスが 1 つでも)。default (非 MC) ビルドを ESP32 に載せる
構成は存在しない。

その上で「Phase 4 完了前にカーネルのみ先行して実機リスクを消化する」
か「Phase 4 完了後に全体を載せる」かをユーザに確認する。先行する場合、
Phase 4 の事前作業 1-5 (pin 更新 / MC 配線 / instance create /
estalloc 配線 / Linux 回帰) はこちらで先に実施することになる。
desktop も載せる場合は T4-0 (マルチプログラムリンク対応) 完了が必須。

対象ハード: ESP32-S3 (Xtensa LX7, 32bit, 内部 SRAM ~512KB,
PSRAM 搭載, ESP-IDF + FreeRTOS)。グラフィクス/音声は子マイコンに
SPI で委譲されるため、Spinel 側の移植対象は計算・制御コードのみ。

## 背景 (調査済みの移植ハザード一覧)

fork の以下の箇所が ESP-IDF/newlib で問題になる (行番号は目安):

1. `lib/sp_gc.c`: `#include <malloc.h>` と `malloc_trim(0)`。
   **SP_MULTI_CTX では Phase 3.5 で既に no-op 化済み** (ESP32 は常に
   MC なので実害なし)。`#include <malloc.h>` 自体が newlib で通るかは
   確認し、必要なら include ガードを fork へ (upstream PR 候補)。
2. `lib/sp_gc.h`: `SP_GC_STACK_MAX` デフォルト 65536 エントリ
   (32bit で 256KB)。Phase 3 でルートスタックは per-instance の
   heap 確保 (`root_stack_entries` config) になった。**静的 BSS 側の
   default 配列が SP_MULTI_CTX ビルドで確保されない (BSS に残らない)
   ことを map ファイルで確認** (残るなら fork 側で直す)。
3. `lib/sp_alloc.c`: GC トリガ閾値デフォルト 256KB x2 (obj/str)。
   instance config で下げる (「しきい値 < プールサイズ」契約)。
4. `sp_runtime.h:53` 付近: `#include <sys/mman.h>` が無条件。
   ESP-IDF に sys/mman.h はない → fiber を使わない構成でも
   ヘッダが通らないため、include を fiber 使用時のみに
   ガードする fork 修正が必要 (upstream PR 候補)。
5. fiber / thread / net / system / signal 系メンバ: リンク対象外に
   する (使わなければ参照されない設計だが、ESP-IDF コンポーネントの
   SRCS に**そもそも入れない**)。
6. `__attribute__((constructor))`: **SP_MULTI_CTX では Phase 3 で解決
   済み** (SP_TU_CTOR は MC で空、フック登録は entry 冒頭の
   `sp_tu_ctx_init()` と `sp_instance_create` に移動)。ESP32 は常に
   MC なので constructor タイミング問題は存在しない。lib 側に MC でも
   残る constructor が無いか grep で確認だけする。
7. Ruby グローバル変数・シンボルテーブル等の生成 TU 側 static は
   そのまま .bss/.data に載る。サイズを確認し、大きければ配置を検討。

## タスク

### T5-1: fork の ESP32 対応パッチ (1-2 日)

1. 上記 1, 4 のガードを fork に実装 (汎用ガードとして。
   `#ifndef SP_HAVE_MALLOC_TRIM` のような feature マクロ方式でも良い。
   upstream に受け入れられやすい形を選ぶ)。
2. 32bit 問題: Phase 0 の -m32 検証で見つかった問題の修正を fork に
   実装し、-m32 でのテスト実行を fork の CI 相当 (Makefile ターゲット
   `make test32` 等) として追加する。Xtensa 固有ではなく 32bit 一般の
   問題として直すこと。
3. `make test` (64bit) 全パス維持。

### T5-2: ESP-IDF コンポーネント化 (1-2 日)

1. 3 点セット (fork push 確認 → `SPINEL_PIN` 更新 → `rake spinel:setup`
   → `import_from_fork.rb`) でスナップショット更新。
2. `components/fmrb_spinel_rt/CMakeLists.txt` を ESP32 ビルド対応:
   - SRCS は必要メンバのみ (fiber/sched/net/system/re/bigint/crypto/
     pack を除外。time/random は生成コードが要求するなら含める)。
   - `-DSP_MULTI_CTX` + **`-include sp_mem_override.h`** (Phase 4
     事前作業 2 と同じ両側一致必須の組。生成 C 側と単一変数から配る)、
     `-ffunction-sections -fdata-sections`
     (ESP-IDF はデフォルトで有効のはず。確認して重複指定は避ける)。
   - ESP-IDF のリンクは --gc-sections が既定。生成 C とランタイムの
     未使用関数が最終 elf から落ちていることを `idf.py size` 系で確認。
   - nm チェック (Phase 4 で移植済みのはず) を ESP32 ビルドの
     オブジェクトにも適用: Spinel 由来オブジェクトが newlib の
     malloc 系を参照していないこと。
3. `sp_ctx_current` の ESP-IDF 実装を追加
   (`components/fmrb_spinel_rt/port/esp32/sp_ctx_port.c`):
   - FreeRTOS タスクローカルストレージポインタ
     (`vTaskSetThreadLocalStoragePointer` /
     `pvTaskGetThreadLocalStoragePointer`) を使う。
   - スロット番号は `fmrb_task_config.h` に定数として定義。
     既存の TLSP 利用と衝突しないこと、
     `CONFIG_FREERTOS_THREAD_LOCAL_STORAGE_POINTERS` が足りることを
     確認。不足する場合は sdkconfig を**編集せず**、必要な設定変更を
     ユーザに提案する。
   - Linux (WSL2 コンテナ) ビルドはこれまでどおり __thread 実装。
     ESP-IDF の Linux ターゲットでビルドしている場合はどちらの
     実装が適切か確認して選ぶ。
4. アロケータ接続 (**ユーザ決定 2026-07-23: estalloc 標準、Phase 4
   事前作業 4 で配線済みの方式をそのまま ESP32 に適用**):
   - spawn 時にタスク所定 mempool (`fmrb_get_mempool_ptr(mempool_id)`)
     へ `est_init` → `ESTALLOC*` を `ctx->est` に格納し、instance
     config の mem_ud + フック (est_calloc/est_realloc/est_free) へ。
     `fmrb_app_ps` は ctx->est 経由で統計 (Monitor 表示も同一機構)。
   - ESP32 固有の確認点: mempool 領域の配置 (PSRAM/内部 SRAM) は
     既存の mempool 設計に従う。ヒープ本体 (GC オブジェクト) は
     PSRAM 許容、root スタック・mark stack (sp_instance_create の
     arena) を内部 SRAM に置く使い分けが要るかは、まず全部タスク
     プールで動かして計測してから決める。分ける場合は instance
     config の arena 用フックを分離する fork 改修を起案。
   - estalloc は 24bit アドレッシング (プールあたり最大 16MB) で
     ESP32 の PSRAM プール規模に対して十分。

### T5-3: カーネルの実機投入 (1-2 週、実機検証込み)

1. インスタンス設定の初期値:
   - root_stack_entries: 4096 (16KB) から開始。溢れログが出たら増やす
     (Phase 3 で溢れ時にエラーが出る実装にしてある)。
   - gc_threshold / str_threshold: 各 64KB から開始し、GC 頻度と
     停止時間のバランスで調整。
   - タスクスタック: mruby カーネルタスクの現行値から開始
     (fmrb_task_config.h)。Spinel は C スタック消費が mruby より
     深くなりうる (再帰的な Ruby 呼び出しが C 再帰になる) ため、
     FreeRTOS のスタックハイウォーターマークを起動後にログして確認。
2. `rake clean_all && FMRB_KERNEL_ENGINE=spinel rake build:esp32` を通す。
   リンクエラー (欠落シンボル) が出た場合、生成コードが想定外の
   ランタイムメンバ (time 等) を要求している。要求元を特定して
   メンバ追加 or Ruby 側書き換えを判断。
3. 実機書き込み・起動はユーザに依頼する (AI は書き込みまで行わない。
   ログの取得方法・確認ポイントを具体的に指示する)。確認項目:
   - ブート完了 (デスクトップ表示)
   - マウス/キーボード操作、ウィンドウドラッグ
   - アプリ起動/終了
   - シリアルログにエラー・スタック警告がないこと
4. 計測 (カーネル Ruby 内の共通計測コード + C 側):
   - イベント処理 max/avg latency (mruby 版実機と比較)。計測コードは
     Phase 2 で input_router.rb に実装済み (hid_lat ログ、1000 イベント
     ごとの sum/max/閾値カウント)。実機でもそのまま出る。
     Linux 実測 (mruby 比 80-150 倍、25ms 警告ゼロ) が比較基準。
     **実機での本命確認は「hid_event slow (>25ms) 警告が Spinel 版で
     ゼロになること」** (Linux では両版ゼロで差が出なかった項目)
   - GC 停止時間: sp_gc_collect の前後を esp_timer で計測するログを
     fmrb_spinel_rt の port 層に追加 (fork 側にフックがなければ
     fork に GC 計測フック (関数ポインタ) を追加。upstream PR 候補)
   - メモリ: 内部 SRAM / PSRAM の使用量 (heap_caps_get_info)、
     mruby 版カーネルのヒーププールと比較した増減
   - フラッシュ増分: mruby 版と spinel 版の .bin サイズ比較
5. 安定性: ユーザ操作ベースの soak (30 分以上) と、Linux で使った
   マウス洪水注入の実機版が可能か検討 (USB HID 注入が無理なら
   ユーザの手動操作 + 長時間放置で代替)。

### T5-4: desktop (+ 対象化した場合 shell) の実機投入 (Phase 4 完了後、1 週)

1. 各インスタンスのヒープ設計: desktop は UI 生成物が多いので
   閾値を大きめ (PSRAM 前提)、kernel は小さめ。設定値と根拠を
   レポートに記録。
2. `idf.py size` 相当でフラッシュ内訳を確認。複数プログラムの生成 C が
   大きい場合の対策候補 (評価だけして適用は相談):
   生成 C の -Os コンパイル、共有できる Ruby ライブラリ部の
   共通 TU 化 (fork 改修が必要なので費用対効果を見る)。
3. 検証項目は Phase 4 の T4-5 と同じセットの実機版 + ユーザによる
   操作感確認 (音声・NTSC 出力はユーザ確認事項)。

## 受け入れ基準

1. `FMRB_KERNEL_ENGINE=mruby` の ESP32 ビルド・動作に一切回帰がない。
2. spinel カーネルが実機で安定動作し、イベントレイテンシと GC 停止
   時間が mruby 版より改善 (数値をレポート)。
3. メモリ収支 (内部 SRAM / PSRAM / フラッシュ) が表になっており、
   mruby ヒープ削減分と Spinel ランタイム増分の差引が明確。
4. (T5-4 まで行う場合) kernel + desktop (+ shell 対象化時) の
   複数インスタンス同居が実機で安定、soak クリーン。
5. fork の ESP32 対応が汎用ガードとして実装され、64bit テストが
   全パスのまま。

## 落とし穴・注意

- sdkconfig / sdkconfig.defaults は編集禁止。TLSP 数、スタック
  オーバーフローチェック設定などで変更が必要になったら**提案のみ**。
- ESP-IDF の newlib には isatty/ioctl 等の癖がある。sp_io.c で
  リンク/実行時に問題が出たら、その関数が本当に必要か (stdout への
  puts だけなら不要) を確認し、port 層でスタブする。
- Xtensa の THREADPTR ベース __thread は ESP-IDF でサポートされるが、
  .tbss/.tdata は**全タスク**の TLS 領域に複製される。fork/port 層に
  ポインタ 1 本以外の __thread を置かないこと (Phase 3 の設計どおり)。
- PSRAM アクセスは内部 SRAM より遅い。GC ヒープを PSRAM に置くと
  マーク/スイープが遅くなるトレードオフがある。まず計測、それから配置。
- 生成 C の関数が長大で Xtensa gcc の最適化が遅い/メモリを食う場合、
  ビルドマシン側の問題としてコンパイル分割を検討 (fork の出力分割
  機能はないので、必要になったらレポートに起案)。
- キャッシュ/IRAM: ホットパス (GC マークループ等) を IRAM_ATTR に
  する最適化は**動いてから**。最初から手を出さない。

## 完了レポート

`doc/spinel_aot/reports/phase5_report.md`:
- fork ESP32 パッチ一覧 (upstream PR 候補の別)
- インスタンス設定値 (ヒープ閾値、root スタック、タスクスタック) と
  チューニング経緯
- 実機計測表 (レイテンシ、GC 停止、メモリ、フラッシュ、mruby 比)
- 既知の制限・残課題 (IRAM 最適化、コンパイル分割等の起案)
- ユーザ確認が必要な項目のチェックリスト (音声、NTSC、操作感)
