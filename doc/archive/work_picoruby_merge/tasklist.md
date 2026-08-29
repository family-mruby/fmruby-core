# マージ タスクリスト

パッチ 1 件 (または 1 領域) = 1 項目。各項目に **意図 (なぜ必要か)** を記す。
意図を知らず構文的にマージすると壊れるパッチがあるため必須。

状態記号: `[ ]` 未着手 / `[~]` 調査済 (diff 把握) / `[R]` rebase 済 / `[W]` lib/ へ書き戻し済 / `[!]` 高リスク・実機確認要

diff 実体は `diffs/` に保存済 (L0_tracked.diff, L1_mruby.diff, L1_compiler2.diff)。
intent 出典: 主に `doc/reference/picoruby_upstream_pr_candidates.md` (socket/net/json)、
`doc/archive/work_picoruby_merge/plan.md`、memory (vm.c tick)、Rakefile setup コメント。

作業開始時 pin: L0 picoruby `c14aa4400` / L1 mruby `7a4622678` / L1 mruby-compiler2 `29113090` /
estalloc `ae50a9649` (**未変更=clean**)。

---

## A. lib/add/ (新規追加。upstream と直接衝突なし)

マージ主眼: 依存する picoruby 側 API (Machine/Sandbox/Kernel/IO 等) が upstream で変わっていないか。
これらは rebase の conflict には出ないため、G2 と合わせ「新 API への追従」を目視確認する。

- [~] A1-A12. picoruby-fmrb-{const,log,msgpack,kernel,app,filesystem,io}, syntax-highlight,
  rx8900, rx8130, fmrb-picorabbit, fmrb-bmp332 — fmruby 独自 mrbgem 群。
  意図: family-mruby のアプリ/カーネル/IO/RTC/画像基盤。
- [~] A13. family_mruby.gembox — **どの mrbgem を含めるか**。upstream の gem 追加/削除/改名に追従が必要 (→G2)。
- [~] A14. build_config/family_mruby_{linux,esp32}.rb — ビルド設定 (toolchain, gem 構成, define)。

## B. lib/replace/ (丸ごと置換。rebase に乗らない → 再導出)

- [!] B1. picoruby-machine (**最重要・最注意**) — upstream の変更が一切反映されない置換。
  全ファイル: include/{hal,machine,ringbuffer}.h, mrbgem.rake, mrblib/{io,kernel,machine,signal}.rb,
  ports/{common,esp32,esp32_linux,posix,rp2040}/*, sig/*, src/machine.c, src/mruby/machine.c, src/mrubyc/machine.c。
  意図: Machine/HAL/IO/signal を family-mruby (FreeRTOS + posix SDL) 向けに書換。
  `ports/esp32_linux/hal_freertos.c` は fmrb 独自の新規 port (FreeRTOS tick/IRQ を集約、
  posix task_hal.c=D7 と対になる)。
  作業: 新 upstream picoruby-machine と現行 replace 版の diff を取り、我々の変更を新版上に再導出。
  高リスク・実機確認要。

## C. lib/patch/ L0 picoruby 直下

- [~] C1. picoruby-env/ports/posix/env.c — posix env port の上書き。
- [~] C2. picoruby-require/mrbgem.rake — (1行追加)。
- [~] C3. picoruby-yaml/mrbgem.rake
- [~] C4. picoruby-sandbox/mrbgem.rake
- [!] C5. picoruby-sandbox/src/mruby/sandbox.c — 意図: Sandbox.new の未初期化 mrb_value name 修正 (1行)。**VM 隣接・高リスク**。
- [~] C6. picoruby-i2c/{include/i2c.h, src/mruby/i2c.c} — 意図: I2C#close メソッド + I2C_release 宣言追加。
- [~] C7. picoruby-socket 一式 (mrbgem.rake, src/mruby/{socket,ssl_socket}.c, ports/esp32/{tcp_socket,ssl_socket,tcp_server}.c, ports/posix/{tcp_socket,ssl_socket}.c)
  意図 (pr_candidates #1,2,7,10,11,12,13,14):
  - picorb_alloc(NULL) クラッシュ回避 → fmrb_sys_malloc/free 置換 (esp32 ports)
  - SSLSocket GC リーク修正 (free で状態不問に close)
  - close_notify を EOF 扱い / SSLSocket_ready 実装 / getaddrinfo freeaddrinfo
  - esp32p4 (ESP-IDF) ビルド対応, デフォルト timeout, esp_crt_bundle TLS, posix EINTR リトライ
  - **fmrb 固有**: fmrb_sys_malloc, FMRB_SOCKET_*_TIMEOUT_MS, esp_crt_bundle default attach
- [~] C8. picoruby-mbedtls/mrbgem.rake — 意図: esp32p4 で bundled mbedTLS obj をスキップ (ESP-IDF 提供)。build.name 判定 (pr #13)。
- [~] C9. picoruby-net-websocket/mrbgem.rake — 意図 (pr #6): mruby-pack gemdir 修正 (存在しない picoruby-pack を指す→ビルド不能)。
- [~] C10. picoruby-net-http/mrblib/http_client.rb — 意図 (pr #3,4,8,9): get_response/post_form がレスポンスを返さない修正,
  chunked デコード追加, finish の closed? ガード撤廃, URI オブジェクト受理 (CRuby 互換)。117行と大きい。
- [~] C11. picoruby-json/mrblib/json.rb — 意図 (pr #5): parse_float が小数点を落とすバグ ("26.2"->262.0)。in_fraction フラグ導入。
- [~] C12. picoruby-mruby (mrbgem 本体, lib/patch/picoruby-mruby 直下分):
  - src/alloc.c → D2 参照 (estalloc マルチ VM)。
  - src/file_ext.c — 意図: File#fsync を stub 化 (fmrb-io は mruby-io を使わないため upstream 版は不要)。
  - mrbgem.rake — 意図: posix で mruby-io 依存を外す (picoruby-fmrb-io と競合) + ESTALLOC_DEBUG を常時有効 (est_take_statistics メモリ監視用)。
  - include/hal.h — **現状 upstream と diff 無し** (patch ファイルは存在するが現 pin と一致)。新 pin で乖離しないか確認。

## D. lib/patch/ L1 mruby submodule 内 (picoruby-mruby/lib/mruby/...)

- [!] D1. src/vm.c — 意図: **タスクスイッチ修正**。mrb_task_yield_ok() で cci>0 (C 関数から mrb_funcall で再入した VM) の間は
  async task-switch を延期し安全な top-level 復帰点まで待つ。early return 時に mrb->jmp=prev_jmp を復元。
  これを怠ると "task context corrupted: no proc on resume" / longjmp 先が死んだフレームになる。
  memory: project_mruby_tick_disabled_for_rubykaigi.md。**最高リスク・実機確認必須**。
- [!] D2. picoruby-mruby/src/alloc.c (L0 側) — 意図: **estalloc マルチ VM 対応**。**高リスク・実機確認必須**。
  ※ estalloc submodule 自体は未変更 (wrapper の alloc.c のみ)。
- [~] D3. mrbgems/mruby-io/mrblib/file_constants.rb — 現 pin と diff 無し (patch は存在)。新 pin で確認。
- [~] D4. mrbgems/mruby-task/{src/task.c, mrbgem.rake} — 意図: mrb_task_reset_context の stack clear 追加 + HAL auto-load 無効化。
- [~] D5. mrbgems/mruby-dir/mrbgem.rake — 意図: ESP32 で HAL 自動検出をスキップ。
- [~] D6. mrbgems/hal-posix-dir/src/dir_hal.c — 意図: Linux Dir.open に "flash/" プレフィックス付与 (fmrb_hal_file_posix.c の仮想 namespace に整合)。
- [!] D7. mrbgems/hal-posix-task/src/task_hal.c — 意図: SIGALRM/setitimer を撤去し tick/IRQ を hal_freertos.c (B1) に一元化。
  posix でも FreeRTOS ベースで動かすため。**tick 系・実機確認要**。setup の -rf コピーで暗黙に載る (個別 cp 指定なし)。

## E. lib/patch/ L1 mruby-compiler2 submodule 内 (compiler/)

バイトコード互換性: mruby バージョンアップで compiler2/mrbc2 と VM をバイトコード版で揃える必要 (→G3)。
prism アロケータ関連が新 compiler2 にそのまま当たるか要確認。

- [!] E1. include/prism_xallocator.h — 意図: prism アロケータ (85行差)。適用可否要確認。
- [!] E2. lib/prism_alloc.c — **新規ファイル** (untracked)。意図: prism アロケータ実装。
- [~] E3. mrbgem.rake — prism_alloc のビルド組込み等 (13行追加)。
- [~] E4. src/compile.c — 4行追加。意図要確認 (アロケータ hook 想定)。

## F. 対象外

- F1. lib/patch/esp_littlefs/CMakeLists.txt — components/esp_littlefs 向け。picoruby 無関係でマージ対象外。

## G. 追従作業

- [ ] G1. 各入れ子 submodule pin を新 picoruby HEAD が指す commit に更新 (mruby / mruby-compiler2 / prism / estalloc)。
- [ ] G2. gembox (A13) と build_config (A14) を upstream の gem 構成変化に追従。
- [ ] G3. mrbc2 と VM のバイトコード版整合を確認。
