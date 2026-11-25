# S4b 指示書: 音 (MML・SMF・MIDI・マイク) と、タイル・P5 の残りを sig に書く

> 2026-11-25 | 親セッションからサブ (Sonnet) への指示。計画は plan.md (5 章の S4、7 章の決定 12・22)。
> 書き方の正は `sig/README.md`。前の一括は report/s4a.md (同じ進め方。報告の形はこれに合わせる)。S4 の後半。

## 目的

音とタイル・P5 の API のうち、実装にあって sig に無いものを埋める。

1. **`FmrbMidi::MmlPlayer`** (実装 38 のうち sig に 12。`load_file`・`load_text`・`tempo_scale`・`device=` ほか) と
   **`FmrbMidi::SmfPlayer`** (47 のうち 13。`load_string`・`timing_stats`・`lookahead_us=` ほか)。
2. **`FmrbMidi`** (モジュールの関数。`gm_name`・`register`・`unregister`・`transports`・`device`・`scheduler` ほか) と
   **MIDI の送り先** (`FmrbMidi::SerialTransport`・`ApuTransport`・`Sam2695`・`NoteScheduler`・`MIDI::Device`)。
   - **決定 12**: 送り先のクラスは、**同梱のアプリ (`flash/app/` の midi_bench・midi_apu・mml・smf_player ほか) が
     呼んでいるメソッドだけ**書く (`device=`・`transport`・`defer_voices`・`flush_voices`・`send_note_on` など、実際に
     呼ばれているもの)。パケットの送受信などの下回りは書かず、報告の「載せない候補」に理由つきで並べる
     (ignore.yml は親が直す)。
3. **`FmrbAudio` のマイク** (`mic_available?`・`mic_rate`・`mic_enable`・`mic_read`) と、`FmrbAudio` の残り。
4. **`TileMap`・`TileSheet` の残り** (`load_json`・`load_packed`・`packed?`・`picture_height` ほか。内側は載せない候補へ) と
   **`P5` の残り** (`font_height` ほか。`matrix_multiply`・`draw_edge` などの内側は載せない候補へ)。
5. **`BMP332`** (`png_path?`・`save_bmp`)、**`NsfHeader`** の残りなど、明細 `tmp/sig_coverage/report.md` の
   「2. implemented」で、音・絵のファイルに当たるクラスの残り (どれもアプリ向けか内側かを実装とアプリの使い方で判断)。

## やること (fmruby-core、ブランチ `sig-s4b`)

1. `rake sig:coverage` を流し、`tmp/sig_coverage/report.md` の「2. implemented, not in sig/」から上のクラスの行、
   「1. used by apps」からそれらのメソッドの行を控える (前の数)。
2. **実装を読む** (推測で書かない)。`lib/gems/picoruby-fmrb-midi*/`・`lib/gems/picoruby-fmrb-audio*/`・
   `lib/gems/picoruby-fmrb-app/mrblib/` (tilemap・p5) の Ruby と C、アプリでの使い方。
   - 音は、つまずく所が多い: **鳴らし始めてから戻るか・鳴り終わるまで止まるか**、`update`・`tick` を
     `on_update` から呼び続ける必要があるか、声の数の上限、内蔵の音源 (APU) と外の音源 (シリアル) の切り替え、
     端末に無い機器 (マイク・外の音源) で呼んだときの戻り値か例外か、テンポや時間の単位 (ms か µs か tick か)。
     mrblib のコメントと実装に書いてあることだけを書く。
   - キーワード引数と省略できる引数は、シグネチャに実装のとおりに書く (決定 22)。既にある宣言のシグネチャが実装と
     食い違っていたら直してよい (前の形 → 後の形と根拠を報告に)。**既にある要約 (1 行目) は変えない**。
     既にある長文が実装と食い違っていたら、直してよい (何を直したかを報告に)。
3. **sig に書く**。MIDI は `sig/fmrb_midi.rbs`、音は `sig/fmrb_audio.rbs`、タイルは `sig/fmrb_tile.rbs`、P5 は
   `sig/fmrb_p5.rbs`、絵のファイルは `sig/fmrb_picture.rbs` か今ある宣言のファイル。
   - 要約は `日本語 <<en>> English` で 111 バイトまで。素直なものは「例だけ」、つまずくものは型紙の順。
   - 例は同梱のアプリでの実際の使い方に合わせる (39 桁以内)。強調 `**` は 1 行の中で閉じる。
   - `%a{fmrb.boundary}` の付いたクラスに足すときは `sig/README.md` の「本体との境界の印」の決まりを読む。
     境界の検査が落ちたら `ruby tools/boundary/gen.rb` で作り直して一緒にコミットし、`sdk/` の差分を報告に書く。
4. **確かめる**: `rake ti:test`、`rake regress`、`rake sig:coverage` (対象の「無い」が載せない候補だけになる。
   「3. doc gaps」は全部 0 のまま)。`rake sig:coverage:update` で基準を更新し、別コミットにする。型データベースの
   各領域の大きさの前後を控える (シグネチャの領域は今 63%。80% を超えそうなら報告の先頭に書く)。
   **ビルドと sim の確認はしない** (親が検収で行う)。

## 報告に必ず書くこと (doc/archive/sig_coverage/report/s4b.md)

1. **根拠の表**: 書いたメソッドごとに、読んだ実装の `パス:行`。失敗のしかた・呼べる場面・単位を書いたものは、その根拠の行も。
2. 書いた数 (クラスごと)、素直 / つまずくの内訳。
3. 直した既存のシグネチャと長文 (前 → 後、根拠)。
4. **載せない候補** (内側と判断したもの。クラス#メソッドと理由)。迷ったものは別に並べる。
5. アプリが使っているのに実装に見つからなかったもの。
6. 数える道具の前後の数、型データベースの大きさの前後。
7. **親が sim で確かめるとよい項目 10〜15 個** (sim のシェルの irb に打つ式 → sig に書いた期待の値か例外。
   または、エディタに打つ行 → Ctrl+E で指摘されない / される。1 行 60 文字以内。音を鳴らす式は、鳴ること自体は
   親が数値で確かめるので、戻り値を書く)。
8. 読んでいて見つけた実装の不具合の疑い (直さずに書く)。確かめていないこと。迷った所。

## 範囲と止まる条件

- 触ってよい: `sig/fmrb_midi.rbs`・`sig/fmrb_audio.rbs`・`sig/fmrb_tile.rbs`・`sig/fmrb_p5.rbs`・`sig/fmrb_picture.rbs`・
  `sig/fmrb_misc.rbs` (対象のクラスの宣言があるとき)、`sdk/` (境界の作り直しが要るときだけ)、
  `tools/sig_coverage/baseline.json`、`doc/archive/sig_coverage/report/s4b.md`、`rake docs:index` による `doc/README.md`。
- 変えない: 実装・アプリ・上の一覧に無い sig・`sig/README.md`・plan.md・`tools/` の道具と ignore.yml・型エンジンの試験の
  期待値 (試験が落ちたら止まって報告)。既にある宣言の要約。
- ビルドをしない (sim・ESP32・ブラウザ版・ネイティブ版とも。`rake clean`・`rake build:*` を流さない)。
- 量が多いので、まとまりごとにコミットしてよい。時間が足りなくなりそうなら、済んだ所までをコミットして報告に残りを書く。
- push しない・マージしない。ユーザに質問しない。実在の値 (ドメイン・IP・ノード名・鍵) を書かない。
  コメントとコミットは英語 (件名は `feat(sig): ...`・`fix(sig): ...`・`test(sig_coverage): ...`・`docs(sig_coverage): ...`)、
  報告は日本語 (常体)。
