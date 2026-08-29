# fmrb_app_kill 到達不能問題 実装レポート

作業計画は [README.md](README.md)。

## Phase 1: 診断 (2026-08-01)

### 結論

**容疑 1 (優先度飢餓) が原因。ただし想定より 1 段手前で効いている。**

debugd タスクは優先度 3、ゲストアプリは優先度 5。FreeRTOS は厳密な
優先度スケジューリングなので、ビジーループするアプリは常に ready のまま
であり、**debugd は CPU をもらえない**。その結果 debugd は
**ソケットからコマンドを読むことすらできず**、`fmrb_app_kill` は
一度も呼ばれない。`should_exit` も立たない。

つまり「kill が固まる」のではなく「**kill が始まらない**」。猶予ループも
強制経路も、そこへ到達する前の段階で止まっている。

### 確定に使った証拠

**1. `ps` も同じく応答しない**

kill と無関係な `ps` コマンドも、ビジーアプリの実行中は同じくタイムアウト
する (2 回試行、いずれも "no response after 3 attempts")。kill 固有の問題
ではなく debugd 自体が動いていないことが、これで確定する。

**2. gdb の全スレッドバックトレース (freeze 状態)**

debugd (LWP 108) は kill の中ではなく、次のコマンド待ちのところに居た:

```
#9  vTaskDelay ()
#10 select (fd=7, ...) at FreeRTOSSimulator_wrappers.c:93
#11 tcp_poll () at main/drivers/debug/fmrb_debug_transport_tcp.c:113
#12 debugd_main () at main/drivers/debug/fmrb_debugd.c:379
```

他のタスクはすべて正常に動作中で、詰まっているものは無かった:

| スレッド | タスク | 位置 |
|---|---|---|
| LWP 106 | host_task (prio 10) | fmrb_msg_receive 待ち |
| LWP 107 | kernel / Spinel (prio 9) | drain_messages |
| LWP 109 | system_desktop (prio 8) | mrb_fmrb_app_spin |
| LWP 108 | debugd (prio 3) | tcp_poll (**動けていない**) |
| LWP 119 | Wedge = ビジーアプリ (prio 5) | lua_exit_check_hook 内 |

優先度 5 より上のタスク (host / kernel / desktop) はすべて生きている。
実際 freeze 中も transport の統計ログは出続けていた。止まっているのは
優先度 5 未満のものだけ、という切り分けと一致する。

**3. ビジーアプリ自身は正常に exit フックを回している**

```
#5  fmrb_app_poll_exit_signal (fmrb_app.c:76)
#7  lua_exit_check_hook (fmrb_app.c:692)
#8  luaD_hook / #9 luaG_traceexec / #10 luaV_execute
```

`while true do end` の最中でも Lua VM フックは毎命令走っており、
`should_exit` を見に行っている。アプリ側は壊れていない。

**4. gdb で `should_exit` を確認 -> false**

```
$1 = "Wedge"   $2 = PROC_STATE_RUNNING   $3 = false   $4 = 5 (uxPriority)
```

kill が一度も実行されていないことの直接証拠。

**5. gdb で `should_exit` を手動で立てると即座に協調終了する**

```
set var g_ctx_pool[4].should_exit = 1
-> I (…) fmrb_app: [Wedge gen=1] State: STOPPING -> FREE
-> I (…) fmrb_app: [Wedge gen=1] Reaped
```

フラグさえ立てば、ビジーループ中でも猶予内に協調終了する。
`fmrb_app_kill` の猶予ループも強制経路も、**そもそも直す必要がない**。

### kill の呼び出し経路 (計画ステップ 2 の回答)

**現状、kill の経路は debugd 1 本しかない。**

- debugd: `DBG_CMD_KILL` -> `fmrb_app_kill` を **debugd タスク上** で実行
  (fmrb_debugd.c:142)。優先度 3。
- カーネル経由: `fmrb_kernel.rb` の APP_CONTROL `"kill"` は
  `Log.info("... (not implemented)")` の TODO で、**未実装**。
- タスクモニタ (monitor.app.rb) に kill の UI は無い。

つまり**唯一の kill 手段が、殺す対象より低い優先度のタスクで動いている**。

### 優先度表 (計画ステップ 3 の回答)

| 優先度 | タスク |
|---|---|
| 10 | HOST |
| 9 | KERNEL |
| 8 | SYSTEM_APP (デスクトップ) |
| 6 | HW_PROXY / M5GFX |
| **5** | **USER_APP / SHELL** / USB_HOST / USB_HID / SPI_CONN / I2C_KBD |
| 4 | BLE / BLE_FS |
| 3 | RTC / **DEBUGD** |
| 2 | STATUS_LED |

ビジーアプリ (5) に飢えるのは DEBUGD(3) / RTC(3) / BLE(4) / STATUS_LED(2)。
同値 (5) の USB / SPI / I2C_KBD はラウンドロビンで動くが取り分は減る。

### 経路別の切り分け (計画ステップ 4 の回答)

閉じるボタン経由は「アプリ自身が自分を終わらせる」動作なので、この問題とは
無関係に動く (Phase A-C の検証で確認済み)。カーネル経由の kill は未実装
なので比較対象が存在しない。**現象は debugd 経路固有**だが、それは
debugd が唯一の経路だからであって、優先度 5 未満のタスクに kill を
担わせれば同じことが起きる。

## Phase 2 の方針案 (ユーザ確認待ち)

直すべきは「**ゲストのビジーループがシステムタスクを飢えさせられる**」
という優先度設計。案は 2 つ、併用も可。

### 案 A: ゲストアプリの優先度をシステムタスクより下げる (推奨)

`FMRB_USER_APP_PRIORITY` / `FMRB_SHELL_APP_PRIORITY` を 5 から
STATUS_LED(2) より下 (例: 1) へ。

- 長所: 計画の「飢餓耐性は優先度設計で持つのが筋」に沿う。debugd だけ
  でなく RTC / BLE / LED も守られる。将来どのタスクが kill を担っても
  安全。ゲストコードが最低優先度なのは素直な設計。
- 短所/確認したい点: アプリが BLE / RTC / LED / debugd に譲るようになる。
  これらは普段ブロックしているので実害は小さい見込みだが、**描画と音の
  レイテンシへの影響はユーザ確認が要る**。HOST(10) / KERNEL(9) との
  相対関係は変わらないので、GFX 送出やリンクの帯域には影響しないはず。

### 案 B: debugd をアプリより上げる (最小)

`FMRB_DEBUGD_TASK_PRIORITY` を 3 から 6 へ。

- 長所: 変更が 1 行で、影響範囲が debugd に閉じる。デバッガは対象を
  観測・介入する立場なので、対象より高い優先度は妥当。
- 短所: 今回の症状しか直らない。RTC / BLE / LED の飢餓は残り、
  「kill を担うタスクはアプリより上でなければならない」という暗黙の
  制約も残る。

### 補足: カーネル経由 kill の実装 (別途)

タスクモニタから kill できるようにするなら、カーネル (優先度 9) で
実行されるので優先度問題を最初から踏まない。UI が要るなら別タスクとして
提案する。

### Phase 2 完了条件への見通し

- 「再現ケースが協調終了で猶予内に死ぬ」は、上記どちらの案でも満たせる
  見込み (証拠 5 で実証済み: フラグさえ立てば死ぬ)。
- 「協調終了が効かないケースで強制経路が動く」は、C バインディング内で
  無限ブロックする一時テストコードが別途必要。VM フックのある言語では
  再現しないため。

## Phase 2: タスク優先度の変更 (2026-08-02)

### 変更

`fmrb_task_config.h` の 3 行のみ (doc/reference/task_priority.md の段割りに従う)。

| 定数 | 変更 |
|---|---|
| FMRB_USER_APP_PRIORITY | 5 -> 2 |
| FMRB_SHELL_APP_PRIORITY | 5 -> 2 |
| FMRB_STATUS_LED_TASK_PRIORITY | 2 -> 3 |

優先度 1 は予備段として空けたまま。

### 暗黙依存の調査 (変更前)

優先度の数値を前提にした比較・分岐は**無かった**。

- コード中に優先度どうしの比較は 1 件も無い。値は生成時に渡すだけ。
- 唯一の読み出し口 `fmrb_task_get_status_list()` (fmrb_task.c) は
  `info->priority` を詰めるが、**呼び出し元が存在しない**。表示にも
  判定にも使われていない。
- `fmrb_app.c:989` は自タスクの優先度をログに出すだけ。
- ドキュメントで数値に言及しているのは doc/reference/boot_performance.md の
  「カーネルタスク (prio 9)」のみで、今回変えない値。
  (BASIC 仕様書の「優先度=0」はスプライトの話で無関係)

### 判定

| 条件 | 結果 |
|---|---|
| ビジーループ Lua が協調終了する | OK: kill が **0.09s** で完了、`Exited on request` |
| ビジーループ中に debugd が応答する | OK: `ps` が即応 (変更前は 3 回試行して全滅) |
| 強制経路の実走 | OK: **初めて到達**。下記 |
| 強制後に資源回収と再起動 | OK: canvas 削除 -> 同アプリを再 spawn 成功 |
| Python 排他の解放 | OK: 下記 |
| 4 言語デモの回帰 | OK: 4 本とも起動・描画・終了、canvas は作成 27 / 削除 25 (差 2 = デスクトップの window + 背景) |
| Spinel デスクトップ構成 | OK: 起動と Shapes の起動・終了 |

**強制経路の初実走** (C 側で無限ブロックする一時 Lua アプリ):

```
W No response in 1000ms, forcing termination
I State: RUNNING -> STOPPING / Closing Lua VM
I app_canvas: [CBlock] Deleted window canvas 4
W Killed by force
-> 直後に同じアプリを re-spawn 成功 (gen=3)
```

kill の所要は 1.08s (猶予 1000ms + 後始末)。協調終了の 0.09s と明確に
区別できる。

**Python 排他の解放** (一時的な C ブロックを fmrb_mp_exec に仕込んで検証、
コミットしない):

```
PyBlock spawn -> 2 本目の Python は "already running" で拒否 (排他が効いている)
kill PyBlock -> 1.08s, "No response in 1000ms, forcing termination" / "Killed by force"
-> 直後に python.app.py が起動 ("Python demo started on linux")
```

`force_release_resources` -> `destroy_vm` -> `fmrb_mp_close` の順で
`s_owner` が NULL に戻るため、強制経路でも排他は解放される。

### 性能の前後比較 (同一条件、Linux sim)

| 指標 | 変更前 | 変更後 |
|---|---|---|
| bench_01_loop (中央値) | 0.061s (n=3) | **0.059s** (n=7) |
| bench_04_tick (中央値) | 10.049s (n=3) | 10.06s (n=7) |
| 重い描画中の入力追従 | 8/8 (350ms 以内) | **8/8** (350ms 以内) |

- bench_01 (インタプリタ純度) は誤差内で不変。競合が無いときのゲスト性能は
  優先度を下げても変わらない、という予想どおり。
- bench_04 は**変更前後とも 10.05s と 11.87s の二峰**になる
  (変更後 7 本: 低 4 / 高 3、変更前 3 本: 低 2 / 高 1)。両条件で同じ 2 値に
  割れるので、この指標は優先度変更を区別しない。フレーム歩調の起動位相に
  依存すると見られる。中央値の上下は標本の割れ方の差でしかない。
- 入力追従は変更前後とも満点。もともと HOST(10) が上位で律速していないため。

### LED 心拍パターン

`status_led.c` に実装した。3 状態:

| 状態 | 緑 LED |
|---|---|
| 生存 + CPU 余力あり | 1.9s 点灯 / 0.1s 消灯 (従来の心拍、不変) |
| 生存 + CPU 飽和 | 0.5s 点灯 / 0.5s 消灯 を 2 回 (2s 周期は同じ) |
| 真のハング | 変化なし (不変) |

- idle フックのカウンタ (`esp_register_freertos_idle_hook`) を 2s 周期の
  先頭で 1 回サンプリングし、前回と同値なら飽和と判定する。フックは
  `return true` で tick ごとの呼び出しにしてある (2s のサンプルには十分)。
- 周期は両パターンとも 20 tick = 2s に固定した。赤 LED のエラーパターンは
  100ms tick ごとに進むので、緑のパターンが変わっても赤の歩調は変わらない。
- **sdkconfig の変更は不要だった**。`CONFIG_FREERTOS_USE_IDLE_HOOK` は
  古典的な `vApplicationIdleHook` 用で、IDF の
  `esp_register_freertos_idle_hook()` はそれとは独立に使える。
- **Linux シミュレーションでは動作を確認できない**。linux ターゲットの
  esp_system は `freertos_hooks.c` をビルドしないため API 自体が無い
  (IDF の esp_system/CMakeLists.txt が linux で早期 return する)。
  `#ifndef CONFIG_IDF_TARGET_LINUX` で囲み、sim では従来どおり通常心拍に
  固定してある。**飽和パターンの実挙動は実機確認が必要** (下記依頼)。
- esp32s3 でビルドが通ることは確認済み (2,448,496 B)。

### 実機 (ESP32-S3) でお願いしたい確認

1. **kill 再現**: canvas を作ってから `while true do end` する Lua アプリを
   一時的に置き、debugd (`python3 tool/debug/fmrb_dbg_client.py <host>:5555
   kill pid=N`) から kill して協調終了すること。ビジーループ中に `ps` が
   応答することも。
2. **LED**: 上記ビジーループ中に緑 LED が **0.5s 点滅**に変わり、アプリ終了で
   1.9s 点灯の通常心拍に戻ること。ハング時に消灯しないこと。
3. **音の途切れ**: NSF プレイヤ等で、ゲスト優先度を下げたことによる
   音の途切れ・詰まりが無いこと (sim では確認できない)。
4. **双核の挙動差**: 変更前は debugd が core 0 に逃げられるぶん実機では
   sim ほど酷くならない構図だった。変更後は core 1 固定の RTC / LED /
   touch も飢えないはずなので、ゲストのビジーループ中に時計が進み続け、
   LED が打ち続けることをログ・目視で。

### 気づき

1. **INIT 状態のアプリは kill できない**。`fmrb_app_kill` は
   RUNNING / SUSPENDED しか受け付けないため、VM 生成中 (create_vm_*) に
   固まったアプリは `Cannot kill app in state INIT` で拒否される。Python の
   場合は排他を握ったまま INIT で固まると、**再起動まで Python 全体が
   起動不能**になる。今回の検証で一時ブロックを `fmrb_mp_start` に入れた
   ときに実際に踏んだ。Phase 3 の検討対象として申し送る。
2. 強制経路の到達は「猶予 1000ms + 後始末」で kill 呼び出しが 1.08s。
   協調終了の 0.09s と一桁違うので、ログを見なくても所要時間で
   どちらの経路を通ったか判別できる。

### 実機確認の結果 (ESP32-S3, 2026-08-02)

依頼した 4 項目すべて OK (ユーザ報告)。操作は Web コンソールの Debug
パネル (tool/web、この確認のために追加した ps / kill / spawn UI) から実施。

| 項目 | 結果 |
|---|---|
| ビジーループ Lua (busy.app.lua) の spawn -> kill -> 再 spawn | OK。kill は協調終了で即時 |
| ビジーループ中の ps 応答 | OK (Phase 1 で全滅していた症状の完治) |
| LED パターン | OK。ビジー中 0.5s 点滅 (飽和) に変わり、終了で 1.9s 心拍に復帰 |
| 音 (NSF プレイヤ) | OK。途切れ・詰まりなし |
| ビジー中の時計・入力 | OK。時計は進み続け、マウスも通常応答 |
| 閉じるボタン経由の停止 (カーネル経路) | OK |
| Web コンソール既存機能 (ファイル/ログ) の回帰 | OK。デバッグサービス相乗りの影響なし |

これで Phase 2 は実機まで完了。残るは Phase 3 (強制経路の安全化) のみ。

## Phase 3a: 危険ウィンドウの全数調査 (2026-08-02、実装なし)

### 調査範囲と方法

ゲストタスクが実行し得る C を対象にした: 5 バインディング
(mruby `ports/esp32/*.c` / Spinel `fmrb_spx_*.c` / Python
`fmrb_bridge.c` + `fmrb_mp.c` / Lua `fmrb_lua_gfx.c` / BASIC
`fmrb_basic_gfx.c`) と、その先の `fmrb_gfx` / `fmrb_msg` /
`fmrb_transport` / `fmrb_hal`。`fmrb_semaphore_take|give|create`、
`fmrb_malloc|fmrb_sys_malloc`、`fmrb_hal_file_open`、
`fmrb_transport_send_sync` の全出現を追い、取得から返却までの区間を
数えた。

### 分類の判断基準

- **分類 1 (削除安全)**: 資源を持たずにブロックしているだけ。または
  持っている資源が**自動で回収される**もの。補償不要。
- **分類 2 (枯渇・恒久ロック)**: 削除されると資源が**永久に 1 個減る**か、
  ロックが**永久に握られたまま**になる。繰り返すと系が停止する。**補償必須**。
- **分類 3 (有界リーク)**: 1 回の kill につき有限量が漏れるだけで、
  枯渇に向かわない。頻度と量を見て補償するか文書化するかを選ぶ。

### 全数表

| # | ウィンドウ | 場所 | 分類 | 削除された場合 | 頻度 |
|---|---|---|---|---|---|
| 1 | GFX フローセマフォのスロット (take -> msg_send 完了) | `fmrb_gfx_cmd.c:36-53` | **2** | スロットが 1 個永久に減る。繰り返すと 96 枠が枯渇し、全アプリの描画が止まる | 描画 1 回ごと。系で最も熱い経路 |
| 2 | MicroPython 排他 mutex `s_lock` | `fmrb_mp.c:127-140`, `299-301` | **2** | mutex が永久ロック。以後 Python アプリは `fmrb_mp_acquire` で無限待ち (FMRB_MAX_DELAY) | Python アプリの起動時と終了時の各 1 回 |
| 3 | メッセージ登録簿 mutex `g_registry_lock` | `fmrb_msg.c:105/149/189/209/240/259` | **2** | mutex が永久ロック。**全タスクの send/receive が停止** = 系の停止 | メッセージ送受信ごと。極めて熱い |
| 4 | transport の同期リクエストスロット (`req->active`) | `fmrb_transport.c:510-541` | **1** | タイムアウト掃除 (`:872`) が `active=false` に戻すので**自己回復**する | create_mask / create_image_from_file |
| 5 | 同期コンテキストのセマフォ (`create_binary` -> `take` -> `delete`) | `fmrb_gfx.c:40/67`, `gfx.c:148/631/786`, `fmrb_spx_gfx.c:61/448/558`, `fmrb_spx_app.c:564`, `app.c:1097`, `fmrb_transport.c:585` | **3** | セマフォ 1 個 (約 80 B) が漏れる。**加えて下記の危険あり** | 同期 API 1 回ごと |
| 6 | ファイル転送バッファ (`fmrb_sys_malloc` -> host へ所有権移転) | `gfx.c:717`, `fmrb_spx_gfx.c:515` | **3** | ファイルサイズぶん漏れる (アイコン 1.6KB〜シート数十 KB) | transfer_file / sync_file。アプリ起動時のアセット同期 |
| 7 | ファイルハンドル (`file_open` -> `file_close`) | `gfx.c:723`, `fmrb_spx_gfx.c:520` | **3** | FD 1 個が漏れる | 同上。区間は read 1 回ぶんと短い |

### 分類 2 の 3 件が 3b の対象

README は分類 2 として GFX スロットを挙げていたが、調査で**あと 2 件**見つかった
(#2 Python 排他、#3 メッセージ登録簿)。いずれも「短い非ブロック区間」で、
`g_registry_lock` は設計上ブロック呼び出しの前に必ず解放されている
(`fmrb_msg.c:202` のコメントどおり) ため窓は数命令ぶんしかない。それでも
**当たれば系が止まる**ので、分類 2 として同じ形 (ctx の保持カウンタ +
kill 側での補填) を当てる対象になる。

窓の広さは #1 が桁違いに広い (msg_send がキュー満杯で最大 5 秒ブロックし得る
区間をまるごと含む)。#2/#3 は数命令。**実際に踏む確率は #1 が圧倒的**。

### 分類 3 の扱い (ユーザ判断をお願いしたい)

量と頻度は上表のとおりで、**1 回の強制 kill につき最大でも
「セマフォ 1 個 + ファイル 1 本ぶんのバッファ + FD 1 個」**。枯渇には
向かわない。ただし見落とせない点が 2 つある。

1. **ハングしたアプリはこの窓に居る確率が高い**。強制 kill は 1 秒の猶予後に
   撃たれるので、対象は「応答しないまま何かを待っている」状態であることが
   多い。#5 の同期待ち (get_pixel / file_status / transfer / define_prog) は
   まさにその「待っている場所」なので、レアな競合ではない。
2. **漏れよりも危険な副作用がある**。#5 で削除すると、host_task や transport の
   コールバックが後から**死んだスタック上の同期コンテキストに書き込む**
   (`sc->result` 代入や `fmrb_semaphore_give(sc->done)`)。削除済みタスクの
   スタックは別用途に再利用され得るので、これはリークではなく**メモリ破壊**の
   可能性がある。#6 も同様に、host_task が転送バッファを読む前後で
   アプリのメモリプールが破棄されると解放済み領域を読む。

このため私の推奨は **「分類 3 のうち #5 は補償ではなく無効化で対処、#6/#7 は
制限事項として文書化」** です。具体的には:

- #5: `gfx_cmd_sync_ctx_t` / `file_cmd_result_t` を**アプリのスタックではなく
  ctx 側 (または host が所有する固定枠) に置き**、kill 側で「もう誰も
  書き込まない」印を立てる。これは 3b のカウンタ方式と同じ「消してから
  帳尻を合わせる」で扱えるが、**3b より作業量が大きい**ため、Phase 3 の
  範囲に入れるかは判断をいただきたい。
- #6/#7: 1 回あたり数 KB と FD 1 個。強制 kill 自体が異常系で頻発しない
  前提なら、**制限事項として文書化**で足りると考える。

### 調査中に見つかった別件 (Phase 3 の対象外だが要記録)

**GFX フローセマフォの計上が既に非対称**。`host_task.c:1311` は処理した
GFX コマンド 1 件につき 1 回 give するが、`fmrb_gfx.c` は 6 箇所
(`:59` の同期送出、`:210`, `:256`, `:294`, `:460`, `:483`) で
`fmrb_gfx_submit` を通さず `fmrb_msg_send` を直接呼んでおり、**take せずに
give される**。カウンティングセマフォは最大値で頭打ちになるので上限は
超えないが、描画が走っていて残数が最大未満のときは**幻のスロットが 1 個
増える**。HID 予約枠 (Phase A) の保護がそのぶん緩む。

これは分類 2 の #1 を補償する際、**補償が正しいかを残数で検証できなくする**
ので、3b に着手する前に整理したほうがよい。修正案は「同期送出も
`fmrb_gfx_submit` を通す」か「host 側で sync フラグ付きコマンドを give の
数から除く」のどちらか。**これも判断をいただきたい。**

なお `fmrb_gfx_msg.h:227` の「bytecode_buf/strtable_buf は fmrb_malloc され、
Host Task が free する。所有権は Host Task に移る」というコメントは**現状と
一致していない**。実際の `fmrb_gfx_define_prog` は呼び出し側のバッファを
memcpy させるだけで所有権を渡さず、free もしていない (`fmrb_gfx.c:402` の
コメントが正)。リークではないがコメントが誤り。

## Phase 3b-0 / 3b / 3c / 3d: 実装 (2026-08-02)

### 3b-0: GFX セマフォ計上の非対称

`fmrb_gfx.c` の直接送出 6 箇所のうち **5 箇所をメーター経由
(`fmrb_gfx_submit`) に**、`delete_canvas` の 1 箇所だけを新設の
`fmrb_gfx_submit_unmetered()` にした。前者はすべてアプリタスク上でしか
呼ばれない (呼び出し元を全数確認)。後者はカーネルの reap と強制 kill 経路
からも呼ばれるため、アプリの描画が補充するセマフォで待たせるわけにいかない。

`gfx_cmd_t` に `unmetered` を足し、host 側は take されたぶんだけ give する。

**実装中に自分で作った不具合を計測で捕まえた**。最初は give 側で
`cmds[i].unmetered` を見たが、同期コマンドは `cmds[]` に格納されないまま
`count++` だけされる (host_task.c の `handle_sync`) ので、未初期化の索引を
読んでいた。結果 3 スロットが失われた。到着時に `metered` を数える形に直した。

**検証手法** (README の宿題): take と give に一時カウンタを仕込み、gdb で
読む。`docker compose` に `cap_add: SYS_PTRACE` の一時 override を足し
(コミットしない)、`docker exec -u 0 ... gdb -p <pid> -batch -ex 'p ...'`。
セマフォ残数は `((Queue_t*)g_host_gfx_queue_semaphore)->uxMessagesWaiting`
で読める。

| 状態 | takes | gives | 差 | 残数 |
|---|---|---|---|---|
| 修正前 (最初の実装、不具合あり) | 5195 | 5192 | 3 | 93 |
| 修正後 アイドル | 5338 | 5338 | **0** | **96** |
| 修正後 BASIC デモ後 | 10063 | 10063 | **0** | **96** |
| 修正後 Shapes 後 | 10181 | 10181 | **0** | **96** |
| 修正後 再アイドル | 10246 | 10246 | **0** | **96** |

10,246 コマンド (同期コマンドを含む) を通して take と give が完全一致し、
残数は常に満枠の 96。**幻スロットは消えた**。

なお元の非対称 (fmrb_gfx.c の 6 箇所が take せず give される) 自体は
コード読解で確定したもので、修正前バイナリを同じ計測器にかけてはいない。
アイドル時の残数はカウンティングセマフォが最大値で頭打ちになるため
修正前も 96 を示し、残数だけでは区別できない (だからカウンタを入れた)。

### 3b: mutex ガード

`fmrb_msg_registry_lock_barrier()` と `fmrb_mp_lock_barrier()` を追加し、
強制経路が `fmrb_task_delete` の**直前**に両方を呼ぶ。取れた = 対象は
保持窓の外、が保証になる。ホットパス (毎メッセージのレジストリ操作) には
何も足していない。

### 3c: INIT 状態の kill

- `fmrb_app_kill` の受け付け状態に INIT を追加。
- `is_valid_transition` に **INIT -> STOPPING** を追加 (強制経路が使う)。
- 猶予ループの早期 reap 条件から INIT を**外した**。この分岐は
  RUNNING/SUSPENDED しか kill できなかった間は到達不能で、INIT kill を
  許した今は「create_vm の中でまだ動いている」を意味するため、
  mutex ガードのある強制経路へ回す必要がある。
- **検証中に見つけて直した実バグ**: `ctx->mp_active` は `fmrb_mp_start`
  成功後に立っていたため、`fmrb_mp_start` 内で固まったアプリを強制 kill
  しても `destroy_vm` が `fmrb_mp_close` を呼ばず、**Python 排他が解放
  されなかった** (最初の検証で実際に再現)。ロックを取った直後に
  `mp_active` を立てる形に変更 (`fmrb_mp_close` は早期呼び出しを明示的に
  許容している)。

**判定**: INIT で固めた Python アプリを kill -> `INIT -> STOPPING` ->
`Killed by force` -> `MicroPython runtime closed` -> 直後に
`Python demo started on linux`。排他が解放されている。

### 3d: 同期 I/O 中は消さずに待つ

`ctx->sync_io_depth` を追加し、同期待ち 10 箇所を
`fmrb_app_sync_io_begin/end` で囲んだ (GFX 同期送出、mruby/Spinel の
ファイルコマンド、transport の send_sync、get_pixel)。強制経路は旗が
立っていれば最大 `KILL_SYNC_WAIT_MS` = 30 秒待つ。

**判定** (大きめのファイルを繰り返し転送する一時アプリで再現):

- **(a) 応答が来る場合**: 転送の合間に隙のあるアプリを kill ->
  `In a sync round trip; waiting up to 30000ms` の後、アプリが目を覚まし
  `Task exiting normally` -> `Reaped`。**協調終了に化けた** (`Killed by
  force` は出ない)。
- **(b) 窓から出てこない場合**: 転送を連続で回すアプリを kill ->
  30 秒待って `Sync round trip did not end in 30000ms; deleting anyway`
  -> canvas 回収 -> `Killed by force`。系は生存。

### スロット枯渇の実証

GFX を流し続ける一時 Lua アプリの spawn -> kill を **10 回**繰り返し、
前後でセマフォ残数を gdb で読んだ: **96 -> 96 (減少なし)**。直後に
BASIC デモを動かして 470 cmds/s 出ており、描画は従来どおり。

ただし**この 10 回はすべて協調終了で決着した** (`Exited on request` x10、
`Killed by force` は 0 件)。Lua の VM フックが `fill_rect` の合間に必ず
効くためで、**「スロットを保持したまま強制削除される」窓は今回踏んでいない**。
契約上その劣化は許容 (最大 1/96) なので完了条件は満たすが、実証したのは
「3b-0 の計上が正しく、10 回の kill で残数が減らないこと」であって
「窓を踏んでも減らないこと」ではない。

### 回帰

4 言語デモ (mruby / Lua / BASIC / Python) の起動・終了、Spinel デスクトップ
構成での Shapes の起動・描画・終了、いずれも正常。canvas は作成と削除が
1 対 1。

### 副産物の記録

- `Killed by force` のログに degraded 警告を追加 (契約どおり 1 行)。
- **debugd の bind が EINTR で失敗することがある**。検証中に
  `bind(:5555) failed: Interrupted system call` -> `debugd not running` を
  1 回踏んだ。FreeRTOS シミュレータはスケジューリングに signal を使うため
  syscall が中断される。`bind` を EINTR で再試行していないのが原因で、
  Phase 3 とは無関係の既存の脆さ。再起動で復帰する。
