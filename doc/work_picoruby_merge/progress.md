# 作業ログ (時系列)

中断復帰の起点。新しい作業は末尾に追記する。日付は JST。

> **【重要・現在の状態: pin 切替済み / Rakefile・再導出 未完のためビルド不可】**
> submodule は **新 pin に切替済み** (L0 picoruby c932f70b, nested も全て新 pin。working tree の
> checkout であり fmruby-core には未 commit = pointer 未更新)。
> ただし **(1) Rakefile setup が旧パス (mruby-compiler2 等) と撤去予定の prism/vm.c コピー行を
> まだ参照** しており、**(2) D4/D7/B1/D2/D6 の再導出と socket TLS/net-http 統合が未完** のため、
> まだ `rake build:linux` は通らない。ビルド検証は Rakefile 更新 + 再導出完了後。
> 復帰時の注意: fmruby-core で `git submodule update` すると旧 pin に戻るので実行しないこと。
> submodule の pin 切替手順は progress 末尾「pin 切替」ログ参照 (再現可能)。

---

## 2026-07-12

### セットアップ / 現状把握

- fmruby-core 作業ブランチ: `merge_picoruby_20260712` (移動済み、依頼者が用意)
- 作業フォルダ `doc/work_picoruby_merge/` を作成。README / progress / tasklist を配置。
- 現状の submodule pin を確認 (README の表を参照):
  - L0 picoruby `c14aa4400`
  - L1 mruby `7a4622678`, mruby-compiler2 `29113090`, estalloc `ae50a9649`
- picoruby submodule の working tree は既に patch 適用済みの状態 (前回の `rake setup` の痕跡)。
  - `git status --short` で lib/replace, lib/patch 由来の M / D、lib/add 由来の未追跡が見える。
- Rakefile `setup` タスク (L84-201) の全コピー内容を把握し tasklist に転記。
  - 注意: `cp -rf lib/patch/picoruby-mruby` はディレクトリ丸ごとコピーで、
    入れ子 mruby submodule の中 (mruby-io/file_constants.rb, mruby-task, hal-posix-dir, vm.c 等) まで書き込む。

### diff 採取 完了

- `rake setup` 再実行済。3 階層の diff を `diffs/` に保存:
  - `L0_tracked.diff` / `L0_status.txt` (picoruby 直下, 30 files, +925/-305)
  - `L1_mruby.diff` / `L1_mruby_status.txt` (mruby submodule 内, 6 files)
  - `L1_compiler2.diff` / `L1_compiler2_status.txt` (mruby-compiler2 内, 3 files + prism_alloc.c untracked)
- **重要な発見**:
  - `cp -rf lib/patch/picoruby-mruby` は個別 cp 指定に無い `hal-posix-task/src/task_hal.c` (165行) と
    `src/file_ext.c` (fsync stub 化) も巻き込む。tasklist に D7 / C12 として追加済。
  - `picoruby-mruby/include/hal.h` と `mruby-io/file_constants.rb` は現 pin と diff 無し (patch は存在)。新 pin で乖離確認要。
  - estalloc submodule 自体は未変更 (clean)。alloc.c パッチは L0 の wrapper のみ。
- intent 出典を確定: `doc/picoruby_upstream_pr_candidates.md` が socket/net-http/json パッチ (C7-C11) の
  詳細な症状・修正理由・fmrb 固有部分を網羅。tasklist に反映済。
- tasklist.md を完全版に更新 (全パッチ列挙 + 意図)。

### upstream 調査 完了 (詳細は upstream_analysis.md)

- picoruby master HEAD = **c932f70b0** (2026-07-11), 旧 pin から **413 commits** 先行。
- 新 pin: mruby=f56d44e / estalloc=971b793 / mruby-compiler=10408c3 / mruby-bin-mrbc=8456898。
- **構造変化 (重要)**:
  - mruby remote 移行 hasumikin/mruby → **本家 mruby/mruby** (task scheduler は存続, D1 該当継続)。
  - **mruby-compiler2 → mruby-compiler**, **mruby-bin-mrbc2 → mruby-bin-mrbc** に改名 (パス変更)。
  - 新規 submodule: picoruby-funicular, picoruby-littlefs/lib/littlefs。
- fetch 実施済 (picoruby master, mruby/mruby master)。いずれも read-only、push なし。

### 現在地 / 次アクション

- 「差分の理解」フェーズ完了。次は「マージ作業」フェーズ (rebase)。
- 着手前に依頼者へ upstream 構造変化 (改名・repo 移行・scope 413commits) を報告し、
  判断ポイント (compiler 改名対応, 新規 gem 取捨) をすり合わせる。
- rebase は submodule 内ローカル操作 (branch/commit/rebase) で許可範囲内。push と
  fmruby-core 側 commit のみ要確認。

### 依頼者の方針決定 (2026-07-12)

- **新規 gem (picoruby-funicular / picoruby-littlefs): 現状維持=取り込まない**。
  gembox で有効化せず、fmrb 独自 FS (esp_littlefs, picoruby-fmrb-filesystem) を継続。マージ scope を最小化。
- **進め方: 領域ごとに rebase→検証し、区切りごとに進捗報告**。

### rebase 戦略メモ

- 3 階層それぞれ独立に branch→commit→rebase する必要がある
  (L0 のコミットは L0 ネイティブ file のみ捕捉。nested の vm.c/task.c/compiler は各 submodule 内で別途)。
  - L0 picoruby: c14aa4400 起点 → origin/master c932f70b0
  - L1 mruby: 7a4622678 起点 → f56d44e (本家 mruby/mruby)
  - L1 mruby-compiler(2): 29113090 起点 → 10408c3 (改名先)
- 領域コミット単位: compiler / vm / task / sandbox / socket / net-http / net-ws / json / i2c / env / misc-rake / picoruby-mruby-core。
- 実施順 (予定): まず L0 picoruby を領域別コミット→rebase (churn 把握・報告) → 次に nested。

### conflict triage 完了 (詳細は triage_matrix.md)

全 3 階層でパッチ対象ファイルの upstream churn を一括判定し、conflict map を作成。
- **自動/低リスク**: i2c, file_ext, env, file_constants, prism_alloc.c(新規), prism_xallocator.h。
- **中**: json, net-websocket, require, yaml, mbedtls, socket rake/socket.c, mruby-dir。
- **高**: net-http, sandbox, socket ssl_socket.c 群, compiler mrbgem.rake/compile.c, alloc.c(estalloc)。
- **最高 (再導出・実機確認必須)**: vm.c(**976/480 で VM 全面書換**), task.c(**380/189**),
  消滅した hal-posix-dir/task_hal.c(D6/D7), picoruby-machine 置換(B1)。
- compiler は同一 repo で path 改名のみ、**prism pin 据え置き** → アロケータパッチ流用性高。

fetch 実施: picoruby master, mruby/mruby master, mruby-compiler2 master (全て read-only)。

### L0 マージ着手 (merge-file 方式 / 詳細は resolutions.md)

- マージエンジン決定: `git merge-file` per-file (git 3-way)。nested submodule と picoruby-machine は
  working tree から退避 (clean 化) し L0 を分離。ours=working tree, base=old pin, theirs=origin/master。
  staging = merge/L0/。
- 新 build API 把握: `lib/picoruby/build.rb` に `picoruby?`(=`vm_mruby?` alias)/`femtoruby?`(=`vm_mrubyc?`)/
  `platform?(:esp32)`(=define PICORB_PLATFORM_ESP32)。
- L0 CHG 17ファイルを一括 3-way: **CLEAN 4 / CONFLICT 13**。
- **今回解決し lib/ 反映済 (6件)**:
  - CLEAN 検証: require(fmrb-fs 依存), socket.c(ESP32_PLATFORM ガード), esp32/tcp_socket.c(timeout/freeaddrinfo)。
  - **sandbox.c: merged==upstream** → upstream が同一バグ修正済、C5 は DROP候補 (verify 後判断)。
  - CONFLICT 解決: yaml(fmrb-io 採用), net-websocket(新述語 picoruby?/femtoruby? + 正しい mruby-pack パス)。
- **残 L0 CONFLICT 11件 (TODO)**: mbedtls(要判断), json, picoruby-mruby/mrbgem.rake, net-http,
  socket/mrbgem.rake, ssl_socket 群(src/esp32/posix), tcp_server, posix/tcp_socket, alloc.c(高リスク)。

### L0 batch 2 (socket TLS/TCP 群) — 重要発見と一部解決

- **重要発見: upstream が ports API を `picorb_state *vm` 貫通型にして pr#1 を根本解決**。
  旧 `TCPServer_create(port)`→`picorb_alloc(NULL)` クラッシュを、新 `TCPServer_create(mrb)` で解消。
  → 我々の fmrb_sys_malloc 置換 (pr#1) は不要化。
- **socket/ports/esp32/tcp_server.c** [DONE] — upstream 全採用で lib/ 反映済 (DROP候補)。
- upstream が既に取り込み済みと確認できた我々の修正: **pr#1 (alloc), pr#2 相当の sandbox, pr#7 (close_notify)**。
  → 我々のパッチ負債が減る良い方向。
- **esp32/ssl_socket.c, posix/ssl_socket.c** は our版/upstream版が大きく分岐。判定は記録済
  (alloc/close_notify=upstream, SSLSocket_ready=ours[pr#10 は upstream 未修正], crt_bundle/timeout=保持)
  だが、統合は esp32/Linux 実ビルドで検証しながらの慎重作業のため**集中セッションへ保留**。
- 詳細は resolutions.md の「socket TLS/TCP 群」節。

### 現在の lib/ 反映状況

- 反映済 (7ファイル): require, socket/src/mruby/socket.c, esp32/tcp_socket.c, sandbox.c,
  yaml(不変), net-websocket, esp32/tcp_server.c。
- 未: mbedtls(要判断), json, picoruby-mruby/mrbgem.rake, net-http, socket/mrbgem.rake,
  ssl_socket 群(src/esp32/posix), posix/tcp_socket.c, alloc.c(高リスク)。
- L1 (mruby/compiler), B1(machine) は未着手。

### L0 batch 3 — json 確定 + 構造的発見

- **json.rb [DONE]**: upstream が pr#5 を独自修正済み → upstream 全採用 (DROP候補)。lib/ 反映。
- **picoruby-mruby/mrbgem.rake [mruby層と同時解決へ]**: 我々の依存先 `hal-posix-task` gem が upstream で
  廃止 (mruby-task 単独化)。D6/D7 の HAL 消滅と同根。方向性=upstream 構造採用+mruby-io 除去。
  ESTALLOC_DEBUG は merge が upstream/our 両ブロック残置 (機能正・冗長)、確定時に一本化。
- **net-http [集中対応へ]**: upstream read 実装が我々と別物 ("simplified" chunked, 単一 read)。
  我々の堅牢化を upstream 構造へ再適用要。Linux 影響。
- **mbedtls/socket rake [ビルド検証時判断]**: 我々の esp32 は PICORB_PLATFORM_POSIX 定義 (ESP32 でない)。
  upstream の platform?(:esp32) 判定と噛み合わない。esp32 は CMake ビルドで影響複雑。現状 ours 維持。

### L0 進捗まとめ (この時点)

- **lib/ 反映済 (8ファイル)**: require, socket/src/mruby/socket.c, esp32/tcp_socket.c, sandbox.c,
  yaml, net-websocket, esp32/tcp_server.c, json.rb。
- upstream が独自修正済みと確認 (我々のパッチ DROP候補): pr#1(alloc/tcp_server), pr#2(sandbox),
  pr#5(json), pr#7(close_notify)。→ 我々の維持コスト減。
- **保留 (集中/同時解決)**: net-http, ssl_socket 群(src/esp32/posix), posix/tcp_socket, mbedtls/socket rake,
  picoruby-mruby/mrbgem.rake(mruby層と), alloc.c(最高リスク)。
- L1(mruby/compiler), B1(machine), Rakefile 改名対応 未着手。

### L1 compiler 層 — 完了 (prism は Option A 決定で簡素化)

- rename: mruby-compiler2 → mruby-compiler (同一 repo, path のみ), 新 pin 10408c3, prism pin 据置(c0e37816)。
- **compile.c [DONE]**: NULL ガード (parse root==NULL 防御) を upstream 99/22 書換に載せ lib/ 反映。
- **prism アロケータ [依頼者判断: Option A = upstream 方式に統一]**:
  - 撤去対象: prism_xallocator.h, prism_alloc.c, mruby-compiler2-mrbgem.rake (全て upstream 採用)。
  - 波及削除: fmrb_mempool.c の prism プール, fmrb_mem_config.h の FMRB_MEM_PRISM_POOL_SIZE,
    picoruby-machine ports の fmrb_prism_lock (B1 で), global_mrb 配線確認。
  - 実際の rm + Rakefile 行削除 + path 改名は Rakefile/pin 切替フェーズでまとめて実施。
  - 詳細は resolutions.md「L1 compiler」節。
- 結果: compiler 層は「再導出」不要になり、compile.c 1点維持 + prism 撤去のみ。パッチ負債さらに減。

### L1 mruby 層 — 構造マップ完了 (再導出の設計図)

- HAL 独立 gem 廃止 → mruby-task/ports/posix/task_hal.c, mruby-dir/ports/posix/dir_hal.c に統合。
- 移設マップ確定 (resolutions.md「L1 mruby」節):
  - D7 → mruby-task/ports/posix/task_hal.c (新版も SIGALRM、FreeRTOS 化は依然必要・再導出)
  - D6 → mruby-dir/ports/posix/dir_hal.c ("flash/" prefix 再導出)
  - D4 task.c (新版が stack nil クリアを持つ → 一部 upstream 化の可能性・要 diff)
  - D5 mruby-dir/mrbgem.rake (port 自動選択の ESP32 対応)
  - D1 vm.c (tick、976/480 書換、再導出・最高リスク・実機必須)
  - D2 alloc.c (estalloc マルチ VM、新 estalloc pin 971b793)
- ここまでで「どのパッチを新構造のどこへどう再適用するか」の設計図が揃った。以降は C 実装の再導出。

### vm.c/task.c 再導出設計 完了 (rederive_vm_task.md) — 検証は Linux で

- 依頼者方針: **検証は Linux 版**。fmrb Linux も FreeRTOS tick パス (esp32_linux port) を通るので
  Linux で案D 経路を検証可能。
- **D1 vm.c [不要化・撤去]**: **新 upstream vm.c が我々の tick 修正の上位互換を実装済**
  (issues #6862-6887。task_across_c_boundary + jmp 復元 + exc/gc/root 拡張)。
  我々の ESP32 検証済み修正が本家へ取込。→ vm.c パッチ削除、upstream 採用。**最高リスク1件消滅**。
- **D4 task.c [一部撤去+案D 再導出]**: stack-clear は upstream 化済→撤去。
  案D top/bottom-half tick split のみ再導出 (bottom-half を task_run_body ループ先頭へ)。
- **D7 task_hal [再導出]**: FreeRTOS top-half を実装 (switching+pending 蓄積)、B1 と統合。
- 詳細な再適用箇所・コード片・未確定点は rederive_vm_task.md に記載。

### 次アクション (back-half: 実装フェーズ。Linux ビルドで反復)

- **D4/D7 実装** (案D bottom-half + FreeRTOS top-half)。
- **D6 dir_hal "flash/" 再導出** (新 mruby-dir/ports/posix/dir_hal.c)、D5 mruby-dir/mrbgem.rake。
- **B1 picoruby-machine 再導出** (+Option A の prism-lock 除去)。
- **D2 estalloc/alloc.c 再導出**。
- **Rakefile/pin 切替フェーズ**: 改名 path 更新、prism 撤去行削除、全 submodule 新 pin へ、
  fmrb_mem/config の Option A cleanup、global_mrb 配線確認。
- **socket TLS/net-http 集中統合** (ビルド検証しながら)。
- **B1 picoruby-machine 再導出** (prism lock 除去含む Option A cleanup も)。
- **estalloc/alloc.c (D2)** 再導出。
- Rakefile/pin 切替フェーズ: 改名 path 更新 + prism 撤去行削除 + 全 submodule 新 pin へ + fmrb_mem/config cleanup。
- socket TLS/net-http はビルド検証しながらの集中統合。
- **ビルド検証は不可(先頭「ビルド禁止の中間状態」を参照)**。submodule 新 pin 切替後に限り可能。
  切替前の rake build:* は無意味に失敗するので「マージ失敗」と誤認しないこと。ビルド前に `rake clean`(切替時 `clean_all`)。

### pin 切替 完了 (2026-07-12) — submodule を新 pin へ (working tree checkout, 未 commit)

手順 (再現可能。全て picoruby submodule 内のローカル操作):
1. L0 working tree を clean (`git checkout -- . && git clean -fd`。scratch は lib/ から再生成可)。
2. `git checkout c932f70b0` (新 master, detached)。旧 mruby-compiler2/mrbc2 dir は stale 化。
3. `git submodule sync` (mruby remote を hasumikin→本家 mruby/mruby に、改名パスを反映)。
4. `git submodule update mrbgems/picoruby-mruby/lib/{estalloc,mruby}` → estalloc 971b793 / mruby f56d44e。
5. `git submodule update --init mrbgems/mruby-compiler mrbgems/mruby-bin-mrbc` → 10408c3 / 8456898 (改名パス)。
6. `git -C mrbgems/mruby-compiler submodule update --init lib/prism` → prism c0e37816 (据置)。
7. stale dir 除去: `rm -rf mrbgems/mruby-compiler2 mrbgems/mruby-bin-mrbc2`。
8. `git submodule update` で mrubyc(71a231b)/regex_light(39e112a) も新 pin へ。
- funicular / littlefs は **未初期化のまま** (依頼者方針: 新規 gem 不採用)。
- 結果: L0 working tree クリーン (modified tracked = 0)。ツリーは upstream 新 pin の素の状態
  (まだ lib/ パッチ未適用)。

### 次アクション (実装フェーズ / Linux ビルドで反復)

- **Rakefile setup 更新**: (a) compiler パス改名 mruby-compiler2→mruby-compiler、
  (b) prism 撤去 (prism_xallocator.h/prism_alloc.c/mrbgem.rake 行削除)、(c) vm.c コピー行削除 (D1 撤去)。
- **rake setup** で lib/ を新ツリーに適用 → コンパイルエラーを見ながら D4/D7/D6/D5/D2/B1 を再導出。
- D4/D7 は rederive_vm_task.md の設計どおり (bottom-half を task_run_body へ、FreeRTOS top-half、
  take_pending_ticks の原子性、per-VM カウンタ)。
- B1 machine 再導出 (+prism-lock 除去)、fmrb_mem/config の Option A cleanup、global_mrb 配線確認。
- socket TLS/net-http 集中統合。
- `rake build:linux` を主検証に反復 → 通ったら esp32 → 実機は依頼者。

## 2026-07-12 (続き) — 実装フェーズ (pin 切替後)

pin 切替 (55a3659) 以降の実装コミット。詳細は resolutions.md 各節。

- **a87c65a Step A**: gembox `mruby-compiler2`→`mruby-compiler`、Rakefile compiler section
  (prism 撤去・compile.c パス改名)、**D1 vm.c パッチ削除**(upstream 化)、prism 3ファイル削除(Option A)。
- **43d992b D2**: alloc.c を新 upstream (複数アロケータ分岐) 上で ESTALLOC 分岐のみ per-VM 化
  (fmrb_get/set_current_est) + upstream の est_set_critical_section 保持 + stats ヘルパ。
- **2c9b92f D5/D6**: D5 撤去(upstream が mruby-dir の HAL 自動検出削除)。D6 dir_hal の "flash/" prefix を
  新 location mruby-dir/ports/posix/dir_hal.c へ再導出、全 path 関数に適用。
- **51e262e D4**: task.c 案D bottom-half を新 task_run_body へ (stack-clear は upstream 化で撤去)。
  task mrbgem.rake パッチ撤去 (upstream は effective_ports 方式)。
- **03fd034 D7/B1 Step1**: port 選択 matrix を build_config に。linux=`:esp32_linux,:freertos,:posix`,
  esp32/p4=`:esp32,:freertos,:posix`。消滅 hal-posix-task/dir gem 行を linux config から削除。
- **856d4ee D7**: mruby-task/ports/freertos/task_hal.c 新設 (案D top-half)。既存 machine 側 tick manager を
  抽出・共通化、原子性は g_tick_manager.mutex、notification idle、per-VM。

### 現在地

- lib/ 側の再導出: D1(撤去)/D2/D4/D5(撤去)/D6/D7 完了。compiler(Option A)完了。L0 の 8ファイル完了。
- **未完**: B1 picoruby-machine (merge-file + machine 側 tick manager 撤去 + prism-lock 除去)、
  global_mrb 配線、picoruby-mruby/mrbgem.rake 確定、socket TLS/net-http 統合、fmrb_mem cleanup、build 反復。
- submodule は新 pin (working tree checkout, fmruby-core 未 commit)。ビルドは B1 等未完のため未実施。

### 次アクション

- B1 着手前に旧 tick API の呼び出し元棚卸し (main/ lib/add grep、CMake PICORUBY_SRCS 確認) → resolutions.md へ。
- B1: merge-file で machine 差分を洗い出し → tick 撤去 + prism-lock 除去を重ねる。
- → global_mrb 配線 → rake setup → build:linux 反復。

## 2026-07-12 (続き) — B1 大半完了 + rake setup 検証OK

- **B1 picoruby-machine**: 3-way マージで CLEAN 16 + conflict 5 を解決 (posix/hal.c=ours,
  src/mruby/machine.c=#if対応, mrbgem.rake=マージ, hal.h=upstream alias採用+fmrb宣言追加)。
  esp32/machine.c は残 (esp32 専用; Linux は esp32_linux port 使用)。
- **呼び出し元付け替え**: fmrb_app.c hal_register_vm→mrb_hal_task_register_vm,
  fmrb-app app.c hal_deinit→mrb_hal_task_final。
- **picoruby-mruby/mrbgem.rake 確定**: mruby-io 除去, mruby-task 単独, ESTALLOC_DEBUG 常時on。
- **`rake setup` 完走 (エラー無し)** — Rakefile 改名パス・lib/ 構造の整合を検証。submodule working tree に
  patch 適用済 (build 準備状態)。

### 現在地: オフライン再導出フェーズ ほぼ完了 → build 反復フェーズへ

lib/ 側で完了: L0 8ファイル, compiler(Option A), D1撤去, D2, D4, D5撤去, D6, D7(freertos port),
B1(machine 大半), port選択, 付け替え, picoruby-mruby/mrbgem.rake。

**build 反復フェーズで解決する残件** (`rake build:linux` = idf.py/Docker を回して実エラーで対応):
1. **hal_freertos.c の tick manager 撤去** (Linux 重複シンボル): hal_freertos.c は 228行全体が tick manager で
   freertos task_hal.c と重複。CMake PICORUBY_SRCS(L78) から外す or ファイル gut。**build:linux は idf.py(CMake)**
   なので、rake の freertos port と CMake の hal_freertos.c のコンパイル経路整合を実ビルドで確定。
2. **esp32/machine.c の tick manager 撤去** (esp32 ビルド)。
3. **socket TLS/net-http 統合** (deferred): esp32/posix ssl_socket.c, src ssl_socket.c, posix/tcp_socket.c,
   net-http/http_client.rb は旧パッチのまま。新 upstream に対して統合が必要 (build エラーで顕在化)。
   判定は resolutions.md「socket TLS/TCP 群」参照 (alloc/close_notify=upstream, ready=ours, crt_bundle/timeout=保持)。
4. **prism-lock/pool cleanup** (Option A): fmrb_mempool.c の g_prism_memory_pool + est 初期化除去,
   fmrb_mem_config.h の FMRB_MEM_PRISM_POOL_SIZE 除去, machine ports の fmrb_prism_lock/unlock 除去
   (prism_alloc.c 削除済で呼び出し元無し=defined-but-unused、build は通るが cleanup)。
5. **global_mrb 配線** (runtime): compile 点で mutex→設定→compile→復元 (依頼者決定=mutex 方式)。build は通る
   (global_mrb は mruby-compiler ccontext.c 定義)、runtime で prism alloc に必要。
6. **mbedtls/socket rake の platform 判定** (ビルド時判断、resolutions 参照)。

### 次アクション

- `rake build:linux` を回し、上記 1→3→2 の順 (Linux 優先) でコンパイル/リンクエラーを潰す。
- lib/ 編集ごとに `rake clean`。ターゲット切替時 `rake clean_all`。
- green 後 esp32 ビルド。実機(デュアルコア)長時間走行・GUI 実行は依頼者確認項目 (tasklist)。

## 2026-07-12 (続き) — build 反復フェーズ開始 (rake build:linux)

`rake build:linux` (idf.py/Docker) を実行し実エラーで反復。

- **修正済 (コミット)**: CMakeLists の `mruby-compiler2/include`→`mruby-compiler`(a…), 
  `conf.microruby`→`conf.picoruby`(build.rb で改名, 旧picoruby→femtoruby), 
  syntax-highlight の gem 依存 `mruby-compiler2`→`mruby-compiler`。
  → ESP-IDF component 群を通過し mruby(libmruby)ビルド段階まで到達。
- **到達エラー + 構造決定**: rake が hal_freertos.c を compile → `freertos/FreeRTOS.h` 無し。
  FreeRTOS 依存 port は CMake(PRIV_REQUIRES freertos)でしか compile 不可。
  → **依頼者承認の holistic 再配置**: conf.ports :posix + `hal-task-freertos` ダミー gem
  (resolve_external_hal! で mruby-task の SIGALRM port 抑止) + freertos task_hal.c を
  PICORUBY_SRCS へ + hal_freertos.c 除去/削除 + machine posix を PICORUBY_SRCS から除去。
  詳細は resolutions.md「FreeRTOS port のコンパイル経路」節。
- **§3.5 (Linux シグナル禁止) コンプライアンス確認 [済]**: instruct §3.5 = FreeRTOS POSIX sim は
  signal 内部利用のため、リンクコードで sigaction/setitimer/signal() 禁止。
  - posix/hal.c は ours 解決で upstream の SIGALRM+setitimer を不採用 (準拠)。
  - posix/machine.c (CLEAN merge) は pthread_sigmask + sigaddset(許容) と ITIMER/SIGALRM の
    コメントのみ (Machine_delay_ms chunked ループの根拠)。実際の sigaction/setitimer 呼び出しは無し。
  - freertos task_hal.c もシグナル不使用。→ **B1 マージは §3.5 準拠**。
  - build:linux green 後、検証項目5・6 (SIGALRM port 未リンク, sigaction/setitimer 呼び出し元が
    sim 内部のみ) を nm/objdump で確認し resolutions.md に記録する (TODO)。
- **注**: conf.ports :posix は **Linux ビルドの決定**。esp32 の番で port マトリクスを別途確定し
  instruct_d7_b1_tick.md の表を実態に更新する (TODO)。

### 次アクション: holistic 再配置を実施 → build:linux 継続

## 2026-07-13 — Linux ビルド GREEN + §3.5 検証

**`rake build:linux` GREEN**: `build/fmruby-core.elf` (5.5MB) 生成、EXIT 0。
picoruby 最新版マージ全体 (vm/task 案D, compiler 改名, socket TLS/OpenSSL, machine 再構成,
estalloc per-VM, dir_hal, prism Option A 等) が Linux でビルド成立。

### build 反復で解決した実エラー (コミット列)

1. CMake `mruby-compiler2/include` → `mruby-compiler` (改名)。
2. `conf.microruby` → `conf.picoruby` (VM setup メソッド改名; 旧 picoruby→femtoruby)。
3. syntax-highlight の gem 依存 `mruby-compiler2` → `mruby-compiler`。
4. **build コンテナに openssl (libssl-dev) 追加** — docker/Dockerfile は既に libssl-dev 記載だが
   published :latest が古かった。同タグでローカル再ビルドして解決 (依頼者承認)。
5. socket TLS を upstream の vm 貫通 API へ 3-way 統合 (src/mruby/ssl_socket.c で pr#2 保持,
   posix ports は upstream の nonblock recv 採用) + posix tcp_socket.c に `<fcntl.h>`。
6. src/mruby/machine.c poll_signal の io_raw_bang/io_cooked_bang を FMRB_NO_IO_CONSOLE でガード。
7. prebuild のコンパイラ binary `picorbc` → `mrbc` (mruby-bin-mrbc 改名)。
8. CMake 側に `MRB_USE_TASK_SCHEDULER` 追加 (rake libmruby.a と mrb_state.task の ABI 整合)。
9. posix machine.c の io_raw_q を FMRB_NO_IO_CONSOLE でガード + `MRB_UTF8_STRING` を build_config へ
   (core と string-ext の mrb_utf8len_table 整合)。
10. **mruby-task posix task_hal.c を空スタブに patch** — conf.ports :posix で rake が SIGALRM 版を
    compile し CMake freertos port と mrb_hal_task_* が二重定義 → 空スタブで解消 (実シンボルは CMake freertos)。

### §3.5 (Linux シグナル禁止) 検証 — 準拠確認 (nm/objdump on elf)

- **項目5 (SIGALRM port 未リンク)**: mruby-task posix task_hal は空スタブ→未提供。tick は
  `mruby_tick_task`/`mrb_hal_task_init` (我々の freertos port) が定義。SIGALRM 二重 tick 源 無し。
- **項目6 (signal 呼び出し元)**: `sigaction`/`setitimer` の呼び出し元は FreeRTOS POSIX シミュレータ内部の
  `prvSetupSignalsAndSchedulerPolicy` (scheduler signal 設定) のみ。fmrb シンボル (mrb_/picorb_/machine/hal_)
  は sigaction/setitimer を一切呼ばない (objdump 確認)。許容の pthread_sigmask は posix/machine.c のみ。
- → **B1 マージ + case-D は §3.5 準拠**。

### 残作業 (Linux GREEN 後)

- **実機/Linux 実行時検証** (依頼者): preempt / sleep 精度 / ブロッキング後まとめ適用 / 長時間 tick 破壊非再発。
- **esp32 ビルド**: port マトリクス確定 (task_hal の rake/CMake 経路)、CMake 側の同型対応。
- prism-pool cleanup (fmrb_mempool/fmrb_mem_config の prism プール除去) + global_mrb 配線 (mutex 方式)。
- deferred socket の timeout/EINTR 堅牢化を runtime 再検証。
- fmruby-core への commit / submodule pointer 更新は依頼者確認事項 (未実施)。
