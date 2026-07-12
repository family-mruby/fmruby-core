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

## L1 mruby / L1 compiler / B1 machine
- 未着手 (triage_matrix.md 参照)。vm.c / task.c / 消滅 HAL / machine は再導出。
