# 作業指示書 R3: 実機なしで進められる残り (sdkconfig の見積もり・小物・Spinel の定数)

対象: 実装担当のサブエージェント。前提: plan.md、report/r1.md (3 章の棚卸し、7 章、
8 章の sdkconfig の提案)、report/r2.md、reference/internal_ram_budget.md (進め方・
落とし穴・計測手順)。report は `report/r3.md` へ。

実機はつながっていない。実機での確認が要るもの (PPA の scaled / block、I2S の
バッファ、apu_emu、S3 の R2 相当) は今回は扱わない (R4 以降)。

## T1: sdkconfig を変えた場合の効果の見積もり (変更はしない)

目的はユーザが判断するための表を作ること。**リポジトリの sdkconfig と
sdkconfig.defaults* は編集しない**。

- report/r1.md 8 章の各設定 (RMT の IRAM 配置、S3 の SPI master/slave の ISR、
  RINGBUF / HEAP / FREERTOS の flash 配置、lwIP の PSRAM 配置、
  `CONFIG_MDNS_TASK_CREATE_FROM_INTERNAL`、P4 の WiFi の IRAM 最適化) について、
  1 設定ずつ試しのビルドをして、静的な D/IRAM (IRAM と DRAM を分けて) の変化を
  3 機種 (TAB5 / NARYAv4 / S3) で測る。
  - 試しのビルドは、scratchpad に置いた追加の defaults ファイルを
    `SDKCONFIG_DEFAULTS` に足す形か、別の build ディレクトリで行う。どちらでも、
    終わったらリポジトリの sdkconfig* が develop と同じであることを
    `git status` / `git diff` で確かめる。
  - 足し算で効くとは限らないので、最後に「採用候補の組」でもう 1 回まとめて測る。
- 各設定の**条件**をコードで確かめて表に書く (推測で書かない):
  - その機能を割り込みから使っているか、flash 書き込み中 (キャッシュが止まる間) に
    動く必要があるか。例: 状態 LED の RMT、S3 の SPI リンク (使う構成があるか)、
    UART の ISR を IRAM に置く設定との両立、IRAM の ISR から heap を確保していないか。
  - IDF の Kconfig の依存 (その設定が選べない・他を巻き込む) も書く。
- 速さへの影響は、見込み (どの経路が flash 実行になるか) を書く。計測は要らない。
- 出力: 設定ごとに「3 機種の IRAM / DRAM の減り」「条件」「懸念」「推奨 (採用 / 見送り /
  実機で確かめてから)」の表。ユーザが sdkconfig を変えるかを決める。

## T2: 小物を PSRAM へ

report/r1.md 3 章の「PSRAM 候補 (未)」: `g_put_cache` (ble_task)、`s_sync_cmd_buf` /
`g_file_transfer` (host_task)、ほかに map を読み直して見つかる同種の小物
(タスク文脈でだけ触る管理表・作業領域で、DMA・割り込み・flash 書き込み中の参照が
無いもの)。

- 手法は既存に揃える (`FMRB_EXT_RAM_BSS_ATTR`、書かないものは `const`)。
- 1 つ移すたびに map で確かめる。移したものごとに安全と言える理由を report に書く。
- 迷うものは移さずに記録する。

## T3: Spinel の生成 C の定数を PSRAM へ

report/r1.md 7 章の案: Spinel の生成 C の `cst_*` / `pool_count` / `pool_max`
(3 機種とも約 689 B、173 個)。

- フォークの変更が要る。clone `/home/kishima/fmrb/wt/spinel-rebase` の `fmrb-ext` に
  コミットし (push はしない)、`import_from_fork.rb` でスナップショットを取り直す
  (SPINEL_PIN の更新は親の判断。report に新しいコミットを書く)。
  `SP_TU_NIL_SLOT` と同じく、生成時に `SP_TU_BSS` を付ける形を基本にする。
- **P4 (RISC-V) で .sbss から本当に外れたか**を map で確かめる。
- `cst_` は GC の根として印付けのたびに読むので、**GC の停止時間を前後で比べる**
  (sim で。計測の方法は reference/internal_ram_budget.md と既存の GC 計装に揃える。
  同じコミットの前後で。回数を取って揺れも書く)。
- 変更が大きくなる (生成器の広い範囲を触る) なら、止まって案を返す。
- 上流 PR にはしない (フォーク独自の配置の仕組みのため)。

## 検証

- ビルド: TAB5 / NARYAv4 / S3 / Linux (標準・互換) / wasm。静的な D/IRAM が
  develop より減る (増えない)。移した項目ごとの減り方を表に。
- `rake test` を通す。
- sim の標準構成と互換構成 (全 mruby): 起動、エディタの起動・1 打鍵・保存、
  アプリの起動と kill、ファイル操作。T2 で host_task のものを移したら、遠隔の
  ファイル転送 (sim で通る経路) も。
- T2 で `g_put_cache` (BLE) を移した場合、BLE は sim で動かないので、ビルドと
  コードの読みで安全を示し、report に「実機未確認」と書く。

## 作業の決まり

- 作業ブランチ `feature/iram-reduction-r3` (親が develop から切る)。本体の
  checkout で作業する (worktree は使わない)。移す単位ごとにコミットし、自分が
  変えたファイルだけをパスで指定する。push とマージはしない。
- .env は書き換えずコミットしない。ビルドするときにターゲットを変えるには、
  既存の手順 (report/r1.md 9 章) に従う。最後に `git diff .env` が scratchpad の
  env_before_iram_r3.diff と一致することを確かめる。
- sdkconfig / sdkconfig.defaults*、graphics-audio、submodule・外部の部品は変えない。
- report は日本語の常体。コードのコメントと commit メッセージは英語
  (件名は `<領域>: <要約>`、Co-Authored-By: Claude Opus 5.5 (1M context)
  <noreply@anthropic.com>)。
- 結果を reference/internal_ram_budget.md の「計測記録」に追記する。
- 終わったら試しのビルドの build ディレクトリを消す。本体の build/ は最後に
  Linux (標準構成) のビルドで終える (親が sim で検収するため)。

## 止まる条件

- 実機が要ると分かったとき (その項目は移さずに記録して、ほかは続けてよい)。
- 見立てと食い違う事実 (例: 移したら壊れた理由が説明できない、GC の停止時間が
  はっきり悪化した)。
- 範囲外の変更 (sdkconfig、submodule、graphics-audio) が要ると分かったとき。

## report に書くこと

- T1 の表 (3 機種・設定ごと・組での値、条件、推奨)
- T2 / T3 で移したものと減り方、安全と言える理由、移さなかったものと理由
- GC の停止時間の前後
- 見立てと違った点、撤回した仮説、残件 (R4 に回すもの)

## 受け入れ条件

- T1 の表がある (ユーザが sdkconfig を変えるか決められる形)
- 3 機種の静的な D/IRAM が develop より減っている (T2 + T3 の分)
- sim の 2 構成で退行が無い、`rake test` が通る
- GC の停止時間がはっきりとは悪化していない (数値つき)
