# 解決ログ (per-file merge resolution)

各パッチ対象ファイルの 3-way マージ結果と解決判断を記録。復帰・レビュー用。

マージ方式: `git merge-file`(git 自身の 3-way)を per-file 実行。
ours = 我々のパッチ(working tree == lib/) / base = old pin c14aa4400 / theirs = upstream master c932f70b0。
staging: `doc/work_picoruby_merge/merge/<layer>/<path>`。解決結果は lib/ へ書き戻す。

状態: DONE=解決し lib/ 反映済 / TODO=未 / DROP候補=upstream が取り込み我々のパッチ不要。

## L0 picoruby

### 自動マージ不要 (clean: upstream 未変更)
- i2c/include/i2c.h, i2c/src/mruby/i2c.c, picoruby-mruby/src/file_ext.c, picoruby-env/ports/posix/env.c
  → upstream 未変更のため現行 lib/ パッチをそのまま維持。**要 API 確認のみ** (依存 API 変化の影響)。状態: 未検証。

### 自動マージ CLEAN (検証済・lib/ 反映済)
- **require/mrbgem.rake** [DONE] — 我々の `picoruby-fmrb-filesystem` 依存追加が upstream 版へクリーンに載る。
- **socket/src/mruby/socket.c** [DONE] — 我々の `!defined(ESP32_PLATFORM)` ガードが残存。
- **socket/ports/esp32/tcp_socket.c** [DONE] — timeout / connect-timeout / freeaddrinfo 追加が全てクリーンに残存。
- **sandbox/src/mruby/sandbox.c** [DONE / **DROP候補**] — **merged == upstream**。
  upstream が同じ未初期化 name バグを独自修正済み → 我々の C5 パッチは不要化。
  今回は merged(=upstream)を lib/ に反映。**VM 隣接のため実機 verify 後にパッチ削除を判断** (要 Rakefile setup 修正)。

### CONFLICT 解決済 (lib/ 反映済)
- **yaml/mrbgem.rake** [DONE] — 我々の `picoruby-fmrb-io` 置換を採用 (ours)。
  upstream の新 IO 分岐 (picoruby-posix-io / mruby-io / **picoruby-littlefs / picoruby-vfs**) は
  「新規 gem 不採用」方針とも合致するため破棄。結果は現行 lib/ と同一 (書換不要)。
- **net-websocket/mrbgem.rake** [DONE] — upstream の新述語 `build.picoruby?` / `build.femtoruby?`
  (旧 `vm_mruby?`/`vm_mrubyc?` の別名) を採用しつつ、我々の正しい gemdir パス `mruby-pack` を維持
  (upstream は依然 `picoruby-pack` を指すバグ、pr#6)。`mruby-pack` は新 mruby tree に存在確認済。

### socket TLS/TCP 群 (pr#1 系統は upstream が解決済みと判明)

**重要発見**: upstream は ports API を `picorb_state *vm` 貫通型にリファクタし、pr#1
(picorb_alloc(NULL) クラッシュ) を根本解決した。旧 `TCPServer_create(port,backlog)` が
内部で `picorb_alloc(NULL,...)` していたのを、新 `TCPServer_create(mrb,...)` が有効な mrb を渡す。
mruby build で `picorb_alloc(mrb,size)=mrb_malloc(mrb,size)` となり安全。
→ **fmrb_sys_malloc 置換パッチ (pr#1) は不要化**。

- **socket/ports/esp32/tcp_server.c** [DONE / DROP候補] — 全 7 conflict が alloc パターン。upstream 全採用
  (pr#1 修正済)。lib/ を upstream 版に更新済。将来パッチ削除候補。
- **socket/ports/esp32/ssl_socket.c** [TODO・要集中対応・esp32 専用] — 7 conflict。判定:
  - conflict 1,2,3,4,6 (alloc) → **upstream 採用** (pr#1 修正済、vm は関数引数で有効)。
  - conflict 5 (close_notify コメント) → **pr#7 は upstream が recv で `MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY`
    → return 0 実装済** (337-339,352-354)。upstream 採用。ただし我々の recv 構造 (net_ctx ベース) と
    upstream recv の統合を要確認 (close_notify ロジックを失わないこと)。
  - conflict 7 (SSLSocket_ready) → **ours 維持**。upstream は `base_socket` を宣言のみで**未割当**
    (assignment 無し) のため upstream 版 `Socket_ready(vm, base_socket)` は依然常に false (pr#10 未修正)。
    我々の `select(net_ctx.fd)` 版が必要。
  - **fmrb 固有で必須保持**: esp_crt_bundle_attach (33,123-124), FMRB_SOCKET_IO_TIMEOUT_MS (41-42)。
    merged では非 conflict 領域に残存確認済。
  - この file は our版と upstream版が大きく分岐。esp32 実ビルドで検証しながらの慎重な統合が必要なため
    集中セッションへ保留。alloc/close_notify=upstream, ready=ours, crt_bundle/timeout=保持 が方針。
- **socket/ports/posix/ssl_socket.c** [TODO・要集中対応] — 同様に timeout/EINTR (pr#14) の fmrb 固有 +
  upstream 大改造 (144/73)。posix なので Linux ビルドに影響。esp32 版と同じ方針で慎重統合。
- **socket/ports/posix/tcp_socket.c** [TODO] — 2 conflict。timeout/EINTR。Linux ビルド影響。
- **socket/src/mruby/ssl_socket.c** [TODO] — GC リーク修正 (pr#2)。upstream が同修正を入れたか要確認。
- **socket/mrbgem.rake** [TODO] — 2 conflict。ESP-IDF 判定 (pr#13、mbedtls と同じ platform 判断)。

### batch 3

- **json/mrblib/json.rb** [DONE / DROP候補] — **upstream が pr#5 を独自修正済**。upstream の parse_float は
  '.' で `decimal_divider = 10.0` にし、以降 `result += (byte-48)/divider` で正しく小数を処理
  ("26.2"→26.2 検算 OK)。我々の in_fraction パッチは不要化。lib/ を upstream 版に更新。
- **picoruby-mruby/mrbgem.rake** [TODO・mruby層と同時解決] — **重要**: 我々の posix 依存パッチは
  `hal-posix-task` gem に依存するが、**upstream は hal-posix-task を廃し mruby-task を単独 add_dependency に
  再構成**(D6/D7 の GONE と同根の HAL 再構成)。方向性: **upstream 構造を採用しつつ mruby-io 依存だけ除去**
  (picoruby-fmrb-io を使うため)。確定は L1 mruby の HAL/task 再構成理解 (D4/D7) と同時に行う。
  - ESTALLOC_DEBUG 常時有効化: merge が upstream の `if PICORB_DEBUG` ブロックと我々の `unless ESTALLOC_DEBUG`
    ブロックを両方残した (機能は正=常時有効だが冗長)。確定時に我々の unless 版へ一本化してクリーンにする。
- **net-http/mrblib/http_client.rb** [TODO・要集中対応・Linux 影響] — upstream の read 実装は我々と異なる:
  content-length は単一 read (我々は複数 read ループ)、chunked は "simplified" で dechunk ステップが見当たらない。
  → 我々の堅牢化 (複数 read=pr, dechunk=pr#4, get_response 戻り値=pr#3, URI=pr#9) は upstream に無い可能性大。
  upstream 構造の上に我々の修正を再適用する慎重統合が必要。集中セッションへ保留。
- **mbedtls/mrbgem.rake, socket/mrbgem.rake** [TODO・ビルド検証時に判断] — upstream が `build.name`判定→
  `build.platform?(:esp32)`。**我々の esp32 firmware ビルドは CMake で PICORB_PLATFORM_POSIX を定義し
  PICORB_PLATFORM_ESP32 は使わない** (CMakeLists.txt:193)。よって upstream 版そのままだと esp32 判定が
  効かない。ただし esp32 の mbedtls は rake でなく ESP-IDF/CMake がビルドするため影響範囲が複雑。
  → 現状 ours 維持。ビルド検証フェーズで実際のエラーを見て確定。pr#13。
- **socket/src/mruby/ssl_socket.c** [TODO] — GC リーク修正 (pr#2)。upstream が同修正を入れたか要確認。
- **socket/ports/posix/tcp_socket.c, posix/ssl_socket.c** [TODO・Linux 影響] — timeout/EINTR (pr#14)。
  ssl_socket 群と同じ慎重統合方針。
- **picoruby-mruby/src/alloc.c** [TODO・高リスク D2] — estalloc マルチ VM。2 conflict。alloc.c/vm.c/estalloc
  と合わせて最高リスク再導出フェーズで対応。

## L1 compiler (mruby-compiler2 -> mruby-compiler, 同一 repo・path 改名, 新 pin 10408c3)

3-way 結果: compile.c CLEAN / prism_alloc.c NEW-FILE / mrbgem.rake CONFLICT(2) / prism_xallocator.h CONFLICT(1)。

- **src/compile.c** [DONE] — 我々の NULL ガード (`mrc_load_string_cxt` で parse root==NULL → return NULL、
  NULL deref 防止) が upstream の 99/22 書換にクリーンに載る。配置も正 (parse 直後・load_exec 前)。lib/ 反映済。
- **prism アロケータ trio (prism_xallocator.h / prism_alloc.c / mrbgem.rake の prism_alloc 統合)** [BLOCKED・設計判断待ち]
  - **upstream の新方式**: mrbgem.rake が自前で `PRISM_XALLOCATOR` + `PRISM_DEPTH_MAXIMUM=256` (スタック保護) を定義。
    新 prism_xallocator.h は MRC_TARGET_MRUBY 非 LIBC 時 `extern mrb_state *global_mrb; xmalloc=mrb_malloc(global_mrb,..)`
    → **prism を VM の estalloc ヒープに流す**。global_mrb は upstream で picoruby.h:127 のマクロが設定 (配線可能)。
  - **我々の方式 (現行パッチ)**: prism 専用 estalloc プール (`g_prism_memory_pool` from fmrb_mempool.c、
    サイズ `FMRB_MEM_PRISM_POOL_SIZE`、`fmrb_prism_lock` で mutex 保護)。VM ヒープから隔離。
    横断インフラ: fmrb_mem/fmrb_mempool.c, fmrb_common/fmrb_mem_config.h, picoruby-machine ports(esp32/posix)の lock フック。
  - **依頼者判断 (2026-07-12): Option A = upstream 方式に統一**。専用プールを撤去し prism を VM estalloc ヒープへ流す。
  - **Option A 実行計画**:
    - [撤去] lib/patch/compiler/prism_xallocator.h (upstream 版を使う。setup の cp 行も削除)
    - [撤去] lib/patch/compiler/prism_alloc.c (新規ファイル削除。setup の cp 行も削除)
    - [撤去] lib/patch/compiler/mruby-compiler2-mrbgem.rake (upstream mrbgem.rake を使う。setup の cp 行も削除)
      - mrbgem.rake の conflict 1 (我々の include path 追加) も撤去: `#{dir}/include` は upstream 既存で重複、
        `fmrb_common/include` は prism_alloc.c 撤去で不要。
    - [維持] lib/patch/compiler/mruby-compiler2-compile.c (NULL ガードのみ。upstream 新 base に適用済)。
    - **波及削除 (Option A cleanup)**:
      - components/fmrb_mem/fmrb_mempool.c: prism 専用プール (g_prism_memory_pool) 部分を除去 (他プールは残す)。
      - components/fmrb_common/include/fmrb_mem_config.h: FMRB_MEM_PRISM_POOL_SIZE 除去。
      - lib/replace/picoruby-machine ports (esp32/machine.c, posix/hal.c): fmrb_prism_lock/unlock と
        g_prism_memory_pool 定義を除去 → **B1 machine 再導出に含める**。
      - **global_mrb 配線確認**: upstream は picoruby.h:127 のマクロで global_mrb=vm を設定。我々の compile 経路
        (sandbox/compile 呼び出し) でこのマクロが通り global_mrb が設定されるか、ビルド/実機で確認。
    - 実際の rm + Rakefile setup 行削除 + path 改名 (mruby-compiler2→mruby-compiler) は Rakefile/pin 切替
      フェーズでまとめて実施 (整合を保つため。それまでビルド不可なので問題なし)。
- **prism submodule pin 据置 (c0e37816)** なので prism 本体 API 変化は無し。

## L1 mruby (7a4622678 hasumikin -> f56d44e 本家 mruby/mruby)

### 構造変化: HAL が独立 gem 廃止 → 各 gem の ports/ に統合

- 旧: hal-posix-task, hal-posix-dir, hal-posix-io, hal-posix-socket, hal-win-* が独立 gem。
- 新: **mruby-task/ports/{posix,glib,win}/task_hal.c**, **mruby-dir/ports/{posix,win}/dir_hal.c** に統合。
  mruby-task は task_queue.c/gc.c/queue.rb/examples/tests を持つ成熟版。

### 我々のパッチ移設マップ (再導出タスク)

- **D7 task_hal.c** [TODO・再導出・tick 高リスク] — 旧 hal-posix-task/src/task_hal.c → 新
  **mruby-task/ports/posix/task_hal.c**。新版も **SIGALRM/setitimer を使用** (strip されていない) ため、
  我々の「SIGALRM 撤去・FreeRTOS tick 一元化」の意図は依然必要。新 ports/posix/task_hal.c に対し再導出。
  fmrb は esp32/posix-linux 双方で FreeRTOS tick (picoruby-machine esp32_linux/hal_freertos.c=B1) を使う。
- **D6 dir_hal.c** [TODO・再導出] — 旧 hal-posix-dir/src/dir_hal.c → 新 **mruby-dir/ports/posix/dir_hal.c**。
  "flash/" prefix (fmrb_hal_file_posix.c の仮想 namespace 整合) を新版へ再適用。
- **D1 src/vm.c** [**不要化・撤去 (upstream 採用)**] — 詳細設計 rederive_vm_task.md。
  **新 upstream vm.c が我々の tick 修正の厳密な上位互換を実装済**
  (`task_across_c_boundary` + `mrb->jmp=prev_jmp` 復元 + `!exc`/`!gc.iterating`/`c!=root_c` まで拡張、
  issues #6862/#6863/#6864/#6868/#6887)。我々の ESP32 検証済み修正が本家へ取込まれた形。
  → lib/patch の vm.c を削除、Rakefile setup の vm.c 行も削除 (pin 切替フェーズで)。**最高リスクが1件消滅**。
- **D4 task.c** [**DONE (bottom-half)・要実機確認**] — 詳細設計 rederive_vm_task.md。
  - stack nil クリア → upstream 化済につき**撤去** (新 upstream task.c をそのまま土台に採用)。
  - **案D bottom-half を実装済**: 新 `task_run_body` の `while(1)` ループ先頭 (`t = q_ready_` 直前) に
    `{ uint32_t pending = mrb_hal_task_take_pending_ticks(mrb); while (pending--) mrb_tick(mrb); }` を挿入。
    `extern uint32_t mrb_hal_task_take_pending_ticks(mrb_state*)` を task.c 冒頭に宣言。
    mrb_tick は排他を取らない (IRQ 側) ので排他外呼び出しで正。run_once には入れない (fmrb 未使用)。
    top-half は D7 (FreeRTOS port)。**実機確認: preempt/sleep/wake、tick 破壊非再発**。
  - **mrbgem.rake は upstream 採用 (パッチ撤去)**: 新版は effective_ports/conf.ports 方式。旧 HAL-auto-load
    削除パッチは陳腐化。**port 選択は build_config で行う (D7)**。task.c/task_hal は -rf コピーで配布。
- **D7 task_hal.c** [再導出] — 新 location mruby-task/ports/posix/task_hal.c。FreeRTOS top-half
  (switching+pending 蓄積、mrb_tick 呼ばない) を実装。B1 の hal_freertos.c と統合。rederive_vm_task.md 参照。
- **D5 mruby-dir/mrbgem.rake** [**不要化・撤去**] — 新 upstream mruby-dir/mrbgem.rake は 3 行 (metadata のみ) で
  **HAL 自動検出ロジックを丸ごと削除済**。我々の「ESP32 で自動検出スキップ」パッチは対象消滅。
  lib/patch/mruby-dir/mrbgem.rake + Rakefile 該当行を削除。
- **D6 dir_hal.c ("flash/" prefix)** [DONE・要確認] — hal-posix-dir 消滅 → 新 location
  **mruby-dir/ports/posix/dir_hal.c** に再導出。新版は path 関数が増えている (open/mkdir/rmdir/chdir/chroot/
  is_directory) ため resolve_path("flash" prefix) を**全 path 関数に適用**、getcwd は逆変換 (prefix 除去) を追加。
  lib/patch 内の旧 hal-posix-dir/ を削除し新 path へ移設。`cp -rf lib/patch/picoruby-mruby` で配布 (Rakefile 個別行不要)。
  要確認: getcwd 逆変換・chroot の "flash" 化が fmrb の Dir 利用と整合するか (実機/Linux)。
- **D2 picoruby-mruby/src/alloc.c** [DONE・要実機確認] — estalloc マルチ VM。新 estalloc pin 971b793。
  新 upstream alloc.c (TLSF/O1HEAP/TINYALLOC/ESTALLOC/DEFAULT の複数分岐) を土台に、**ESTALLOC 分岐のみ**を
  per-VM 化: module-static `est` → `fmrb_get_current_est()`/`fmrb_set_current_est()` (per-task TLS)。
  **upstream 新規の `mrb_alloc_set_critical_section` / `est_set_critical_section` を保持**しつつ per-VM est に適合。
  `mrb_get_estalloc_stats` ヘルパも追加。ESTALLOC_DEBUG guard は upstream 版採用 (mrbgem.rake で常時 on 予定)。
  実機確認: 複数 VM 並走時に各 est が正しく分離されるか、critical_section が正しい est に効くか。
- **D3 mruby-io/file_constants.rb** [clean] — 現 pin と diff 無し。新 pin で再確認。
- **picoruby-mruby/mrbgem.rake** (L0) [DONE] — 新 upstream 版を土台に: (1) posix の `add_dependency 'mruby-io'`
  を除去 (fmrb-io を使う、mruby-io 競合)、(2) mruby-task を単独 add_dependency (hal-*-task gem は upstream で廃止、
  HAL は conf.ports :freertos が供給)、(3) ESTALLOC_DEBUG を常時 on 化 (est_take_statistics 用、冗長解消)。

**呼び出し元付け替え [DONE]**:
- main/app/fmrb_app.c:360 `hal_register_vm(ctx->mrb)` → `mrb_hal_task_register_vm(ctx->mrb)`。
- lib/add/picoruby-fmrb-app/ports/esp32/app.c:673 `hal_deinit(mrb)` → `mrb_hal_task_final(mrb)`。

## D7 + B1 (FreeRTOS tick top-half / picoruby-machine) — 指示書 instruct_d7_b1_tick.md に従う

### port 選択機構 (確定)

- `conf.ports(:a, :b)` → build ごとの port 優先リスト。gem は `ports/<name>/` のうち**リスト先頭一致の 1 つ**を
  コンパイル (mruby core lib/mruby/gem.rb:65-77)。
- **重要: `effective_ports` は CrossBuild で `conf.ports` 未指定なら `[]` (port 一切非コンパイル)**
  (lib/mruby/build.rb:199-209。host build のみ posix/win にフォールバック)。fmrb は CrossBuild なので
  **conf.ports 明示必須**。未指定だと mruby-task の HAL が付かずリンクエラー、あるいは (旧構成の名残で)
  posix SIGALRM port を拾って案D と二重 tick になる罠。
- **port 選択マトリクス** (instruct 指示):
  - linux: `conf.ports :esp32_linux, :freertos, :posix` → mruby-task=freertos(新設), machine=esp32_linux, socket=posix, mruby-dir=posix(D6 flash)
  - esp32: `conf.ports :esp32, :freertos, :posix` → mruby-task=freertos, machine=esp32, socket=esp32
- **build_config 修正**: 消滅した `hal-posix-task`/`hal-posix-dir` の `conf.gem gemdir:` 行を削除
  (family_mruby_linux.rb)、`conf.ports` を追加。
- **esp32 の CMake 二重コンパイル要確認**: esp32 は gem C ソースを CMake (PICORUBY_SRCS) でもビルド。
  task_hal.c が rake port と CMake の二重コンパイル / どちらも拾わない、を排除する (Linux 検証後に確認)。

### 【build 反復で判明・要決定】FreeRTOS port のコンパイル経路 (指示書§1 の宿題)

**症状**: `conf.ports :esp32_linux` により rake mruby build が machine/ports/esp32_linux/hal_freertos.c を
コンパイルしようとし `freertos/FreeRTOS.h: No such file` で失敗。

**構造的原因** (完全診断):
- 新 picoruby は CrossBuild で `effective_ports=[]` (何もコンパイルしない) がデフォルト → conf.ports 明示必須。
  旧 picoruby は POSIX ビルドで全 gem の ports/posix を自動コンパイルしていた (CMakeLists L81-84 コメント)。
- **FreeRTOS ヘッダは CMake (picoruby-esp32 component, PRIV_REQUIRES freertos) にのみ有り、rake build には無い**。
  よって FreeRTOS 依存 port (machine hal_freertos.c, mruby-task freertos task_hal.c) は CMake でしかコンパイル不可。
- しかし socket/mruby-dir の ports/posix は rake でコンパイルする必要があり `conf.ports :posix` が要る。
  その conf.ports :posix が **mruby-task に posix(SIGALRM) port を拾わせ** (二重 tick) + machine posix port を
  rake compile して CMake PICORUBY_SRCS の posix/machine.c と重複させる。
- **`resolve_external_hal!`** (gem.rb:474): gem `hal-<short>-*` (short=対象 gem 名末尾) が読み込まれると対象の
  port_objs を drop。mruby-task の short="task" → **`hal-task-*` gem** で mruby-task の port compile を止められる。

**推奨案 (holistic、Linux)**:
1. `conf.ports :posix` に変更 (socket/mruby-dir/machine が posix port を rake compile。posix ヘッダのみで OK)。
2. **`hal-task-freertos` ダミー gem を新設** (lib/add、C ソース無し。名前だけで resolve_external_hal! を発火させ
   mruby-task の posix SIGALRM port compile を抑止)。
3. **freertos task_hal.c を CMake PICORUBY_SRCS に追加** (FreeRTOS ヘッダ有り)。mruby-task の HAL はこれが供給。
4. **hal_freertos.c を PICORUBY_SRCS から除去 + ファイル削除** (tick は freertos task_hal.c へ移動済で重複)。
5. **machine posix/machine.c を PICORUBY_SRCS(L77) から除去** (conf.ports :posix で rake compile されるため重複回避)。
   common/machine.c(L79) は port でないので CMake のまま (rake は posix のみ compile、重複しない)。
- esp32 ビルドも同型 (machine=esp32 port は CMake、conf.ports で rake は posix。task_hal freertos は PICORUBY_SRCS)。
  Linux green 後に確定。
- **要決定**: この holistic 再配置で進めてよいか (architectural、指示書が記録指定した判断)。

### D7 ports/freertos/task_hal.c (top-half, 案b) [DONE (初版)・build 反復で調整]

- 作成: lib/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-task/ports/freertos/task_hal.c。
  既存の案D 実装 (lib/replace/picoruby-machine/ports/esp32/machine.c の g_tick_manager +
  mruby_tick_task + take_pending_ticks) を**抽出・共通化**し、ESP_LOG(IDF 固有)除去、FreeRTOS-only 化。
- **原子性**: pending の read+0クリアと timer 側 increment は**両方 g_tick_manager.mutex で保護** (既存資産が
  既に満たす。レビュー要件)。**この mutex は削除しないこと**とコメントに明記。
- **idle**: `ulTaskNotifyTake(timeout)`。top-half が tick 毎に vm_task へ `xTaskNotifyGive` → idle 起床レイテンシ≒0。
- **registry per-VM**: `vms[MRB_TASK_MAX_VMS]`, 各 entry に vm_task handle 保持 (notification 先)。
  `mrb_hal_task_init` が tick タスク生成 (idempotent) + 呼び出し VM を登録。register は idempotent。
- **include 衝突回避**: mruby-task の task.h は include せず、`mrb->task.switching` を mruby.h 経由で直接使用。
  FreeRTOS は `<freertos/...>` prefix (fmrb 規約、同名衝突回避)。
- **build 反復で確認する点**: (a) mruby-task gem の port ビルドに FreeRTOS include path が通るか、
  (b) 旧 `hal_register_vm`/`hal_deinit_by_pool` (fmrb_app.c から呼ばれる) と本 port の register の統合、
  (c) MRB_TASK_MAX_VMS(=8) と FMRB_MRB_MAX_VMS の整合。

### B1 着手前の棚卸し (旧 tick / prism API 呼び出し元、CMake、削除前に記録)

grep 実施 (main/ lib/add components/fmrb_mem components/fmrb_common)。B1 で撤去する前に付け替え先を確定。

**旧 tick / VM registry API の呼び出し元** → D7 (task_hal.c) の新契約へ付け替え:
- `main/app/fmrb_app.c:360` `hal_register_vm(ctx->mrb)` → **`mrb_hal_task_register_vm(ctx->mrb)`** に改名。
  (D7 の init が呼び出し VM を登録するが、fmrb_app は VM 生成直後の明示登録を続ける。register は idempotent。)
- `lib/add/picoruby-fmrb-app/ports/esp32/app.c:673` `hal_deinit(mrb)` → **`mrb_hal_task_final(mrb)`** に付け替え。
- `mrb_hal_task_take_pending_ticks` は D7 が提供 (task.c bottom-half が使用)。machine 側の同名定義は撤去。
- `machine_hal_init` / `mruby_tick_task` / `g_tick_manager` は machine から撤去 (freertos port へ移動済)。

**prism-lock / prism pool (Option A 除去)**:
- `components/fmrb_mem/fmrb_mempool.c`: `g_prism_memory_pool` 定義 (L9) と est 初期化 (L90-92)、
  release 判定 (L112-117) を除去。他プールは残す。
- `components/fmrb_common/include/fmrb_mem_config.h`: `FMRB_MEM_PRISM_POOL_SIZE` (L21,25) を除去。
- `fmrb_prism_lock/unlock` は machine ports (esp32/machine.c, posix/hal.c) 内定義で、唯一の呼び出し元
  だった prism_alloc.c は削除済 → machine から定義ごと撤去。

**CMake PICORUBY_SRCS** (components/picoruby-esp32/CMakeLists.txt):
- Linux (L67-79): `ports/posix/machine.c` (L77), **`ports/esp32_linux/hal_freertos.c` (L78)**, `ports/common/machine.c` (L79)。
- ESP32 (L96-108): **`ports/esp32/machine.c` (L106)**, `ports/common/machine.c` (L107), `src/machine.c` (L108)。
- tick manager は hal_freertos.c (Linux) と esp32/machine.c (ESP32) に在る → B1 で撤去。
  freertos/task_hal.c は **rake の port 機構**でコンパイル (conf.ports :freertos)。CMake との二重コンパイルは
  起きない (task_hal.c は PICORUBY_SRCS に無い)。撤去後、hal_freertos.c / machine.c は tick 以外の責務で存続。

### B1 picoruby-machine [進行中・merge-file 方式]

per-file 3-way (git merge-file) 実施。base=旧 pin, ours=lib/replace, theirs=新 HEAD c932f70b。staging=merge/machine/。
triage: CLEAN 16 / CONFLICT 5 / OURS-NEW 1 (esp32_linux/hal_freertos.c)。

**完了**:
- CLEAN 16ファイルを lib/replace へ書き戻し済 (posix/machine.c 179/11 も含め fmrb 内容保持を確認)。
- **ports/posix/hal.c** [DONE] — ours 解決 (upstream の SIGALRM tick を破棄。案D では tick は freertos port。
  かつ Linux は machine=esp32_linux port を使うため posix/hal.c は未コンパイル)。

**残 CONFLICT (要慎重解決)**:
- **include/hal.h** [TODO・複雑] — upstream が `picorb_hal_*` マクロ alias 体系に refactor
  (picorb_hal_init→mrb_hal_task_init 等)。解決方針: upstream の alias 体系を採用しつつ、fmrb 宣言を保持/改名:
  - `hal_register_vm` → **`mrb_hal_task_register_vm`** 宣言 (定義は freertos port)。`hal_deinit`→`mrb_hal_task_final`。
  - **`mrb_task_request_switch` 宣言は撤去** (port が mrb->task.switching を直接立てる)。
  - `mrb_hal_task_take_pending_ticks` の machine 側宣言は撤去 (task.c が extern 宣言、freertos port が定義)。
  - `hal_deinit_by_pool` は fmrb_app が使うなら保持 (要確認)。
- **mrbgem.rake** [TODO] — upstream 追加: picoruby-require dep, picoruby-io-console dep, posix で -pthread
  (stdin reader thread), posix で mruby-io。我々: io-console 不使用 (fmrb-io), mruby-task/mruby include path 追加。
  解決: upstream の -pthread + require を採用、io-console は我々どおり不使用、include path は両立。
- **ports/esp32/machine.c** [TODO・**tick manager 撤去**] — 5 conflict + 我々の tick manager (g_tick_manager,
  mruby_tick_task, take_pending_ticks, machine_hal_init, hal_register_vm, hal_deinit, hal_deinit_by_pool) を撤去
  (freertos port へ移動済)。撤去後 machine は sleep/console/HAL init 等に限定。**fmrb_prism_lock/unlock も除去**。
- **src/mruby/machine.c** [TODO・#if 対応] — IO override 無効化 (memory: project_picoruby_machine_io_override)。
  conflict は `#if PICORB_VM_MRUBY` 内で ours=(void)suppression / upstream=IO methods 定義 + `#else` posix 分岐
  (_stdin_gets/getc)。**fmrb は posix+mruby VM なので #else 分岐を失わないよう #if 構造を保って解決**すること
  (盲目的 ours 不可)。要 490-525 精読。

**呼び出し元付け替え (B1 と同時)**:
- main/app/fmrb_app.c:360 hal_register_vm → mrb_hal_task_register_vm。
- lib/add/picoruby-fmrb-app/ports/esp32/app.c:673 hal_deinit → mrb_hal_task_final。

**Option A prism-lock / prism pool 除去 (B1 と同時)**:
- machine ports (esp32/machine.c, posix/hal.c) の fmrb_prism_lock/unlock 定義除去。
- components/fmrb_mem/fmrb_mempool.c の g_prism_memory_pool + est 初期化除去、fmrb_mem_config.h の
  FMRB_MEM_PRISM_POOL_SIZE 除去。

**hal_freertos.c (esp32_linux, OURS-NEW)**: tick manager をここからも撤去 (freertos port へ一本化)。tick 以外の責務は存続。

### global_mrb (compiler Option A) [TODO・mutex 方式 (依頼者決定)]

- 各コンパイル呼び出し点 (sandbox 経由が主) で **mutex → global_mrb 設定 → compile → 復元 → unlock**。
  性能考慮不要。設定箇所と mutex の実体を本節に追記すること。

### 検証 (Linux ビルド、instruct §検証項目)

preempt / sleep 精度 / ブロッキング後まとめ適用 / 長時間走行で tick 破壊非再発 /
**二重 tick 源不在 (ports/posix/task_hal.c=SIGALRM がリンクに無いこと)**。
pending 原子性は Linux(単一スレッド sim)では検証不可 → **コードレビューで critical section 確認**。
実機デュアルコア長時間走行は依頼者確認項目 (tasklist に残す)。
