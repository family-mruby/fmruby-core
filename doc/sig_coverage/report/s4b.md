# S4b 音 (MML・SMF・MIDI・マイク) とタイル・P5・絵のファイルの残りを sig に書く

> 2026-11-25 | サブ (Sonnet) の報告。指示書は instruction_s4b.md。ブランチ `sig-s4b`。

## 1. 結果

実装にあって sig に無かった 55 メソッドと、アプリが使う定数 10・クラス 2 を書いた。`rake sig:coverage` の差は
`app_class -2, app_constant -10, app_method -7, app_name -18, impl_ruby -55, impl_c -1`。書き方の欠け (「3. doc gaps」) は 0 のまま。

指示書の「TileMap の `load_json`・`load_packed`・`packed?`、P5 の `font_height`・`matrix_multiply`・`draw_edge`」は、今の道具の明細には
無かった (`load_json` ほかは `private` 以降の定義、`font_height` は FmrbGfx 側にあり P5 は別のメソッド、`matrix_multiply` ほかは明細に出ない)。
明細に残っていた分 (`TileMap#layer_count`・`TileSheet.picture_height`・`P5#commit`) を書いた。

| クラス | 書いた数 | 内訳 |
|---|---|---|
| `FmrbMidi` (モジュール) | 5 + 定数 1 | `sam2695_device`・`serial_device`・`scheduler`・`tick`・`tick_delay`、`GM_DRUM_CHANNEL` |
| `FmrbMidi::SmfPlayer` | 22 + 定数 1 | `load_string`・`next_delay`・`device`・`device=`・`lookahead_us`・`lookahead_us=`・`transpose`・`transpose=`・`channel_mask`・`channel_mask=`・`ignore_program_change`・`ignore_program_change=`・`track_count`・`division`・`format`・`late_count`・`late_sum_ms`・`late_max_ms`・`stall_count`・`underrun_count`・`timing_stats`、`LOOKAHEAD_US` |
| `FmrbMidi::MmlPlayer` | 14 | `load_file`・`load_text`・`total_clocks`・`next_delay`・`tempo_scale`・`tempo_scale=`・`loop`・`loop=`・`device`・`device=`・`sent_count`・`stall_count`・`underrun_count`・`timing_stats` |
| `FmrbMidi::ApuTransport` (送り先) | 8 | `.new`・`note_on`・`note_off`・`defer_voices`・`flush_voices`・`all_off`・`auto_map`・`map_channel` (全部アプリが呼ぶ分) |
| `MIDI::Device` | 2 | `.new`・`transport` |
| `FmrbAudio` | 4 + 定数 6 | `mic_available?`・`mic_rate`・`mic_enable`・`mic_read`、`MAIN`・`SUB`・`CH_PULSE1`・`CH_PULSE2`・`CH_TRIANGLE`・`CH_NOISE` |
| `FmrbMic` (新しいモジュール) | 1 | `available?` (アプリが直接呼ぶ 1 つだけ) |
| `TileMap` / `TileSheet` | 1 / 1 | `layer_count` / `.picture_height` |
| `P5` | 1 + 定数 12 | `commit`、`BLACK`..`GRAY` の 9 色と `REPLACE`・`ADD`・`XOR` |
| `BMP332` | 1 | `png_path?` |

素直 (例だけ) とつまずく (型紙の順の散文つき) の数は数えていない。つまずくものは、`FmrbMidi` のモジュール関数 5・`SmfPlayer` の読み込み・開始・切り替え・`lookahead_us=`・`transpose`・`channel_mask`・`ignore_program_change`・`MmlPlayer#load_file`・`load_text`・`loop`・`ApuTransport` の 8 つ・`MIDI::Device.new`・`FmrbAudio#mic_*`・`TileSheet.picture_height`。残り (読み取り・カウンタ・定数) は例だけ。
`FmrbAudio#mic_*` は Ruby の包みなので `%a{fmrb.internal}` を付けた (`boundary:check` は通り、`sdk/` の差分は無い)。
`ruby tools/boundary/gen.rb` は検査のために 1 回流れた (「nothing changed」)。

## 2. 直した既存のシグネチャと長文 (前 → 後)

| 宣言 | 前 | 後 | 根拠 |
|---|---|---|---|
| `FmrbMidi.gm_name` | `-> String`、例 `"Acoustic Grand Piano"` | `-> String?`、例 `"Ac Grand Piano"` | fmrb-gm.rb:64-68 は範囲外・nil で nil を返す。名前は 16 字に縮めた表 (同 1-12・GM_NAMES) |
| `MIDI::Device#note_on`・`note_off` | `(Integer note)` | `(Integer note, ?Integer velocity, ?channel: Integer)` | midi_device.rb:35・43。例は元から 3 引数・`channel:` を使っていた |
| `#pitch_bend`・`program_change`・`control_change`・`volume`・`pan`・`modulation`・`expression` | 必須引数だけ | 各 `?channel: Integer` を足した | midi_device.rb:51・58・66・168-185 |
| `#sustain` | `(Integer on_off)` | `(bool on_off, ?channel: Integer)` | midi_device.rb:188-191 (`on_off ? 127 : 0`)。長文は元から true/false |
| `#trigger` | `(Integer note)` | `(Integer note, ?Integer velocity, ?duration: Integer, ?channel: Integer)` | midi_device.rb:112。長文も直した: 止めるのは自動でなく `FmrbMidi.tick` (fmrb-midi.rb:189-196)・`duration:` の既定 100 (midi_device.rb:112) |
| `#trigger_batch` | `-> void` | `-> Integer` | midi_device.rb:141・fmrb-midi.rb:707-720 (入った note_on の数) |
| `#all_notes_off`・`all_sound_off`・`reset_all_controllers` | `()` | `(?channel: Integer)` | midi_device.rb:194-206。例は元から `channel:` つき |
| `#send_sysex` | `(String data)`、例は文字列 | `(Array[Integer] data, ?wrap: bool)`、例は配列 | midi_device.rb:235-278 (`bytes.empty?`・`bytes[i]` を整数と比べる。文字列では要素が文字列で SysEx と見なされず、送れない) |
| `SmfPlayer#start`・`pause`・`resume` | `-> void` | `-> bool` | fmrb-smf.rb:207-262 (`false unless ...`・`true`) |
| `SmfPlayer#tick` | `-> void` | `-> Integer?` | fmrb-smf.rb:368-379 (待ち時間か nil)。長文に戻りを追記 |
| `MmlPlayer#load_string` | `(String mml)` | `(String mml, ?channel: Integer, ?velocity: Integer)` | fmrb-mml.rb:111。長文は元からキーワードを説明 |
| `MmlPlayer#add_string` | `(String mml)` | 同上 | fmrb-mml.rb:189 |
| `MmlPlayer#start`・`pause`・`resume`・`tick` | `-> void` | `bool`・`bool`・`bool`・`Integer?` | fmrb-mml.rb:220-266・313-324 |
| `FmrbAudio#play` | `(String path)` | `(String path, ?track: Integer)` | fmrb-audio.rb:61 (長文にも `track:` を追記) |
| `FmrbAudio#play_slot` | `(Integer slot_id)` | `(Integer slot_id, ?instance: Integer)` | fmrb-audio.rb:96 (長文に追記) |
| `FmrbAudio#note_on` | `(Integer channel, Integer freq)` | `(..., ?Integer volume, ?Integer duty, ?Integer sweep)` | fmrb-audio.rb:134 (長文に既定値を追記) |
| `TileSheet.new` | `(FmrbGfx gfx, String path)` | `(..., cols: Integer, ?rows:, ?tile_size:, ?source:)` | fmrb-tilemap.rb:30。長文の「シグネチャは型推論の都合」を削除 |
| `TileSheet#stamp` | `(Integer index)` | `(Integer? index, dst_x:, dst_y:, ?clip_x:, ?clip_y:, ?clip_w:, ?clip_h:)` | fmrb-tilemap.rb:83-84 (nil は何もしない: 同 84) |
| `TileMap#tile_at` | `(Integer x, Integer y)` | `(..., ?layer: Integer)` | fmrb-tilemap.rb:167。長文に `layer:` を追記 |
| `TileMap#layers` | `-> Array` | `-> Array?` | `.map.bin` では `@layers = nil` (fmrb-tilemap.rb:352 の `load_packed` 末尾) |
| `TileMap#render_view` | `(TileSheet sheet)`、例は `origin_x:` 等を欠く | 7 個のキーワードを全部必須で、例も直した | fmrb-tilemap.rb:229 (元の例は `origin_x:` `origin_y:` が無く ArgumentError になる) |
| `TileMap#render`・`TileRing.new` の長文 | 「シグネチャは型推論の都合で sheet だけ」 | 削除 (シグネチャは元から正しかった) | 決定 22 |
| `TileRing#ensure_view` | `-> void` | `-> bool` | fmrb-tilemap.rb:442-449・486 (描いたら true、present が要る: 同 436-441 のコメント) |
| `BMP332.info` | `(String path)` | `(?String path, ?data: String)` | bmp332.c:278 (`MRB_ARGS_OPT(1) \| KEY(1)`)、長文の例は元から `data:` |
| `BMP332.to_png` | `(String path)` | `(?String path, ?data:, ?max_width:, ?max_height:, ?max_bytes:, ?max_file_bytes:)` | bmp332.c:203-214。既定値 (幅・高さ 2048、max_bytes 無制限) を長文に追記 |

## 3. 根拠の表

行は `lib/gems/picoruby-fmrb-midi/mrblib/` の `fmrb-midi.rb`・`fmrb-mml.rb`・`fmrb-smf.rb`・`fmrb-serial-midi.rb`・`fmrb-gm.rb`、
`lib/gems/picoruby-midi/mrblib/midi_device.rb`、`lib/gems/picoruby-fmrb-app/mrblib/` の `fmrb-audio.rb`・`fmrb-tilemap.rb`・`p5.rb`、
`lib/gems/picoruby-fmrb-app/ports/esp32/app.c`、`lib/gems/picoruby-fmrb-bmp332/` の `mrblib/bmp332.rb` と `ports/esp32/bmp332.c`、
`lib/gems/picoruby-fmrb-midi/ports/esp32/fmrb_midi_serial.c`、`main/drivers/audio_p4/audio_p4_hw.c`。アプリは使い方を写した所。

### fmrb_midi.rbs

| 書いたもの | 実装 | アプリ・失敗のしかた・単位の根拠 |
|---|---|---|
| `FmrbMidi.sam2695_device`・`.serial_device` | fmrb-serial-midi.rb:208-225 | nil: 同 209-210 のコメント・213・221 (`transport.connected?`)。毎回 `Sam2695.new` が `reset` (GM System On と全 channel 停止): 188-201・191。ポートは共有: fmrb_midi_serial.c:131-137 (すでに開いていれば OK)。端末に TX ピンが無いと失敗: 同 121-128。baud 31250・`rx` 既定 -1: fmrb-serial-midi.rb:46・68。アプリは `FmrbMidi.sam2695_device if @serial.nil?` と 1 度だけ作る: midi_apu.app.rb:297・mml.app.rb:240・smf_player.app.rb:218 |
| `FmrbMidi.scheduler` | fmrb-midi.rb:129-138 | nil の条件: 同 124-128 (ホストの試験)。アプリは有無だけ見る: midi_time.app.rb:46・midi_bench.app.rb:599 |
| `FmrbMidi.tick`・`.tick_delay` | 154-163・176-186 | ms: 同 165-175。0 未満にならない: 185。例は midi_apu.app.rb:73・110。「呼ばないと trigger の音が伸びる」: fmrb-midi.rb:173-175・189-196 |
| `FmrbMidi.gm_name`・`GM_DRUM_CHANNEL` | fmrb-gm.rb:60-68 | smf_player.app.rb:200・209・368 |
| `SmfPlayer#load_string`・`load` の既定値戻り | fmrb-smf.rb:128-160・486-516 | 失敗の理由の文字列: 521-540・158。形式 2: 535-538。設定が初期値に戻る: `reset_state` 486-516 (`@transpose`・`@channel_mask`・`@ignore_program_change`・`@scale_permille`)。smf_player.app.rb:120-129 は load のあとで設定している |
| `SmfPlayer#start`・`stop`・`pause`・`resume`・`finished?` | 207-270 | 戻り値: 同行。すぐ戻る・最初の tick で時計を合わせる: 216-222。resume が最大 lookahead 先から続く: 255-258。`finished?` が読み込んだだけでも true: 268-270 (`@loaded && !@playing`) |
| `SmfPlayer#tick`・`next_delay` | 368-379・476-482 | 戻り 0 / 20 / nil: 381-389・390-404 (`ms_until`)・438-448 (`DRAIN_POLL_MS` 59)。例 midi_apu.app.rb:91・smf_player.app.rb:396 |
| `SmfPlayer#device`・`device=` | 67・72-79 | 古い口の音を止め、待ち行列を捨てる: 74-75。アプリは load のあとに `@player.device =` する: smf_player.app.rb:123・midi_apu.app.rb:293・304 |
| `SmfPlayer#lookahead_us`・`lookahead_us=`・`LOOKAHEAD_US` | 89-99・355 | 0 の意味と、口が受けられないと 0 のまま: 89-99・85 (`@voices && @sched ? LOOKAHEAD_US : 0`)。変えると待ち行列を捨てる: 97。例 midi_time.app.rb:90 |
| `SmfPlayer#transpose`・`channel_mask`・`ignore_program_change` | 296-311・793・826-835 | ドラム (channel 9) は動かさない・範囲外は元のまま: 826-833。mask の意味: 793。Program Change を送らない: 807。音を止めずに変えると note off も今の値で計算: 843-846 (`send_note_off`)。smf_player.app.rb:126・185 |
| `SmfPlayer#track_count`・`division`・`format` | 67・498-500・531-534 | 読み込む前は 0: 498-500 |
| `SmfPlayer#late_*`・`stall_count`・`underrun_count`・`timing_stats` | 322-344・395-424 | しきい値: `STALL_MS` 49・`UNDERRUN_US` 65。文字列の形: 325-344。遅れは 0 以上: 419-424。例 midi_apu.app.rb:98 |
| `MmlPlayer#load_file`・`load_text` | fmrb-mml.rb:138-183 | 書式: 117-135。失敗: 143-146 (`"path: message"`)・181 (`no parts in the file`)。sound の 4 設定は読み込み時に口へ送る: 341-353・131-135 のコメント。mml.app.rb:189 のコメントも「device を先に選ぶ」。設定が以降のパートに残る: 156-179 (変数が part ごとに戻らない) |
| `MmlPlayer#load_string`・`add_string` | 111-115・189-196 | `t120` は読まない: 39-41。false と error: 192-195 |
| `MmlPlayer#total_clocks` | 77・192・34-37 | 四分音符 24 clock。ms への換算は 425 (`60_000_000 / (bpm * 24)`)。1 clock = 2500 / bpm ms |
| `MmlPlayer#start`・`pause`・`resume`・`tick`・`next_delay`・`finished?` | 220-274・313-333 | SmfPlayer と同じ形。`tick` の nil: 314・512 |
| `MmlPlayer#tempo_scale`・`tempo_scale=`・`bpm` | 276-288・212-218 | 範囲 1〜100000 パーミル: 279-281。`bpm` は別に持つ: 276-277 のコメント |
| `MmlPlayer#loop`・`loop=` | 210・457-464・170 | 初期値 false: 85。`reset_state` (365-387) は `@loop` を戻さない。mml.app.rb:173・207・265 |
| `MmlPlayer#device`・`device=`・`sent_count`・`stall_count`・`underrun_count`・`timing_stats` | 77・90-96・292-309・476-492 | mml.app.rb:172・249・82 |
| `ApuTransport.new` | fmrb-midi.rb:253-296 | owner が FmrbAudio ならそのまま: 257。`FmrbMidi.register` する: 295。midi_bench.app.rb:572-574 |
| `ApuTransport#note_on`・`note_off` | 406-455 | 声の無い channel は nil: 407-409・429-431。押さえたままの前の音に戻る: 447-454。midi_bench.app.rb:576-583 |
| `ApuTransport#defer_voices`・`flush_voices` | 478-504 | 開いていれば何もしない: 479。`FmrbMidi.tick` が閉じる: 149-153・160。`due_us` の意味: 477・482。midi_bench.app.rb:577-593 |
| `ApuTransport#all_off`・`auto_map`・`map_channel` | 519-527・369-395・348-354 | `auto_map` は先に全部 nil にする: 370-374、空だと戻る: 375。`map_channel` の失敗: 349-350。midi_apu.app.rb:146・279、smf_player.app.rb:122、mml.app.rb:171 |
| `ApuTransport` のクラスの説明 | fmrb-midi.rb:11-20・245-247・314-316 | 声 4 つ・既定の割り当て・捨てるメッセージ・SUB を使う |
| `MIDI::Device.new`・`transport` | midi_device.rb:8-17 | transport の条件: 8-10 (`send_packet`・`read_available`・`bytes_available`・`connected?`)・`info` は 26・`_get_transport_mask` 294-305。trigger に要る登録・`@pending`・`NoteScheduler`: fmrb-midi.rb:189-205・690-705。midi_bench.app.rb:37-88 の `CountingTransport` が最小の見本。`transport` の呼び出し: midi_apu.app.rb:146・279 |
| 直し: `MIDI::Device` の引数 | midi_device.rb:35-278 | 2 章 |

### fmrb_audio.rbs

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `FmrbAudio#mic_available?`・`mic_rate`・`mic_enable`・`mic_read` | fmrb-audio.rb:42-59 → `FmrbMic` app.c:2352-2447 | 「マイクの無い端末では available? が false・read は nil」: fmrb-audio.rb:33-36・app.c:50-56 (ESP32-P4 だけ)。`mic_rate` は P4 で 47160 (`15720 * 3`、audio_p4_hw.c:70-72・493-495)、それ以外 0 (app.c:2362-2370)。`mic_enable` の戻り (成功なら true、マイク無し・失敗は false、例外なし): app.c:2373-2384・audio_p4_hw.c:497-523。`mic_read`: count 1〜4096 の ArgumentError は app.c:2394-2396、2 * count バイトの String か nil (半端は返さない): 2386-2388・2400-2420、timeout 既定 200: 2392、ドライバは 256 ほどずつ: 2386 のコメント、timeout は 1 回の読みごと: 2404-2410、有効にしていないと負で nil: audio_p4_hw.c:540。例 mic_spectrum.app.rb:70-84・120・147 |
| `FmrbMic.available?` | app.c:2352-2360・2444 | mic_spectrum.app.rb:70 が `FmrbAudio` を作る前に呼ぶ |
| `FmrbAudio::MAIN`・`SUB`・`CH_*` | fmrb-audio.rb:12-23 | picoruby.app.rb・picorabbit.app.rb・mahjong_sound.rb |
| 直し: `play`・`play_slot`・`note_on` | fmrb-audio.rb:61・96・134 | 2 章 |

### fmrb_tile.rbs・fmrb_p5.rbs・fmrb_picture.rbs

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `TileMap#layer_count` | fmrb-tilemap.rb:152・306・370 | `.map.bin` は見出しの 11 バイト目、JSON は `layers.size`。`TileRing#ensure_view` が使う: 459 |
| `TileSheet.picture_height` | 61-72 | 0 の条件: 63 (26 バイト未満)・69 (どちらでもない)・70-72 (`rescue 0`)。BMP の上下反転: 68 |
| `P5#commit` | p5.rb:91 | `alias_method :commit, :present` |
| `P5` の定数 | p5.rb:29-47 | picoruby.app.rb:843-850。`SUBTRACT`・`MULTIPLY`・`SCREEN` は `REPLACE` と同じ (46-47) なので書かない |
| `BMP332.png_path?` | bmp332.rb:30-32 | sprite_editor.app.rb:233・273 |

## 4. 載せない候補 (内側。親が ignore.yml に入れる)

合計 49 (クラス#メソッド。定数を除く)。

| クラス | メソッド | 理由 |
|---|---|---|
| `FmrbMidi::SerialTransport` (19) | `.new`・`all_off`・`baud`・`bytes_available`・`close`・`connected?`・`defer_voices`・`device_info`・`flush_voices`・`note_off`・`note_on`・`path`・`read_available`・`rx_pin`・`send_packet`・`send_raw`・`transport_id`・`tx_pin`・`uart_num` | 同梱のアプリは作らず、`FmrbMidi.sam2695_device` が返す `MIDI::Device` を使う (`serial_device` も同じ)。`send_packet`・`read_available`・`bytes_available`・`connected?`・`device_info`・`transport_id`・`defer_voices`・`flush_voices`・`note_*` は `MIDI::Device`・プレーヤー・`FmrbMidi.tick` が呼ぶ下回り (fmrb-serial-midi.rb:93-181)。`baud` などの読み取りは作るときの引数の写し |
| `FmrbMidi::Sam2695` (3) | `.new`・`device_info`・`reset` | 同上。`sam2695_device` が作り、作った時点で `reset` が走る (191) |
| `FmrbMidi::ApuTransport` (9) | `audio`・`bytes_available`・`connected?`・`control_change`・`device_info`・`read_available`・`send_packet`・`transport_id`・`voice_for` | `send_packet` ほか 6 つは `MIDI::Device` が呼ぶ下回りで、アプリは `MIDI::Device` 越しにしか触らない (fmrb-midi.rb:300-341)。`control_change` は `MIDI::Device#control_change` の先 (506)。`audio` は作ったときの `FmrbAudio` の写し。`voice_for` は割り当ての読み取りで、同梱のアプリは呼ばない |
| `FmrbMidi::NoteScheduler` (3) | `trigger`・`next_due_in`・`tick` | `MIDI::Device#trigger`・`FmrbMidi.tick_delay`・`FmrbMidi.tick` が呼ぶ。アプリは `include` するだけ。迷ったものへ |
| `MIDI::Device` (4) | `send_note_on`・`send_note_off`・`send_control_change`・`send_program_change` | プレーヤーが呼ぶ割り当てなしの形 (fmrb-midi.rb:657-687 のコメントは「手書きのアプリは読みやすい `note_on(..., channel:)` を使うべき」)。同梱のアプリは呼ばない。`MIDI::Device#cable` (`attr_accessor`、常に 0) も同様 |
| `FmrbMidi::SmfPlayer` (7) | `close_instant`・`open_instant`・`drain_or_finish`・`drop_queued`・`ms_until`・`run_due`・`voice_group_of` | `tick` の内側 (381-482)。`drop_queued` は 101-110 のコメントのとおり停止・切り替えが内部で使う |
| `FmrbMidi::MmlPlayer` (1) | `voice_group_of` | 同上 |
| `NsfHeader` (2) | `.new`・`.extract_string` | `NsfHeader.parse` が組み立てる内側 (nsf-header.rb:18-60)。アプリは `parse` だけ (nsf_player.app.rb:174・429) |
| `BMP332` (1) | `.save_bmp` | `BMP332.save` が拡張子で振り分けて呼ぶ。アプリは `save` を使う (sprite_editor.app.rb:266) |
| `FmrbMidi` の定数 | `CH_*`・`PULSE_FREQ`・`TRIANGLE_FREQ`・`DRUM_MAP`・`CC_*`・`VOICE_NAMES`・`SmfPlayer` の `T_*`・`QUEUE_MARGIN` ほか | 実装の表・定数で、アプリは `FmrbMidi::` では使っていない (道具の明細にも出ない) |

迷ったもの (書いていない。親が決める):

- **`FmrbMidi::NoteScheduler`**: アプリが `include FmrbMidi::NoteScheduler` する (midi_bench.app.rb:38) ので、クラス名は道具の「アプリが使う未定義」に残る。
  `trigger`・`next_due_in`・`tick` はアプリが呼ばない (呼ぶのはライブラリ) ため決定 12 で書かなかった。自作の送り先の作り方は `MIDI::Device.new` の長文に書いた。
  書くなら 3 メソッドの宣言になる。
- **`FmrbMidi::SerialTransport`・`Sam2695`**: 同梱のアプリは直接作らない。外の音源を自分で組みたいアプリのために `.new` だけでも書くか、は判断が要る。
- **`FmrbMidi.scheduler` の戻り**: C の `FmrbMidi::Sched` (`_` 始まりのメソッドだけ) なので `untyped` にした。

## 5. アプリが使っているのに実装に見つからなかったもの

この範囲では無い。`FmrbMidi`・`SmfPlayer`・`MmlPlayer`・`ApuTransport`・`MIDI::Device`・`FmrbAudio`・`BMP332`・`NsfHeader`・`TileMap`・`TileSheet`・`TileRing`・`P5` の
呼び出しを `flash/app` (`test/` を除く)・`flash/lib/templates`・`flash/usr/share/samples` で grep し、全部実装に定義があった。

## 6. 数える道具の前後と型 db の大きさ

- `rake sig:coverage` 前 → 後: sig の 79 クラス・1058 メソッド → 81 クラス・1117 メソッド (クラスの増えは `FmrbMic` と `FmrbMidi::ApuTransport`)。
  アプリが使う未定義: クラス 24 → 22 (71 → 69 回)、定数 18 → 8、受け手のわかるメソッド 43 → 36 (90 → 72 回)、型の決まらない名前 85 → 67 (247 → 195 回)。
  実装にあって sig に無い: Ruby 499 → 444、C 49 → 48。基準との差 `app_class -2, app_constant -10, app_method -7, app_name -18, impl_ruby -55, impl_c -1`。
  `rake sig:coverage:update` で基準を更新 (別コミット)。書き方の欠け 0 (要約 111 バイト超 0・`<<en>>` 無し 0・長文なし 0・例なし 0・強調が行をまたぐ 0。
  `<<en>>` の無い長文 (数に入らない) も 0)。
- 型 db (tidbgen を前の版 `715dff07` の sig と今の sig で別々に走らせ、`tools/ti/test_sig` を重ねて読んだ):

| 領域 | 前 | 後 | 差 | 上限 |
|---|---|---|---|---|
| 名前 | 10,794 | 11,620 | +826 | 65,535 |
| シグネチャ | 41,088 | 44,108 | +3,020 | 65,535 |
| 要約 (文書) | 78,062 | 82,431 | +4,369 | 24 ビット |
| メソッドの数 | 3,442 | 3,558 | +116 | |
| 引数の数 | 2,917 | 3,055 | +138 | |
| クラスの数 | 81 | 83 | +2 | |
| 定数の数 | 201 | 221 | +20 | |

  上限に対して シグネチャ 67%、名前 18% (80% には届いていない)。シグネチャの領域が前より 3 ポイント増えた。1 メソッドあたり約 26 バイトなので、S5 (Asterism ほか、約 300 メソッド) を足すと
  約 79% になる見込みで、80% に近い。次の段の前に領域の手当て (plan 2.4) を考えたほうがよい。

## 7. 確認の結果

- `rake ti:test`: 通った (期待値の直しは不要)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass)。`rake boundary:check`: 一致 (141 functions、`sdk/` の差分は無い)。
- `rake sig:coverage`: 通った (上のとおり)。
- ビルドと sim は流していない。

## 8. 親が sim で確かめるとよい項目 (エディタに打って Ctrl+E で指摘の有無、または irb)

1. `FmrbMidi.gm_name(0)` → "Ac Grand Piano"。`FmrbMidi.gm_name(200)` → nil
2. `FmrbMidi.sam2695_device` → sim で受け皿 (FIFO) が無ければ nil
3. `@m = FmrbMidi::MmlPlayer.new(FmrbMidi.device(self))` のあと `@m.load_string("cde", channel: 0)` → true
4. `@m.load_string("")` → false、`@m.error` → "no notes in the MML"
5. `@m.start` (読み込み済み) → true、`@m.tick` → Integer か nil (何も読んでいなければ `start` は false)
6. `@m.finished?` を読み込み直後に打つ → true (鳴らしていなくても)
7. `@m.loop = true` のあと `@m.loop` → true
8. `s = FmrbMidi::SmfPlayer.new(FmrbMidi.device(self))` → `s.lookahead_us` → 300000 (スケジューラがあるとき)
9. `s.load("/nonexistent.mid")` → false、`s.error` に "cannot read" で始まる文字列
10. `s.transpose = 2` → Ctrl+E で指摘されない。`s.channel_mask = 0b101` も同様
11. `s.timing_stats` → "no events yet" (何も送る前)
12. `FmrbMidi.tick_delay(100)` → 100 以下の整数 (trigger の予約が無ければ 100)
13. `FmrbMidi.device(self).note_on(60, 100, channel: 1)` → Ctrl+E で指摘されない (前は引数の数で指摘された)
14. `FmrbAudio.new(self).mic_available?` → sim では false、`mic_rate` → 0、`mic_read(512)` → nil
15. `BMP332.png_path?("/home/a.PNG")` → true、`TileSheet.picture_height("/nonexistent")` → 0

## 9. 読んでいて見つけた実装の不具合の疑い (直していない)・確かめていないこと

- **`SmfPlayer#transpose` を鳴らしながら変えると note off がずれる**: `send_note_on` は `shift` した高さを `@sounding` に覚えるが (fmrb-smf.rb:837-841)、
  `send_note_off` は今の `@transpose` で `shift` し直すので (843-858)、変更前に始めた音の note off は別の高さへ行く。止まらない音が残りうる。実行では確かめていない (長文には「ずれることがある」と書いた)。
- **`MIDI::Device#pitch_bend` の `clamp`**: midi_device.rb:68 の `Integer#clamp` は、fmrb-smf.rb:810-812 のコメントと doc/midi/report/p2.md:154-157 では「この mruby にはない (NoMethodError)」とある。
  今のビルドで足されたかは確かめていない。アプリは `pitch_bend` を呼んでいない。sig には書いたまま (`pitch_bend: (Integer value, ?channel: Integer)`)。
- **`MmlPlayer#load_text` の `@error` が残る**: 空のパートのあとに鳴るパートが来ると `@loaded` は true になるが、`@error` は "no notes in the MML" のまま (fmrb-mml.rb:194)。
  実害は `error` を読んだときだけ。
- **`SmfPlayer#drop_queued` は共有の待ち行列 (`FmrbMidi::Sched._clear`) を空にする**: 2 つのプレーヤーを同時に鳴らすと、一方の停止で他方の予約も消える
  (fmrb-smf.rb:104-110)。同梱のアプリは同時に 1 つしか鳴らさない。確かめていない。
- **`FmrbAudio#mic_enable(false)` の戻り**: マイクの無い端末では false (失敗と区別できない)。これは sig の長文に書いた。
- **確かめていないこと**: マイクの値 (47160 Hz) は実装の `15720 * 3` を読んだもので、実機では測っていない。シリアルの口の失敗 (`send_packet` が -1) は sig に書いていない (`void` のまま)。
  `rake sig:coverage` の「アプリが `_` 付きを呼ぶ」件 (`FmrbMidi.scheduler._clear` など) は決定 19 のとおり載せていない。
  `(Symbol | Integer voice)` のような引数の和型が型エンジンで読めるか不明だったので `map_channel` の `voice` は `untyped` にした。
