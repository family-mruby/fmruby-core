# 実装指示書 P1: エディタ本気モード (全画面 + 計測 + 差分描画 + sim 解像度連動)

対象: 実装担当セッション (Opus)。作業リポジトリ: fmruby-core。
背景と設計判断の根拠は同ディレクトリの plan.md を先に読むこと。
本指示書は plan.md の段階 1-3 に相当する。

## 進め方の約束

- fmruby-core/CLAUDE.md のルールに従う (submodule 直編集禁止、lib/add・
  lib/patch 経由、fmrb_log.h ラッパー、素の malloc 禁止、など)。
- 実装中の気づき・計測値・引継ぎ事項は doc/editor_serious_mode/report/p1.md
  に書く。この plan.md / 指示書には確定結果だけ反映する。
- タスクは T1 → T2 → T3 → T4 の順。T2 (計測) を T4 (差分描画) より先に
  入れるのは、前後比較の数字を取るため。各タスク完了ごとにコミットする
  (コミットメッセージは英語)。
- 検証は Linux sim (ルートの tools/dev_run_check.sh、fmrb_input.rb、
  fmrb_screenshot.py) を基本とし、実機確認が必要なものは report に
  「ユーザ確認待ち」と明記する。Tab5 は tools/fmrb_rd_input.rb /
  fmrb_rd_snap.rb で遠隔検証できる (ルート CLAUDE.md 参照)。
- 罠 (既知): lib/ を編集したら rake clean。rake build:linux は stale な
  esp32 build/ があると Xtensa のまま偽グリーンを出すので、検証前に
  `file build/fmruby-core.elf` で x86-64 を確認。sim は 3 コンテナ
  まとめて再起動する (core だけ再起動すると framebuffer が死ぬ)。
  .env の FMRB_HW_TARGET は環境変数指定を上書きするので注意。

## T1: エディタ全画面「本気モード」(Retro / Modern 両対応)

### 要件

- エディタを全画面で起動できるようにする。**通常の窓モードは残す**
  (本気モードは選択式)。Retro (320x240) / Modern (426x240) / Linux sim
  のすべてで動くこと。解像度変更は本タスクの範囲外。
- 全画面時は他アプリが suspend される既存挙動をそのまま使う。これが
  速度面の狙いでもある (T2 の計測で効果を数字にする)。

### 実装方針

1. spawner の組み込みテーブル (main/app/fmrb_app_spawner.c:114-133 付近の
   default/editor エントリ) を参考に、**同じ editor_irep を指す全画面版
   エントリ** (例: パス "default/editor_fs") を追加する。属性:
   fullscreen = true、fullscreen_switchable = true (Ctrl+Tab で park
   できるように)。large_memory は付けない (LARGE プールは排他 1 本で、
   多重 VM 計画と競合するため。大ファイル編集で必要になったら別途)。
   - 全画面時のコンテキストサイズは fmrb_app.c:1512-1516 が
     display サイズから設定する。エディタのレイアウトは on_resize →
     recompute_layout (editor.app.rb:176-194) が追随するので、エディタ側の
     ジオメトリ改修は原則不要のはず。崩れたらそこを直す。
2. 入り口を最低 1 つ用意する: shell の edit コマンド (main/prebuild_scripts/
   default_app/shell/shell_commands.rb の edit 実装) に -f オプションを足し、
   全画面版エントリを spawn する。エディタへのファイルパス引き渡しは
   既存の shell → editor の中継 (tick_pending_edit) をそのまま使う。
3. 実行時の「窓 ⇔ 全画面」トグルは**本タスクではやらない** (P4 の canvas が
   spawn 時サイズ固定のため。plan.md 3 章)。ただし Retro では技術的に
   可能なので、余力があれば調査結果だけ report に残す。

### 受け入れ条件

- Linux sim: editor_fs 起動でメニューバーからステータス行まで画面全体を
  使い、窓枠が無いこと (スクリーンショットで確認)。編集・保存・
  ハイライト・Ctrl+Tab park/復帰が窓モードと同等に動くこと。
- 窓モードの従来動作に退行が無いこと。
- esp32 ビルド (S3 / P4 両方) が通ること。実機の見た目確認は
  ユーザ確認待ちで良い。

## T2: 計測の整備 (前後比較の物差し)

### 要件

以下の 2 つの数字を取れるようにし、T1 時点 (全面再描画のまま) の
基準値を report に記録する:

1. **打鍵から present までの遅延**: エディタ側 (Ruby) で、キーイベント
   受領から redraw_all 完了 (present 発行) までの時間を計測し、
   一定件数ごとに集計をログへ出す (mean / max / 25ms 超の件数程度)。
   常時オンでもコストが無視できる作りにする (計測自体でゴミを出さない
   よう、フレーム間引きや整数集計にする)。
2. **1 フレームの描画コスト**: present 1 回あたりの所要時間。P4 は
   display_p4_task の render 経路、Retro/sim は graphics-audio 側。
   最低限、Linux sim と S3 で取れること。既存の GFX STATS
   (main/kernel/host/host_task.c:799-819 の cmds/s・presents/s) への
   追加でも、別ログでも良い。

### 記録すること (report/p1.md)

- 窓モード vs 全画面モードの比較 (全画面 suspend の効果測定)。
- 小ファイル (1KB 未満、ハイライト有効) と大ファイル (10KB 程度、
  ハイライト自動オフ) の両方。
- 測定条件 (ターゲット、ファイル、操作内容) を必ず添える。

## T3: Linux sim の解像度を .env のターゲットに連動

### 要件

FMRB_HW_TARGET が P4 系 (TAB5 / NARYAv4) のとき、Linux sim を 426x240 で
起動し、Modern の UI 確認を sim で行えるようにする。S3 系 (NARYAv3) の
ときは従来どおり 320x240。

### 実装方針

1. **先に手検証**: config/system_conf_linux.toml の display_width/height を
   手で 426x240 に変えて sim を起動し、sdl2-display のウィンドウ・
   fmrb_screenshot.py・fmrb_input.rb の座標系が追随するかを確認する。
   表示経路は INIT_DISPLAY 駆動で fb は動的確保のため追随する見込みだが、
   sdl2-display 側に固定値があれば直す (fmruby-graphics-audio 側の変更に
   なった場合は docker compose build sdl2-display が必要)。結果を report へ。
2. 追随が確認できたら config/system_conf_linux_p4.toml を追加
   (426x240。壁紙等 Modern 相当にしたければ system_conf_p4.toml を参考に。
   sync_files のアセットが Retro 用と異なる場合はそちらも)。
3. Rakefile の linux 向けコピー箇所 (Rakefile:688-691) で HW_TARGET
   (Rakefile:93 で取得済み) が P4 系ならそちらをコピーする分岐を入れる。
4. ルートの CLAUDE.md の「座標はフレームバッファ座標 (320x240)」の記述に
   ターゲット連動の注記を足す。

### 受け入れ条件

- FMRB_HW_TARGET=TAB5 で rake build:linux → dev_run_check.sh すると
  426x240 の画面 PNG が取れること。NARYAv3 では従来どおり 320x240。
- fmrb_input.rb のクリック座標が 426x240 系で正しく効くこと
  (メニュー操作 → スクリーンショットで確認)。

## T4: 差分描画 (dirty-line) + 全文 join の撤廃

### 要件

打鍵ごとの全面再描画をやめ、変更のあった行だけ再描画する。目標は
T2 の計測で **打鍵→present の p99 < 33ms、25ms 超ゼロ** (sim と S3)。
併せてハイライト経路の全文 String 再構築 (@lines.join("\n")) を無くし、
ゴミ発生量を減らす (GC 起因の引っかかり対策。plan.md 7 章)。

### 実装方針

1. EditorApp に dirty 管理を入れる: 行編集 → その行だけ、行挿入/削除・
   スクロール・折返し境界の変化・選択範囲の変化 → 影響範囲 (画面全体に
   フォールバックして良い)。カーソル移動だけならカーソル行 (旧位置と
   新位置) のみ。redraw_all は「dirty な行の描画 + ステータス行 + present」
   に変える。メニュー/ペインは従来どおりイベント時のみ。
2. ハイライト: 現状の「全文 join → tokenize」をやめ、行単位の増分
   tokenize にする。SyntaxHighlight.tokenize (C 実装) が行単位入力で
   使えるか確認し、複数行にまたがる構文 (複数行文字列など) の色が
   多少崩れるのは v1 では許容する (report に既知差分として記録)。
   行単位化が難しい場合は「join を避けて全文バッファを使い回す」だけでも
   ゴミ削減効果はあるので、段階的で良い。
3. HL_AUTO_LIMIT_BYTES (editor.app.rb:63) は**まだ消さない**。T4 完了後の
   計測で 10KB ファイルでもハイライトが目標内に収まるなら、閾値を
   引き上げるか撤廃する (report に判断材料を書く)。

### 受け入れ条件

- T2 の計測で改善が数字で示せること (T1 基準値との比較表を report に)。
- 表示の正しさ: スクロール・選択・検索ハイライト・IME なし入力・
  デバッガペイン表示 (Retro 下分割) で描画残り/欠けが無いこと。
  sim のスクリーンショットで代表ケースを確認し report に貼る (パスを書く)。
- 窓モード / 全画面モードの両方で動くこと。

## 範囲外 (やらない)

- 解像度引き上げ (640x360) — plan.md 段階 5。計測器 (T2) の数字が
  出てから別途判断する。
- editor-gem 分割・Spinel 化 — plan.md 段階 4/6。
- FMRB::Debug まわりの変更。
- 実行時の窓 ⇔ 全画面トグル (P4 の canvas 再確保が前提)。

## 完了報告

report/p1.md に: 実施内容、計測値 (T1 基準 / T4 後)、既知差分、
ユーザ確認待ち項目 (実機の見た目・操作感)、次段階 (editor-gem 分割) への
引継ぎ事項。
