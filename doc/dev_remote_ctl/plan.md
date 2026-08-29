# WiFi 経由の開発用リモート制御(アプリ起動 / kill / 一覧)実装計画

## 目的

Claude(および開発者)が実機(Tab5 / ESP32-P4)の検証を、速く・確実に回せるようにします。
いま時間を溶かしているのは次の 2 点で、本計画はその片方(操作)を WiFi で潰します。

- **アプリ起動がランチャー操作頼み**で壊れやすい(メニュー→Launcher→スクロール/矢印→Enter)。
  → **パス指定で直接起動**できれば消える。
- 稼働アプリの後始末(kill→起動し直し)や状態把握が手作業。→ **kill / 一覧**を足す。

もう片方(クラッシュ/ブートログ)は本計画の対象外です(理由は下記)。

## 方針

- **WiFi は制御プレーン専用**にする(アプリ起動 / kill / 一覧)。**ログ取得は載せない**:
  クラッシュ時は WiFi ごと落ちるので、肝心のクラッシュログが取れず、あてにできない。
  クラッシュ/ブートログは**セッション開始時に開きっぱなしにしたシリアル**で取る運用に固定する
  (見たい時に開くとリセットする)。
- **既存の remote desktop HTTP サーバ(rd_http)に相乗り**する。WiFi・HTTP・`/status` は既にある。
- **土台は実績のある関数を使う**。`debugd` が既に同じことを自前タスクからやっている:
  - 起動: `fmrb_app_spawn_app(const char *path, int32_t *out_pid)`(パス or 組み込み名)。
  - kill: `fmrb_app_kill(int32_t pid)`(ほか `stop` / `suspend` / `resume` もある)。
  - 一覧: debugd の `DBG_CMD_PS` の列挙。
  httpd タスクからの呼び出しは debugd タスクと同じ文脈なので、リスクは小さい。
- **P4(Modern)限定**。remote desktop がある機種だけ。S3(Retro)は debugd が BLE のみなので対象外。

## スコープ(最小)

エンドポイント 3 つだけ。RPC 的な作り込みはしない。

- `POST /app/launch` … パス/名前でアプリ起動
- `POST /app/kill` … pid でアプリ終了
- `GET  /app/list` … 稼働アプリ一覧(または `/status` を拡張)

## エンドポイント仕様

- **起動**: `POST /app/launch?path=<PATH>`(または JSON `{"path": "..."}`)
  - `<PATH>` は `fmrb_app_spawn_app` にそのまま渡す(例 `/app/demo/mic_spectrum.app.rb`、
    組み込み名も可)。
  - 応答: `{"ok":true,"pid":<id>}` / 失敗時 `{"ok":false,"err":<code>}`。
  - 実体: `fmrb_app_spawn_app(path, &pid)` を呼ぶだけ(debugd の `handle_spawn` と同じ)。
- **kill**: `POST /app/kill?pid=<PID>`
  - 応答: `{"ok":true}` / `{"ok":false}`。
  - 実体: `fmrb_app_kill(pid)`。将来 `stop`/`suspend`/`resume` も同型で足せる。
- **一覧**: `GET /app/list`(または `/status` に `apps` 配列を追加)
  - 応答: `{"apps":[{"pid":4,"name":"Mic Spectrum","state":"RUNNING"}, ...]}`。
  - 実体: debugd の `DBG_CMD_PS`(`handle_ps`)が使う列挙を再利用する。公開列挙 API が
    無ければ、その内部列挙を小さな関数に切り出して共有する。

## 実装ポイント

- `main/drivers/remote_desktop/rd_http.c`:
  - `launch_handler` / `kill_handler` / `list_handler` を `status_handler` と同じ書き方で追加。
    クエリ取得は `httpd_req_get_url_query_str` + `httpd_query_key_value`。
  - `start_server()` の登録列(`uri_status` 等の並び)に 3 つ `httpd_register_uri_handler` する。
  - include: `fmrb_app.h`(kill)、`fmrb_app_spawner.h`(spawn)、一覧用の列挙ヘッダ。
- **タスク文脈**: debugd は自前タスクからこれらを直接呼べている。httpd タスクからも同様に
  直接呼ぶ(新しいキューは作らない)。ただし kill は文脈次第で固まる既知の注意がある
  (`doc/archive/app_kill_fix`)。**自分自身や kernel を kill しない**ガードを入れ、ユーザアプリの
  pid だけ受け付ける。
- **入力の妥当性**: `path` 長制限、`pid` 範囲チェック。失敗は 4xx + JSON で返す(端末を
  落とさない)。

## Ruby ツール(既存 `fmrb_rd_*` に揃える)

- `tools/fmrb_rd_launch.rb <IP> <path>` … `POST /app/launch`。
- `tools/fmrb_rd_kill.rb <IP> <pid>` … `POST /app/kill`。
- `tools/fmrb_rd_ps.rb <IP>` … `GET /app/list` を整形表示。
- いずれも curl でも叩ける素の HTTP。標準ライブラリのみ(周辺ツールは Ruby 方針)。

## dev フラグとセキュリティ

- **新しいセキュリティ境界は増えない**。remote desktop で既にランチャーを遠隔操作=任意アプリ
  起動が可能。本機能はその近道にすぎない。README も「WiFi/BLE はアクセス制御なし」と明記済み。
- とはいえ露出は広がるので、**dev ビルド限定のコンパイルフラグ**(例 `FMRB_DEV_REMOTE_CTL`)で
  3 エンドポイントの登録を囲む。リリースビルドでは登録しない。
- README に「開発用・無認証」と 1 行足す。

## 実装手順(段階)

- **D1: launch のみ**。`/app/launch` + `fmrb_rd_launch.rb`。ランチャー操作を置き換えて、
  Mic Spectrum / raycaster などをパス起動で上げられることを確認。ここが一番効く。
- **D2: kill**。`/app/kill` + `fmrb_rd_kill.rb`。自己/kernel 保護ガード込み。
- **D3: 一覧**。`/app/list`(or `/status` 拡張)+ `fmrb_rd_ps.rb`。pid を取れるようにして
  kill とつなぐ。
- **D4: dev フラグで囲う** + README 追記。

## 検証(この機能自体のテスト)

- Linux sim には remote desktop が無いので、**実機(Tab5)で確認**する。
- 手順: `curl -X POST "http://<IP>/app/launch?path=/app/demo/spinel_hello.app.rb"` →
  `fmrb_rd_snap.rb` で窓が出ることを確認 → `/app/list` で pid 確認 → `/app/kill?pid=<pid>` →
  スナップでデスクトップに戻ることを確認。
- クラッシュ観測が要る検証は、**開始時に張った常設シリアル**を tail する(本機能では取らない)。

## 対象外・非目標

- **WiFi ログ取得はやらない**(クラッシュで WiFi が落ちるため無意味)。クラッシュ/ブートログは
  常設シリアル。
- ブレークポイント等の本格デバッグは debugd の領分。ここでは触らない(将来、下記 Option B で
  合流の余地あり)。

## 発展(将来の選択肢、今はやらない)

- **Option B: rd_http を debugd の代替トランスポートにする**。debugd は PS/KILL/SPAWN/STOP/
  SUSPEND/RESUME/ブレークポイント等の一式を既に持つ。rd_http から `fmrb_dbg_req_t` を debugd の
  キューへ渡して応答を返せれば、**全デバッグコマンドが WiFi で使える**うえ実装が一本化される。
  ただし debugd を BLE トランスポートから切り離す改修が要るので、まずは本計画の直接呼び出し
  (Option A)で小さく始める。

## 落とし穴

- **kill の文脈依存ハング**(`doc/archive/app_kill_fix`)。自己/kernel を kill しない。ユーザアプリ限定。
- **一覧の列挙 API が未公開**なら、debugd の PS 内部列挙を共有関数に切り出す(重複実装しない)。
- httpd ハンドラは端末を落とさないこと(不正入力は JSON エラーで返す)。
- これは**開発用**。リリースに残さない(dev フラグで囲う)。

---

## 実施結果 (2026-08-15)

**D1-D4 全て完了、実機 (Tab5) で検証済み**。

- `POST /app/launch?path=` / `POST /app/kill?pid=` / `GET /app/list` を
  `rd_http.c` に追加 (`FMRB_DEV_REMOTE_CTL` で囲い、既定 ON。
  リリースは `CMAKE_OPTS="-DFMRB_DEV_REMOTE_CTL=OFF"`)。
- 一覧は **`fmrb_app_ps()` が既に公開 API** だったので、debugd からの切り出しは不要だった。
- ツール: `tools/fmrb_rd_launch.rb` / `fmrb_rd_kill.rb` / `fmrb_rd_ps.rb`
  (+ 共通の `fmrb_rd_http.rb`)。標準ライブラリのみ、curl でも叩ける。
- README の Security Note に 1 項目追加。

実機での通し確認: list (kernel/desktop) → launch で raycaster が pid 4 で起動・
描画を確認 → list で RUNNING を確認 → kill で終了・デスクトップ復帰を確認。
異常系も確認: 存在しないパスは 500 + `err:-7`、`path` 無しは 400、
存在しない pid は 404、**kernel (pid 0) の kill は 400 で拒否**。Guru/abort ゼロ。

### つまずいた点

- **`httpd` の `max_uri_handlers` 既定が 8**。既存 7 本 + 新規 3 本で溢れ、
  `httpd_register_uri_handler` は **ESP_ERR_HTTPD_HANDLERS_FULL を返すだけ**なので、
  サーバは正常に起動して見えたまま**新しい経路が全部 404** になった。
  16 に引き上げ、併せて**登録の戻り値を必ず見る** `rd_register_uri()` を通すようにした
  (同じ穴を二度作らないため)。
- ツール側: 素の `readpartial` はブロックし続けるので、呼び出し側の timeout が効かない。
  `IO.select` で締め切りを持たせ、`Content-Length` 分読んだら抜けるようにした。
