# W2 の上限を本体に入れる (決定 27) (2026-11-17)

> 親: plan.md の決定 27、report/w2.md 6 章 | ネイティブ版 (x86) と `rake regress` で確認。P4 の確認は親。

## 1. 変えたもの

| 値 | 置き場 | 前 | 後 |
|---|---|---|---|
| wasm のメモリの最大 | `fmrb_wasm.h` `FMRB_WASM_DEFAULT_MEMORY_PAGES` | 枠の空き − 64 KB (P4 で 14 ページ) | **全環境で 4 ページ**。`.app.toml` の `wasm_memory_pages` で 1〜枠の空きまで |
| 1 回の呼び出しの命令数 | `fmrb_wasm.c` `FMRB_WASM_CALL_INSN_LIMIT` | 2,000 万 | **500 万** |
| アプリのタスクのスタック | `fmrb_wasm.h` `FMRB_WASM_APP_TASK_STACK_SIZE` | 24 KB | **16 KB** (Ruby のアプリと同じ。`task_stack_kb` で変えられる) |
| 共通のプール・演算のスタック・`NATIVE_STACK_GUARD`・`POOL_MARGIN` | `fmrb_wasm.c` | 256 KB・16 KB・6 KB・64 KB | 変えない (コメントだけ直した) |

- `wasm_memory_pages` は spawner (`main/app/fmrb_app_spawner.c`) が読み、spawn の属性 → アプリの context
  (`wasm_memory_pages`、0 は未指定、-1 は整数でない・1 未満) で `fmrb_wasm_app_run` に渡す。範囲の判定は
  バイナリの写しを取った後の枠の空きで行う。
- 断る文言 (どれもエラーの Window とログ、ほかの wasm の断りと同じ経路):
  - `<path>: wasm_memory_pages in its .app.toml must be a whole number from 1 to N (...)`
  - `<path>: wasm_memory_pages = 99 in its .app.toml is more than this machine's app frame has room for (N pages of 64 KB)`
  - `<path> starts with N pages of memory, more than its limit of M (raise wasm_memory_pages ...)`
    (モジュールの初期ページ数が上限を超えるとき。WAMR は黙って初期ページ数に合わせるので、本体で断る)
- プールが足りないとき: 読み込み (`wasm_runtime_load`)・インスタンス作成・exec env の作成で WAMR が
  `allocate memory failed` を返したら、`<path> does not fit the shared wasm pool: F KB of 256 KB free (the module
  is B bytes with N functions, and each function takes some of it)` に置き換える。ほかの wasm のアプリが動いていれば
  `; other wasm apps hold the rest` を足す。空きは読み込みの前の値、関数の数は WAMR が写しに手を入れる前に数える。
- インストール (W6、`fmrb_wasm_check_file`): 関数のセクションの数を読み、`関数 × 100 B + 20 KB` が 256 KB を超える
  (空のプールにも入らない) ものだけ断る。100 B は P4 の実測 120 B より小さい下限で、入るものを断らない側に寄せた。
  入るかどうかは起動の時点の空きで決まるので、それ以上の判定はしない。
- `NATIVE_STACK_GUARD` 6 KB は 16 KB でも保つ: 余裕の上に約 9 KB が残り (24 KB のとき 17 KB)、tour の全ページの
  使用の最大 6.6 KB (スタック全体) はその中に収まる。P4 の `native stack room` の値は親の確認で見る。

## 2. 確認

- `rake regress`: host 33 / 33、描画 233 / 233。
- ネイティブ版 (`rake native:core`、`native_up`):
  - tour 4 言語を同時に起動し (共通のプール 135 KB)、それぞれ 10 ページを送って閉じた。トラップ 0、
    `loaded ... up to 4`。
  - `w2b_big4000` (`gen_big.rb 4000`、183,066 B、scratchpad でビルド、リポジトリに入れていない) は単独で
    `does not fit the shared wasm pool: 250 KB of 256 KB free (... 4003 functions ...)`、C tour と big1000 の後では
    `27 KB of 256 KB free ...; other wasm apps hold the rest`。エラーの Window に全文が出た。
  - `wasm_memory_pages = 8` で `up to 8`、`= 99` は「枠は 22 ページ」の文言で、`= "four"` は「1 から 22」の文言で断った。
  - `w2b_spin` は 7 ms で `instruction limit exceeded` (前は 29 ms)。
  - インストールの判定 (`FmrbApp._wasm_check`、使い捨ての Ruby のアプリで呼んだ): big4000 は
    `cannot fit the shared wasm pool of 256 KB even alone: its 4003 functions take at least 411 KB of it`、
    big1000・C tour は通る。
- ネイティブ版はタスクのスタックが pthread のもので (`native stack room` 約 244 KB)、16 KB への変更は効かない。
  スタックの確認は P4 で行う。ESP32 のビルド・ブラウザ版のビルドはしていない。

## 3. 残り

- P4 で: `native stack room` (約 9 KB の見込み)、tour の全ページ、`fmrb_task:` の Stack Free、内蔵 RAM が
  1 本 8 KB 減ること、命令数の上限が約 0.9 秒であること。
- 演算のスタックを `.app.toml` で増やす案 (`wasm_stack_kb`) と、S3 の共通のプールの大きさは未確定事項に残した。
- SDK (family-mruby-sdk) の getting_started・maintaining を新しい値と文言に合わせた (別のコミット、push していない)。
