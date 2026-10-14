# Asterism で音声を運ぶ (TTS と AI の連携)

> 状態: 案 | 更新: 2026-10-14 | 端末は「マイクの音を送る → 返った音を鳴らす」だけにし、TTS・音声認識・対話は CRuby のノードに置く案の机上の調べ。**端末と wasm が受け取れる 1 通は約 4 KB まで** (zenoh-pico の組み立ての上限、超えると黙って捨てる) で、音は 100 ms ほどの塊に分けて運ぶ。端末で流しながら鳴らす口は無く (`play_wav` はファイルを丸ごと読む)、Step 1 は塊を集めて `/tmp` の WAV にしてから鳴らす。長さが 65536 標本を超える WAV が止まらない疑いを机上で見つけた

## 目的

- TTS (読み上げ) と AI (音声認識・対話) を、Asterism の網を通して端末から使えるようにする。
- API キーを端末に持たせない。今の TTS はビルドのときにキーをファームの設定に入れている (下の「出発点」)。
- AI と TTS は VPS か家の PC の CRuby のノードに置く。端末は「マイクの音を送る → 返った音を鳴らす」だけにする。
- console の MCAP の記録に音声も残し、あとで聞き直せるようにする。

## 出発点: 端末が直接クラウドを呼ぶ今の形 (V2b)

Family mruby 単体で、クラウドの TTS は実機で動いている (doc/user_extension/voice/plan.md、report/v1.md・v2.md・v2b.md)。

- **WAV 再生の口 (V1)**: `FmrbAudio#play_wav(path)` / `#stop_wav`。PCM 16 bit・モノラル・8000-48000 Hz・2 MB まで。APU の合成のあとの mono 15720 Hz に線形補間で変換し、飽和加算で重ねる。同時に 1 本。Modern だけ (Retro は WROVER 側に音があるので、やらないと決めた)。
- **tts サービス (V2・V2b)**: `flash/usr/share/services/tts.rb` と `tts_http.rb`。端末の中の pub/sub の topic `tts/say` に `{"text"=>...}` が来たら、キャッシュ `/tmp/tts_<鍵>.wav` → VOICEVOX (`server`) → OpenAI の TTS (`api_key`) の順に解き、`play_wav` で鳴らす。
  - OpenAI へは `POST /v1/audio/speech`、`Authorization: Bearer`、`response_format: "wav"` (tts.rb:198-211)。応答は chunked、PCM 16 bit・モノラル・24000 Hz、data の長さは 0xFFFFFFFF (流しながら書くため。report/v2b.md の「T0 の 1-2 の答え」)。
  - 応答を文字列で丸ごと持つとサービスホストの VM が尽きたので、受けながら `/tmp` のファイルへ直接書く (tts_http.rb:152-154、`MAX_BODY` 2 MB は tts_http.rb:22)。
  - 実機 (Tab5) の数字: TLS 1 接続の内蔵 RAM 5,864-6,200 B、合成 4,895 ms (sim は 2.0-2.5 秒)、キャッシュに当たれば即時。
- **鍵の置き場**: `~/.openai_key` を rake が生成物の `/etc/services.toml` の `[tts.config] api_key` に足す (rakelib/build.rake:10-28、commit 02006786)。開発のビルドだけで、CI (リリース) では入れない。`/home/services.toml` に書いても動く (平文、開発用の `/fs` で読める)。ブラウザ版は、まとめた中に鍵が入っていないかを rake で調べて止める作り (rakelib/wasm.rake:271-290)。
- **AI (chat・Whisper)**: doc/ai/ideas.md に構想があるだけで未着手。

## 2 つの形の比べ

| | 端末が直接クラウドを呼ぶ (V2b) | Asterism の CRuby のノードに任せる (この案) |
|---|---|---|
| 鍵の置き場 | 端末の `/etc/services.toml` (ビルドで注入) か `/home/services.toml` (平文)。開発のビルドだけ | ノードの環境だけ。端末とファームに鍵が無いので、リリースのビルドでも使える |
| 端末の内蔵 RAM | 合成のたびに TLS 1 接続 約 6 KB | 網の接続を開いていれば足しは無い (網そのものの費用は report/z2.md の 4 章) |
| 動く端末 | Modern でサービスホストを持つもの (Tab5・P4-Nano)。sim も可 | 網に入れるものすべて。ただし鳴らすのは Modern と (未確認の) ブラウザ版だけ (調べ 2) |
| ブラウザ版 | 不可 (wasm にはソケットも TLS も無い。lib/add/family_mruby_wasm.rb:76-77。fetch の橋は設計だけで見送り: doc/wasm/instruction_p5.md:52-66) | 可 (wasm は ws で網に入れる) |
| AtomS3R | 不可 (音の出口が無く、サービスホストも動かない) | 鳴らす口が無いので同じく不可。網の中継や表示だけなら参加はできる |
| 遅れ | 合成の時間 (実機 約 5 秒) + ファイルを読む時間 | 合成の時間 (PC の CRuby) + 網での転送。流しながら鳴らす口 (Step 3) ができれば最初の塊で鳴り始められる |
| 記録 | 無い | 配信 (put) で運べば console の MCAP に残る。呼び出しの答えは残らない (調べ 6) |
| PC・VPS が無いとき | 動く (家の WiFi とクラウドだけ) | 動かない (ノードが要る) |
| 音声認識・対話 | 端末で multipart の POST・JSON の扱いを書き足すことになる。サービス 1 ファイル 10 KB の上限もある (report/v2b.md) | CRuby で普通の gem を使って書ける。端末は音を送って受けるだけ |
| 費用の管理 | 端末ごとに鍵 | ノード 1 か所で数え、まとめてキャッシュできる |

**使い分け**:

- 端末が直接呼ぶ形は残す。PC も VPS も無い家で、1 台の Tab5 が時報・アラーム・予定の読み上げをする用途はこれで足り、網に頼らない。
- ノードに任せる形は、(1) ブラウザ版、(2) 鍵を入れないリリースのビルド、(3) 音声認識と対話、(4) 記録を残したいとき、(5) 端末が何台もあるとき、に使う。
- 2 つは端末の tts サービスの中で並べられる見込み: 解く順に「Asterism のノード」を足し、アプリは今のまま `tts/say` を publish するだけにする (サービスホストの VM から Asterism を使えるかは未確認)。

## 運び方

| | (1) 呼び出しの答えで返す | (2) 通し番号つきで配信する |
|---|---|---|
| 向く用途 | TTS の読み上げ (文が決まっていて、全部できてから鳴らしてよい) | マイクの音、話しながら返すアシスタント |
| 端末の手間 | 答えを待って並べる | 購読して、少し溜めてから鳴らす |
| 失ったとき | 呼び出しが時間切れになるので、同じ塊をもう一度呼べる | 番号の抜けで分かる。取り戻すなら別に呼ぶ |
| 記録 | MCAP に残らない | MCAP に残る |

1 通に音を丸ごと入れる形は、端末とブラウザ版では受け取れない (調べ 1)。どちらの運び方でも、音は**約 3 KB (100 ms) の塊**に分ける。

## 調べた結果

パスは fmruby-core の中は相対、ほかのリポジトリは名前を前に付けた。

### 1. 1 通の大きさの上限

**分かったこと**

- **端末とブラウザ版 (zenoh-pico 1.10.1) が受け取れる 1 通は 4096 B まで**。
  - 版: picoruby-asterism-zenoh の `ZENOH_PICO_PIN` (1.10.1、e1ab223)。
  - 設定: `Z_FRAG_MAX_SIZE` 4096、`Z_BATCH_UNICAST_SIZE` 2048、`Z_FEATURE_FRAGMENTATION` 1 (picoruby-asterism-zenoh の include/zenoh_generic_config.h:44-53、:156-157)。ESP32・Linux の sim・wasm のどのビルドもこの値を上書きしていない (components/picoruby-esp32/CMakeLists.txt:252-257、lib/add/family_mruby_wasm.rb:92-96)。
  - 受ける側は 1 つの batch (2048 B) を超える通を分割 (fragment) で受け、4096 B の領域で組み立てる。合わせて 4096 B を超えると、最後の分割が来たところで**捨てる** (vendor/zenoh-pico/src/transport/unicast/rx.c:206-232)。知らせは `_Z_INFO` の 1 行だけで、ビルドは `ZENOH_LOG_ERROR` なので出ない。gem の `dropped` にも数えられない (呼び返しまで届かないため)。
  - 4096 B には、MessagePack の外側、キー、Zenoh の見出しも入る。呼び出しの答えがこれを超えると、呼んだ側は答えが来ないまま時間切れになる (asterism の mrblib/future.rb:72-76)。
  - 組み立ての領域は zenoh-pico の `z_malloc` から取り、端末では PSRAM だけから取る (picoruby-asterism-zenoh の ports/esp32/zp_system_esp32.c:42-44、report/z2.md:13)。上限を上げても内蔵 RAM は増えないが、分割された通が来るたびにその大きさを PSRAM に取る。
- **S3 のカメラの JPEG はこの上限の内側だった**: 1 枚 2,757-2,807 B (report/s1.md:14、:91)。plan.md の S の節の見積もり「1 枚約 5 KB」なら 4096 B を超えて端末に届かなかったことになる。音の塊も同じ線の内側に置く。
- **送る側には上限が無い**。batch に入らない通は分割して送る (vendor/zenoh-pico/src/transport/common/tx.c:191-192、:247-248)。端末から CRuby へ、5 秒の声 (約 157 KB) を 1 つの呼び出しの引数で送ることは、zenoh-pico の作りの上ではできる (未確認)。
- **CRuby (asterism-zenoh、zenoh-c 1.10.1)** には gem の側の大きさの上限が無い。中継の zenohd は、cloud の設定で `transport.link.rx.max_message_size` 4 MiB (asterism-console の lib/relay/cloud_config.rb:99、:149-150)。家の compose の zenohd は大きさの設定を書いていない (docker-compose.yml:88-95)。
- **door** (asterism-console の bin/ws_door): 登録済みの側の口 (G1) はバイトを流すだけで、1 通の上限も書き換えも無い (:115-117)。部屋の口 (G2) は WebSocket の 1 通を 65535 B までにし (`WS_DOOR_ROOM_MAX_MESSAGE`、bin/ws_door:138、lib/relay/room_door.rb:403、:462-464)、zenohd からの最初の InitAck の batch_size を 2 小さく書き換える (room_door.rb:431-449。TLS の長さの 2 B のため)。ブラウザ版は zenoh-pico なので、受け取りの上限は端末と同じ 4096 B で、door の 65535 B より先にこちらに当たる。
- **ブラウザ版の ws の口**: 1 つの WebSocket の通が 1 つの batch。受信の輪 256 KB・送信の輪 64 KB (wasm/zenoh/zp_ws_emscripten.c:105-109)。
- **符号化 (protocol 1)**: 呼び出しも答えも MessagePack の値 1 つ (asterism の docs/protocol.md:65-67)。**バイナリの文字列は bin で運ばれ、そのまま通る**。CRuby では ASCII-8BIT の文字列 (`File.binread`・`.b`) が bin になる。端末では mruby の binary の印のある文字列が bin になり、bin を受けると印つきの文字列になる (lib/add/picoruby-fmrb-msgpack/ports/esp32/msgpack.c:17-36)。適合の見本にも bin の往復がある (asterism の test/protocol/gen_samples.rb:98-103、:273-275)。
  - 端末の生の put を受けた文字列には印が無い。それをそのまま送り直すと str になるので、`.b` を付ける (protocol.md:571)。
  - 試し (CRuby、msgpack 1.8.5): 3200 B の PCM を `{"value"=>bin}` で包むと 3210 B、`{"seq","t","last","data"}` で包むと 3235 B。往復して同じバイト列、ASCII-8BIT で戻る。
- **受け取りの列**: 購読の既定の深さは CRuby・端末とも 16。1024 は CRuby の get・生存の問い合わせ・生存の見張りの既定 (asterism-zenoh の ext/asterism_zenoh/zenoh.c:65-72、picoruby-asterism-zenoh の src/zenoh.c:91-99)。端末の深さの上限は 1024、CRuby は 65536。列があふれると古いものから捨てて `dropped` を 1 増やす (src/zenoh.c:372-379)。
- **呼び出しの時間切れ**: 既定 2 秒 (asterism の mrblib/asterism.rb:31-32)。`timeout:` で変えられ、get は最長 600000 ms。合成に数秒かかる TTS の呼び出しは、時間切れを延ばすか、すぐ答えて結果を別に取りに行く形にする。
- **端末の記憶**: 受けた通は、まず PSRAM に 1 回写し (src/zenoh.c:356-370)、取り出すときに `mrb_str_new` でアプリの VM へもう 1 回写す (:415-421)。MessagePack を解くと bin がさらに 1 つの文字列になるので、VM の中では塊の約 2 倍を一時に持つ。VM はすべて PSRAM (components/fmrb_mem/fmrb_mempool.c:14-24)。Modern のアプリは 1 MB (大きい枠は 2 MB、1 つのアプリだけ)、S3 は 500 KB (components/fmrb_common/include/fmrb_mem_config.h:86-90)。文字列 1 本は 1 MB まで (`MRB_STR_LENGTH_MAX` の既定)。3 KB の塊なら効かない。

**未確認**

- [ ] 4096 B を超える 1 通を端末とブラウザ版が本当に黙って捨てるか (実機と wasm で 1 通ずつ大きさを変えて送る)。
- [ ] Zenoh の見出しとキーで何 B 使われるか。音の塊の安全な上限 (いまの見立て 3.5 KB) を実測で決める。
- [ ] 端末から 100 KB を超える 1 通を送れるか、そのときの WiFi の時間とアプリの止まり。
- [ ] 家の compose の zenohd の `max_message_size` の既定。

### 2. 端末で鳴らす口

**分かったこと**

- **流しながら鳴らす口は無い**。PCM を出す口は `play_wav(path)` だけで、ファイルを丸ごと PSRAM に読んでから鳴らす (main/drivers/audio_p4/audio_p4_task.c:126-200)。Ruby の文字列から鳴らす口、あとから足す口、終わりの知らせ、状態の問い合わせはどれも無い。V2 の計画でもストリーミング再生は「やらない」に入れてある (voice/plan.md の「やらないこと」)。
- 混ぜる所: audio_p4_task.c の 60 Hz の回りの中の `fmrb_wav_stream_mix` (:560)。線形補間で 15720 Hz に落とし、エイリアスを止める濾波は無い (components/fmrb_audio/fmrb_wav.c:93、:109-125)。どんな周波数で送っても、聞こえる帯域は約 7.8 kHz まで。
- **長い WAV が止まらない疑い (机上)**: 読む位置が 16.16 の `uint32_t pos_q16` (components/fmrb_audio/fmrb_wav.h:55) で、整数の部分は 65535 までしか持てない。止まる判定は `idx >= frames` (fmrb_wav.c:109-110) なので、65536 標本を超える WAV では位置が一回りして先頭の 65536 標本を鳴らし続ける。24 kHz なら 2.73 秒、16 kHz なら 4.1 秒、15720 Hz なら 4.17 秒を超える音が当たる。
  - CRuby で同じ計算をまねた試し (72000 標本・24 kHz を 15720 Hz で混ぜる): 10 秒たっても止まらず、位置の最大は 65535、3 回ひと回りした。
  - V2b の実機のログには 72000 標本の音を鳴らした記録がある (report/v2b.md の「実機で喋った」)。そのとき何度もくり返して鳴ったかの記録は無い。fmruby-graphics-audio の main/common/fmrb_wav.c も同じ中身なので sim も同じ。host テストは 1 秒の音しか使っていない (test/wav/main.c)。
- **機種ごと**:
  - Tab5: ES8388 (DAC)、I2S_NUM_1、47160 Hz (= 15720 x 3) の 16 bit ステレオ。APU の 1 標本を 3 回くり返して左右に出す (audio_p4_hw.c:17-19、:356-381)。
  - P4-Nano (NARYAv4): ES8311 1 つで出入りとも。I2S は 47160 Hz (audio_p4_hw.c:113-122、:313-336)。
  - Retro (S3 + WROVER): 音は WROVER の APU で、`play_wav` は方針として断る。
  - **AtomS3R: 音の出口が無い**。APU の代わりに `audio_none.c` を組み込み (main/CMakeLists.txt:270-287)、`HW_FAMILY` は `"mini"` なので `play_wav` は false (lib/add/picoruby-fmrb-const/ports/esp32/const.c:388-391)。サービスホストも Modern だけ (main/app/fmrb_app_spawner.c:315-320)。音の出る S3 の小型機 (M5StickS3・StopWatch、ES8311) は doc/headless_s3/plan.md:159-160 の候補にあるだけ。
  - **ブラウザ版**: audio_p4_task.c・fmrb_wav.c を同じく組み込み (wasm/CMakeLists.txt:165、:300-302)、15720 Hz の 16384 標本の輪に書き、AudioWorklet が SharedArrayBuffer から読む (wasm/backend/audio_backend_wasm.c:21-60、wasm/web/audio-worklet.js:29-58、遅れ 約 100 ms)。`HW_FAMILY` は modern なので `play_wav` の道は通っているはずだが、鳴らした記録は無い。
  - sim: graphics-audio の audio_task.c が別のプロセスで鳴らす。`play_wav` は `sync_file` でファイルを写してから (lib/add/picoruby-fmrb-app/mrblib/fmrb-audio.rb:146-148)。

**足すもの (流しながら鳴らすとき)**

- audio_p4_task.c の `g_wav_stream` (:77-78) の隣に PSRAM の輪を置き、混ぜる側が輪から読む形。読み切ったら無音を出して待つ (今の `fmrb_wav_stream_t` は固定のバッファで、終わりに来ると止まる作り)。
- Ruby の口は `FmrbMic` と同じく C を直接呼ぶ形 (lib/add/picoruby-fmrb-app/ports/esp32/app.c:1741-1750)。カーネルのメッセージを通すと 1 通 約 150 B (components/fmrb_audio/audio_commands.h:27-33) に当たる。例: `pcm_open(rate)`・`pcm_write(str)` (受けた B 数を返す)・`pcm_buffered`・`pcm_close`。
- ブラウザ版は同じ audio_p4_task.c なので同じ輪で動く見込み。sim は graphics-audio へリンクで送ることになり、1 通 4096 B (components/fmrb_common/include/fmrb_link_protocol.h:806)。
- 速さ 15720 Hz で受ければ端末は変換しないで済む (調べ 4 の終わり)。

**未確認**

- [ ] 65536 標本を超える WAV が実機と sim で止まらないか (4.17 秒を超える音を 1 回鳴らして聞く、または音の採り出しで長さを測る)。
- [ ] ブラウザ版で `play_wav` が鳴るか。
- [ ] 輪の大きさと溜める長さ (WiFi の揺れに対して 200-300 ms で足りるか)。

### 3. 端末でマイクを取る口

**分かったこと**

- Ruby の口: `FmrbAudio#mic_available?`・`mic_rate`・`mic_enable(on)`・`mic_read(count, timeout_ms = 200)` (lib/add/picoruby-fmrb-app/mrblib/fmrb-audio.rb:38-55)。`mic_read` は 16 bit・リトルエンディアン・モノラルの文字列を返し、`count` は 1-4096 (app.c:1791-1818)。ESP32-P4 のビルドだけにある (app.c:55-61)。S3・ブラウザ版・sim では使えない。
- Tab5: ES7210、I2S_NUM_1 の受ける側 (DIN は GPIO28)、2 つのマイクの平均でモノラル (audio_p4_hw.c:553-564)。P4-Nano は ES8311 のマイク (利得 30 dB、audio_p4_hw.c:126)。
- **標本化は 47160 Hz で固定**。スピーカーと 1 本のクロックを共にするので選べない (audio_p4_hw.c:493-495、doc/mic_spectrum/report/track_b.md:33-40)。16 kHz で取るには、クロックを別のコントローラへ引くか、変換するかになる。
- **変換の道具は無い** (入力の変換のコードはどこにも無い。fmrb_wav.c は出力の変換だけ)。
- **47160 Hz は 15720 Hz のちょうど 3 倍**。3 標本ずつ平均すれば 15720 Hz (約 16 kHz) になり、出力の速さとも合う。C なら数行。Ruby で 1 標本ずつ回すのは重い (毎秒 47160 標本を `getbyte` で読んで足すことになる) ので、C に足すのがよい。例: `mic_read(count, timeout_ms, decimate: 3)`。
- 47160 Hz のまま送ると毎秒 約 94 KB で、S3 のカメラ (毎秒 約 14 KB) の 7 倍ほど。15720 Hz なら毎秒 31,440 B。
- ブラウザ版のマイクは無い。足すならページの getUserMedia から AudioWorklet で取り、wasm へ渡す橋が要る。

**未確認**

- [ ] 3:1 の平均で音声認識の精度が足りるか (エイリアスを防ぐ濾波を入れるかどうか)。
- [ ] 音声認識の API が 15720 Hz の WAV をそのまま受けるか (受けなければノードで 16 kHz に直す)。

### 4. CRuby の側

**分かったこと**

- asterism gem の書き方 (asterism の README.md:251-275、lib/asterism/cruby/objects.rb): `Asterism.connect(router, node:, app:) { |net| ... }`、`net.expose(name, obj, methods: {...}, caller: :from)`、`net.run` (この thread で答える) か `net.start` (裏の thread)。素の Zenoh は `net.session` の `put`・`publisher(key).put(payload, attachment:)`・`subscribe(key, depth:)`。
- バイナリを返す呼び出し: 返す値を `.b` の文字列にすれば bin になる。配信は `net.session.put(key, MessagePack.pack({"seq"=>i, "data"=>bytes.b}))` の形 (README.md:547)。キーは `asterism/<node>/<app>/<obj>/<続き>` にすればオブジェクトの層とぶつからない (README.md:585-589)。
- ノードの形の案 (TTS、塊を取りに来てもらう):

```ruby
# 例。ノード名・アプリ名は仮
Asterism.connect("tcp/192.0.2.10:7447", node: "voice", app: "ai") do |net|
  net.expose("tts", Tts.new, methods: { say: 1, chunk: 2 }, caller: :from)
  net.run
end
# Tts#say(text)      -> {"id"=>..., "rate"=>15720, "bytes"=>..., "chunks"=>n}
# Tts#chunk(id, i)   -> pcm.byteslice(i * 3144, 3144).b   (100 ms、1 通 4 KB 未満)
```

- 端末の側は `Asterism.call_async` が Future を返す (asterism の mrblib/asterism.rb:796-812) ので、いくつかの塊を同時に取りに行ける。
- TTS・音声認識の API: OpenAI の TTS は今の端末と同じ `POST /v1/audio/speech` を使える。CRuby では応答を丸ごと持ってよく、ノードで 15720 Hz に落としてから塊にする (濾波もノードでかけられる)。音声認識は音声のファイルを multipart で送る API を CRuby から呼ぶ。応答の形式の選び方と API の細部は着手のときに確認する。
- **既存の道具との関係**: `tools/fmrb_zenoh.rb` は zenohd の REST の口を叩く手の道具で、MessagePack の文字列はすべて str で送る (tools/fmrb_msgpack.rb:64-73)。bin を送れないので、音のノードには使わない。S4 の「頭脳」は端末のアプリ (`flash/app/test/rover_brain.app.rb`) で、`fmrb_zenoh.rb --caller` はそれを呼ぶ手の道具。音のノードは asterism gem の CRuby のプロセスとして新しく書く (置き場は未定)。docker の重ね合わせ (docker/asterism/) にも TTS・音声認識のものは無い。
- 端末の中の tts サービスは端末の中の pub/sub (`tts/say`、1 通 8 KB まで: doc/ai/ideas.md:13) で、Asterism とは別の道。

**未確認**

- [ ] 端末のサービスホストの VM (Modern 1 MB、全サービスで共有) から Asterism を使えるか。使えれば tts サービスの解く順に足せる。
- [ ] ノードの置き場 (asterism の examples、親のリポジトリの tools、console の横のどれか)。
- [ ] 合成を待つ間の答え方 (時間切れを延ばすか、`say` はすぐ id を返して塊はできた順に取りに来てもらうか)。

### 5. 圧縮

端末の手間で比べる。

| | 量 (15720 Hz) | 端末の手間 | 向く所 |
|---|---|---|---|
| PCM のまま | 毎秒 31,440 B | 無し。`play_wav` も輪もそのまま使える | 家の LAN、VPS でも 1-2 本なら。S3 のカメラ (毎秒 約 14 KB) の 2 倍ほど |
| IMA ADPCM (4 bit) | 約 1/4 (毎秒 約 7.9 KB) | 復号も符号化も C で数十行。Ruby で 1 標本ずつは重いので C に足す。ブラウザ版も同じ C で動く | 部屋で何人にも配るとき、長い記録 |
| Opus | 声なら毎秒 2-3 KB | 符号化・復号の部品が要る。今のファームに無い (esp_codec_dev は音の部品の操作だけ)。Tab5 の flash の残りが少ない。CPU と記憶も ADPCM より重い | インターネット越しで帯域を詰めたいとき。ブラウザ版はページの側で解ける見込み |

- まず PCM のまま。量が効いてきたら ADPCM を C に足す。Opus は最後。
- 塊の大きさは同じ時間 (100 ms) なら ADPCM で約 800 B になり、4096 B の上限に余裕ができる。

### 6. console の記録

**分かったこと**

- 記録は put だけを購読して残す (`RECORD_DEPTH` 1024、asterism-console の lib/bridge/runner.rb:73、:863-868)。**呼び出し (get と答え) は記録されない**。音を記録に残すなら配信で運ぶ。
- 中身は受けたバイト列のまま (`payload.to_s.b`、lib/bridge/recorder.rb:164)。MCAP にもそのまま書く (lib/mcap/writer.rb:83-91)。Asterism のキーの channel は message_encoding が `msgpack`、schema が `asterism/msgpack` (docs/v4.md:52-56)。
- 再生は記録したキーへ同じバイト列を put し直す (lib/bridge/player.rb:131-149)。網へ流すのは管理者だけで、確認が要る (docs/v4.md:116-123)。速さは 0.25・1・4 倍。
- 上限: 1 本の記録は既定 64 MB・300 秒 (最大 512 MB・3600 秒)、同時に 2 本、全部で 4 GB (docs/v4.md:125-133)。15720 Hz の PCM は 300 秒で約 9.4 MB。
- 画面での見せ方: bin は 16 進で出る (lib/bag/decode.rb:95-117)。**音として聞く口は無い**。

**足すもの**

- 音の塊の形を決める (例: `{"seq", "t", "rate", "fmt", "data"}`)。console の画面で、その形の channel を WAV に書き出すか、ページで鳴らす口。
- 時間の帯 (カーソル) と音を合わせる (カーソルの位置から鳴らす)。

### 7. 部屋 (G2) の中で使うとき

**分かったこと**

- 部屋の中のブラウザ版は zenoh-pico なので、受け取りの上限は 4096 B のまま。door の 1 通 65535 B (調べ 1) は、それより先には当たらない。
- 部屋の名前空間は picoruby-asterism-zenoh の C の層でキーの前に `room/<枠>/` を付ける (plan.md の G2 のステップ 1 の結果)。キーが数 B 長くなるぶん、1 通の余裕が減る。
- 1 部屋の接続は 16 まで、door 全体で 48 (asterism-console の lib/relay/room_door.rb:82-83)。1 本の音 (毎秒 31 KB) を部屋の 15 人に配ると、VPS から毎秒 約 470 KB 出ていく。
- ノード (CRuby) は、ゲームホストと同じく枠の証明書で直接入る形 (plan.md の G2) をとれる。
- ACL の `**` が `@adv` を含むキーに当たらない件 (plan.md の G2 の注意) は、advanced pub/sub で音の履歴を取り直す形にするなら効く。

**未確認**

- [ ] 部屋の中の 2 つのブラウザ版で音の塊が届くか、その遅れ。
- [ ] VPS の帯域と、何本まで同時に流せるか。

## Step

### Step 1: TTS の音を塊に分けて返し、P4 で鳴らす

- 先に直すもの: **65536 標本を超える WAV が止まらない疑い** (調べ 2)。位置を 64 bit にするか、整数の位置と端数を分ける。host テストに 5 秒の音を足す。fmruby-graphics-audio の同じファイルも同じに直す。
- CRuby のノード: `tts.say(text)` が OpenAI の TTS で合成し、15720 Hz・16 bit・モノラルの PCM にして id・長さ・塊の数を返す。`tts.chunk(id, i)` が 100 ms (3144 B) の塊を bin で返す。
- 端末 (P4-Nano、Tab5): 試しのアプリが `say` を呼び (時間切れを延ばす)、`chunk` を `call_async` でいくつか並べて取り、`/tmp` に WAV (見出しは Ruby で組む。Array#pack は無いので setbyte) を書いてから `play_wav`。
- 測るもの: 合成の時間、転送の時間 (塊の数と並べ方ごと)、鳴り始めるまでの時間、内蔵 RAM と PSRAM の前後、時間切れの数。sim とブラウザ版でも同じアプリを動かす (ブラウザ版は `play_wav` が鳴るかの確認を兼ねる)。
- 採らなかった案: `Z_FRAG_MAX_SIZE` を上げて 1 通で返す。PSRAM だけで済むが、分割された通のたびにその大きさを取り、上限を超えたときに黙って捨てるのは変わらず、ブラウザ版も同じに上げることになる。

### Step 2: 1 通の上限を見える形にする

- 4096 B を超えて捨てた通を数えるか、ログに出す (zenoh-pico の該当の 1 行の扱い、あるいは gem の側で数える)。
- asterism の docs/protocol.md と README に、端末とブラウザ版が受け取れる 1 通の大きさを書く。
- 実機と wasm で、大きさを変えて送る試しで上限と見出しの大きさを測る。

### Step 3: 流しながら鳴らす

- audio_p4 に PCM の輪と Ruby の口 (`pcm_open`・`pcm_write`・`pcm_buffered`・`pcm_close` の形) を足す。ブラウザ版も同じ口。
- ノードは塊を通し番号つきで配信する (実時間より少し速い一定の速さ)。端末は深さを 64 ほどにして購読し、200-300 ms 溜めてから鳴らす。抜けは無音で埋めて数える。
- console で記録して聞き直せることを確認する (このときに、調べ 6 の「音として聞く口」を足す)。

### Step 4: マイクの音を送って文字にする (押して話す)

- `mic_read` に 3:1 の平均を足し、15720 Hz で取る。
- 押している間を `/tmp` に溜め、離したら 1 つの呼び出しの引数で送る (大きい 1 通を送れるかの確認を兼ねる)。届かなければ塊に分けて配信する。
- ノードで音声認識 → 文字を返す。端末の画面に出す。

### Step 5: 話しながら返すアシスタント

- Step 4 の文字をノードで対話の API に渡し、返事を TTS にして Step 3 の形で流す。端末から見ると「押して話す → 返事が鳴る」。
- 往復の遅れを、音声認識・対話・合成・転送・溜める時間に分けて測る。
- マイクを流しっぱなしにする形 (声の区切りをノードで決める) は、ここで要るかを決める。

### Step 6: ブラウザ版と部屋

- ブラウザ版のマイクの橋 (getUserMedia → wasm)。
- 部屋の中で音を配る。帯域が足りなければ ADPCM を C に足す。

## 未確定事項

- [ ] 4096 B の上限を実機と wasm で確認し、音の塊の大きさ (100 ms、3144 B) で安全かを決める。
- [ ] 65536 標本を超える WAV が止まらない疑いを実機と sim で確認する (Step 1 の前)。
- [ ] Step 1 の運び方: 塊を取りに来てもらう (呼び出し、失えば取り直せる、記録に残らない) か、配信 (記録に残る、抜けがありうる) か。いまの案は呼び出し。
- [ ] 合成を待つ呼び出しの時間切れの扱い。
- [ ] 端末のサービスホストから Asterism を使えるか (tts サービスに「ノード」を足せるか)。
- [ ] ノードの置き場と名前、ノードが使う TTS と音声認識の API (OpenAI のほかに家の PC の VOICEVOX を並べるか)。
- [ ] 音の塊の形 (キー、`seq`・`t`・`rate`・`fmt`)。console の見せ方と合わせて決める。
- [ ] マイクの 3:1 の平均で足りるか、濾波を入れるか。
- [ ] 流しながら鳴らす輪の大きさと溜める長さ。
- [ ] ブラウザ版で `play_wav` が鳴るか。
- [ ] 圧縮 (ADPCM) をいつ足すか。
- [ ] 部屋の中で音を配るときの VPS の帯域。
