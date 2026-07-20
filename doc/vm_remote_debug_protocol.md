# PicoRuby VM リモートデバッグ プロトコル仕様

作成日: 2026-07-16
ステータス: Phase 1 実装と並行して確定中 (これが正)
関連: `doc/vm_remote_debug_design.md`, `doc/vm_remote_debug_impl_plan.md`

デバイス (fmrb_debugd) とホスト (Python クライアント / DAP アダプタ) 間の
デバッグプロトコル。トランスポートは Linux シミュレーションが TCP、
ESP32 実機が BLE GATT (Phase 3b で追加)。msgpack body は両者で共通。

## 1. トランスポートとフレーミング

### TCP (Phase 1)

- サーバ: デバイス側 `0.0.0.0:5555` (`FMRB_DEBUG_TCP_PORT`) で listen。
- フレーム: `[u32 length (big-endian)] [msgpack body]`。
  - `length` は body (msgpack) のバイト数のみ (プレフィクス4バイトは含まない)。
  - 最大フレーム長 = 4096 バイト (`FMRB_DEBUG_MAX_FRAME`)。超過はプロトコルエラー。
- 同時接続は 1 クライアントのみ。接続中に来た新規 accept は即 close。

トランスポート層は length プレフィクスの付与/剥離を担当し、上位 (proto/debugd) には
「完全な msgpack body 1 個」単位で受け渡す。

### BLE GATT (Phase 3b)

ESP32 実機 (S3 / P4) のトランスポート。msgpack body は TCP と完全に同一で、
異なるのはフレーミングのみ。既存 BLE ファイル転送サービスと同じ COBS+CRC32 方式
(`main/drivers/ble/ble_framing.h`) を用いる。

```
plain   = [len_hi][len_lo][msgpack body]          // len = body バイト長 (u16 BE)
frame   = COBS( plain + CRC32(plain) ) + 0x00     // CRC32 は 4B BE
```

- CRC32 は多項式 0xEDB88320 (zlib/PNG 版)、COBS 前の平文に対して計算する。
- COBS 出力に 0x00 は現れないため、最初の 0x00 がフレーム終端。
- 受信側は COBS デコード -> 末尾 4B の CRC32 検証 -> `len` と `decoded_len - 6` の
  一致検証を行い、いずれか不一致ならフレームを破棄する (警告ログのみ、切断しない)。
- body 最大長は 4096 バイト (`FMRB_DEBUG_MAX_FRAME`)。エンコード後の最大長は
  4120 バイト (`BLE_DBG_MAX_ENC`)。
- デリミタ以降に同一 write で続くデータは破棄する。ホストは 1 フレームずつ write すること。

GATT 特性 (ベース UUID `xxxxxxxx-4252-5942-4c45-000000000000` の byte[0] で区別。
0x01-0x04 はファイル転送サービスが使用済み):

| byte[0] | 種別 | プロパティ |
|---|---|---|
| 0x05 | debug service (primary) | - |
| 0x06 | debug RX (host -> device) | WRITE / WRITE_NO_RSP |
| 0x07 | debug TX (device -> host) | NOTIFY |

- 送信は MTU-3 バイト単位に分割して notify する (ホスト側で再結合)。
- 接続とみなす条件は「接続済み かつ debug TX を購読済み」。切断すると debugd は
  attach 中の全 VM を detach する。
- **登録前フレームの喪失**: boot 順序上、BLE 初期化から debugd の
  トランスポート登録までにわずかな隙がある。この間にホストが送ったフレームは
  黙って捨てられる。ホスト側は応答が返らない場合にリトライすること。

ホスト側実装の約束 (Phase 3c、`tool/debug/fmrb_ble_transport.py`):

- 送信は 1 フレームを `MTU-3` バイト (取得できなければ 20) ずつ
  write-without-response で分割送信する。デリミタはフレーム末尾にのみ現れるので、
  「デリミタ以降のデータは捨てる」デバイス側の規則と衝突しない。
- 受信 notify はバイト列として蓄積し、0x00 ごとに 1 フレームとして切り出す。
  CRC/len 検証に失敗したフレームは黙って捨てる (上位はタイムアウトで気づく)。
- **接続確立は `version` のリトライで行う**: 接続後、最初に `version` を
  タイムアウト 2 秒 x 最大 3 回送り、応答を得てから上位コマンドを流す。
  これで登録前喪失と購読直後の取りこぼしを吸収する。TCP でも同じ手順を通る。

## 2. メッセージ構造

すべて msgpack の配列。先頭要素 `type` でメッセージ種別を区別する。

```
request : [0, seq(u16), cmd(str),  payload(map|nil)]
response: [1, seq(u16), err(int),  payload(map|nil)]
event   : [2, 0,        name(str), payload(map)]
```

- `seq`: ホストが採番する 16bit 通し番号。response は対応する request と同じ seq を返す。
- `err`: `fmrb_err_t` 準拠。`FMRB_OK = 0`、負値がエラー。
- event は非同期通知 (デバイス -> ホスト)。seq は常に 0。

### エラーコード (response.err で使用する主なもの)

| 値 | 名前 | 意味 |
|---|---|---|
| 0 | FMRB_OK | 成功 |
| <0 | FMRB_ERR_* | `fmrb_err.h` 準拠 (INVALID_PARAM / INVALID_STATE / BUSY / NOT_FOUND / TIMEOUT 等) |

## 3. コマンド一覧 (Phase 1)

payload は msgpack map (キーは文字列)。`-` は payload 無し (nil)。

| cmd | req payload | resp payload | 備考 |
|---|---|---|---|
| `version` | - | `{proto:u8, fw:str}` | proto=1 |
| `ps` | - | `{apps:[app,...]}` | app は下記 |
| `attach` | `{pid:int}` | `{ok:bool}` | hook 装着 + dctx 割当 |
| `detach` | `{pid:int}` | `{ok:bool}` | BP 全解除、パーク中なら continue して hook 解除 |
| `bp_set` | `{pid:int, file:str, line:int}` | `{bp_id:int}` | |
| `bp_clear` | `{pid:int, bp_id:int}` | `{ok:bool}` | bp_id=-1 で全解除 |
| `pause` | `{pid:int}` | `{ok:bool}` | 停止は stopped イベントで通知 |
| `continue` | `{pid:int}` | `{ok:bool}` | |
| `step_in` | `{pid:int}` | `{ok:bool}` | |
| `step_over` | `{pid:int}` | `{ok:bool}` | |
| `step_out` | `{pid:int}` | `{ok:bool}` | |
| `stack_trace` | `{pid:int, max:int}` | `{frames:[frame,...]}` | パーク中のみ有効 |
| `frame_vars` | `{pid:int, frame:int}` | `{vars:[var,...]}` | パーク中のみ有効 |
| `log_read` | `{pos:u32, max_lines:int}` | `{lines:str, pos:u32, overrun:bool}` | |
| `kill` | `{pid:int}` | `{ok:bool}` | 既存 fmrb_app API。attach 中は BUSY |
| `stop` | `{pid:int}` | `{ok:bool}` | 同上 |
| `suspend` | `{pid:int}` | `{ok:bool}` | 同上 |
| `resume` | `{pid:int}` | `{ok:bool}` | 同上 |
| `spawn` | `{path:str}` | `{pid:int}` | |

### 複合型

```
app   : {pid:int, name:str, state:int, vm:int,
         mem_used:u32, mem_total:u32, stack_hw:u32}
frame : {idx:int, func:str, file:str, line:int}
var   : {name:str, type:str, value:str, truncated:bool}
```

- `state`: `fmrb_proc_state_t` の数値 (FREE/INIT/RUNNING/SUSPENDED/STOPPING)。
- `vm`: `fmrb_vm_type_t` の数値 (0=mruby)。
- var.value の整形は型別フォーマッタ (`mrb_inspect` は使わない):
  - Integer/Float/Symbol/true/false/nil: そのまま文字列化
  - String: 先頭 64 バイト + `truncated`
  - Array/Hash: `Array(len=N)` / `Hash(size=N)` サマリ
  - その他: `#<ClassName>`

## 4. イベント一覧 (Phase 1)

| event | payload |
|---|---|
| `stopped` | `{pid:int, reason:str, file:str, line:int, bp_id?:int}` |
| `resumed` | `{pid:int}` |
| `exited` | `{pid:int}` |
| `output` | `{lines:str}` (log_stream 有効時。Phase 1 は log_read ポーリングでも可) |

- `stopped.reason`: `"breakpoint"` / `"step"` / `"pause"`。`bp_id` は breakpoint 時のみ。

## 5. 制約 (Phase 1)

- 同時デバッグセッション: 1 クライアント。
- attach 可能な VM: 最大 `FMRB_DEBUG_MAX_ATTACH` (=4)。
- 1 VM あたりの BP 数: 最大 `FMRB_DEBUG_MAX_BP`。
- attach 中の pid への debugd 経由 kill/suspend は `FMRB_ERR_BUSY` で拒否。
- BP の file 照合は basename 比較 (パス揺れ吸収)。厳密化はホスト側 pathMappings の責務。

## 6. 変更履歴

- 2026-07-16: 初版 (Phase 1 実装開始時)。
- 2026-07-21: BLE GATT トランスポートのフレーミング / UUID を追記 (Phase 3b)。
