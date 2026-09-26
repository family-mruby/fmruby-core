# 実装指示書 P2b-2: 実機で確かめ、SPINEL_PIN を確定し、develop に入れる準備

対象: 実装担当のサブエージェント。前提: plan.md、report/p2c.md (7 節「P2b-2 に
入れるべきこと」)、report/p2b1.md。report は `report/p2b2.md` へ。

ユーザの決定 (2026-09-26): **P2b-2 を行い、上流を取り込んだものを develop で使う**。
develop へのマージは親が検収後に行う。サブはマージも push もしない。

## 作業場所と git

- fmruby-core の本体 checkout、作業ブランチ `feature/spinel-upstream`
  (親が切り替え済み)。コミットしてよい (自分が変えたファイルだけをパスで add)。
- `.env` はユーザの設定 (TAB5 向け、未コミット)。書き換えず、コミットに含めない。
  最後に `git diff .env` が
  `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/env_before_p2b2.diff`
  と一致することを確かめる。
- フォークは kishima/spinel の `fmrb-next` (`0b350247`、push 済み)。clone
  `/home/kishima/fmrb/wt/spinel-rebase` は読むだけ (修正が要ったら止まる)。
- `/home/kishima/fmrb/wt/spinel-pr/` (上流 PR のレビュー対応用) は触らない。
- 実機の操作は MCP の fmrb ツール (serial_start / serial_log / flash / tab5_*) を使う。
  **シリアルはセッションの最初に 1 回開いて開きっぱなし** (Tab5 は開くだけで
  リブートする。ルートの CLAUDE.md と各ツールの説明を先に読む)。
- sdkconfig / sdkconfig.defaults は編集禁止。

## T0: 前提の確認 (デバイス不要の部分から)

- 本体 checkout で `rake clean_all` (直前まで develop の build があった)。
- Tab5 が見えるか (`/dev/ttyACM*`、tab5_ip)。**見えなければデバイス不要の
  T1-1 / T4 を先に進め、T1-2 以降は親に「接続待ち」と返して止まる**
  (usbipd の attach は Windows 側の権限が要るのでユーザが行う)。

## T1: 基準を同じ板で測る

1. 基準のファームを作る: develop (`git worktree` を scratchpad に作ってそこで
   ビルドするか、ブランチを一時的に切り替える。**本体の作業ブランチの未コミットの
   変更を失わないこと**)。TAB5 で `rake build:esp32`。
2. **Tab5 の /home を退避する**: 書き込みの前に、tab5_fs で /home 以下を全部
   scratchpad の `p2b2/home_backup/` に取る (ファイル一覧とバイト数を report に残す)。
   /home が空ならそう書く。
3. 基準を**全体書き込み**で焼く (区画が 6M の develop 版)。ブート後、待機状態で
   2 分以上置き、次を採る:
   - `M1|` 行 (最後の定常値)、10 秒周期の `IRAM free:` (数回分)、`fmrb_task:` / `fmrb_app:` の周期ダンプ
   - 操作 (tab5_app / tab5_input): エディタを開いて 50 打鍵程度、閉じて開き直して打鍵、
     raycaster を 30 秒、fft_bench、アプリの起動と kill を数回
   - その間の `edit_lat` / `spx: hid_lat` / `GFX STATS` (`render_ms`) / raycaster の `cast`
   - `Guru|abort` の件数

## T2: 新しいファームを同じ板で測る

1. 作業ブランチ (区画 7M、fmrb-next の取り込み) で TAB5 を `rake clean_all` →
   `rake build:esp32` (まだ `SPINEL_DIR=/home/kishima/fmrb/wt/spinel-rebase` が必須)。
2. **全体書き込み** (区画が変わるので `flash:app` では足りない)。
3. T1-3 と**同じ手順・同じ待ち時間・同じ操作**で同じ項目を採る。
4. 期待値: 待機時の内蔵 RAM 空きは静的な差 (P4 で -6,812 バイトの使用 = 空きが
   その程度**増える**) 前後。ずれたら実行時の確保を疑って内訳を追う
   (report/p2c.md 7 節)。Guru は 0 件。
5. 速度の比較で退行があれば (目安: `edit_lat` や `render_ms` の中央値が 10% 以上
   悪化)、PSRAM に移した何が効いているかを切り分ける案を書く (直すのは親と
   相談してから。止まってよい)。

## T3: /home を戻す

T1-2 で退避した /home を tab5_fs で書き戻し、一覧とバイト数が一致することを確かめる。

## T4: SPINEL_PIN を確定する (デバイス不要)

1. `components/fmrb_spinel_rt/SPINEL_PIN` を `repo: https://github.com/kishima/spinel.git`、
   `branch: fmrb-next`、`commit: 0b350247b91f5c5c329152e7959a77566ae962ad` に。
2. `rake spinel:setup` が vendor/spinel をこの commit にすることを確かめる
   (vendor/spinel は gitignore の管理下のクローン。今は fmrb-dev を指している)。
3. `ruby components/fmrb_spinel_rt/import_from_fork.rb vendor/spinel` で
   スナップショットを取り直し、`IMPORT_INFO` の `fork_dir` が `vendor/spinel` に
   戻り、中身が P2c の取り込みと**同じ**であること (差分が IMPORT_INFO の
   パス欄だけであること) を確かめる。
4. **`SPINEL_DIR` を付けずに** `rake spinel:gen` と `rake build:linux` が通り、
   食い違いの警告が出ないこと。
5. 標準構成の sim で、P2b-1 と同じ操作 (起動、エディタの起動・打鍵・閉じる・
   再起動・打鍵、raycaster / spinel_hello / fft_bench、設定ダイアログ) が通ること。
6. 互換構成 (全部 mruby: `FMRB_KERNEL_ENGINE=mruby FMRB_APP_ENGINE_EDITOR=mruby` を
   環境変数で。`.env` は書き換えない。ただし .env の値が優先される場合は
   memory の注意どおりなので、その場合は一時ファイルで上書きする手を取らず、
   できなかったと書く) で sim が起動し、エディタで打鍵できること。
7. CI (`.github/workflows/`) が vendor/spinel や SPINEL_PIN をどう使うかを読み、
   新しい PIN で CI が壊れないかを確かめる (spinel-gen のジョブなど)。

## T5: S3 (任意)

S3 (Retro) がシリアルに見えていれば、作業ブランチの S3 ビルドを全体書き込みして、
ブートログで `Guru|abort` 0 件と `IRAM free:` を記録する (基準との比較ができれば
なおよい)。見えていなければ省いて report に書く。S3 は /home の退避手段が
シリアルにしか無いので、**S3 を焼く場合は /home が消えることを report の先頭に書く**。

## 止まる条件

- Tab5 が見えないとき (T0。デバイス不要の作業を終えてから)
- Guru / abort が出たとき: ログを保存し、状態を捨てる前にバックトレースを
  (シリアルの panic 出力から) 取って report に書いて止まる
- 速度の退行を切り分けるのに設計の判断が要るとき
- フォーク側の修正が要ると分かったとき

## report/p2b2.md に書くこと

- /home の退避と復元の記録
- T1 / T2 の比較表 (待機時の内蔵 RAM 空き、各遅延の中央値と最大、Guru 件数)
- T4 の結果 (PIN、スナップショットの一致、SPINEL_DIR 無しのビルド、sim、互換構成、CI)
- T5 の結果
- develop へのマージの前に親が知っておくべきこと (利用者側の手順: 区画変更による
  初回の全体書き込み、インストーラへの影響など)
- 見立てと違った点・撤回した仮説
- **PR 候補**節 (台帳は編集しない)
- `.env` が作業前と同じことの確認

## 受け入れ条件

- Tab5 で、基準と新の待機時内蔵 RAM 空き・遅延・Guru 件数が同じ板・同じ手順で表になっている
- 新のファームで Guru / abort が 0 件、待機時の内蔵 RAM 空きが基準以上
- /home が元に戻っている
- SPINEL_PIN が fmrb-next に確定し、`SPINEL_DIR` 無しでビルドと sim が通る
- `.env` が作業前と同じ
