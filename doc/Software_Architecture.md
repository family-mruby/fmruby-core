# Family mruby OS — ESP32-S3 ソフトウェア構成

## 全体概要

ESP32-S3上では、**FreeRTOSタスク**として複数の処理系が動作します。  
中心となるのは `Kernel.rb` を実行する **fmrb-kernelタスク** で、GUI、アプリライフサイクル、システムイベント管理を担います。  
HID入力や描画・オーディオ出力などは、システムタスクを介して各ハードウェア抽象化層（HAL）に転送されます。

---

## 構成レイヤー

### 1. USB Host 処理層

- **対象クラス**
  - HID（キーボード／マウス）
  - CDC (PicoRuby経由の仮想シリアル)
  - MSC (PicoRuby経由のマスストレージ)

- **主要タスク**
  - `FreeRTOS Task USB Host+HID`
    - `TinyUSB event handling`
      - HIDイベントを1msecループで処理
      - `HID msg` を Systemタスクへ転送
  - 将来的に `FreeRTOS Task MSC`, `FreeRTOS Task CDC` も追加予定

---

### 2. Kernel タスク（メインループ）

- **FreeRTOS Task**
  - `Kernel.rb` (PicoRuby上で動作)
  - 構成要素:
    - Window Management  
    - App Life Cycle  
    - System GUI  
  - 動作周期: **60Hz loop**

- **モジュール**
  - `picoruby-fmrb-kernel`
  - Systemタスクとの通信に `IPC Queue` を使用（メッセージ送受信）

---

### 3. System タスク

- **FreeRTOS Task (System)**
  - Window描画とAudio出力の統合管理
  - HIDディスパッチ
  - AV同期（将来的にデュアルコア動作対応）
  - System Message Signalなどの統合

- **通信経路**
  - `IPC Queue` で KernelやUSB Hostとメッセージ連携
  - `draw/audio` イベントを各HAL層へ転送

---

### 4. HAL層および共通ライブラリ

- **描画・オーディオ層**
  - `fmrby-gfx`
  - `fmrby-audio`
  - `fmrby-ipc`

- **ハードウェア抽象化層 (HAL)**
  - `fmrby-hal`
    - `platform/linux`
    - `platform/esp32`
    - `common`

- **通信方式**
  - Linux上では `Socket/msgpack`
  - 実機では `SPI/msgpack` 経由で WROVERチップと接続

- **ファイルシステム**
  - `fmrb-filesystem`（スレッドセーフ設計）
  - 各Appからのファイルアクセスを仲介

---

### 5. アプリケーション層

- **Userアプリ**
  - 各アプリごとに独立した FreeRTOS タスクで動作
  - `UserApp.rb` を `picoruby-fmrb-app` 上で実行
  - 必要に応じて `picoruby-gpio` や `picoruby-i2c` を利用
  - 描画・オーディオ指示は Systemタスクを経由してHALへ

---

### 6. 外部プロセス／デバイス連携

- **ESP32-WROVER**
  - SPI経由で映像・音声レンダリングを担当
  - `fmrby-hal` からの `SPI/msgpack` メッセージを受信

- **Linux Test Process**
  - PC上のデバッグ・シミュレーション環境
  - Socket通信経由で同一インターフェースを再現

---

## データフロー概要

| 種類 | 送信元 → 送信先 | 内容 |
|------|------------------|------|
| HID msg | USB Host Task → System Task | 入力イベント（キー、マウス） |
| IPC msg | Kernel ↔ System | Window操作、描画要求、アプリイベント |
| draw/audio | System → HAL | グラフィック・オーディオデータ送信 |
| file access | UserApp ↔ Filesystem | 読み書きリクエスト |
| msgpack | HAL ↔ WROVER / Linux | ハードウェア通信 |

---

## 備考

- `picoruby` ベースで各タスクは独立したVMインスタンスを持ち、  
  FreeRTOSのマルチタスク上で並列実行される。
- HIDイベントはTinyUSB経由でSystemタスクに集約され、GUIへ配信。
- GUI更新は60Hzループで動作し、fmrb-gfx/audio層を通じて出力される。
