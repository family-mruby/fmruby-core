# 作業指示書 R1: 棚卸しと、安全なものの移設

対象: 実装担当のサブエージェント。前提: plan.md、reference/internal_ram_budget.md
(進め方・落とし穴・計測手順・計測記録)、doc/spinel_upstream_ext/report/p2c.md 10 節、
doc/fullscreen_hires/report/h3.md 7 章。report は `report/r1.md` へ。

## T1: 棚卸し

- develop の今のソースで、TAB5 / NARYAv4 / S3 (NARYAv3) を clean_all からビルドし、
  map を読んで**内蔵 RAM (DIRAM の .data / .bss、IRAM) に載るシンボルを大きい順に**
  表にする (シンボル、所属、区分、バイト数、3 機種それぞれ)。
- 各行を分類する: **flash (const)** / **PSRAM** / **外す** / **残す (理由)** /
  **確かめてから (R2)**。懸念 (DMA、割り込み、キャッシュが止まる間の参照、
  速さ、IDF の決め打ち) を書く。
- 上位 40 件程度と、まとめて移せる群 (同じ部品の小物の集まり) を挙げる。
- ESP-IDF や外部の部品 (managed_components、submodule) の中のものは、設定
  (sdkconfig) で移せるなら提案として書くだけにする。直接は編集しない。

## T2: 安全なものを移す

- T1 で「flash (const)」「PSRAM」「外す」に分類し、懸念が無いと言い切れるものから
  移す。手法は既存のものに揃える (`FMRB_EXT_RAM_BSS_ATTR`、`const`、Spinel の
  `SP_TU_BSS` 系、mruby の生成物なら lib/patch 経由)。
- mruby の gem_init / picogem_init の `.data` を const にする件は、生成スクリプトの
  どこで `const` が付かないかを調べ、lib/patch (submodule の直接編集は禁止) で
  直せるなら直す。
- Spinel の生成 C の定数 (`cst_*`) と module の pool の数えを PSRAM に置く手当ては、
  フォーク (kishima/spinel fmrb-ext) の変更が要るなら、R2 に回して案だけ書く。
- 1 つ移すたびに、その区分の変化を map で確かめる。まとめて移してから測らない。

## 検証

- 3 機種のビルドで、静的な D/IRAM が develop より減る。移した項目ごとの減り方を表に。
- sim の標準構成と互換構成 (全 mruby): 起動、エディタの起動・打鍵・閉じる・
  再起動・打鍵、ファイルの保存と読み込み、アプリの起動と kill、設定ダイアログ。
- 実機 (P4-Nano (NARYAv4)。/dev に無ければ rake attach を自分で。Tab5 がつながって
  いればそれも): 起動、待機時の IRAM free (develop と比べる)、エディタ、ファイル操作、
  アプリの起動と kill、描画 (render の時間が大きく悪化しないこと)、Guru 0。
  flash / serial / tab5_* は許可済み。目視は要らない (表示の後段は変えないため)。
  表示に関わるバッファを移して見た目に影響しうる場合だけ、最後に 1 回目視を依頼する。
- S3 の実機は、つながっていなければビルドとサイズだけにして report に「未」と書く。

## 作業の決まり

- 作業ブランチ `feature/iram-reduction` (親が develop から切る)。移す単位ごとに
  コミットし、自分が変えたファイルだけをパスで指定する。push とマージはしない。
- .env は書き換えずコミットしない。最後に `git diff .env` が scratchpad の
  env_before_iram.diff と一致することを確かめる。
- sdkconfig と graphics-audio は変えない (graphics-audio の内蔵 RAM は対象外)。
- report は日本語の常体。コードのコメントと commit メッセージは英語。
- 結果を reference/internal_ram_budget.md の「計測記録」に節として追記する。

## 受け入れ条件

- T1 の表 (3 機種、分類つき) がある
- 3 機種の静的な D/IRAM と、P4 実機の待機時の IRAM free が develop より減っている
- sim の 2 構成と P4 実機で退行が無い
- 移したものごとに、安全と言える理由が report にある
