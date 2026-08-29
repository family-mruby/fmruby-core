# タスク優先度の全体設計

fmrb_app_kill 到達不能問題 (doc/archive/app_kill_fix/) の診断で、ゲストアプリの
ビジーループが低優先度タスクを飢餓させることが実証されたのを機に、
タスク優先度を全数調査して設計として整理する。本書が優先度の正であり、
変更はここを更新してから行う。

## 現状の全数表 (2026-08-01 調査)

### fmrb_task_config.h で管理されているもの

| 優先度 | タスク | core | 備考 |
|---|---|---|---|
| 10 | HOST | 0 | GFX/音声の転送ポンプ |
| 9 | KERNEL | 1 | |
| 8 | SYSTEM_APP (desktop/overlay) | 1 | |
| 6 | HW_PROXY | 0 | PSRAM タスクのファイル I/O 代行 |
| 6 | M5GFX 受信 (link local) | 0 | ATOM 構成 |
| 5 | SHELL / **USER_APP (ゲスト)** | 1 | |
| 5 | USB_HOST / USB_HID | 0 | |
| 5 | SPI_CONN | **1** | ヘッダの core 方針コメントと不一致 |
| 5 | I2C_KBD | 0 | ATOM 構成 |
| 4 | BLE_FS | 0 | |
| 3 | RTC | **1** | 方針コメントと不一致 |
| 3 | DEBUGD | 非固定 | 唯一の kill 経路 |
| 2 | STATUS_LED | **1** | 方針コメントと不一致 |

### ヘッダの外にハードコードされていたもの (2026-08-01 に fmrb_task_config.h へ等値移動済み)

| 優先度 | タスク | core | 定義場所 |
|---|---|---|---|
| 6 | audio_p4 | 0 | main/drivers/audio_p4/audio_p4_task.c |
| 5 | touch (Tab5) | 1 | main/drivers/touch/touch_task.c |
| 5 | tab5_kbd | - | main/drivers/tab5_keyboard/tab5_keyboard.c |
| 5 | usb_rx (Linux) / posix link (Linux) | - | usb_task_linux.c / fmrb_hal_link_posix.c |
| 4 | rd_stream / rd_start / rd_mjpeg | - | main/drivers/remote_desktop/ |

### ESP-IDF / FreeRTOS が起こすタスク (fmrb 側では定義しない)

| 優先度 | タスク | core | 対象 | 性質 |
|---|---|---|---|---|
| 24 | ipc0 / ipc1 | 各 core | esp32 | クロスコア呼び出し・flash 操作。マイクロ秒単位のバースト |
| ~23 | BT controller | sdkconfig | S3 のみ (P4 は BT_CONTROLLER_DISABLED) | 無線タイミング厳守 |
| 22 | esp_timer | 0 | 全 | esp_timer コールバックの配送 |
| 4 | **nimble_host** | 1 (sdkconfig) | S3 | **NimBLE ポート既定値。fmrb 側からは動かせない前提で扱う** |
| 1 | main (app_main) | 0 | 全 | 初期化後は 100 秒周期スリープの休眠 |
| 1 | Tmr Svc (FreeRTOS タイマデーモン) | 0 | 全 | fmruby-core はソフトタイマ未使用のため常時ブロック |
| 0 | idle0 / idle1 | 各 core | 全 | |

## 見つかった問題

1. **ゲスト (5) が下の段を飢餓させる** (実証済み)。debugd(3) / RTC(3) /
   nimble_host(4) / LED(2) はゲストのビジーループ中に走れない。Linux sim
   (単核) では全滅、実機 (双核) でも core 1 固定の RTC / LED / nimble_host /
   touch は同罪。非固定の debugd だけは実機なら core 0 に逃げられる —
   **sim と実機で症状が変わる**構図。
2. **ゲスト (5) が入力ドライバ (5) と同格**。ラウンドロビンで取り分を奪う。
3. **kill の唯一の経路 (debugd) が殺す相手より低い** (app_kill_fix Phase 1)。
4. **core 方針コメントと実際の pin が不一致**。RTC / LED / SPI_CONN / touch /
   nimble_host が core 1 に居り、「HW 系は core 0 / VM 系は core 1」の宣言と
   矛盾。core 1 に VM と HW 系が混在している。
5. **優先度の定義が散在**。ヘッダの「集中管理」宣言に反し 8 箇所に
   ハードコードがある。

## 設計原則

1. **段モデル**: 数字は個別調整ではなく「役割の段」で決める。段内は同値
   ラウンドロビン。
2. **fmrb のタスクは優先度 1-10 に収める**。IDF システムタスク (22-24 帯) の
   下に置き、無線・タイマ・IPC は常に fmrb より先に走る。10 より上は使わない。
3. **ゲストはすべてのシステムタスクより下**。ゲストの無限ループは仕様上
   あり得る入力で、その耐性は優先度の構造で持つ。
4. **外部で決まる値は錨として扱う**: nimble_host (4) と Tmr Svc / main (1) は
   fmrb 側から動かせない (sdkconfig は編集禁止・提案のみのため)。段割りは
   これらと衝突しない形にする。

## 段割り (目標)

| 段 | 優先度 | 役割 | 所属 |
|---|---|---|---|
| リアルタイム | 10-8 | 落とすと絵・音・OS が壊れる | HOST(10) / KERNEL(9) / DESKTOP(8) |
| (予備) | 7 | 将来のリアルタイム系 | - |
| ストリーミング | 6 | DMA・転送の途切れが音や表示に出る | audio_p4 / HW_PROXY / M5GFX |
| 入力ドライバ | 5 | 人間の操作の取りこぼし禁止 | USB / I2C_KBD / touch / tab5_kbd / SPI_CONN / (Linux: usb_rx, posix link) |
| 通信サービス | 4 | 遅延許容だが停止不可 | nimble_host (錨) / BLE_FS / remote_desktop |
| 制御・死活 | 3 | ゲストの状態に依らず動く義務 | DEBUGD / RTC / **STATUS_LED (2 から移動)** |
| **ゲスト** | **2** | 何をしても上の段を害せない | USER_APP / SHELL (5 から移動) |
| **予備** | **1** | 将来の「ゲストより下」枠 | (現住: 休眠中の Tmr Svc / main のみ) |
| idle | 0 | | idle |

### ゲスト直下に予備段 (1) を置く理由

- 将来「background ウィンドウモードのアプリを前面ゲストより下げる」
  「バッチ的なゲスト作業」のような **前面ゲストにすら譲るべき実行物**の
  置き場になる。段を後から挿すのは全体renumber になるので、先に空けておく。
- 予備段には現状、休眠中の Tmr Svc と main (100 秒周期スリープ) しか居らず、
  実害はない。**将来この段を実際に使うときの注意**: そこに置いたタスクは
  Tmr Svc と同格になるので、その時点でソフトタイマの利用有無を再確認し、
  必要なら CONFIG_FREERTOS_TIMER_TASK_PRIORITY の引き上げを提案する
  (sdkconfig は編集禁止のため提案扱い)。

### STATUS_LED を 3 (制御・死活の段) へ移す理由

ゲストを 2 に置くため、LED は「ゲストより上」を保って 3 へ 1 段上げる。
同段の DEBUGD / RTC はいずれもほぼ休眠のタスクで競合しない。
「点滅 = OS 生存」「完全停止 = 真のハング」の意味は不変。
「CPU 余力の表示」は優先度でなく点滅パターンで行う: idle フックのカウンタを
LED タスクがサンプリングし、進んでいれば通常心拍・止まっていれば別パターン。
これで 3 状態 (生存+余力 / 生存+飽和 / 真のハング) が 1 個の LED で出る。
LED を最低優先度にする案は、WDT の IDLE 監視無効化時に決めた「真のハングは
status LED で検知」という役割を壊す (重いアプリのたびにハングと同じ見た目に
なる) ため不採用。

## 変更内容まとめ

1. **FMRB_USER_APP_PRIORITY / FMRB_SHELL_APP_PRIORITY: 5 -> 2**
   -> **実施済み (2026-08-02、app_kill_fix Phase 2)**。
2. **FMRB_STATUS_LED_TASK_PRIORITY: 2 -> 3**
   -> **実施済み (2026-08-02、同上)**。あわせて「CPU 余力の表示」を
   点滅パターンで行う実装 (idle フックのカウンタを LED タスクが
   サンプリング) も入れた。sdkconfig の変更は不要だった。
   実測と実機確認の依頼は doc/archive/app_kill_fix/report.md の Phase 2 節。
3. **ハードコード 8 箇所を fmrb_task_config.h へ移す** (値は等値移動)。
   nimble_host は移せない (ポート既定) ので、ヘッダに参照コメントとして併記。
   -> **実施済み (2026-08-01、優先度変更とは独立の 1 コミット)**。
4. 数値変更は上記 1-2 のみ。他タスクは全員が段割りに既に適合しており動かさない。
5. **core pin の不一致は今回は現状維持で記録のみ**。意図的 (I2C バス等) かの
   履歴確認が要るため、優先度変更と混ぜない。見直すなら別作業。

## 期待される効果

- ゲストのビジーループ中でも: kill (debugd) が効く、RTC が時を刻む、BLE が
  切れない、LED が心拍を打つ、入力ドライバが全速で動く。
- sim (単核) と実機 (双核) で飢餓の構図が一致する。
- HOST/KERNEL/DESKTOP との相対は不変。描画経路 (フローセマフォ律速) への
  影響は原理上ない。アイドル時のアプリ性能も競合が無い限り不変。

## 検証 (app_kill_fix Phase 2 と共通)

- 優先度値への暗黙依存を grep で洗ってから変更する (特に「5」を前提にした
  比較・表示が無いか)。
- 変更前後で BASIC ベンチと「重い描画中の入力追従」(doc/archive/gfx_unification の
  Phase A の手法) を同一条件で計測し、数字で比較する。
- Lua ビジーループの kill 再現ケースが協調終了すること。C 内で無限ブロック
  する模擬ケースで強制経路が実走し、資源回収・Python 排他解放・再起動まで
  通ること。
- ビジーループ中に LED の心拍が続き、パターンが「飽和」に変わること
  (パターン実装を入れる場合)。
- 4 言語デモ + Spinel デスクトップ構成の headless 回帰。
- 実機: S3 で同再現ケース + 音 (NSF プレイヤ等) の途切れ確認はユーザ。
  双核での挙動差 (debugd が core 0 に逃げる件) が消えたことも実機ログで見る。
