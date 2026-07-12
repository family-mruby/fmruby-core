# 作業ログ (時系列)

中断復帰の起点。新しい作業は末尾に追記する。日付は JST。

> **【重要・ビルド禁止の中間状態】**
> 現在、lib/ の解決済みファイルは **新 HEAD 前提**の内容 (新述語 `build.picoruby?`、新 mruby ツリーの
> パス等) に書き換わっているが、**submodule はまだ旧 pin (c14aa4400)** のままである。
> この状態で `rake build:linux` / `rake build:esp32` を実行すると、setup が新前提のファイルを
> **旧ツリーに被せる**ため、無意味なビルド失敗になる (マージの正否とは無関係の失敗)。
> **ビルド検証は submodule を新 pin (c932f70b0 および各入れ子の新 pin) に切り替えてからでないと不可能。**
> それまでビルドで検証しようとしないこと。

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

### 次アクション (back-half: 深い再導出。実機検証が要るため集中セッション向け)

- **D1 vm.c / D4 task.c 再導出** (最高リスク)。新実装を精読し tick 安全化・stack clear を再適用。
- **D6/D7 HAL ports 再導出** (dir_hal "flash/", task_hal FreeRTOS 化)。
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
