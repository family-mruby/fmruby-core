# Phase 3c 実装計画書: ホスト側 BLE バックエンド + VSCode 接続

作成日: 2026-07-21
ステータス: 未着手 (本書が実装指示)
対象読者: 本フェーズを実装する AI / 開発者。本書のみで着手できる粒度で書く。
背景が必要な場合は以下を参照する。

- 全体設計: doc/remote_debug/vm_remote_debug_design.md
- 全体計画: doc/remote_debug/vm_remote_debug_impl_plan2.md sec 5 (本書はその詳細化・確定版)
- ワイヤ仕様 (正、Phase 3b で確定): doc/remote_debug/vm_remote_debug_protocol.md sec 1 "BLE GATT"
- デバイス側実装: main/drivers/ble/ble_task.c (debug service)、
  main/drivers/debug/fmrb_debug_transport_ble.c、main/drivers/ble/ble_framing.c

## 1. ゴールと完了条件

Python 製デバッグクライアント / DAP アダプタに BLE トランスポート (bleak) を追加し、
VSCode から ESP32 実機 (S3) の VM を BLE 経由でデバッグできるようにする。

完了条件 (実装 AI が自律確認できる範囲):

1. フレーミングコーデック (COBS + CRC32) の Python 実装が固定テストベクタで
   デバイス実装 (ble_framing.c) とバイト一致する (セルフテストを同梱し PASS)。
2. TCP パス無退行: `tool/debug/test_phase1.sh` / `test_phase2.sh` が PASS
   (Linux sim。fmruby-graphics-audio が ESP32 ビルド状態で残っていると起動に
   失敗するので、その場合は graphics 側で `rake clean_all && rake build:linux`)。
3. VSCode 拡張が `npx @vscode/vsce package` で VSIX 化できる。
4. ドキュメント更新 (sec 9) とコミット準備 (git 操作はユーザ指示を待つ)。

実機 BLE E2E (S3 "Retro") はユーザ作業。sec 8.2 の手順書を提供すること。

## 2. スコープ外 (やらないこと)

- デバイス側の変更 (Phase 3b で完了済み。必要が生じたら実装せず報告する)
- P4 実機の実測・調整 → Phase 3d
- evaluate / log_stream push / DEBUGGING 状態 / アダプタ exe 化 → Phase 4
- BLE 自動再接続 (切断 -> 再 attach はユーザが VSCode で再起動する運用。
  E2E で不便が確認されたら Phase 3d/4 で検討)

## 3. プロジェクト規約 (本作業に効くもの)

- コード内コメント・docstring は英語。絵文字・非 ASCII 記号は使わない
  (日本語ドキュメント .md は除く)。
- 対象は 2 リポジトリにまたがる:
  - fmruby-core (tool/debug/*.py, doc/*.md) — このリポジトリでコミット
  - family-mruby リポジトリルート (vscode-fmrb-debug/) — ルートでコミット
  - 両者は独立 git リポジトリ (submodule ではない)。
- git 操作 (commit/push) はユーザの指示を得てから行う。
- VSCode 拡張は VSIX インストール運用。package.json / extension.js を変更したら
  再パッケージし、ユーザに再インストールを案内する (リロードでは反映されない)。

## 4. 現状の構造 (2026-07-21 時点の facts)

### 4.1 ホスト側 Python (fmruby-core/tool/debug/)

- `fmrb_dbg_client.py` (316 行): `FmrbDebugClient` — 同期 request/response +
  背景 reader スレッド。**TCP 直書き**: `connect()` が `socket.create_connection`、
  `_send_frame()` が u32 BE 長プレフィクス付与、`_read_loop()` -> `_feed()` が
  長プレフィクスで再組み立てし `_dispatch()` (seq 突き合わせ + event コールバック)
  へ渡す。CLI (one-shot / REPL) 同梱。target 書式は `host[:port]`
  (`_parse_hostport`、既定ポート 5555)。
- `fmrb_dap_adapter.py` (370 行): DAP (stdio) <-> FmrbDebugClient。
  `req_attach()` が launch args から `host` / `port` を読んで
  `FmrbDebugClient(host, port, on_event=...)` を直接生成 (222 行付近)。
  行マッパー (combined.map.json) はトランスポート非依存で変更不要。
- `test_phase1.py` / `test_phase2.py`: 自律テスト。target を自前で
  `host:port` パースして client を生成。
- msgpack プロトコル本体 (`[type, seq, cmd, payload]`) はトランスポート非依存。

### 4.2 VSCode 拡張 (family-mruby/vscode-fmrb-debug/)

- `package.json`: debug type "fmrb"。`extensionKind: ["workspace", "ui"]`
  (現状 Remote-WSL では WSL 側で動く。TCP 開発向けの暫定で、
  **BLE には Windows 側実行が必須なので "ui" 優先に戻すのが本フェーズの作業**。
  この経緯は impl_plan2 sec 2.2 参照)。
- launch 属性: host / port / app / pythonPath / adapterPath / pathMappings /
  projectMappings / combinedMaps。transport の概念はまだ無い。
- `extension.js`: `DebugAdapterExecutable(python, [fmrb_dap_adapter.py])` を
  spawn する。adapterPath 未指定時は近傍の fmruby-core checkout を探す。
- インストールは VSIX (`npx @vscode/vsce package` -> Install from VSIX)。

### 4.3 BLE デバイス側 (Phase 3b で実装済み、変更不可の前提)

- GATT UUID (文字列形式は tool/web/js/app.js のファイルサービス版と同規則:
  `BLE_UUID128_INIT` のリトルエンディアン配列を逆順にしたもの):
  - debug service: `4652414d-4252-5942-4c45-000000000005`
  - debug RX (host -> device, WRITE | WRITE_NO_RSP): `...-000000000006`
  - debug TX (device -> host, NOTIFY): `...-000000000007`
- デバイス名: `Family-mruby-XXXXXX` (XXXXXX = MAC 下位 3 バイトの小文字 hex。
  例: Family-mruby-c4823e)。"FamilyMruby" 固定ではない。
- フレーム: `COBS([u16 BE body_len][msgpack body][CRC32 4B BE]) + 0x00`。
  CRC32 は多項式 0xEDB88320 (= Python の `zlib.crc32` / `binascii.crc32` と同一)、
  COBS 前の平文に対して計算。body 最大 4096 (`FMRB_DEBUG_MAX_FRAME`)、
  エンコード後最大 4120 (`BLE_DBG_MAX_ENC`)。
- デバイスの notify は MTU-3 バイトに分割して届く (ホストで 0x00 まで再結合)。
- デリミタ以降に同一 write で続くデータは捨てられる。**1 write に 1 フレーム断片
  のみ**、フレームは複数 write に分割してよい (access_cb が蓄積する)。
- 「接続」の条件はGATT 接続 + debug TX 購読。切断で debugd は全 VM を detach する。
- **登録前フレーム喪失**: BLE 接続確立から debugd のトランスポート登録までの隙間で
  ホストが送ったフレームは黙って捨てられる。ホストはリトライで回復すること
  (protocol.md 記載の約束)。
- 暗号化・ペアリングは要求しない (fs サービスと同じ。Windows のペアリング
  ダイアログは出ない設計)。

### 4.4 実行環境の制約

- 開発ツリーは WSL2。**WSL2 からは Bluetooth にアクセスできない**ため、
  BLE を使うプロセス (DAP アダプタ) は Windows 側の Python で動かす。
  拡張を `extensionKind: ["ui"]` にすると Remote-WSL 接続でも拡張・アダプタが
  Windows 側で動く。ワークスペースは `\\wsl$\...` UNC パス越しになるが、
  Windows Python は UNC パスのスクリプト実行・ファイル読みが可能。
- TCP (Linux sim) は Windows からも `localhost:5555` で届く (WSL2 の
  localhost フォワード) ので、"ui" 固定で TCP / BLE 両立できる。
- 実装 AI の環境 (WSL) では bleak の実 BLE 動作は検証できない。
  検証はコーデックのセルフテストと TCP 回帰までとし、BLE はユーザ E2E。

## 5. 設計方針

1. **トランスポート抽象を fmrb_dbg_client.py に導入する。**
   `FmrbDebugClient` は「msgpack body の送信」「受信 body の dispatch」だけを
   持ち、フレーミングと I/O はトランスポートオブジェクトへ出す。
2. **target 文字列でトランスポートを選ぶ** (CLI / テスト / アダプタ共通):
   - `host[:port]` -> TCP (従来どおり、既定)
   - `ble` -> BLE、`Family-mruby-` プレフィクスをスキャンして 1 台なら接続
   - `ble:<name-or-address>` -> BLE、名前完全一致または MAC アドレス指定
3. **bleak は遅延 import** (BLE を使わない限り不要。WSL の TCP 運用や CI で
   bleak 未インストールでも壊れないこと)。
4. **コーデックは純関数で分離しセルフテスト同梱** (デバイスと突き合わせ可能な
   固定ベクタ)。CRC32 は `binascii.crc32` を使う (多項式が同一)。
5. 同期クライアント + 背景スレッドの構造は維持する。BLE は asyncio (bleak) を
   デーモンスレッドのイベントループに閉じ込め、同期 API へブリッジする。

## 6. 実装項目

### 6.1 fmrb_dbg_client.py のトランスポート分離

```python
class TcpTransport:
    """u32 BE length-prefixed msgpack bodies over a TCP socket."""
    def __init__(self, host, port): ...
    def connect(self): ...                    # raises on failure
    def close(self): ...
    def send_body(self, body: bytes): ...
    def set_body_handler(self, cb): ...       # cb(body: bytes), reader thread
```

- 現行 `FmrbDebugClient` の `_send_frame` / `_read_loop` / `_feed` を
  `TcpTransport` へ移動 (Legacy コードは残さない)。
- `FmrbDebugClient(transport, on_event=None, timeout=5.0)` に変更し、
  `FmrbDebugClient.from_target(target, on_event=..., timeout=...)` classmethod で
  target 文字列から生成できるようにする。**呼び出し元 3 箇所
  (fmrb_dap_adapter.py, test_phase1.py, test_phase2.py) と CLI も更新する**
  (旧 (host, port) シグネチャの互換維持は不要、全部書き換える)。
- 応答タイムアウトの既定 5.0 秒は維持。BLE では応答に 1 秒程度かかり得るが
  5 秒で足りる想定 (足りなければ E2E 後に調整)。

### 6.2 BLE フレーミングコーデック (tool/debug/fmrb_ble_framing.py 新規)

純関数のみ。bleak に依存しない。

```python
def cobs_encode(data: bytes) -> bytes
def cobs_decode(data: bytes) -> bytes        # stops at 0x00 or end
def encode_frame(body: bytes) -> bytes       # [len BE][body][crc BE] -> COBS + b"\x00"
def decode_frame(frame: bytes) -> bytes | None   # verify crc/len, None if bad
```

- `encode_frame`: body > 4096 は ValueError。CRC は `binascii.crc32` (4B BE)。
- `decode_frame`: COBS デコード -> 平文 6 バイト未満 / CRC 不一致 /
  len != decoded_len - 6 なら None (呼び出し側が警告して破棄)。
- **セルフテスト**を `if __name__ == "__main__":` に同梱し、以下を検証:
  - COBS 既知ベクタ (例: `b"\x00"` -> `b"\x01\x01"`、`b"\x11\x22\x00\x33"` ->
    `b"\x03\x11\x22\x02\x33"`、254 バイト非ゼロ連続の境界ケース)
  - encode -> decode の往復一致 (0 / 1 / 4096 バイト body、0x00 を含む body)
  - デバイス実装との一致確認用の固定フレーム 1 本
    (例: body = `msgpack.packb([0, 1, "version", None])` の encode 結果の
    hex ダンプをコメントに記録。実機疎通で食い違ったらここを比較する)

### 6.3 BleTransport (tool/debug/fmrb_ble_transport.py 新規)

`TcpTransport` と同一のインタフェース。内部は bleak + asyncio を
デーモンスレッドのイベントループに隠蔽する。

- UUID 定数 (sec 4.3 の 3 本) と名前プレフィクス `Family-mruby-` を定義。
- `connect()` (同期、タイムアウト 15 秒程度):
  1. イベントループスレッド起動。
  2. 指定が MAC アドレス形式ならスキャン省略で直接接続。名前指定なら
     `BleakScanner` で完全一致を探す。無指定なら `Family-mruby-` プレフィクスで
     スキャンし、1 台なら接続、複数台なら候補一覧を含むエラー、0 台ならエラー。
  3. `BleakClient` 接続 -> debug TX (`...0007`) を `start_notify`。
     notify データはバイト蓄積し、0x00 出現ごとに `decode_frame` ->
     成功なら body handler へ (handler はイベントループスレッドから呼んでよい。
     client 側 dispatch はスレッドセーフ)。
  4. 切断コールバック (`disconnected_callback`) で保留中 request を全て
     エラー解放し、以後の `send_body` は例外にする。
- `send_body()`: `encode_frame` -> チャンク分割 -> RX (`...0006`) へ
  write-without-response で順次送信。チャンクサイズは
  `client.mtu_size - 3` が取れればそれ、取れなければ 20 にフォールバック。
  イベントループへは `asyncio.run_coroutine_threadsafe` でブリッジし、
  完了を同期待ちする (タイムアウト付き)。
- 依存: `bleak` (>= 0.21 目安) と `msgpack`。import はモジュール先頭で行うが、
  `fmrb_dbg_client.py` 側がこのモジュールを **BLE target のときだけ** import
  する構造にする (TCP 利用時に bleak 不要)。

### 6.4 接続直後のリトライ (登録前フレーム喪失対策)

`FmrbDebugClient` に接続確立処理を追加: `connect()` 後、最初に `version` を
タイムアウト 2 秒 x 最大 3 回リトライで送る (`from_target` 経由の生成時のみ
でもよい)。3 回とも無応答なら接続失敗として例外。これで sec 4.3 の
登録前喪失とサブスクライブ直後の取りこぼしを吸収する。TCP でも同じパスを
通す (既存 debugd は version に即応するので回帰しない)。
test_phase1/2 の version 呼び出しと二重になっても害はない。

### 6.5 DAP アダプタと launch 属性

- `req_attach()`: 新 args `transport` ("tcp" | "ble"、既定 "tcp") と
  `deviceName` (省略可)。target 文字列を組み立てて
  `FmrbDebugClient.from_target()` を使う:
  - tcp: `f"{host}:{port}"` (従来既定と同じ)
  - ble: `"ble"` または `f"ble:{deviceName}"`
- 接続失敗 (BLE スキャン 0/複数台、リトライ枯渇) は attach 失敗応答の
  message にそのまま載せる (VSCode に表示される)。

### 6.6 VSCode 拡張 (family-mruby/vscode-fmrb-debug/)

1. `package.json`:
   - `extensionKind` を `["ui"]` のみにする (sec 4.4 の理由。TCP も ui で動く)。
   - launch 属性に `transport` (enum ["tcp", "ble"]、既定 "tcp") と
     `deviceName` (string、説明に `Family-mruby-XXXXXX` 形式と省略時スキャンを明記)
     を追加。
   - `configurationSnippets` に BLE attach のスニペットを 1 本追加
     (transport: "ble"、app 指定、deviceName はプレースホルダ)。
   - description の "(Phase 2: TCP / Linux sim)" を現状に合わせて更新し、
     version を 0.0.2 に上げる。
2. `extension.js`: 変更は原則不要 (アダプタ引数は launch args 経由で渡るため)。
   pythonPath の既定 "python3" は Windows では通らないことがあるので、
   README に Windows での設定例 (`"pythonPath": "py"` またはフルパス) を書く。
3. README.md / README.ja.md: Windows セットアップ手順を追記:
   - Windows 側 Python に `pip install bleak msgpack`
   - launch.json 例 (transport: "ble", deviceName, pythonPath)
   - 拡張が Windows 側 (UI ホスト) で動くこと、TCP は localhost フォワードで
     従来どおり動くこと
   - VSIX 再インストール手順 (`npx @vscode/vsce package` -> Install from VSIX)
4. VSIX を再パッケージし、生成物のパスをユーザに報告する
   (インストールはユーザ操作)。

### 6.7 テストスクリプト

- `test_phase1.py` / `test_phase2.py`: target パースを `from_target` に置き換える
  (これで `ble:Family-mruby-xxxxxx` を渡せば実機にも流用できる)。
  シェルスクリプト (Linux sim / TCP) は変更不要。
- 新規の自動テストは 6.2 のセルフテストのみ (BLE 実機は自動化対象外)。

## 7. 実装しないことの明確化

- デバイスコードには触れない。ワイヤ仕様が合わない疑いが出たら、デバイス側を
  直さず protocol.md と突き合わせて報告する。
- アダプタの自動再接続・BP 再送は実装しない (VSCode のデバッグ再起動で足りる
  かを E2E で観察してから判断)。
- exe 化 (pyinstaller) は Phase 4。

## 8. 検証

### 8.1 実装 AI が行うもの

1. `python3 tool/debug/fmrb_ble_framing.py` セルフテスト PASS。
2. `python3 -m py_compile` 相当で全変更ファイルの構文確認
   (bleak が WSL に無くても import エラーにならない構造の確認を含む。
   `python3 -c "import fmrb_dbg_client"` が bleak 無しで通ること)。
3. TCP 回帰: 両リポジトリ Linux ビルド済みの状態で
   `fmruby-core/tool/debug/test_phase1.sh` / `test_phase2.sh` PASS。
4. `cd vscode-fmrb-debug && npx @vscode/vsce package` が成功。

### 8.2 ユーザ E2E 手順書 (実装 AI が README または進捗ログに整備する)

S3 実機 (Retro) で:

1. Windows Python に bleak / msgpack をインストール。
2. VSIX 再インストール -> VSCode 再起動。
3. launch.json に BLE 構成を追加 (transport: "ble"。deviceName は起動ログの
   `BLE device name:` 行の値。省略時はスキャン)。
4. 実機を起動し、適当なアプリ (例: Kamon) を起動 -> VSCode で attach。
5. 確認項目 (impl_plan2 sec 5.3 の再掲 + 3b 積み残し):
   - attach -> BP -> 停止 -> stack/vars -> step -> continue -> disconnect の一連
   - パーク中の他アプリ操作・描画が生きていること、WDT/監視系の誤検知が無いこと
   - BLE 切断 (電波遮断/ホスト kill) -> デバイスログに detach_all が出て
     VM が走行再開すること
   - 再接続 -> 再 attach -> BP 再設定で復帰できること (VSCode デバッグ再起動で可か)
   - 応答体感 (stack_trace / frame_vars の遅延)。遅ければ数値を記録して 3d へ

## 9. ドキュメント更新とコミット

1. `doc/remote_debug/vm_remote_debug_protocol.md`: ホスト側の注意 (チャンクサイズ、
   version リトライによる接続確立) を BLE 節に 1 段落追記。
2. `doc/remote_debug/vm_remote_debug_progress.md`: Phase 3c 実施記録。
3. `doc/remote_debug/vm_remote_debug_impl_plan2.md`: sec 5 冒頭にステータス行を追記。
4. コミット (ユーザの指示を得てから):
   - fmruby-core: tool/debug の変更 + doc 更新
   - family-mruby ルート: vscode-fmrb-debug の変更 (VSIX 生成物はコミット対象に
     するか現状の扱いに合わせる。既存の fmrb-debug-0.0.1.vsix がコミット済みなら
     0.0.2 も同様に追加)
   - いずれも数行の英文コミットログ。

## 10. リスク・注意

| 項目 | 内容 | 対応 |
|---|---|---|
| bleak の Windows 挙動 | スキャンが Bluetooth OFF や省電力設定で空振りする | deviceName 直指定 (スキャン省略に近い動作) を用意。エラーメッセージに候補と対処を書く |
| MTU / write サイズ | WinRT で mtu_size が取れない場合がある | 20 バイトへフォールバック (遅いが動く)。実測は 3d |
| asyncio ブリッジ | 同期待ちのデッドロック | すべての run_coroutine_threadsafe に タイムアウトを付け、超過は例外 + 切断扱い |
| 登録前フレーム喪失 | 接続直後の最初のコマンドが消える | version リトライで接続確立を確認してから上位コマンドを流す (6.4) |
| UNC パス実行 | Windows Python が \\wsl$ のスクリプトを実行 | 動作するが、初回 E2E で問題が出たらアダプタ一式を Windows 側へコピーする逃げ道を README に記載 |
| 拡張 ui 化の影響 | TCP デバッグも Windows 側プロセスになる | localhost フォワードで動作する想定。E2E で TCP も 1 回確認する |
| VSIX 更新漏れ | 拡張の変更が反映されない | 再パッケージ + 再インストールを完了条件に含める (ユーザ操作) |
