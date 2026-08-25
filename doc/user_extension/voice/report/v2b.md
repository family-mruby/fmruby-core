# V2b 報告: クラウド TTS (OpenAI) — PC なしで喋る

指示書 `../instruction_v2b.md` の実装記録。V2 の tts サービスに 3 段目を
足し、VOICEVOX を動かす PC が無くても新しい文章を喋れるようにした。

## T0: 素振り

### 4. sim での TLS 検証 (鍵が要らないので先に済ませた)

- **core コンテナには `/etc/ssl/certs/ca-certificates.crt` がある**
  (293 通)。posix ビルドには CA が焼き込まれていないので、`SSLContext#
  ca_file=` にこのパスを渡す。**実際に api.openai.com への TLS が sim で
  通った**ので、`tls_verify = false` の逃がしは**使わずに済んでいる**
  (設定としては用意してあるが、既定 true のまま触っていない)。
- 実機は `esp_crt_bundle` が既定で付くので何も要らない。

### 1-3. 応答形式・WAV の形・モデル

**鍵は有効だが、アカウントに残高が無かった** (`429 insufficient_quota`、
"You have no credits remaining")。そのため**音声そのものはまだ 1 度も
受け取れていない**。分かったのは次まで。

- 認証は通る (401 ではなく 429)。**鍵・ヘッダ・URL・TLS はすべて正しい**。
- HTTP/1.1 での**エラー応答は Content-Length** (283 バイト)。音声本体が
  chunked で来るかどうかは 200 を貰うまで確かめられない。
- WAV の形 (レート・チャンネル数) も同様に未確認。

**chunked は届く前提で先に実装した**ので、当たりがどちらでも困らない。
クラウドが音声を流しながら返すのはありふれた形で、うちのクライアントは
Content-Length 専用だった。16 進の長さ行を読むだけ (正規表現不要、転送量も
増えない)。

モデルと声の既定はユーザ判断で **`gpt-4o-mini-tts` / `alloy`**。どちらも
`[tts.config]` の `cloud_model` / `cloud_voice` で変えられる。

## T1: 実装

### HTTP クライアント (`TtsHttp`)

- `post(host, port, path, body, content_type, tls = false, headers = nil)`。
  **TLS は SSLSocket に差し替えるだけ**で、あとは同じ経路。
  `SSLSocket.open(host, port, ctx)` は名前解決・接続・handshake を 1 回で
  やるので、`new` + `connect` より短い。
- **chunked 復号**を追加。長さ行の `;extension` も大文字の 16 進も読む。
  尻切れ (約束より短い) と 0 チャンクが来ないまま終わる形は拒否する。
  Content-Length と両方あるときは Content-Length を優先。
- `Host:` ヘッダはスキーム既定のポート (443/80) では省く形にした
  (明示の `:443` を拒む API があるため)。

### SSL 側のタイムアウトも設定に追従させた

V2 で `TCPSocket.timeout_ms=` を足したが、**esp32 の SSL ポートはそれを
見ていなかった**。2 か所直した。

- 読みの時間切れ: `mbedtls_ssl_conf_read_timeout` に固定の
  `FMRB_SOCKET_IO_TIMEOUT_MS` を渡していたのを `TCPSocket_get_timeout_ms()`
  に。
- **接続には時間制限が無かった**。`mbedtls_net_connect` はブロッキングで、
  経路はあるが黙っている相手に対して stack が諦めるまで (1 分超) VM の
  タスクを握る。TCPSocket と同じ形 (非ブロッキング + select) の
  `ssl_net_connect_timeout()` に置き換えた。
- posix の SSL は下回りに `TCPSocket_create` / `TCPSocket_connect` を使って
  いるので、**設定値が最初から効いていた** (直す必要なし)。

### tts サービスの 3 段目

- 解決順は **キャッシュ → VOICEVOX (`server` 設定時) → クラウド
  (`api_key` 設定時)**。PC なし構成では `server` を書かず、キャッシュ →
  クラウド直行になる。
- **両方設定したときは VOICEVOX が答えるか、その文は喋らない**。
  失敗のたびにクラウドへ落ちると、黙って課金が続くため。
- 設定: `api_key` / `cloud_model` / `cloud_voice` / `cloud_timeout_ms`
  (既定 10000)。鍵は `/home` に平文 (V2 と同じ割り切り、config と
  サービスの両方に明記)。
- キャッシュ鍵はクラウド経由だけ `"openai:" + model + voice` を材料にする
  (同じ文でも VOICEVOX とは別ファイル)。**VOICEVOX 側の鍵は変えていない**
  ことを host テストで固定した (既存のキャッシュを無効にしないため)。
- 401 は専用のログ文言にした。外から見ると鍵の誤りは不達と区別がつかず、
  その区別こそが要る情報なので。

## T2: 検収 (sim, Modern)

| 見たこと | 結果 |
|---|---|
| TLS が通る | **api.openai.com:443 へ handshake 成功、証明書検証つき** (`/etc/ssl/certs/ca-certificates.crt`)。`tls_verify` は既定 true のまま |
| 鍵誤り (401) | `tts: cloud rejected the api_key (401)` 1 行、**325 ms** で戻る、ホスト健在 |
| 不達 + 時間切れ | 届かない相手に対し `cloud failed (RuntimeError: SSL connection failed)` が **10029 ms** (`cloud_timeout_ms = 10000` どおり)、ホスト健在 |
| 「キャッシュ → クラウド直行」の順序 | `server` 未設定 + `api_key` ありで `tts ready (openai/gpt-4o-mini-tts)`、以後クラウドへ直行 |
| chunked 復号・ヘッダ組み立て・JSON 逃がし | host テスト (実 API 不要) |

`rake test` の tts スイートは 40 件超に増えた。chunked (拡張つき長さ行 /
大文字 16 進 / 尻切れ / 0 チャンク無し / バイナリ)、JSON 文字列の逃がし、
クラウドとローカルで鍵が分かれること、**既存の VOICEVOX 鍵が変わって
いないこと**。

## T2: 検収 (Tab5 実機)

`server` を書かず `api_key` だけの構成 (= PC なし運用の本番形) で確認。

| 見たこと | 結果 |
|---|---|
| 起動 | `tts ready (openai/gpt-4o-mini-tts)`、`Guru`/`abort` 0 件 |
| TLS が実機で通る | `esp_crt_bundle` の既定のまま api.openai.com へ handshake 成功 (`tls_verify` は触っていない) |
| **TLS 1 接続あたりの内蔵 RAM** | **6200 バイト** (`free 92428 -> 86228`)。plan の見積もり ~50KB より**一桁小さい** |
| 往復 | 429 の応答まで約 2 秒、ホスト健在 |
| 鍵の状態 | 429 の本文がログ 1 行に出るので、鍵誤りか残高切れかが**その場で分かる** |

### サービスファイルには実機だけの上限がある (今回いちばんの発見)

**1 ファイルに全部入れた tts.rb (21KB) は、実機でサービスホストを黙って
落とした**。sim では同じファイルが通る。壊れ方が厄介で、

- 例外は出ない (`[Services] No exception detected`)
- `services: cannot load ...` も出ない (`require` の rescue に入らない)
- ホストが「正常終了」して自動再起動を繰り返す
  (`Service host died unexpectedly (1); restarting`)
- **その手前まで起動したサービスは動いている**ので、一覧の途中で切れる

切り分けの結果:

| 置いたもの | 結果 |
|---|---|
| V2 版 tts.rb (11.3KB) | 起動する |
| V2b 版 tts.rb (21.0KB、1 ファイル) | **ホストが落ちる** |
| V2b 版からコメントを全部削除 (13.2KB) | **やはり落ちる** |
| V2b 版を 2 ファイルに分割 (8.1KB + 12.9KB) | 起動する |

**バイト数ではなくコード量が効いている** (コメントを落としても直らない)。
compile は `require` の中で走るので、山は 1 ファイルあたり。分割すれば
順番に compile されて両方収まる。

対処は**分割**にした (V2 の指示書が選択肢として認めていた形)。
`tts_http.rb` (HTTP クライアント) と `tts.rb` (サービス本体) に分け、
本体の冒頭で `require "/usr/share/services/tts_http"`。sim・実機とも起動を
確認した。host テストに**両ファイルとも実コードで 10KB 未満**であることを
足したので、次に太らせたときはホストではなく `rake test` が止める。

**目安**: サービス 1 ファイルはコメントを除いて 10KB 程度まで。超えるなら
分けること。この落とし方を知らないと、原因の見当がつかない類のもの。

### 残り (アカウントの残高が要る)

- 実 API で 1 回合成して鳴らす。2 回目がキャッシュで鳴ること。
- そのときに初めて分かること: **音声本体が chunked か Content-Length か**、
  **WAV のレートとチャンネル数** (play_wav の対応内かどうか)。
  どちらの転送形式でも読めるようにはしてある。
- 喋り方の官能評価。
