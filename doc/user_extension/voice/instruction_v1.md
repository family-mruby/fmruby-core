# 実装指示書 V1: WAV 再生の口 (play_wav)

対象: 実装担当セッション。作業リポジトリ: fmruby-core **と
fmruby-graphics-audio** (sim の音は graphics-audio 側で鳴るため)。
先に読むもの: plan.md (同ディレクトリ。3 段の全体像と V1 の位置)、
services/instruction_s1.md の進め方。報告は report/v1.md。

## 下調べ済みの事実

- 音声コマンドの経路: `FmrbAudio` → kernel (`MSG_TYPE_APP_AUDIO`) → 音側の
  handler。**path を渡してファイルを音側で開く前例が `load_fmsq_file`**
  (audio_p4_handler.c 163 行付近。Tab5 は同一 VFS で直接 fopen)。
- sim (と Retro) の音は graphics-audio プロセスで鳴る。ファイルは
  **転送してから** path を渡す流儀 (`load_fmsq_file` のコメント:
  transfer_file で先に押し込む)。
- 混ぜる場所: audio_p4_task.c 404 行 `apuif_process_mix(buffer, ...)` が
  **mono 15720 Hz** の APU 合成を作り、hw 側が 3 倍アップサンプル + ステレオ化
  (audio_p4_hw.c 17 行)。graphics-audio 側にも同じ構造の合成点がある
  (main/tasks/audio_task.c)。**PCM は mono 15720 の段に足すのが最小侵襲**。
- 検証: sim では `tools/fmrb_audio_probe.rb` が合成後のリングを読める。
  `--dump` で WAV に落として周波数まで測れる — **正弦波の音高で
  リサンプラの正しさまで機械検証できる** (レート誤りは音程に出る)。

## 仕様

### API (fmrb-audio.rb)

```ruby
audio.play_wav(path)   # 再生開始。再生中なら置き換え。Retro では false を返して 1 行ログ
audio.stop_wav         # 停止 (再生していなければ何もしない)
```

- 完了通知・状態問い合わせは**無し** (v1。アラームは撃ちっぱなしで足りる)。
- `play_wav` は内部で `sync_file` してから音側へ cache のパスを送る
  (スプライトと同じ型。Tab5 では同一 FS 内コピーで安い)。
- Retro (HW_FAMILY == "retro") は**呼び手側で** false を返す (メッセージを
  送らない)。WROVER の handler にも未知コマンドを 1 行ログで無視する守りを
  入れる (古い core と新しい WROVER の組み合わせ対策)。

### 対応する WAV

- **PCM 16bit・モノラル・8000〜48000 Hz** のみ。ステレオ・8bit・圧縮系は
  1 行ログで拒否 (落とさない)。
- 大きさの上限 2MB (全体を PSRAM に読み込む。ストリーミングはしない)。
- ヘッダの読解は最小 (RIFF/fmt/data チャンクだけ。拡張チャンクは読み飛ばす)。

### 混合

- 読み込んだ PCM を**固定小数点の線形補間で 15720 Hz に変換しながら**、
  APU 合成後の mono バッファへ**飽和加算** (クリップ護り)。ゲインは等倍。
- **スタックを増やさない書き方**: リサンプルの中間バッファを作らない
  (1 サンプルずつ補間して既存バッファへ加算。作業領域はローカル数個)。
  ヘッダは小さな固定配列、本体は PSRAM の確保先へ直接 fread
  (`load_fmsq_file` と同じ malloc → fread の型)。VLA / alloca 禁止。
  定常 (非再生) は追加コードが走らないこと。**新しいタスクは作らない**
  (受理・読み込み・混合とも既存タスク内)。読み込みが audio タスクを塞ぐ間は
  鳴っている音が途切れうるが v1 は許容 — 読み込み時間の実測を report に
  1 行 (BGM 中の途切れが耳につくと分かったときに初めて分離を検討する)。
- 実装は audio_p4 (Tab5) と graphics-audio (sim/Retro 系) の 2 か所に同じ形で。
  変換ロジックは 1 ファイルにまとめて両方から使えるなら共用 (置き場所を
  report に。EXPORT_FRAME のときの protocol 同期と同じ注意: メッセージの
  cmd 文字列と payload は両リポジトリで同一に)。

### 使い道の実例 (同梱)

- 検証用の小さな WAV を**生成して**同梱: `flash/usr/share/sounds/`
  に 440 Hz 正弦 1 秒 (16kHz mono) ともう 1 種 (8kHz)。生成スクリプトは
  tool/ に (Ruby で書ける: 正弦を pack なしで組む)。声のファイルは
  ユーザが /home/voice/ に置く運用 (plan の V2 でキャッシュに育つ)。
- hourly_chime に任意設定 `[chime.config] wav = "/home/voice/hour.wav"` を
  足す: あれば `ctx.audio.play_wav`、無ければ従来の note_on。
  ファイルが無いときは従来動作に落ちて 1 行ログ。

## 検収

### sim (Modern 向け headless)

- `play_wav("/usr/share/sounds/sine440_16k.wav")` (一時アプリかサービスから)
  → `fmrb_audio_probe.rb --dump` で**音のあった窓 > 0、FFT のピークが
  440 Hz ± 5 Hz** (16000 → 15720 の変換が正しい証明)。8kHz 版も同様。
- APU と同時に鳴る: note_on を鳴らしながら play_wav → 両方の周波数が
  スペクトルに出る。飽和で破綻しない (ピークが天井で頭打ち)。
- 置き換え: 再生中に別の WAV を play_wav → 前の音が止まり後の音だけになる。
- stop_wav で止まる。壊れた WAV / ステレオ / 無いパス → 拒否ログ 1 行で
  生き続ける。
- 再生終了後に放置 → 音の窓が 0 に戻る (リングにゴミを書き続けない)。
- **audio タスクのスタック**: `fmrb_task:` の high-water が、アイドル時は
  V1 前と不変、再生 1 回後の実測値を report に (増分があればその数字)。

### Retro 向け sim

- `play_wav` が false を返して 1 行ログ、音側にメッセージが飛ばない。
  既存の音 (note_on / FMSQ) の回帰が無い (audio_probe で従来どおり)。

### Tab5

- 同じ 440 Hz WAV をサービスから鳴らして**ユーザが聴く** (官能はユーザ)。
  hourly_chime の wav 設定で正時に鳴ることを 1 回 (時刻をずらして待つか、
  config で分を指定できるならそれで)。
- 書き込み前の fmrb_rd_ps 単独確認を忘れない。

## 受け入れ条件

- 上記が report/v1.md に数字 (FFT のピーク値) つきで揃う。
- コミット: (1) graphics-audio の混合 + handler、(2) core の protocol +
  FmrbAudio + audio_p4 + 生成スクリプト + サンプル WAV + chime 設定、
  (3) docs (本書 + plan.md の V1 節を確定結果に)。英語、ユーザ確認のうえ。
- 2 構成ビルド + doctor 新規指摘 0 + `rake test` (WAV ヘッダ読解と
  リサンプラの純粋部分を host テストに)。`.env` 復元。

## やらないこと

- ストリーミング再生・完了コールバック・音量指定・複数同時 WAV
- MP3/OGG、ステレオ、8bit
- Retro (WROVER) での再生 (リンクに PCM を流す設計は plan の後段のまま)
- V2 (tts サービス) / V3 (カレンダー) — この指示書の外
