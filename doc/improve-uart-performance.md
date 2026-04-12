# UART Transport Deadlock Fix + Fire-and-Forget

## Background

Shell app起動時にCREATE_CANVASがUART ACKタイムアウトで失敗する(再現性あり)。
根本原因はACK送信がフレーム受信に結合されたデッドロック + 全コマンドがstop-and-wait方式。

---

## Core側: コマンド送信のコールチェーン

### 通常描画コマンド (fill_rect等) - APPタスク -> HOSTタスク -> UART

```
[APPタスク (Core1, PSRAM)]  <-- mruby実行コンテキスト
  Ruby: gfx.fill_rect(...)
  |
  mrb_gfx_fill_rect()           gfx.c:237
  |
  send_gfx_command()            gfx.c:30
    semaphore_take(gfx_queue)   <-- flow control (96 slots), blocking
    fmrb_msg_send(HOST, msg)    <-- FreeRTOS Queue (128 msgs)
  | ---- タスクコンテキスト切り替え (Queue) ----

[HOSTタスク (Core0, IRAM, prio=10)]
  fmrb_msg_receive()            host_task.c:998 (10ms wait)
  |
  host_task_process_message()   host_task.c:740
  |
  host_task_process_gfx_batch() host_task.c:666
    最大16コマンドをバッチ化
    gfx_cmd_to_batch_entry()    host_task.c:183
  |
  fmrb_transport_send_batch()   fmrb_transport.c:373
    msgpackエンコード
  |
  fmrb_hal_link_send()          fmrb_hal_link_uart_esp32.c:396
    xSemaphoreTake(uart_mutex)  <-- UART排他
    COBS encode
  |
  send_frame_and_wait_ack()     fmrb_hal_link_uart_esp32.c:247
    uart_link_build_frame()     <-- [SYNC|header|COBS data|CRC]
    uart_write_bytes()          <-- UART TX (921600 bps)
    uart_wait_tx_done()
    while (!ack_received):      <-- *** frame-level ACK待ちブロック ***
      poll_uart_rx(10ms)
      vTaskDelay(1ms)
    xSemaphoreGive(uart_mutex)
  |
  semaphore_give(gfx_queue)     <-- flow control解放
```

### 同期コマンド (CREATE_CANVAS) - APPタスクからHOST経由

```
[APPタスク]
  fmrb_gfx_create_canvas()      fmrb_gfx.c:820
  |
  send_graphics_command_sync()   fmrb_gfx.c:51
  |
  fmrb_transport_send_sync()     fmrb_transport.c:575
    callback + semaphore作成
  |
  fmrb_transport_send_async()    fmrb_transport.c:495
    sync_request slot確保 (MAX 4)
    send_raw_message()           fmrb_transport.c:362
      fmrb_hal_link_send()       <-- *** ここでもframe-level ACK待ち ***
  |
  semaphore_take(done, 1000ms)   <-- app-level応答待ち

  応答受信経路:
  fmrb_transport_process()       <-- HOSTタスクがループ毎に呼ぶ
    fmrb_hal_link_receive()      <-- ack_recv_queueからdequeue
    handle_received_message()    <-- seq matchでcallback呼び出し
    sync_response_callback()     <-- semaphore_give(done)
```

問題: send_asyncの中でsend_raw_message -> fmrb_hal_link_send -> send_frame_and_wait_ackと
呼ばれ、frame-level ACK待ちが発生。asyncの名前に反して同期ブロックしている。

---

## Root Cause: Deadlock

```
Core (HOST task)                   Graphics-audio
================================================================
send_frame_and_wait_ack()
  uart_write_bytes(CREATE_CANVAS)
  while(!ack_received):           ->  comm_task: uart_read -> process_complete_frame()
    poll_uart_rx(10ms)                 send_response(RX_OK)     <-- 即座に返す
    ...waiting...                      xMessageBufferSend(msg)   <-- handler taskへ
                                       ack_wait = 100ms          <-- 100ms待つ

                                     message_handler_task: (Core0, prio=5)
                                       graphics_handler: CREATE_CANVAS処理
                                       uart_send_ack(canvas_id)  <-- ACKをqueueに入れる

                                     *** しかし100ms以内にhandlerが完了しなかった場合 ***

                                     process_complete_frame()は終了済み
                                     ACKはs_ack_queueに残ったまま
                                     次のフレーム受信がないとdequeueされない

    poll_uart_rx... 何も来ない  <--  comm_task: uart_read... 何も来ない
    ...waiting...                    ...waiting...
    TIMEOUT (1000ms)
```

---

## Additional Fix: HAL expected_seq gate removal

`process_received_frame()` (fmrb_hal_link_uart_esp32.c) が `expected_seq` マッチングで
応答データ処理をゲートしていた。`send_frame_no_ack()` は `expected_seq` を設定しないため、
全応答がドロップされていた。

修正: 応答データ(APP_OK+payload)の `ack_recv_queue` enqueueを `expected_seq` チェックから独立させた。
Transport層が独自にseqマッチングを行うため、HAL層でのゲートは不要。
`ch->ack_received` のセットのみ `expected_seq` マッチで行う(send_frame_and_wait_ackの互換性維持)。

---

## Changes

### Phase 1: Graphics-audio側 - ACK送信のデカップリング

File: `fmruby-graphics-audio/main/communication/comm_uart_slave.c`

1. `uart_process()` (line 316): 先頭でACKキューを常にドレイン(フレーム受信と独立)
2. `process_complete_frame()` (line 199): ACK wait をpdMS_TO_TICKS(100) -> 0 に変更

### Phase 2: Core HAL - fire-and-forget送信 + 独立RXポーリング

File: `fmruby-core/components/fmrb_hal/fmrb_hal_link.h` (line 36後)
- `fmrb_hal_link_send_noack()` 宣言追加

File: `fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_link_uart_esp32.c`
- `send_frame_no_ack()` 追加: poll_uart_rx(0)でRXドレイン -> フレーム送信 -> return
- `fmrb_hal_link_send_noack()` 追加: 既存sendと同構造、send_frame_and_wait_ack -> send_frame_no_ack
- `fmrb_hal_link_receive()` (line 478) 変更: uart_mutexを取ってpoll_uart_rx()を呼び、
  UART RXを能動的にポーリング -> ack_recv_queueに応答を積む

他プラットフォーム (posix, esp32 SPI, local): fmrb_hal_link_send_noack = 既存sendのラッパー

### Phase 3: Transport層 - 全送信をfire-and-forget化

File: `fmruby-core/components/fmrb_transport/fmrb_transport.c`

- `send_raw_message()` (line 203): fmrb_hal_link_send -> fmrb_hal_link_send_noack に変更
- `fmrb_transport_send_batch()` (line 472): 同上
- `send_async()` (line 542): send_raw_message()経由で自動的にnoack化

### 同期コマンド(CREATE_CANVAS等)の応答受信

fmrb_transport_send_sync()は全てAPPタスクから呼ばれる:
- fmrb_gfx_create_canvas() (gfx.c:848) - APPタスク
- version check (transport.c:923) - kernel初期化時
- file transfer status (gfx.c:730) - APPタスク
- create image from file (gfx.c:851) - APPタスク

応答受信はHOSTタスクが担当:
- APPタスク: send_sync() -> send_async() -> send_raw_message() -> noack送信 -> semaphore_take(done)
- HOSTタスク: ループ毎に fmrb_transport_process() -> fmrb_hal_link_receive() -> poll_uart_rx()
  -> ack_recv_queue -> handle_received_message() -> callback -> semaphore_give(done)

APPとHOSTは別タスクなのでデッドロックしない。
APPがsemaphore待ちでブロック中もHOSTは独立にRXをポーリングできる。

---

## 変更後のコールチェーン

```
[APPタスク] -> fmrb_msg_send(HOST) -> Queue
                                       |
[HOSTタスク] <-- fmrb_msg_receive()  (初回10ms待ち、以降non-blockingドレイン)
  |
  host_task_process_gfx_batch()
  |
  fmrb_transport_send_batch()
  |
  fmrb_hal_link_send_noack()    <-- NEW: ACK待ちなし
    uart_mutex取得
    poll_uart_rx(0)              <-- RXドレイン
    uart_write_bytes()
    uart_wait_tx_done()
    uart_mutex解放               <-- すぐ返る
  |
  fmrb_transport_process()       <-- 同じループ内で呼ばれる
    fmrb_hal_link_receive()
      uart_mutex取得
      poll_uart_rx(0)            <-- NEW: RX能動ポーリング
      ack_recv_queueチェック
      uart_mutex解放
    handle_received_message()    <-- 応答があればcallback呼び出し
```

## Thread Safety

- poll_uart_rx()はstatic RX state machine変数を使う
- fmrb_hal_link_send_noack()とfmrb_hal_link_receive()の両方がuart_mutexを取得 -> 排他OK

## Critical Files

| File | Changes |
|------|---------|
| fmruby-graphics-audio/main/communication/comm_uart_slave.c | ACKドレイン追加、ACK wait->0 |
| fmruby-core/components/fmrb_hal/fmrb_hal_link.h | send_noack 宣言 |
| fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_link_uart_esp32.c | send_noack実装、receiveにRXポーリング追加 |
| fmruby-core/components/fmrb_hal/platform/posix/fmrb_hal_link_posix.c | send_noackラッパー |
| fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_link_esp32.c | send_noackラッパー |
| fmruby-core/components/fmrb_hal/platform/esp32/fmrb_hal_link_local.c | send_noackラッパー |
| fmruby-core/components/fmrb_transport/fmrb_transport.c | send/send_batchをnoack化 |

## Verification

1. rake build:esp32 (両プロジェクト) でビルド確認
2. デスクトップからshell app起動 -> canvas作成成功を確認
3. ACK timeoutログが出ないことを確認
4. transport statsでスループット向上を確認
