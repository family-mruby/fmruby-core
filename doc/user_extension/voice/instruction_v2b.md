# 実装指示書 V2b: クラウド TTS — PC なしで喋る

対象: 実装担当セッション。作業リポジトリ: fmruby-core のみ。
先に読むもの: instruction_v2.md と report/v2.md (tts サービスと HTTP
クライアントの現状、V2b 調査の結果)。報告は report/v2b.md。

## ねらいと社の選定 (決定済み)

- ねらいは **PC なしの運用**: VOICEVOX (PC) が無くても動的な文章が喋れる。
- 採用: **OpenAI TTS** (`POST /v1/audio/speech`、`response_format: "wav"`
  で生のバイナリ WAV、認証は `Authorization: Bearer <key>` の 1 行)。
- 除外 (理由を plan に記録): Google Cloud TTS = **base64** 応答 (ユーザ
  指定で回避)。Amazon Polly = SigV4 署名の実装が端末側で重い。
- 解決順は変えない: **キャッシュ → VOICEVOX (server 設定時のみ) →
  クラウド (api_key 設定時のみ)**。PC なし構成では server を書かず、
  キャッシュ → クラウド直行になる。

## 調査済みの前提 (V2 の成果)

- TLS: SSLSocket / SSLContext があり、**esp32 は esp_crt_bundle が既定 =
  公開 API の証明書検証に CA 焼き込み不要**。時刻は timesync が満たす。
- HTTP クライアント (タイムアウトつき)・キャッシュ・play_wav は流用。

## T0: 素振り (関門 1)

鍵が要るので**ユーザから API key を受け取ってから** (この指示書を読んだら
最初に依頼する)。PC の curl で確かめて report に:

1. **応答が Content-Length か chunked か** (うちのクライアントは
   Content-Length 専用)。chunked なら T1 で chunked 復号を足す
   (16 進の長さ行を読むだけ、正規表現不要、~30 行。base64 と違い転送は
   増えない)。
2. WAV の形式 (サンプルレート・mono か)。play_wav の対応 (16bit mono
   8k-48k) に収まるか。**収まらなければ止まって相談**。
3. モデルと voice の既定をどれにするか (品質と価格で 1 つ選び、config の
   既定値として提案)。
4. **sim での TLS 検証の通し方**: Machine.posix? の枝は ca_file= を使う。
   docker (core コンテナ) 内の CA バンドルのパスを確かめる。無ければ
   `[tts.config] tls_verify = false` (sim 用の逃がし。既定 true) を許して
   report に明記。

## T1: 実装

- HTTP クライアント: 必要なら chunked 対応を追加 (host テストも)。
  HTTPS は SSLSocket を TCPSocket と同じ形で (timeout_ms の効きも確認 —
  SSLSocket 側で効かないなら T0 と同じ穴なので直すか相談)。
- tts サービスに 3 段目: `[tts.config]` に `api_key`、`cloud_model`、
  `cloud_voice`、`cloud_timeout_ms` (既定 **10000** — 合成は数秒かかる。
  その間ホストの他サービスを待たせることは v2 の割り切りのまま。実測を
  report に)。鍵は /home に平文 (V2 と同じ割り切り、文書化済み)。
- キャッシュ鍵: クラウド経由は server の代わりに `"openai:" + model +
  voice` を鍵材料に (VOICEVOX とクラウドで同文でも別ファイル)。
- 失敗系は V2 と同じ (1 行ログで黙る、再試行なし、一時名 → rename)。
  鍵誤り (401) はログの文言で分かるように。
- **TLS 1 接続のヒープ実測** (接続前後の空きを M1 相当のログで) を report に。

## T2: 検収

### sim (Modern 向け)

- 実 API で 1 回合成して鳴る (audio_probe、相対読み)。**課金を伴うので
  実 API の呼び出しは最小限に** — 検収は短い固定文 1-2 種で行い、2 回目は
  キャッシュで鳴る (= オフライン証明と課金抑制を兼ねる)。
- chunked 復号 (該当時) とヘッダ組み立ては host テストで (実 API 不要)。
- 失敗系: 鍵誤り 401 / 不達 / タイムアウト — いずれも 1 行ログでホスト健在。
  これらは**偽の TLS なし経路では作れない**ので、鍵を壊す・URL を曲げる形で
  実 API に対して各 1 回だけ。
- VOICEVOX 設定なし + api_key ありの構成で「キャッシュ → クラウド直行」の
  順序をログで確認。

### Tab5 (PC なし運用の本番形)

- **server を書かず api_key だけ**の config で、`tts/say` → 喋る (聴くのは
  ユーザ)。2 回目はオフライン (WiFi を切っても鳴る、まで見られれば理想)。
- esp_crt_bundle の検証が実機で通ること (tls_verify を触らず既定のまま)。
- 書き込み前の fmrb_rd_ps 単独確認。

## 受け入れ条件

- report/v2b.md に T0 の答え・ヒープと所要の実測・検収の記録。
- コミット 2 本: (1) クライアント拡張 + tts の 3 段目 + テスト、(2) docs
  (本書 + plan.md の V2b を確定結果に。Google/Polly の除外理由も)。
  英語、ユーザ確認のうえ。2 構成ビルド + doctor 0 + rake test。`.env` 復元。

## やらないこと

- base64 系の応答 (Google)、SigV4 (Polly)、複数社の切り替え
- ストリーミング再生 (全部受けてから鳴らす)、読み上げ待ち行列
- 鍵の暗号化保存 (平文 + 文書化のまま)
