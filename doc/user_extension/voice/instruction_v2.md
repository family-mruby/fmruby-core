# 実装指示書 V2: tts サービス (文字列 → WAV の解決役)

対象: 実装担当セッション。作業リポジトリ: fmruby-core のみ。
先に読むもの: plan.md の V2、instruction_v1.md と report/v1.md (play_wav の
現状)、services/plan.md (契約と 50ms 警告)。報告は report/v2.md。
サービスなので Modern 専用。

## 範囲と段

- **V2a (本体)**: キャッシュ + ローカル VOICEVOX (素の HTTP)。ここまでで
  「tts/say に流せば喋る、同じ文は 2 回目からオフライン」が成立する。
- **V2b (調査のみ)**: クラウド TTS (TLS)。SSLSocket / SSLContext /
  Digest は gem に存在することを確認済み。**実装はせず**、クラウド 1 社
  (WAV/PCM で返せるもの) を呼ぶのに足りない物 (CA の焼き込み方・時刻前提・
  鍵の置き場・応答サイズ) を調べて report に書くまで。

## T0: 生死に関わる下調べ (関門 1)

1. **TCPSocket の読み書きにタイムアウトを掛けられるか** (C 実装を読む。
   recv のブロッキング仕様、ノンブロッキングの口、書き込み側も)。
   **無制限ブロックの読みしか無いなら実装に入らず相談** — enable の
   Sandbox 待ちでホストが沈黙した前例と同型で、サーバが応答途中で死ぬと
   ホストごと固まる。選択肢 (小さな C の timeout setter を足す /
   ノンブロッキング + wake_in の状態機械) を並べて止まる。
2. VOICEVOX の API を PC で素振り: `POST /audio_query?text=..&speaker=N`
   (JSON が返る) → `POST /synthesis?speaker=N` (JSON を body に、WAV が
   返る)。**出力 WAV の形式を確認** (既定 24kHz 16bit mono の想定 =
   play_wav の対応内。違ったら report)。
3. キャッシュ鍵に使うハッシュ: mbedtls の Digest (SHA256) が Ruby から
   呼べるか。呼べなければ djb2 + 文字列長で足りる (衝突は実害の出る
   確率でない)。

ここまでを report に書いて**一度止まる** (タイムアウトの答え次第で設計が
変わるため)。

## T1: 最小 HTTP クライアント

- 純 Ruby で POST のみ: Content-Length 応答のみ対応 (chunked 非対応で
  よい — VOICEVOX は Content-Length を返す。違ったら T0 で分かる)。
  接続・読みとも **3 秒のタイムアウト必須** (T0 の答えの方式で)。
  応答上限 2MB (play_wav と同じ)。
- 置き場所はサービスと同じファイルか隣の required ファイルか、実装者判断
  (どちらにしたか report へ)。汎用 gem 化は**しない** (使い手が現れてから)。

## T2: tts サービス (/usr/share/services/tts.rb)

- 契約: topic `tts/say` を購読。payload `{"text"=>...}` (任意で
  `"speaker"=>N`)。解決して `ctx.audio.play_wav`。
- 解決の順: **(1) キャッシュ** `/home/voice/cache/<鍵>.wav`
  (鍵 = server + speaker + text のハッシュ hex)。**(2) VOICEVOX**
  (audio_query → synthesis → WAV をキャッシュに保存してから再生 —
  保存してから、が大事: 途中で切れても半端なファイルを play しない。
  一時名 → rename は fs/put と同じ流儀)。
- 設定 `[tts.config]`: `server` (例 "http://192.168.10.5:50021")、
  `speaker` (既定 1)、`timeout_ms` (既定 3000)。server 未設定なら
  キャッシュ専用モード (ヒットだけ喋る、ミスは 1 行ログ)。
- 失敗系: 不達・タイムアウト・変な応答は 1 行ログして黙る (再試行しない —
  読み上げは鮮度が命で、遅れて喋る方が有害)。
- **ブロッキングの割り切り (文書化)**: 合成中 (~1-2 秒) は他のサービスを
  待たせる。優先度 1 の背景どうしなので v2 は許容。タイムアウトがあるので
  無限には塞がない。実測 (キャッシュヒット / ミスの所要) を report に。
  50ms 警告は tts では出るのが正常なので、警告の文言に pid ではなく
  サービス名が出ること (S1 実装のまま) を確認。
- キャッシュの管理: 上限を数えない (v2)。100 個を超えたら起動時に警告
  1 行だけ (掃除はユーザ。`rmr /home/voice/cache` でよい、と report と
  plan に書く)。

## T3: 検収

### sim (Modern 向け)

- **偽 VOICEVOX** を tools/ か test/ に置く (PC 側で走る小さな Ruby HTTP
  サーバ。audio_query に固定 JSON、synthesis に V1 の正弦 WAV を返す)。
  sim (docker) から PC 上のサーバへは docker の gateway 経由で届く —
  届く形 (URL に何を書くか) を report に。
- `tts/say` に流す → 鳴る (audio_probe で窓 > 0。**V1 の教訓: 絶対周波数は
  APU の既知音との相対で読む**)。
- 2 回目の同文 → **偽サーバを止めても鳴る** (キャッシュの証明)。ログに
  cache hit が出る。
- server 未設定 → キャッシュ専用の動き。不達 → 1 行ログで沈黙、ホスト
  健在 (heartbeat 継続)。timeout_ms を短くして途中切断 → 同上。
- 半端なファイルがキャッシュに残らない (切断後に cache ディレクトリを ls)。
- ハッシュとキャッシュ鍵・HTTP 応答の解析は host テストへ (ネットワークに
  触らない純粋部分。`rake test` に追加)。

### Tab5 (関門 2 で)

- 実 VOICEVOX (ユーザの PC) か偽サーバへ LAN で届き、`tts/say` で喋る
  (**聴くのはユーザ**)。キャッシュヒットがオフラインで鳴ることも 1 回。
- 書き込み前の fmrb_rd_ps 単独確認。

## 進め方

- **関門 1 = T0 の報告** (タイムアウトの答えと VOICEVOX の素振り)。
- **関門 2 = T2/T3 完了時** (実機は関門 2 の後、官能はユーザ)。
- コミット 2 本: (1) tts サービス + 偽サーバ + テスト、(2) docs (本書 +
  plan.md の V2 を確定結果に + V2b の調査結果)。英語、ユーザ確認のうえ。
- 2 構成ビルド + doctor 新規指摘 0 + rake test。`.env` 復元。

## やらないこと

- クラウド (V2b は調査まで)、再試行、読み上げの待ち行列 (同時に来たら
  後勝ちで置き換え)、キャッシュの自動掃除、かな正規化などテキスト前処理
