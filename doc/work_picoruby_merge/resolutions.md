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
- **D4 task.c** [TODO・要 diff・一部 upstream 化の可能性] — 新 mruby-task/src/task.c は
  task context 初期化で stack を nil クリアする処理を持つ (l.259-322)。我々の「mrb_task_reset_context の
  stack clear 追加」が upstream 化していないか精査。HAL auto-load 無効化は mrbgem.rake 側。
- **D5 mruby-dir/mrbgem.rake** [TODO] — 新 mruby-dir は ports/{posix,win} を持ち mrbgem.rake で port 自動選択。
  ESP32 (posix/win でない) で auto-detect が破綻しないか確認し、我々の「ESP32 で HAL 自動検出スキップ」を再適用。
- **D1 src/vm.c** [TODO・最高リスク・再導出] — 976/480 の VM 全面書換だが MRB_USE_TASK_SCHEDULER は存続。
  我々の mrb_task_yield_ok() / RETURN_IF_TASK_STOPPED (cci>0 の C 再入中は async task-switch を延期、
  mrb->jmp=prev_jmp 復元) を新 vm.c の task-switch 実装に合わせて再導出。**実機確認必須**。
- **D2 picoruby-mruby/src/alloc.c** [TODO・高リスク] — estalloc マルチ VM。新 estalloc pin 971b793。
- **D3 mruby-io/file_constants.rb** [clean] — 現 pin と diff 無し。新 pin で再確認。
- **picoruby-mruby/mrbgem.rake** (L0) [TODO] — hal-posix-task 依存廃止に伴い upstream 構造採用+mruby-io 除去
  (batch 3 参照)。D4/D7 と同時に確定。

## B1 machine
- 未着手。vm.c/task.c/HAL 再導出と密接。Option A の prism-lock 除去も含む。
- **注**: prism アロケータ Option A 決定済 → B1 で fmrb_prism_lock/unlock と g_prism_memory_pool 定義を除去。
