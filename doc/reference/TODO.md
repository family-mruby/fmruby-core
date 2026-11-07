# TODO

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
- ランチャー再走査で system_desktop が落ちる (NARYA v4、断続、未解決): [launcher_rescan_desktop_stop.md](launcher_rescan_desktop_stop.md)

## VM の命令の境目の判定を整理して速くする (2026-11-03 記録、今は深追いしない)

mruby の `vm.c` の `RETURN_IF_TASK_STOPPED` (命令の境目ごとのタスク切り替えの判定) を
「2 回の読み込みと 1 回の分岐」に縮め、遅い側を 1 か所の関数に追い出すと、x86-64 で
実行した命令の数が 1.3〜4.7% 減った (valgrind で数えた値。別の実験の副産物。測り方と
数字は `vm_speed_measurement.md`)。`mrb_vm_exec` の中のレジスタの割り当てが楽になり、
`mrb` の読み直しと `regs` の退避が消えたため。

Family mruby の `lib/patch/picoruby-mruby/lib/mruby/src/vm.c` は同じマクロに自前の
時分割 (`TASK_SELF_TICK`、境目ごとに数を 1 つ減らす) を足しているので、ESP32 では
上流より効く可能性がある。ただし Xtensa・RISC-V で同じだけ減るかは未確認。

着手するときの順:

1. ホストで、Family mruby の `vm.c` (自前の patch 込み) に同じ整理を当てた版と元の版の
   命令数を valgrind で比べる (端末不要)。
2. 端末 (S3・P4) で時間を測る (実機を繋いだとき。数回測る)。
3. 効果があれば、上流の mruby への PR の候補にする (判定は上流の `d2a1c43a5` の所)。

## 使い勝手: 設定の画面 2 つ (2026-11-07 記録、NARYAv3 の Wi-Fi を設定し直したときにユーザが指摘)

- **Console の接続の設定 (tool/web の settings) が端末の状態に追いつかない**: 端末で「BLE Setup」を選んで受け付けを開いても、すでに BLE でつないでいたページは「The board is not accepting settings」のままで、「Read from board」が押せない。ページを読み込み直すと書けた。端末のログでは受け付けを開いた (`settings window open for 300 s`) あとにページがつながり、そのあと読み書きの要求が 1 つも来ていない。ページがつないだときの状態を持ったままで、受け付けが開いたことを知る手段 (通知か、押したときの問い合わせ) が無いと見られる。
- **デスクトップの設定の画面 (config_dialog) が Retro の画面に収まらない**: 項目が多く、下のボタンが画面の外に出て押せない (NARYAv3、320x240)。ページの切り替え (タブか、次へ・前へ) が要る。
