# 作業計画 (P7.8: 入力の詰まり対策)

対象: Claude (計画・実装とも)。MIDI 由来だが対象はカーネル入力経路。
前提: [report/p7_6.md](report/p7_6.md) 6.2 (実測と機序、ユーザ指摘での訂正済み)。

## 0. 直すもの

実測済みの機序: アプリが数秒 busy の間に、30Hz に間引き済みの mouse move が
アプリの受信キュー (深さ 32) を ~1.1 秒で満たし、以後 **kernel の HID 転送が
1 件 100ms のタイムアウト待ちでブロック**して入力系全体が麻痺する
(`msg_send TIMEOUT` / `hid_event slow` の連発)。

対策は 2 点。**深さは保険、送信側の作法が本丸**。

## 1. 変更

### 1.1 キュー深さ 32 → 64

- `FMRB_USER_APP_MSG_QUEUE_LEN` / `FMRB_SYSTEM_APP_MSG_QUEUE_LEN`
  (fmrb_task_config.h) を 64 に。
- 記憶域は `heap_caps_malloc(len * item_size, MALLOC_CAP_SPIRAM)`
  (fmrb_msg.c) なので**自動で PSRAM 側が増えるだけ** (+~6.4KB/アプリ)。
  内蔵 RAM は不変。
- 効果: 30Hz の move に対し **~2.1 秒の busy を無傷で吸収**
  (実測済みの過渡 — GC ステップ 382ms 等 — をすべて覆う)。

### 1.2 kernel の move 転送を「待たずに捨てて最新をラッチ」

`input_router.rb` の mouse move 転送 (subtype 3) だけを変える:

- `_send_raw_message` (100ms ブロック) → `_try_send_raw_message` (timeout 0)。
  両カーネル (mruby / Spinel) に実装済み・リサイズプレビューで使用実績あり。
- **失敗したら最新 1 件をラッチ** (`@pending_move_pid` / `@pending_move_data`)。
  新しい move が来たら上書き = 合流。座標は絶対値なので中間の欠落は無害
  (p7_6.md 6.2 で確認済み)。
- **フラッシュは 2 か所**:
  1. `tick_process` (kernel ループの周期フック) — ユーザが動かし終えた
     最終位置が、次のループ周期で必ず届く (trailing 保証)。
  2. **ボタンイベントを同じ pid へ送る直前** — 古い move がボタンを
     追い越さない (順序保証)。入らなければ捨てる (ボタンは自前の座標を
     持つので欠落無害)。
- **ボタン・キー・ホイールは従来どおり** (順序と回数に意味があるため
  ブロッキング送信のまま。キュー 64 化で実質詰まらない)。

### 1.3 sim 用の再現アプリ

`flash/app/test/busy_input.app.rb`: on_update で意図的に数百 ms 眠り、
受け取った move の数と最後の座標をログする。sim で「busy なアプリ +
move の洪水」を再現し、修正の判定に使う。

## 2. 判定

sim (linux ビルド) で busy_input を前面にして move を大量注入:

1. `msg_send TIMEOUT` が **0 件** (従来は連発 — 実機ログで確認済みの症状)。
2. busy アプリの最後の受信座標が**注入した最終座標と一致** (ラッチの
   trailing が効いている)。
3. クリックは 1 件も欠けない。
4. 再生 (smf_player) を並走させて stalls が増えない。
5. 既存 174 テストに影響なし (ホストテストは対象外領域だが確認)。

実機: 再生中にマウスを乱打して stalls / `hid_event slow` が出ないこと
(ユーザ操作)。

## 3. 触らないもの

- host 層の 30Hz スロットル (既存のまま)。
- キー・ボタン・ホイールの転送方式。
- アプリ側 (FmrbApp) のイベント処理。
