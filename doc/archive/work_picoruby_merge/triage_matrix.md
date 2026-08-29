# conflict triage マトリクス (old pin .. new master)

各パッチ対象ファイルについて「upstream が旧pin→新masterで同じファイルを変更したか」を判定。
`clean`=upstream 未変更(我々のパッチがほぼ自動適用) / `UPSTREAM-CHG`=双方変更(3-way マージ要) /
`GONE`=upstream から消滅(移設先の調査要) / `NEW-FILE`=我々の新規追加(衝突なし)。

数値は upstream 側の変更量 (old..new の +追加/-削除)。

## L0 picoruby (c14aa4400 .. c932f70b0)

| 判定 | upstream変更 | ファイル | 領域 |
|------|-------------|----------|------|
| clean | - | picoruby-i2c/include/i2c.h | C6 i2c |
| clean | - | picoruby-i2c/src/mruby/i2c.c | C6 i2c |
| clean | - | picoruby-mruby/src/file_ext.c | C12 |
| clean | - | picoruby-env/ports/posix/env.c | C1 env |
| CHG | 115/40 | picoruby-json/mrblib/json.rb | C11 json (upstream も大改造) |
| CHG | 13/3 | picoruby-mbedtls/mrbgem.rake | C8 |
| CHG | 3/6 | picoruby-mruby/mrbgem.rake | C12 |
| CHG | 10/0 | picoruby-mruby/src/alloc.c | **D2 estalloc (高リスク)** |
| CHG | 31/38 | picoruby-net-http/mrblib/http_client.rb | C10 net-http |
| CHG | 2/2 | picoruby-net-websocket/mrbgem.rake | C9 |
| CHG | 16/13 | picoruby-require/mrbgem.rake | C2 |
| CHG | 39/18 | picoruby-sandbox/src/mruby/sandbox.c | **C5 sandbox (高リスク)** |
| CHG | 3/2 | picoruby-socket/mrbgem.rake | C7 socket |
| CHG | 2/2 | picoruby-socket/src/mruby/socket.c | C7 |
| CHG | 186/52 | picoruby-socket/src/mruby/ssl_socket.c | C7 (upstream 大改造) |
| CHG | 25/11 | picoruby-socket/ports/esp32/tcp_socket.c | C7 |
| CHG | 71/36 | picoruby-socket/ports/esp32/ssl_socket.c | C7 |
| CHG | 13/13 | picoruby-socket/ports/esp32/tcp_server.c | C7 |
| CHG | 26/29 | picoruby-socket/ports/posix/tcp_socket.c | C7 |
| CHG | 144/73 | picoruby-socket/ports/posix/ssl_socket.c | C7 (upstream 大改造) |
| CHG | 2/2 | picoruby-yaml/mrbgem.rake | C3 |

**注目**: socket/net-http/json は我々のバグ修正パッチ (pr_candidates 参照) と upstream の変更が
重なる。upstream が同じバグを独自に直している可能性が高く、古いパッチで upstream 修正を潰さないよう
最重要領域。特に ssl_socket.c (posix 144/73, src 186/52, esp32 71/36) は大規模。

## L1 mruby (7a4622678 hasumikin .. f56d44e 本家 mruby/mruby)

| 判定 | upstream変更 | ファイル | 領域 |
|------|-------------|----------|------|
| clean | - | mrbgems/mruby-io/mrblib/file_constants.rb | D3 |
| CHG | **976/480** | src/vm.c | **D1 task-switch (最高リスク・VM 全面書換)** |
| CHG | **380/189** | mrbgems/mruby-task/src/task.c | **D4 (mruby-task 本家統合で大改造)** |
| CHG | 36/23 | mrbgems/mruby-task/mrbgem.rake | D4 |
| CHG | 0/31 | mrbgems/mruby-dir/mrbgem.rake | D5 (upstream 31行削除) |
| **GONE** | - | mrbgems/hal-posix-dir/src/dir_hal.c | **D6 (対象消滅・移設先調査要)** |
| **GONE** | - | mrbgems/hal-posix-task/src/task_hal.c | **D7 (対象消滅・移設先調査要)** |

**注目**: mruby 本家統合で VM と task scheduler が大規模に書き換わっている。D1/D4 は「rebase」でなく
書き換わった実装への「再導出」。posix HAL (dir/task) の 2 ファイルは upstream から消滅しており、
機能の移設先 (どの gem/ファイルへ移ったか) を特定してから作り直す必要がある。B1 picoruby-machine
(esp32_linux/hal_freertos.c) とも密接。全て実機確認必須。

## L1 mruby-compiler (29113090 mruby-compiler2 .. 10408c3)

- submodule は **同一 repo (picoruby/mruby-compiler2)**、parent 側の **path だけ改名** (mruby-compiler2 → mruby-compiler)。
- **prism submodule pin は据え置き (c0e37816)** → prism アロケータ ABI は不変、パッチ流用性が高い。

| 判定 | upstream変更 | ファイル | 領域 |
|------|-------------|----------|------|
| CHG | 1/2 | include/prism_xallocator.h | E1 (微差・ほぼ流用可) |
| NEW-FILE | - | lib/prism_alloc.c | E2 (我々の新規・衝突なし) |
| CHG | 79/33 | mrbgem.rake | E3 (要マージ) |
| CHG | 99/22 | src/compile.c | E4 (要マージ) |

## まとめ: 難易度ランク

- **自動/低リスク (clean or 新規)**: i2c, file_ext, env, file_constants.rb, prism_alloc.c, prism_xallocator.h。
- **中 (小〜中 conflict, 意図明確)**: json, net-websocket, require, yaml, mbedtls, socket rake/socket.c, mruby-dir。
- **高 (大 conflict, 意図の再適用が必要)**: net-http, sandbox, socket ssl_socket.c 群, compiler mrbgem.rake/compile.c, alloc.c(estalloc)。
- **最高 (再導出。ビルド通過≠正しさ。実機確認必須)**: vm.c(D1), task.c(D4), 消滅 HAL(D6/D7), picoruby-machine 置換(B1)。
