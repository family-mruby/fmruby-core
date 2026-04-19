# TODO

## Dir.open のパス変換をHAL層で吸収する

現状、`File.open` はHAL層 (`fmrb_hal_file_open` の `build_path`) がプラットフォーム差 (Linux: `flash/` prefix, ESP32: `/flash/` prefix) を自動吸収するため、アプリ側でパス変換を意識する必要がない。

一方、`Dir.open` は mruby 標準ライブラリ経由で直接 OS の opendir を呼ぶため、HAL を通らない。そのためアプリ側で `to_os_dir_path` を使って明示的にプラットフォーム依存のプレフィックスを付加する必要がある。

Dir.open もHAL層を経由するようにして、File.open と同様に仮想パス (`/home/music/`) をそのまま渡せるようにすべき。

## Path長チェック

メッセージ構造体で送れるサイズで、パスの長さが決まる。
最大長を検討するとともに、フレームワーク側で、長すぎるパスを弾けるようにする。

## submodule ↔ lib/patch 整合性 CI チェック

fmruby-core は upstream PicoRuby を submodule として保持し、差分は `lib/patch/` (部分上書き) と `lib/replace/` (mrbgem 丸ごと差し替え) で管理している。`rake setup` が `cp -f` で適用する方式のため、submodule を直接編集しても成功し、`lib/patch/` に反映忘れがあっても検出されない (silent drift)。

実際に 2026-04-19 の監査で 2 件の漏れが見つかった (コミット `b5bebd3`, `b0ac57b` で収容):
- `picoruby-i2c/include/i2c.h` と `picoruby-i2c/src/mruby/i2c.c` (I2C#close 追加)
- `picoruby-mruby/lib/mruby/mrbgems/mruby-task/mrbgem.rake` (HAL auto-load 無効化)

### 実装方針

CI (GitHub Actions) で以下を実行:

1. `git submodule update --init --recursive` で submodule を pristine 状態に
2. `rake setup` を実行 (パッチ適用)
3. 各 submodule (ネストも含む) 内で `git diff --name-only` を取得
4. 変更ファイル一覧を lib/patch/ と lib/replace/ の対応ファイルと `diff -q` で突合
5. どれかに不一致があれば失敗

### マッピングルール

- `mrbgems/<gem>/<path>` → `lib/patch/<gem>/<path>` または `lib/replace/<gem>/<path>`
- `mrbgems/picoruby-mruby/lib/mruby/mrbgems/<gem>/<path>` → `lib/patch/picoruby-mruby/lib/mruby/mrbgems/<gem>/<path>`
- 例外: `mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-dir/mrbgem.rake` は `lib/patch/mruby-dir/mrbgem.rake` に置いている (Rakefile L152 参照)。Rakefile の cp 行をパースするか、例外許可リスト化する
- `mrbgems/mruby-compiler2/{include,src}/<file>` → `lib/patch/compiler/<file>` (命名規約が異なる。Rakefile L133-137 参照)

### 推奨実装

`rake check:patches` として Rakefile に追加。ローカル検証にも CI にも使える。`.github/workflows/` で `rake check:patches` を実行するジョブを追加する。

余裕があれば pre-commit フックにも組み込む (ローカルでの早期検出)。

### 関連

- 監査手順の原型: [../../tmp/picoruby_customization_summary.md](../../tmp/picoruby_customization_summary.md) §6.3
