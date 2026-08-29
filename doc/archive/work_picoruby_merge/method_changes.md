# 方式変更の整理 (旧実装から方法を変えた箇所)

picoruby c932f70b マージで、fmrb 側の「実現方法そのもの」を変えた箇所の一覧。
upstream が独自解決していたためパッチを単純撤去した項目 (alloc NULL, sandbox,
json, close_notify, vm.c tick 安全化) はここには含めない。

## 1. tick スケジューリングの実装場所と界面 (案D の再配置)

- 旧: picoruby-machine が独自 tick マネージャを保有 (esp32: ports/esp32/machine.c、
  linux: esp32_linux/hal_freertos.c)。mruby-task へは task.c 直接パッチ +
  hal-posix-task gem 差し替えで接続。登録 API は hal_register_vm / hal_deinit /
  hal_deinit_by_pool。
- 新: upstream が task_hal.h という HAL 契約 + ports/<name>/ 方式に再編したため、
  tick 一式 (top-half timer タスク、per-VM pending カウンタ、registry) を
  **mruby-task/ports/freertos/task_hal.c に一本化** (lib/patch、両ターゲット共通、
  純 FreeRTOS API)。machine からは tick 責務を全撤去。
- 付随変更:
  - 登録 API 改名: hal_register_vm → mrb_hal_task_register_vm、
    hal_deinit → mrb_hal_task_final。hal_deinit_by_pool は呼び出し元ごと廃止。
  - bottom-half は task.c の task_run_body ループ先頭に数行のフックとして再導出
    (旧: mrb_task_run 全体への大きめの patch)。
  - timeslice 間引きを top-half に内蔵 (旧: 毎 tick switching を立てる)。
  - idle が vTaskDelay(1) ポーリングから task notification 待ちに (起床精度向上)。

## 2. VM ごとのヒープ (estalloc) の紐付け方式

- 旧: mruby の allocf が ud 引数を持ち、`mrb_open_allocf(allocf, est)` で
  **per-VM の est を ud として渡せた**。
- 新: mruby 4.0 のアロケータフックは `mrb_basic_alloc_func(void*, size_t)` という
  グローバル関数で mrb も ud も渡らない。upstream の module-static est では
  全 VM が最後に open した VM のヒープを共有してしまうため、
  **FreeRTOS タスクローカル (fmrb_get/set_current_est) で per-VM est を引く方式に変更**。
- 新しい制約: VM のアロケーションはその VM の FreeRTOS タスク上でのみ行える
  (他タスクから mruby API で割当を伴う操作をしてはならない)。

## 3. prism (コンパイラ) のメモリ管理 — Option A

- 旧: prism 用の独自 xallocator (prism_xallocator.h / prism_alloc.c) +
  fmrb_mempool の専用 PRISM プール + fmrb_prism_lock (machine ports) で、
  コンパイルメモリを VM から分離して管理。
- 新: **独自アロケータ層を撤去し upstream 方式に統一** (Option A)。コンパイルは
  global_mrb 経由で VM のアロケータを使う。並行コンパイルは mutex 排他で対応
  (依頼者決定: 頻度が低いため排他で十分)。
- 留意: fmrb_mem 側の PRISM プール定義の残置有無は cleanup 要確認
  (boot ログにはまだ PRISM プールが表示されている)。

## 4. gem port のビルド供給方式 (rake と CMake の分担)

- 旧: gem の ports/ はビルドシステムが自動コンパイルせず、必要なものだけ
  CMake (PICORUBY_SRCS) に明示列挙。
- 新: upstream build が `conf.ports :name` で各 gem の ports/<name>/ を
  **rake 側で自動コンパイル**する方式になったため、ターゲット別に分担を確定:
  - **linux**: `conf.ports :posix` (socket / mruby-dir / machine console が rake)。
    mruby-task の posix port (SIGALRM) は hal-task-freertos ダミー gem
    (resolve_external_hal!) + 空スタブ patch で抑止し、実体は CMake の
    freertos task_hal.c。
  - **esp32/p4**: conf.ports **無し** (rake は port を一切コンパイルしない)。
    必要 port は全て IDF ヘッダ依存のため CMake (PICORUBY_SRCS) が担当。
    conf.ports を置くとヘッダ不足か CMake との二重定義になる (実測)。

## 5. ABI define の管理方法

- 旧: MRB_* define が build_config / mrbgem.rake / CMakeLists に分散し、
  暗黙に整合 (旧 mruby では mrb_state 内に mrb_value 配列が無く、boxing 差が
  レイアウトに効かなかったため事故にならなかった)。
- 新: mruby 4.0 は const_cache (mrb_value 配列) を mrb_state に持ち、
  MRB_NO_BOXING 等がレイアウトを変えるため、
  - rake 側: **build_config (gembox 読込より前) を single source に**。
    gem の mrbgem.rake での `build.cc.defines <<` は自 gem のソースに効かない
    (compiler が先に clone される) ことが判明したため使わない。
  - CMake 側: **mruby_abi_defines.cmake に一元化** (picoruby-esp32 / main 共用)。
  - **boot 時ガード**: rake 側 picorb_abi_mrb_state_size() と CMake 側
    sizeof(mrb_state) を fmrb_app_init() で比較、不一致は即 abort。

## 6. console HAL の命名体系

- 旧: hal_write / hal_flush / hal_getchar / hal_read_available / hal_abort の素名。
- 新: upstream の **picorb_hal_* エイリアス体系** (hal.h が VM 種別に応じて
  mrb_hal_* / mrbc_hal_* へ展開) に合わせ、port 実装は picorb_hal_* 名で定義する
  方式へ (posix hal.c / esp32 machine.c とも)。

## 7. dir HAL の供給方法 (D6)

- 旧: hal-posix-dir gem を lib/patch で差し替え + esp32 は hal-esp32-dir。
- 新: upstream が hal-*-dir gem を廃止し mruby-dir/ports/<name>/ 方式に。
  linux は conf.ports :posix で mruby-dir/ports/posix/dir_hal.c を rake が
  コンパイル、esp32 は従来どおり lib/add/hal-esp32-dir/dir_hal.c を CMake が
  直接コンパイル。

## 8. 名称追従 (機械的だが漏れると即ビルド不能)

- mruby-compiler2 → mruby-compiler、mruby-bin-mrbc2 → mruby-bin-mrbc
  (Rakefile setup のコピー先、CMake include パス、gem 依存名、prebuild の
  コンパイラ binary 名 picorbc → mrbc)。
- build_config: conf.microruby → conf.picoruby。gembox/rake の VM 述語は
  picoruby? / femtoruby? (旧 vm_mruby? / vm_mrubyc? の別名)。

## 9. 新しい規律 (方式変更ではないが実装時の必須制約)

- **C 関数の aspec は実引数と一致必須**: mruby 4.0 は mrb_define_method の
  aspec を VM 側で強制する。aspec と mrb_get_args の不一致は該当メソッドの
  初回呼び出しで ArgumentError (backtrace は (unknown):0) として現れる。
- **Linux ターゲットは Linux シグナル使用禁止** (FreeRTOS POSIX シミュレータが
  内部使用): sigaction / setitimer / signal() を新規コードに書かない。
  詳細は instruct_d7_b1_tick.md 3.5 節。
