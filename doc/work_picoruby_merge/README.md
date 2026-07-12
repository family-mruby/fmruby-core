# PicoRuby 最新版統合 作業フォルダ

このフォルダは `doc/merge_latest_picoruby.md` の作業計画に基づき、picoruby submodule を
upstream master HEAD に更新する作業の記録用フォルダである (git 管理)。

## ゴール

`fmruby-core/components/picoruby-esp32/picoruby/` (入れ子 submodule) を
[picoruby/picoruby](https://github.com/picoruby/picoruby) の **master HEAD** に更新する。

- 現行 pin: `c14aa4400` (2026-03-21)
- VM: picoruby-mruby (mruby VM) 系
- mruby バージョンアップでバイトコード互換性は失われる (compiler と VM をバイトコード版で揃える)

## ファイル構成

- `README.md` … 本ファイル。作業の全体像と再開手順。
- `progress.md` … 時系列の作業ログ。**中断復帰の起点**。何をどこまでやったかを都度追記する。
- `tasklist.md` … パッチ 1 件 = 1 項目のタスクリスト。各項目に「意図 (なぜ必要か)」を記載。
- `diffs/` … 各階層で採取した実際の diff (source of truth は lib/ だが、理解用に保存)。

## 現状の構成メモ (2026-07 時点)

### 3 階層の submodule pin (作業開始時)

| 階層 | パス | 現 pin |
|------|------|--------|
| L0 | `components/picoruby-esp32/picoruby` | `c14aa4400` (3.0.1-2402-gc14aa440) |
| L1 | `mrbgems/picoruby-mruby/lib/mruby` | `7a4622678` (1.0.0-14144-g7a4622678) |
| L1 | `mrbgems/mruby-compiler2` | `29113090` (3.4.1~1) |
| L1 | `mrbgems/picoruby-mruby/lib/estalloc` | `ae50a9649` |
| L2 | `mrbgems/mruby-compiler2/lib/prism` | (mruby-compiler2 の submodule) |

### パッチの 3 分類 (source of truth = `fmruby-core/lib/`)

- **lib/add/** … 新規 mrbgem/設定の追加。upstream と直接衝突しないが依存 API 変更の影響は受ける。
- **lib/replace/picoruby-machine** … ディレクトリ丸ごと置換。rebase に乗らないため「再導出」で別枠扱い。
- **lib/patch/** … ファイル単位の上書き。マージの主対象。`compiler/` は mruby-compiler2 内を、
  `picoruby-mruby/lib/mruby/` は mruby submodule 内を書き換える。

パッチのコピーは `fmruby-core/Rakefile` の `setup` タスク (build:linux/esp32 の依存) が行う。
setup の全コピー一覧は `tasklist.md` に転記済み。

## 作業ルール (merge_latest_picoruby.md / CLAUDE.md より)

- submodule 内のローカル git 操作 (branch/commit/rebase/fetch/checkout) は本作業として許可。
- **push と fmruby-core 側の commit (submodule pointer 更新含む) は依頼者に確認してから**。勝手にやらない。
- マージ結果は必ず lib/ 側のファイルに書き戻す (コピー先 diff は結果にすぎない)。
- lib/ 編集後は `rake clean`、ターゲット切替時は `rake clean_all`。
- 高リスクパッチ (vm.c、alloc.c/estalloc、sandbox、picoruby-machine 置換) は
  「ビルド通過 = 正しさ」ではない。tasklist に明示し実機確認へ引き継ぐ。実機確認は依頼者が実施。

## 再開手順

1. `progress.md` の末尾を読み、直近の状態と次アクションを確認する。
2. `tasklist.md` で各パッチの状態 (未着手/rebase 済/書き戻し済) を確認する。
3. submodule 作業ブランチの状態を `git -C components/picoruby-esp32/picoruby status` 等で確認する。
