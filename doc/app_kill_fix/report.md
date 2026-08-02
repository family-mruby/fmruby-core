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

`fmrb_task_config.h` の 3 行のみ (doc/task_priority.md の段割りに従う)。

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
- ドキュメントで数値に言及しているのは doc/boot_performance.md の
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
