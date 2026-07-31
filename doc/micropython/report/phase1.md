# Phase 1 実装レポート

## 完了条件の判定

| 条件 | 判定 | 検証内容 |
|---|---|---|
| `rake build:linux` が通り、未定義参照ゼロでリンクできる | OK | 188/188。生成物 135 本すべてコンパイル、リンク警告は既存の Lua `tmpnam` のみ。MicroPython 由来の警告はゼロ |
| SELFTEST 有効ビルドで core のログに `mp: 2` が出る | OK | 下記ログ参照 |
| SELFTEST 無効の通常ビルドでも従来どおり起動する | OK | `dev_run_check.sh` のスクリーンショットにデスクトップが出る |
| GC ヒープ初期消費量の実測 | OK | 下記 |

自己診断のログ:

```
I fmrb_mp: MicroPython subsystem initialized (heap=262144 bytes, single instance)
I fmrb_mp: selftest: starting
I fmrb_mp: [mp_selftest] MicroPython started: gc total=259968 used=192 free=259776, stack limit=22376
mp: 2
I fmrb_mp: selftest: exec returned 0
I fmrb_mp: [mp_selftest] MicroPython runtime closed
I fmrb_mp: selftest: done
```

`mp: 2` に fmrb のログ書式が付いていないのは想定どおり。print は
`mp_hal_stdout_tx_strn_cooked` -> `printf` を通ってプロセスの標準出力に出る
(phase1 作業項目 3 の確認結果。port 層の追加実装は不要だった)。

## 実測値

### GC ヒープ

| 項目 | 値 |
|---|---|
| 確保サイズ (FMRB_MP_HEAP_SIZE) | 262,144 B |
| GC が管理できる総量 (gc total) | 259,968 B |
| 起動直後の使用量 (gc used) | 192 B |
| 起動直後の空き (gc free) | 259,776 B |

GC 自身の管理情報が 2,176 B (262,144 - 259,968) を取る。起動直後の使用は 192 B
しかないので、**256KB という初期値は phase3 の描画バインディングまで見ても
十分に余裕がある**。むしろ内蔵 RAM 事情によっては減らす余地があり、
phase4 でアプリ用プールの置き場所と合わせて再検討する。

### C スタック上限

自己診断は起動タスクから走るので `stack limit=22376` は**アプリタスクの値では
ない**。アプリタスクでの実測は phase2 で行う (計画どおり)。

## 実装した内容

### コンポーネント登録 (components/micropython/CMakeLists.txt)

生成ソースの一覧は `mp_embed_srcs.cmake` に切り出し、`rake micropython:gen` が
mp_embed/ と同時に書き出すようにした。CMakeLists は `include()` するだけ。

- `file(GLOB)` は ESP-IDF が明確に非推奨としており、ファイルが増減しても
  再 configure されず黙って古い一覧のまま通ってしまう。
- かといって 135 本を手書きすると、MicroPython のタグを上げたときに誰かが
  更新を忘れて静かに壊れる。

生成器が両方を書くなら、一覧とソースは原理的に食い違わない。

### 拡張子 .c 一覧に全アーキテクチャのソースが入る件

`mp_embed/py/` には nlrx64.c / nlrxtensa.c / asmxtensa.c / emitn*.c のように
他アーキテクチャ向けのソースが揃って入っている。中身は `#if` で自分の
アーキテクチャ以外は空になるので、全部コンパイル対象にしてよい (ホストスモークも
同じ形で通っている)。ビルド対象から外す必要はない。

## 実装中の気づき (次フェーズへの申し送り)

### 1. embed_util.c の `__assert_func` がファーム全体の assert を壊す [対応済み]

`mp_embed/port/embed_util.c` は `NDEBUG` が定義されていないとき
`__assert_func` を**無限ループとして定義する**。newlib は同じ名前の関数を
持っているので、ESP32 ビルドではリンカがこちらの定義を採用し、
**ファームウェアのどこで assert() が失敗しても、メッセージも出さずに
無限ループする**ようになってしまう (Linux ターゲットは glibc が
`__assert_fail` を使うので表面化しない = ESP32 で初めて出る類の罠)。

CMakeLists で `embed_util.c` にだけ `NDEBUG` を付けて定義自体を落とした。
この TU は自前の assert() を持たないので失うものはない。

同じファイルが定義する `nlr_jump_fail` も無限ループだが、こちらは
MicroPython 固有の名前なので他に影響しない。ただし**「nlr ハンドラの外で
raise が起きるとメッセージなしでハングする」**という性質は残る。
`fmrb_mp_exec` は必ず `nlr_push` の内側で実行するので通常は踏まないが、
phase2 で停止機構を入れるときはこの経路に注意する。

### 2. `fmrb_lua_init()` はどこからも呼ばれていない

phase1 の計画は「自己診断の置き場所は fmrb_lua_init の呼び出し元に並べる」と
していたが、`fmrb_lua_init` は定義と宣言があるだけで**呼び出し側が存在しない**
(Lua は `lua_newstate` 時に必要な初期化がすべて済むので実害はない)。

MicroPython は単一インスタンスの排他ロックを先に作る必要があるため、
`fmrb_kernel_start()` の `fmrb_app_init()` の直後に `fmrb_mp_init()` を
置いた。自己診断もその隣。

### 3. 自己診断はユーザアプリプール 0 を借りている

`fmrb_mp_start` はアプリのメモリプールハンドルを要るが、起動シーケンスの
時点では動いているアプリが無い。`POOL_ID_USER_APP0` はまだ誰も使っていないので、
自己診断はそこに一時的にハンドルを作り、終わったら `fmrb_mem_destroy_handle` で
消している。spawn 側が見る状態は変わらない。phase2 で自己診断ごと消える。

### 4. アプリプールの TLSF ハンドルは LUA / BASIC のときしか作られない [phase2 で必須]

`fmrb_app.c` の spawn 処理は、`ctx->vm_type` が `FMRB_VM_TYPE_LUA` または
`FMRB_VM_TYPE_BASIC` のときだけ `fmrb_mem_create_handle` を呼ぶ
(mruby は estalloc がプールを直接管理するため)。MicroPython も
`fmrb_malloc` を使うので、**phase2 でこの条件に
`FMRB_VM_TYPE_MICROPYTHON` を足さないと `ctx->mem_handle` が -1 のままになり、
GC ヒープの確保に失敗する**。phase2 の作業項目 4 には明記されていないので注意。

### 5. SELFTEST は CMake のキャッシュ変数なので、切り戻しは明示的に

`-DFMRB_MP_SELFTEST=ON` は CMake のキャッシュに残る。以後 `rake build:linux`
しても ON のままなので、無効化するときは `-DFMRB_MP_SELFTEST=OFF` を
明示的に渡す必要がある。既存の `ENABLE_SPI_TEST` 等と同じ性質。

### 6. スタック上限は「これまでの最小空き」から取っている

`uxTaskGetStackHighWaterMark` が返すのは**その時点までの最小空き**であって
現在の空きではない。`fmrb_mp_start` は起動直後に呼ぶ想定なので実質同じだが、
ずれる場合は必ず安全側 (実際より小さい上限) に倒れる。

### 7. `mp_embed_init` に渡すスタック先頭は `fmrb_mp_start` のフレーム

本家の例と同じく、start のローカル変数のアドレスをスタック先頭として渡している。
GC はここから現在の SP までを走査するので、**exec を start より浅いフレームから
呼ぶと、その間のスタック上の参照を GC が見落とす**。アプリタスクでは
create -> execute が同じ深さで並ぶので問題にならないが、呼び出し構造を変える
ときは意識する必要がある。

## 環境まわりで引っかかった点 (実装内容とは無関係)

- `fmruby-graphics-audio` 側のビルドが esp32 のまま残っていると、
  シミュレーションの起動が `xtensa-binfmt-P: requires more than reserved
  virtual address space` で落ちる。core 側だけ linux ビルドしても駄目で、
  両リポジトリとも `rake build:linux` が要る (ルート CLAUDE.md 記載のとおり)。
  症状がリンカ/ローダのエラーなので、原因がターゲット取り違えだと気付きにくい。
