# Linux sim がまれに固まる: ログロックの優先度逆転

2026-08-16。robo_explorer を速度 3 (100ms/ターン) で走らせた Linux sim が、
数分でログを出さなくなり (GA 側は 30fps で生存)、入力にも応答しなくなった。
ハング中のプロセスに gdb を当てて全スレッドの backtrace を採取し、機序を
特定した。**実機 (ESP32) には存在しない、Linux ポート固有の問題**。

## 採取した証拠 (要約)

ハング中の 13 スレッドのうち、意味を持つのは 3 本:

```
Thread 9  host_task        fmrb_hal_link_send → ESP_LOGD → esp_log_impl_lock
                           → pthread_mutex_lock(mutex1) で futex 待ち
Thread 3  アプリタスク      log_level_get 内で mutex1 を取得済み。
                           esp_log_impl_unlock → pthread_mutex_unlock の途中で
                           FreeRTOS の tick シグナルに割り込まれ、
                           prvSwitchThread で suspend されたまま
Thread 2  RoboPilot(mruby)  draw_line → fmrb_msg_send(HOST) がキュー満杯で待ち
                           (host_task が止まった二次被害)
```

## 機序: FreeRTOS から見えないロックによる優先度逆転

IDF の Linux ポートでは、ログのロック (components/log/src/linux/log_lock.c)
は**生の pthread mutex** で、FreeRTOS シミュレータのスケジューラからは
見えない。

1. 低優先度のアプリタスクがロックを保持した数命令の間に tick が入り、
   スケジューラに suspend される (FreeRTOS 的には READY のまま)
2. 高優先度の host_task が同じロックを取ろうとして futex で眠る。
   **FreeRTOS 的には「実行中」のまま** (FreeRTOS の待ちに入っていない)
3. 以後の tick でスケジューラは毎回「最高優先度の READY」= host_task を
   選び直す。保持者には永遠に CPU が回らず、全体が止まる

発火には「保持中に preempt される」窓を踏む必要があり、窓はロック取得
1 回につき数命令ぶんしかない。**ESP_LOGD はレベル判定だけでもこのロックを
取る** (esp_log_is_tag_loggable → log_level_get) ため、発火確率はログ
呼び出し回数に比例する。robo_explorer 速度 3 では送信ごとの LOGD +
描画ごとの LOGD で毎秒 400 回超になり、数分で踏んだ。

## 2 度目の捕獲: ログ削りはモグラ叩きだった

ホットパスの LOGD を削って再走したところ、**2 分で別のロックで再発**した。

```
Thread 9  host_task  fmrb_transport の LOGD → esp_log_system_timestamp
                     → localtime → glibc の tzset_lock 待ち
Thread 8  アプリ      localtime → __tz_convert (tzset.c:434) の途中で
                     tzset_lock を保持したまま suspend
```

1 度目はタグ判定ロック、2 度目は壁時計タイムスタンプの tzset ロック。
個々のログ呼び出しではなく、**glibc 内部ロックというクラス全体**が対象で
あることが確定した (stdio の FILE ロック、msgpack-c が叩く生 malloc の
アリーナロックも同族)。発火条件を初めて持続的に満たしたのは Pub/Sub の
ターンループ (10Hz で低優先度アプリ⇔高優先度カーネル/host の往復) である。
ログ自体は昔からあった — 変わったのは通信パターンのほう。

## 対処 (2026-08-16 実施)

**(1) 本命: ロック保持区間を港の critical section で包む (Linux ビルド限定)**

vPortEnterCritical は pthread_sigmask で全シグナルをマスクするので、
その中では tick に preempt されない。リンカの --wrap で
esp_log_impl_lock / esp_log_impl_lock_timeout / esp_log_impl_unlock /
esp_log_system_timestamp を包み (main/boot/sim_log_guard.c、
main/CMakeLists.txt の linux 分岐)、**保持者が必ず走り切る**ようにした。
リンク時ラップなので IDF 内部からの呼び出しも全て通る。ログ 2 家系の
逆転はこれで構造的に組めない。

**(2) 併せて: ホットパスの LOGD 削除**

sim は debug_mode=true で DEBUG が実際に出力されるため、毎メッセージ・
毎描画のログは量として害しかない。

- components/fmrb_hal/platform/posix/fmrb_hal_link_posix.c: 送受信の
  毎メッセージログ 8 箇所
- lib/add/picoruby-fmrb-app/ports/esp32/gfx.c: clear/fill_rect/
  fill_circle/draw_text の毎呼び出しログ 6 箇所
- main/kernel/host/host_task.c: GFX バッチ・マウス移動・描画コマンドの
  毎イベントログ 3 箇所

検証: 修正前は同条件 (robo_explorer 速度 3、周期リセットで連続走行) で
2 分以内に 2 回連続でハング。修正後は **6 分の soak を通過** (ピーク
tx 469 msg/s、沈黙検知・クラッシュともに無し)。

## 残っている穴と、また固まったときの見立て

- **msgpack-c の生 malloc** (fmrb_mem を迂回する既知事項) が glibc の
  アリーナロックを叩く。ログ 2 家系と同じ逆転が理論上組める。ラップは
  していない (malloc を包むのは侵襲が大きすぎる)。Linux でまた固まったら
  まず gdb (下記) で捕まえ、malloc 系なら kernel/host 経路の msgpack を
  fmrb_mem に寄せる話になる
- ~~stdio の FILE ロック (fprintf 内部) も同族だが、esp_log の出力は
  ラップ済みの区間内から呼ばれるため実質カバーされている~~
  **この見立ては誤りだった (2026-08-25 訂正。下記「3 度目の捕獲」)**
- 実機は無関係 (ESP32 のログロックは FreeRTOS mutex)

## 3 度目の捕獲: stdio の FILE ロックは覆われていなかった (2026-08-25)

サービスがブートで 2 行ログを増やしただけで、**ブート中に固まる**ように
なった。gdb で取ると 1 度目・2 度目と同じ形で、ロックだけが違う:

```
Thread 4  Services  write() の中で _IO_stdfile_1_lock を保持したまま
                    tick シグナルで suspend
                    ("...timesync started..." を書いている最中)
Thread 5  desktop   同じ FILE ロック待ち (futex)
Thread 8  host_task 同じ FILE ロック待ち (futex)
```

上の「実質カバーされている」が成り立たない理由: IDF の `esp_log_va` は
**タグ判定ロックを取って放してから**出力関数を呼ぶ (log.c)。つまり
**stdout をロックする vprintf は、どのラップ区間の外**にある。既存の
`--wrap` 4 本 (タグロック 3 + タイムスタンプ) はここに届いていない。
「ログ 2 家系の逆転はこれで構造的に組めない」も、正しくは**2 家系のうち
ラップした側だけ**だった。

### 対処 (3 度目)

`esp_log_set_vprintf` で**出力そのもの**を port の critical section で包む
(`main/boot/sim_log_guard.c` の `sim_log_guarded_vprintf`)。リンカラップでは
なく IDF の API を使ったのは、**必要なのはログ経路だけ**で、機械中の全
`printf` を critical section に入れるのは約束が大きすぎるため。バッファに
半端な行を残さないよう `fflush` も同じ区間の中で行う (次に書く者が他人の
書きかけをロックを持ったまま完成させる窓を塞ぐ)。

代償: 書き込みの間はシグナルが止まるので、**stdout の読み手が遅いとその分
tick が遅れる**。sim の stdout は docker が読み続けるパイプなので、固まるより
こちらの risk を取る。

**教訓**: 対処 (1) の「構造的に組めない」は**ラップした関数についてだけ**の
主張である。glibc 内部ロックというクラス全体が対象、という 2 度目の結論の
ほうが正しく、**新しいロックが出てきたら同じ形でまた出る**。次に固まったら、
まず下記の gdb で「ロックを保持したまま suspend されているスレッド」を探し、
そのロックを誰が取っているかを追うこと。

## 再現とデバッグの道具立て (また要るとき用)

ptrace はコンテナ内外とも封じられているので、同じ PID 名前空間に
デバッグ用コンテナを同居させて取る:

```
docker run --rm -u root --pid=container:fmruby_core --cap-add=SYS_PTRACE \
  -v <repo>/fmruby-core:/project ghcr.io/family-mruby/fmruby-esp32-build:v5.5.4 \
  bash -c "gdb -batch -ex 'set pagination off' \
    -ex 'set sysroot /proc/<PID>/root' \
    -ex 'file /project/build/fmruby-core.elf' -ex 'attach <PID>' \
    -ex 'thread apply all bt 14'"
```

sysroot 指定が肝 (相手コンテナの libc を /proc 経由で読ませないと
シンボルが出ず、FreeRTOS タスクの巻き戻しが 0xa5a5... で切れる)。
