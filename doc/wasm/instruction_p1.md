# 実装指示書 P1: FreeRTOS wasm port 単体 PoC

対象: 実装担当セッション。前提: plan.md と implementation_plan.md (特に P1 節)
を読むこと。作業は fmruby-core リポジトリ内で完結する。タスクごとにコミット
(push はしない)。report は doc/wasm/report/p1.md へ。

## ゴール

協調 (プリエンプション無し) の Emscripten port で、core が使う FreeRTOS API 群が
正しく動くことを、core 本体抜きの PoC で機械判定できる状態を作る。
**ここが通らなければ wasm 対応全体を中止する** (implementation_plan の撤退線)。
成果物は fmruby-core 直下の wasm/ と rakelib/wasm.rake のみで、ESP32/Linux の
ビルドには一切影響を与えない。

## 方針 (決定済み)

- カーネル本体は IDF 同梱の FreeRTOS-Kernel を vendor する (TLS 削除コールバックが
  IDF 拡張のため、素の上流ではなく IDF のコピーを使う)。
- port 層のみ新規に書く。1 タスク = 1 pthread (Emscripten pthread = Worker + SAB)。
- プリエンプションは捨てる。tick=1ms は維持し、実時間からの追いつきで供給する。
- vPortCancelThread (他殺) は非対応: ログを出して no-op。

## T1: vendor 取り込みと FreeRTOSConfig.h

1. 取得元はビルドイメージ内の
   `/opt/esp/idf/components/freertos/FreeRTOS-Kernel` (確認済み)。例:
   ```
   docker run --rm -v $(pwd)/wasm/vendor:/out ghcr.io/family-mruby/fmruby-esp32-build:v5.5.4 \
     bash -c "cp -r /opt/esp/idf/components/freertos/FreeRTOS-Kernel /out/freertos"
   ```
2. wasm/vendor/freertos/ に残すのは tasks.c / queue.c / list.c / stream_buffer.c +
   include/ + LICENSE.md + idf_changes.md。timers.c と event_groups.c は
   core の使用ゼロ (plan.md の棚卸し) なので削る。portable/ も削るが、
   **portable/linux/utils/wait_for_event.{c,h} だけは wasm/port/ へコピーして
   改変元にする** (シグナルを使わない待ち合わせの骨格として流用)。
   参考として portable/linux/port.c と portmacro.h も手元に取り出して読む
   (vendor には入れない)。
3. wasm/port/FreeRTOSConfig.h を作る:
   - configTICK_RATE_HZ=1000、unicore、configUSE_PREEMPTION=1 のまま
     (API 境界での切替は生きる。失うのは非同期 tick による剥がしだけ)。
   - configNUM_THREAD_LOCAL_STORAGE_POINTERS=3、IDF 拡張の TLS 削除コールバックを
     有効化 (有効化マクロの名前は vendor の tasks.c / idf_changes.md を grep して
     確認する)。
   - その他の値は現行 Linux sim ビルドの実効値 (build/ 以下の生成 config か
     sdkconfig) を参照して core の使用範囲に合わせる。PoC に不要な機能は落として
     よい (P4a で拡張する)。
4. tasks.c 等が IDF 固有ヘッダを include していて詰まったら、空 stub ヘッダで
   受ける (wasm/stub/ の先取り)。深追いせず、何を stub したか report へ。

## T2: port 実装 (wasm/port/portmacro.h + port.c)

- スレッドモデル: xTaskCreate ごとに pthread を 1 本作る。非実行タスクは自分の
  event (wait_for_event 由来の mutex+condvar) で待ち、「event を起こされた
  1 スレッドだけが走る」ことで単一実行を表現する (既存 Posix port と同じ骨格)。
- タスクスタック: xTaskCreate の指定値をそのまま pthread_attr_setstacksize へ。
- クリティカルセクション / 割り込みマスク系: 単一のグローバル mutex に落とす
  (ISR は存在しない)。
- tick 供給: setitimer / シグナルは使わない。
  - 走行中のスレッドが port の API 境界 (yield、ブロック、ブロック解除) を通る
    たびに、emscripten_get_now() と前回値の差分から必要回数 xTaskIncrementTick()
    を呼んで追いつく。
  - 全タスクがブロック中の間はアイドルタスクが時間を進める:
    configUSE_IDLE_HOOK=1 のフックで 1ms 寝てから追いつきを行う。
    configUSE_TICKLESS_IDLE は使わない (複雑化を避ける)。
  - 別の設計にしてもよいが、「誰がいつ tick を進めるか」を report に必ず書く。
- vPortCancelThread: ログ + no-op (P2 の自己プリエンプションで不要になる前提)。

Emscripten 固有:

- リンクフラグ: `-pthread -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=32
  -sENVIRONMENT=node` (PoC は node のみ)。-sSTACK_SIZE も明示する
  (rakelib/ti.rake の ti:wasm に前例。prism で 1MB にした経緯コメントあり)。
- pthread pool が尽きた状態での pthread_create はメインループに制御が戻るまで
  完了しない。プール枯渇時に何が起きるかのテストを 1 本入れ、実測挙動を
  report に記録する (P4a のタスク数見積りの材料)。
- emcc は ti:wasm と同じ流儀 (ENV["EMCC"] → PATH の emcc → 無ければ
  `source ~/emsdk/emsdk_env.sh` を案内)。使った emsdk / node の版を report に
  記録し、rakelib/wasm.rake に固定版としてコメントする。

## T3: PoC テスト (wasm/poc/)

C で書き、node で実行する (ブラウザ不要、CI 可能)。個々の項目の判定が
stdout の PASS/FAIL 行で機械判定できること。

テスト項目 (= 受け入れ条件の実体):

1. タスク生成 / vTaskDelay の精度 (±10ms 程度で可) / 優先度どおりの選択。
2. xTaskNotifyGive / ulTaskNotifyTake のタイムアウト付き往復
   (task_hal.c の使い方の写し。mruby-task の心臓)。
3. キューの送受信とタイムアウト、満杯時のブロックと解除。
4. counting セマフォ 96 スロットの枯渇 → 排出 → 再開
   (main/kernel/host/host_task.c:1859 の GFX フロー制御の縮小再現。
   producer が take で詰まり、consumer の give で再開する形)。
5. mutex の所有者制約 (非所有者の give が失敗する)。
6. TLS 3 スロットへの set/get と、削除コールバックが vTaskDelete(NULL)
   (自殺) で呼ばれること。
7. MessageBuffer の可変長メッセージ送受信
   (components/fmrb_hal/platform/esp32/fmrb_hal_link_local.c が使う形)。
8. CPU を回し続けるタスクがいる間は上位が走らず、taskYIELD() した瞬間に
   上位が走ること (= 協調の限界の確認。回っている間に走らないのは仕様)。
9. 長め (10 秒程度) に回して xTaskGetTickCount が実時間から大きく
   乖離しないこと (追いつきモデルの検算)。

## T4: rakelib/wasm.rake

- `rake wasm:poc` (ビルド + node 実行 + 判定) と `rake wasm:clean`。
- `rake build:linux` / `rake -T` が従来どおり動くことを確認する
  (wasm/ は独立 CMake。idf.py には触れない)。

## 受け入れ条件

1. `rake wasm:poc` が全項目 PASS (node 実行、ブラウザ不要)。
2. 連続 5 回実行して flaky でないこと (協調スケジューラの race はここに出る)。
3. rake build:linux / build:esp32 への影響ゼロ (追加ファイルのみ)。

## report に書くこと

- emsdk / node の版 (固定した値)。
- tick 追いつきの確定設計 (誰がいつ進めるか) と捨てた案。
- pthread pool 枯渇の実測挙動、vTaskDelay 精度の実測値。
- stub したヘッダ・マクロの一覧 (P4a の stub/ 設計の入力になる)。
- P4a に向けた懸念 (Emscripten の癖、カーネルの IDF 拡張で困った点)。

## やらないこと (P1 の範囲外)

- ブラウザ実行・Canvas/WebAudio/入力 (P4b/P4c)。
- core のソースを繋ぐこと (P4a)。FreeRTOSConfig の core 向け網羅も P4a。
- プリエンプションの復元・vTaskDelete 他殺の実装。
