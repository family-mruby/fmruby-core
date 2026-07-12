# upstream 調査結果 (2026-07-12 fetch)

picoruby master HEAD を fetch し、旧 pin との構造差を分析した結果。

## target pin

- **picoruby master HEAD = `c932f70b0347ea35ea4c58b83810c9767622f6f6`** (2026-07-11 16:58)
  - コミットタイトル: "Merge pull request #451 from sylph01/fix-usb-cdc-irq-races"
- 旧 pin `c14aa4400` (2026-03-21) から **413 commits** 先行。

## 入れ子 submodule の pin 変化

| パス (新) | 旧 pin | 新 pin | 備考 |
|-----------|--------|--------|------|
| picoruby-mruby/lib/mruby | 7a4622678 | **f56d44eccbd0** | remote 変更 (下記) |
| picoruby-mruby/lib/estalloc | ae50a9649 | **971b79376d65** | pin 更新のみ |
| mrbgems/**mruby-compiler** | 29113090 (compiler2) | **10408c327fb9** | **改名** (旧 mruby-compiler2) |
| mrbgems/**mruby-bin-mrbc** | e3fd48c42 (mrbc2) | **8456898ceb76** | **改名** (旧 mruby-bin-mrbc2) |
| mrbgems/picoruby-funicular | (なし) | **eb72138d7d1a** | **新規** submodule |
| picoruby-littlefs/lib/littlefs | (なし) | **adad0fbbcf53** | **新規** submodule |

## .gitmodules の構造変化 (重要)

1. **mruby ソースの remote 移行**: `hasumikin/mruby.git` → **`mruby/mruby.git`** (本家へ統合)。
   - 新 pin f56d44e は mruby/mruby master 上に存在 (確認済)。
   - 新 mruby にも `mrbgems/mruby-task` が存在し、`src/vm.c` に `MRB_USE_TASK_SCHEDULER` が 4 箇所。
     → **task scheduler 機構は存続。D1 (vm.c tick 修正) は引き続き該当**。ただし実装は書き換わっている
       可能性が高く、パッチがそのまま当たるかは rebase で判定。
2. **mruby-compiler2 → mruby-compiler に改名** (path も変更)。
   → lib/patch/compiler/ の適用先パスと Rakefile setup のパスを全面的に見直す必要 (E1-E4)。
3. **mruby-bin-mrbc2 → mruby-bin-mrbc に改名**。
   → build_config / gembox が mrbc2 を名指ししていないか確認 (A13/A14/G3)。
4. **新規 submodule 追加**: picoruby-funicular, picoruby-littlefs/lib/littlefs。
   → gembox で有効化されるか、esp_littlefs (fmrb 側) と競合しないか確認。

## 戦略への影響 (要確認/判断ポイント)

- **[判断1] compiler2→compiler 改名**: lib/patch/compiler と Rakefile setup のパス変更が必須。
  さらに prism アロケータ (prism_xallocator.h / prism_alloc.c) が新 mruby-compiler にそのまま
  当たるか未知。バイトコード版整合 (G3) と直結。
- **[判断2] mruby 本家統合**: hasumikin fork 固有だった挙動 (mruby-task, hal-posix-*) が本家仕様に
  変わり、D1/D4/D7 のパッチが衝突または不要化する可能性。rebase 前に新 vm.c / mruby-task の
  差分を精読する。
- **[判断3] 新規 gem (funicular, littlefs)**: 取り込むか無効化するか。fmrb は独自 FS/littlefs を持つ
  (esp_littlefs, picoruby-fmrb-filesystem) ため要検討。
- **[判断4] fmruby-core 本体側 (main/ C コード) が picoruby API に依存**している箇所が、
  API 変更で壊れる可能性。ビルドで顕在化するので build:linux/esp32 で検出する。

## 次アクション

1. rebase 用作業ブランチを submodule 内で作成 (旧 pin 起点)。
2. パッチを領域ごとに細かくコミット (compiler / sandbox / socket / net / vm / task / machine 別)。
3. origin/master (c932f70b0) へ rebase。改名パスは rename 追従 or 手動移設で対応。
4. 入れ子 submodule (mruby f56d44e, mruby-compiler 10408c3) も同手順。
