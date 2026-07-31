# Phase 0 実装レポート

対象コミット: `1ee2123` (ブランチ feat/micropython)

## 完了条件の判定

| 条件 | 判定 | 検証内容 |
|---|---|---|
| `rake micropython:gen` が再現可能に走る | OK | `micropython:clean` -> `gen` を 2 回実行し、生成ツリー全体の内容ハッシュが完全一致 (`1f9c4fcd3df5c783a6b3e1d0b7bd44b450fff612...`)。コミット後の再生成でも unstaged diff は 0 ファイル |
| git clean な状態で `rake micropython:smoke` が hello を出す | OK | 出力 `hello` / `sum 285` / `float 0.25` / `list ['f', 'm', 'r', 'b']` / `caught ZeroDivisionError('divide by zero',)` / `free True`、exit 0 |
| `rake build:linux` が従来どおり通る | OK | 508/508 で `fmruby-core.elf` までリンク成功 |

`build:linux` のログに出る micropython 関連は
`Component directory /project/components/micropython does not contain a CMakeLists.txt file. No component will be added`
の 1 行のみ。この時点ではコンポーネント未登録なので想定どおりで、
ファームウェアへの影響はゼロ。

補足: 最初の `rake build:linux` は、既存の build/ が esp32s3 向けのまま
残っていたため sdkconfig のターゲット不一致で失敗した。MicroPython の変更とは
無関係で、`rake clean_all` してから再実行して通した (CLAUDE.md の手順どおり)。

## 実測値

### 生成物 (components/micropython/mp_embed/)

| 項目 | 値 |
|---|---|
| ファイル数 | 209 |
| サイズ | 3.0 MB |
| .c の本数 | 135 |

内訳は py/ 197、genhdr/ 4、port/ 5、extmod/ 1 (modplatform.h のみ)、
shared/runtime/ 2。

### ホストでのサイズ (x86_64, -Os, 全生成ソース + スモーク main をリンク)

| 区分 | サイズ |
|---|---|
| text | 244,709 B |
| data | 18,768 B |
| bss | 888 B |

未使用シンボルを落とさない素のリンクなので上限側の目安。phase4 の
Xtensa/RISC-V 実測とは直接比較しないこと (比較は必ず同一コミットの
ビルド同士で行う)。

## 実装中の気づき (phase1 以降への申し送り)

### 1. `mp_stack_set_limit()` を呼ばないとハングする [phase1 で必須]

`MICROPY_STACK_CHECK` を有効にすると `mp_stack_check()` は
`mp_stack_usage() >= MP_STATE_THREAD(stack_limit)` で判定するが、
`mp_embed_init()` は `mp_stack_set_top()` しか呼ばず limit は 0 のまま。
その結果すべてのチェックが失敗し、`mp_init` や compile 中の raise が
最外の nlr ハンドラの外に出て `nlr_jump_fail()` の無限ループに落ちる。

スモークテストで実際に踏んだ (2 分ハング、SIGTERM で停止)。症状が
「無反応」なので原因が分かりにくい。phase1 の `fmrb_mp_start` では
`mp_embed_init` の直後に必ずタスクスタック残量から算出した limit を
設定すること。

なお v1.28.0 では `py/stackctrl.h` が「deprecated, please use py/cstack.h」と
なっているが、`MICROPY_PREVIEW_VERSION_2` が 0 の間は旧 API が生きており、
embed port 自身 (embed_util.c) も旧 API を使っている。旧 API で揃える。

### 2. `mp_builtin_open_obj` はポートが提供する義務がある [対応済み]

`MICROPY_PY_IO` が有効だと builtins テーブルと io モジュールの両方から
無条件に参照されるが、実装はコアに無い (`py/builtin.h` に
"A port can provide this object" とある)。`MICROPY_VFS` が 0 なので
`mp_vfs_open_obj` への転送も効かない。

`components/micropython/port/mpport.c` に、常に OSError を投げる実装を置いた。
ゲストアプリにファイルシステムを渡さない方針と一致しており、`open` が
「黙って存在しない」より「呼ぶと明示的に失敗する」ほうが分かりやすい。
`io.StringIO` 等は使えるまま残る。

### 3. ROM レベル CORE のままだと `MICROPY_PY_TIME` が未定義参照になる [対応済み]

`MICROPY_PY_TIME` は BASIC レベル (CORE より下) で有効になるが、実装の
`extmod/modtime.c` は生成物に含まれない。mpconfigport.h で明示的に 0 にした。
待機手段は phase3 の `app.sleep` で提供する。

同じ理屈で extmod にある他のモジュール (json, os, re, random, binascii 等) も
使えない。将来これらが要るなら、port/Makefile 側でパッケージへの追加コピーを
足す形になる (submodule の編集は不要)。

### 4. コンパイルには GNU C 方言が要る [phase1 で確認]

`shared/runtime/gchelper_generic.c` が `register long rbx asm ("rbx")` の形で
レジスタを固定するため、厳密な `-std=c99` では通らない。ホストスモークは
`-std=gnu99` にしている。ESP-IDF は既定が gnu17 なので問題にならない見込みだが、
phase1 のコンポーネント登録時に確認する。

### 5. stdout は追加実装なしで済む見込み

`mp_embed/port/mphalport.c` の `mp_hal_stdout_tx_strn_cooked` は `printf` そのもの。
phase1 の「print の出力先をプロセスの標準出力 (docker ログ / UART) にする」は
これで満たせる見込みで、port 層の追加実装は要らない可能性が高い。

### 6. include パスの衝突に注意

コンポーネントの include パスには `mp_embed/` と自前の `port/` が両方載る。
生成物側にも `port/` サブディレクトリ (`mp_embed/port/mphalport.h` 等) があるため、
自前 port/ に同名ファイルを置くと -I の順序で解決先が変わる。現状は衝突なし。

## 判断が要った点

### タグ選定

`git ls-remote --tags` の結果、最新安定は v1.28.0 (v1.29.0-preview は開発版)。
ユーザ確認のうえ v1.28.0 に固定した。

### 取り込み方式

本リポジトリには submodule (Lua) と vendor+PIN (Spinel) の両方の前例がある。
計画どおり submodule 方式で進めることをユーザに確認して実施した。

### USER_C_MODULES を phase0 で配線したこと

phase0 の作業項目そのものではないが、未確定事項として「仕組みだけ見極めておく」
とされていた項目なので、port/Makefile に `USER_C_MODULES = $(abspath ..)` を
入れて実際に走ることまで確認した。phase0 時点ではモジュールが無いため走査結果は
空で、生成物には影響しない。

## 次フェーズへの持ち越し

- 上記 1 (スタック上限) は phase1 の実装必須項目。
- 上記 4 (GNU C 方言) は phase1 のコンポーネント登録で確認。
- GC ヒープの初期消費量の実測は phase1 の完了条件。
- タスクスタック消費量の確認は phase2。パーサ/コンパイラが C スタック再帰を
  使うため、上記 1 の limit 設定値と合わせて決める必要がある。
