# 実装指示書 S1-S4: ESP-IDF 依存の継ぎ目整理

対象: 実装担当セッション。前提: plan.md と、背景として
doc/wasm/guide_integration.md (stub がなぜ生まれたか)。
タスクごとにコミット (push はしない)。report は doc/archive/idf_seam/report/s1_s4.md へ。

## ゴールと原則

共有コードの esp_* 直接依存を fmrb_* 抽象の platform 実装へ吸収し、
**wasm/stub/include/ を vendored カーネル用の 4 枚
(sdkconfig.h / esp_assert.h / esp_compiler.h / esp_heap_caps.h) だけにする**。

- **挙動変更ゼロに徹する**。ヘッダの付け替え・実装の移動だけを行い、
  ログの文言 1 つ変えない。リファクタと改善を混ぜない。
- lib/ (patch/add) はこのテーマでは触らない。対象は components/ と main/。
- wasm P5 が並行する場合、wasm/CMakeLists.txt と wasm/stub は両テーマが
  触る。着手前にユーザへ先行順を確認する。

## S1: 全数調査と台帳

1. wasm ビルドのソース集合 (wasm/CMakeLists.txt が列挙。ビルド後の
   wasm/build/compile_commands.json が確実) に含まれる全ファイルについて、
   esp_* の include を列挙する。
2. 3 分類の台帳を report に作る:
   - **A: ラッパーの底** (fmrb_* の実装が esp_* に立っている) → S2 で分割
   - **B: 網の穴** (共有コードの直接使用) → S3 で吸収
   - **C: 対象外** (実機専用 driver、platform/esp32/、カーネル)
3. 現時点で判明している見取り (検証済みの出発点。網羅は S1 の仕事):
   - A: fmrb_log.h (ESP_LOG* へ直展開。fmrb_log/fmrb_log.h:4)、
     fmrb_hal_time.c (esp_timer)、fmrb_sysinfo.c (esp_mac)、
     fmrb_task.c (esp_log + esp_heap_caps)、fmrb_msg.c (esp_heap_caps)、
     **posix platform 実装群** (fmrb_hal_i2c_posix.c / link_posix / rmt_posix
     が esp_log.h を include — posix なのに IDF に寄りかかっている)
   - B: esp_attr (fmrb_hal_esp.h、fmrb_spx_app.c、fmrb_debug_ctx.h、
     hid_device_config.c ほか)、esp_system / esp_heap_caps の直接使用
     (fmrb_spx_app.c)、esp_random、esp_err_t の共有コード残り、
     esp_cache (display_p4 系)
   - 予想される吸収対象の全体は、いまの wasm/stub/include の 10 枚 +
     esp_stub.c がそのまま台帳になっている。

## S2: ラッパーの底の platform 分割

方針: fmrb_hal の既存流儀 (platform/ ファイル分割) を優先。ヘッダだけで
済むもの (fmrb_log) は #if 分岐でよい。

1. **fmrb_log**: fmrb_log.h の中で分岐する。IDF ターゲット (esp32 / linux)
   は従来どおり ESP_LOG* へ、FMRB_PLATFORM_WASM は printf 系実装へ
   (中身は今の wasm/stub/include/esp_log.h を fmrb 側へ引っ越す形)。
   fmrb_log_buffer との関係を先に確認すること。
2. **fmrb_hal_time**: esp_timer 依存を platform/esp32 へ。posix / wasm は
   clock_gettime (posix 実装 1 枚で両方賄えるはず)。
3. **fmrb_sysinfo**: MAC 取得 (esp_mac) を platform 関数に切り出す。
   posix / wasm は固定値 (今 esp_stub.c がやっている内容を移す)。
4. **fmrb_task / fmrb_msg**: esp_heap_caps の使用を確認し、可能なら
   fmrb_mem 経由へ寄せる。fmrb_mem 自身の底 (heap_caps) は fmrb_mem の
   platform 分岐に集約する — heap の継ぎ目は 1 箇所にする。
5. **posix platform 実装から esp_log を剥がす**: fmrb_log 経由に置換
   (1 の分岐が済んでいれば機械的な置換)。

## S3: 網の穴の吸収

1. **fmrb_attr.h を新設** (components/fmrb_common/include/)。
   EXT_RAM_BSS_ATTR / IRAM_ATTR / DRAM_ATTR など実際に使われている属性を
   S1 の台帳から拾い、ESP32 では esp_attr.h へ転送、それ以外では空展開。
   使用箇所を FMRB_* 名へ一括置換 (esp_attr.h の直接 include を消す)。
   linux / wasm で空展開にしたときに警告が出ないことを確認。
2. **esp_random** → fmrb_hal に乱数関数を 1 つ追加し置換。
3. **esp_system** (esp_restart / esp_read_mac 経由分) → 既存の fmrb_hal /
   fmrb_sysinfo に寄せる。
4. **esp_err_t が共有コードに残る箇所** → fmrb_err.h へ (規約どおり)。
   IDF API の戻り値をその場で受ける変数は対象外 (platform 実装内なら可)。
5. **esp_cache** (display_p4 の msync) → esp32 platform ガード内へ移す。
   wasm では既に FMRB_PLATFORM_WASM で除外されている領域のはずなので、
   S1 で実態を確認してから。

## S4: stub の縮小と規約

1. wasm/stub/include/ から、参照が消えたヘッダを削除していき、
   カーネル用 4 枚だけにする。esp_stub.c は解体し、中身は各 fmrb 抽象の
   wasm (または posix 共用) platform 実装として移動・改名する。
2. fmruby-core/CLAUDE.md の「開発時の注意」に 1 行追加:
   「esp_* を直接 include してよいのは各 fmrb 抽象の esp32 platform 実装と
   実機専用 driver のみ。共有コードは fmrb_* 経由」。
3. 受け入れ検証 (下記) を一式回し、report に結果を載せる。

## 検証

段階ごとに最低限 (ビルド通過)、S4 完了時に全量:

1. **全 5 構成のビルド**: S3 (NARYAv3、rake clean_all 切替) / TAB5 /
   linux (file で x86-64 確認) / wasm core / wasm core_web。
2. **Linux sim 回帰**: MCP (sim_up〜sim_down) でブート、デスクトップ操作、
   エディタ起動 + 1 打鍵 (標準構成の必須項目)。
3. **Tab5 回帰**: 変更はコードのみなので flash の app_only: true で焼き、
   ブート完走 + crash 0 + デスクトップ表示。
4. **wasm 回帰**: rake wasm:poc 全 PASS、rake wasm:run 相当で node ブート +
   framedump.js でデスクトップのフレームが出る。
5. **サイズ**: idf.py size-components で TAB5 / S3 とも誤差範囲
   (bin 差分は使わない)。

## report に書くこと

- S1 の台帳 (全数。A/B/C 分類と吸収先)。
- 分割後の継ぎ目の一覧 (どの抽象が platform 何枚になったか)。
- wasm/stub の before/after (10 枚 → 4 枚の消し込み記録)。
- 挙動変更ゼロの担保に使った証拠 (ビルドマトリクス + 回帰の結果)。
- 詰まった点・判断した点 (特に fmrb_log_buffer、heap の一本化、
  esp_check.h / esp_idf_version.h の扱い)。

## やらないこと

- 実機専用 driver (ble / wifi / flash / partition / conn_check 等) と
  platform/esp32/ 配下の esp_* 使用の変更。
- vendored カーネルとカーネル用 stub 4 枚。
- 機能追加・性能改善・ログ整理 (見つけた改善は report にメモして持ち帰る)。
- lib/patch / lib/add の変更。
