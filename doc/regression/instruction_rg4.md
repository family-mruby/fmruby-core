# RG4 指示書: 実機の起動ログを基準と比べる (コマンド 1 つで)

> 状態: 指示 | 更新: 2026-10-26 | plan.md の RG4。実機を焼いて起動ログを読み、crash マーカー・M1| の内蔵 RAM の段・周期ダンプの IRAM free・タスクの stack high-water を基準と比べるのを 1 コマンドにする。端末の一覧は `tools/regress/targets.rb` に寄せる。**今つないである実機は P4-Nano (NARYAv4) だけ** (2026-10-26 ユーザ: 「P4nano つないだのでこれ使って」)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- doc/regression/plan.md (方針)、usage.md (RG1・RG3b の入口と決まり)、report/rg3b.md (sim では M1| と周期ダンプが出ないので RG4 に回った経緯)。
- `tools/regress/targets.rb` (端末の一覧、`firmware`・`build`・`FIRMWARE_ENV`)、`tools/regress/size.rb`・`build_tree.rb` (ビルドの作り直しと build/ の退避・戻し)。
- シリアルと flash の既存の道具: `tools/fmrb_serial_capture.py`、MCP サーバ (`tools/mcp/lib/serial_manager.rb`、`tools/mcp/fmrb_mcp_server.rb` の flash と boot summary)。ルートの CLAUDE.md の「実機の検証」「ログの読み方」と、fmruby-core/doc/reference/internal_ram_budget.md (M1| の読み方)。
- 機種差: Tab5 はポートを開くだけでリブートする。P4-Nano・S3 (外付け USB-UART) は DTR/RTS でリセットでき、`--no-reset` で稼働中に付けられる。

## 2. 作るもの

1. **`rake regress:boot`** (名前は決めてよい): 端末 (`BOARD=naryav4` など、targets.rb の名前か FMRB_HW_TARGET) を指定して
   - (既定) RG3b と同じ固定のビルド設定 (`firmware_env`) でファームを作り、**app_only で焼く** (/home を消さない。全体の書き込みはしない)。`FLASH=0` で焼かずに、今入っているファームの起動だけ見る口も。
   - リセットして起動ログを決まった秒数 (周期ダンプが 2〜3 回出るまで、など) 取り、書き出す。
   - 起動ログから集める: crash マーカー (`Guru`・`abort`・`panic`・`assert` など、今の boot summary と同じ範囲)、リセット原因、`M1|` の各段 (internal・largest・psram)、周期ダンプの `IRAM free:`・`fmrb_task:` (各タスクの stack の空き、最後の値)・`fmrb_app:` (VM プール・ExcHW)、起動にかかった時間 (取れるなら)。
   - 基準 (`test/regress/boot_baseline.json`、端末ごと、git に入れる) と比べる。crash マーカーは 1 つでも落とす。M1| の段・IRAM free・stack の空きは悪い向きに幅を超えたら落とす (幅は同じファームで数回起動して決め、report に)。出力は端末ごとの短い表 + 落ちた項目だけ。全文のログはファイルの場所を出す。
   - build/ は size.rb と同じく退避して戻す。
   - `rake regress:boot:update` で基準を更新 (人が確認したときだけ、の決まりは同じ)。
2. **端末ごとのつなぎ方**を targets.rb に持たせる (VID:PID、リセットのしかた (DTR/RTS か、開くとリブートか)、ボーレート)。Tab5・NARYAv3・StickS3・AtomS3R の値も書いておく (確認できたのは P4-Nano だけ、と report に)。端末を足すときは 1 行で入る形。
3. **MCP の capture とぶつからないこと**: MCP の serial_start が同じポートを開いていたら、始める前に分かるように止まる (黙ってポートを奪わない)。ユーザのモニタも同じ。
4. **入口**: `rake regress:full` には入れない (実機がつながっているときだけ流す物なので)。usage.md に 8 章として追記。
5. **基準は今の develop (802b16a3 以降) で取る**。

## 3. 確認

- P4-Nano で `rake regress:boot BOARD=naryav4` が通り、表が出る。同じファームで 3 回起動して幅の中に収まる (揺れの実測を report に)。
- `FLASH=0` の口でも同じ表が出る。
- わざと壊す確認: ファームのコードは変えずにできる範囲で (例: 基準の値を一時的に書き換えて落ちることを見る、crash マーカーを含む作り物のログを解析部に食わせる試験を test/ に足す)。ファームに手を入れる確認はしない。
- 解析部 (ログ → 数) はホストの試験にし、`rake regress` (安い方) に入れてよい (実機は要らない)。
- 流した時間 (ビルド・焼く・起動ログ) と出力の行数を report に。
- 終わったら、P4-Nano には develop のファームが入ったまま、/home は消えていない (焼いたのが app_only だったこと) を report に。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の tools/regress/、test/regress/、test/ (解析部の試験)、rakelib/、doc/regression/usage.md。ルートの tools/ (シリアルの道具) は読むだけ。直す必要があれば止まる。
- **触らないもの**: sdkconfig (読むだけ)、`.env` の既定 (環境変数で渡す)、アプリ・gem・ファームのコード、VPS、`~/asterism_keys/`。
- **実機**: 使ってよいのは P4-Nano (`/dev/ttyACM0`、CH343 1a86:55d3) だけ。**全体の書き込み (`rake flash` / erase) はしない** (/home が消える)。app_only の書き込みは何度でもよいが、繰り返しの試しは最小限に (flash の摩耗)。HDMI の画面は見えないので、画面の確認は求めない。P4-Nano が /dev に見えなくなったら `rake attach` を自分で流してよい (bind 済み)。それでもだめなら止まる。
- Tab5・StickS3 など、つながっていない端末には触らない (値を書くだけ)。
- push・タグはしない (親がする)。`git commit -a` は使わない。件名 `tools:` `test:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc/regression の指示書・report はコミットしない (親がする)。usage.md はコミットしてよい。実在の値 (IP・端末名) は基準・コード・文書に入れない (起動ログに端末名や IP が出るなら、基準に書き出す前に伏せる)。
- **止まる条件**: P4-Nano が起動しない・crash する (今の develop のファームで。原因はファームの側なので追わずに状況を返す)、ポートが別の物に使われていて空かない、ルートの tools/ を直さないと進めない、M1| などの数字の揺れが大きすぎて比べられない (幅の決め方を選択肢で返す)。
- **report**: `doc/regression/report/rg4.md` (常体): 作り、P4-Nano の今の表、揺れの幅、流した時間、わざと壊した確認、端末を足すとき、残件、コミット。
