# 内蔵 RAM の削減 (第 2 弾)

> 状態: 進行中 | 更新: 2026-09-28 | 既存の削減計画 (reference/internal_ram_budget.md) の続き。今の develop で内蔵 RAM に載っているものを洗い直し、置く必然性の無いものを PSRAM / flash (const) へ移す。安全な順に進める

## 目的

内蔵 RAM は Tab5 / NARYAv4 / S3 のどれでも余裕が無く、同時に動かせるアプリの数と
機能の追加を縛っている (ユーザの方針: 増やさないのは最低線で、無駄を減らす方向。
速度より RAM を優先し、多少の速度の低下は許容する)。

## 方針

- 進め方と落とし穴は reference/internal_ram_budget.md に従う (回収量ではなく
  **安全に取れる順**、計測してから決める、同じコミットのビルド同士で比べる、
  PSRAM スタック (軸 B) は今回は扱わない)。
- 移し先は 3 つ: **flash (const)** (実行時に書かないもの)、**PSRAM**
  (書くが速さ・DMA・割り込みの制約が無いもの)、**外す** (使っていないもの)。
- 触らないもの: DMA に渡すバッファ、割り込みから触るデータ、flash の書き込み中
  (キャッシュが止まっている間) に読むデータ、ROM / IDF が内蔵を決め打ちするもの。
  判断に迷うものは移さずに記録する。
- sdkconfig は編集しない (IDF の配置の設定が要るものは提案として書く)。

## 候補の出どころ

- doc/spinel_upstream_ext/report/p2c.md 10 節「既存の内蔵 RAM の削減候補」:
  描画系 (g_progs 8.8KB、g_recv_buf 4KB、g_sorted ほか、P4 で計約 16KB)、
  ファイル系 (s_files、s_fs_buf ほか、約 6-10KB)、mruby の gem_init / picogem_init の
  .data (irep 表・シンボル表、約 6KB)、editor_core の g_wrap、mDNS の packet、
  transport の文脈、trilength_lut など
- doc/fullscreen_hires/report/h3.md 7 章: Spinel の生成 C の定数 (`.sbss.cst_*`、
  約 225B) と module の pool の数え (約 464B)
- reference/internal_ram_budget.md の軸 E の残り
- 新しく map を読み直して見つかるもの

## 段階

| 段階 | 内容 | 状態 |
|---|---|---|
| R1 | 棚卸し (3 機種の map をシンボル単位で、移し先と懸念を分類) と、安全なものの移設 | 指示書発行 (instruction_r1.md) |
| R2 | R1 で「確かめてから」に残したもの (DMA / 割り込みの確認が要るもの、速さの計測が要るもの) | 未着手 |

## 受け入れ条件

- 3 機種 (TAB5 / NARYAv4 / S3) の静的な D/IRAM と、実機の待機時の IRAM free が
  develop より減る (増えない)。減った量を表で残す。
- 標準構成と互換構成 (全 mruby) の sim、実機 (P4) で退行が無い。
- 移したものごとに、なぜ安全かを report に書く。
- 結果を reference/internal_ram_budget.md の「計測記録」に追記する。
