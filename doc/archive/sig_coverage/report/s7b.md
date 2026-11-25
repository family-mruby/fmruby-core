# S7b クラスと定数の説明の欠けを埋める (サイトに出す前の仕上げ)

> 2026-11-25 | 指示書 `instruction_s7b.md` の実行結果。ブランチ `sig-s7b`。宣言 (`def`・`class`・`module`・定数の型) は
> 変えていない。変えたのは `sig/*.rbs` のコメントだけ。

## 1. 直した数

| 項目 | 数 | 中身 |
|---|---|---|
| 1. クラスのコメントを新しく書いた | 22 + 名前空間 `Fmrb` | `FmrbApp`・`FmrbGfx`・`FmrbAudio`・`FmrbMidi`・`MIDI`・`FmrbFile`・`P5`・`Fmrb` (Fft・Raycast の名前空間)、Ruby の基本クラス 14 (`Array`・`Class`・`Enumerable`・`FalseClass`・`Float`・`Hash`・`Integer`・`Kernel`・`NilClass`・`Proc`・`Range`・`String`・`Symbol`・`TrueClass`)。`Untyped` は書いていない |
| 2. 英語を足した (日本語だけだったクラス) | 35 | `Asterism` 系 15 (`Asterism`・`Connection`・`Proxy`・`AsyncProxy`・`Future`・`Topic`・`Subscription`・`SampleInfo`・`CurrentCall`・`Error`・`RemoteError`・`TimeoutError`・`Disconnected`・`EncodeError`・`FmrbAsterism::Settings`)、音 6 (`FmrbMic`・`NsfHeader`・`FmrbMidi::SmfPlayer`・`MmlPlayer`・`ApuTransport`・`MIDI::Device`)、絵 8 (`SpriteImage`・`SpriteInstance`・`FmrbHitMask`・`GfxBlock`・`TileSheet`・`TileMap`・`TileRing`・`FmrbVideo`)、`File` (fmrb_file.rbs と fmrb_misc.rbs の 2 か所)・`Log`・`FmrbI18n`・`Digest`・`Module`・`Object` |
| 3. 1 行目を直した (111 バイト・`日本語 <<en>> English`) | 24 | 上の 2 の多くと、`Asterism::ROS`・`ScriptError`・`FmrbConst`・`Fmrb::Fft`・`FmrbHw`・`FmrbReceiveConf`・`Unit8Encoder`・`FmrbToml`・`FmrbColors`・`FmrbNet`・`Fmrb::Raycast`・`GC`・`GPIO`・`I2C`・`IO`・`JSON`・`Machine`・`Math`・`MessagePack`・`RNG`・`Time`・`Dir` と、新しく書いた `Kernel`。折り返していた `FmrbVideo`・`Asterism` は完結した要約にし、続きは長文へ回した |
| 4. 定数 | 要約なし 13 に書いた、英語なし 14 に足した、長すぎ 4 を縮めた | `FmrbAssoc::*` 6、`FmrbAudio::NSF_*` 7、`FmrbGfx::COLOR_*` 9、`FmrbConst` 5 (節の見出しのコメントに続いていて、見出しが 1 行目に数えられていた。見出しの後に空行を入れた)、`Asterism::ROS::DEFAULT_QOS`・`Asterism::CHECK_TIMEOUT`・`FmrbHw::PIN_SYSTEM_EXCLUSIVE`・`GPIO::HIGH_Z` (括弧の説明を長文へ移した) |
| 5. 長文の英語 | 19 (数えスクリプトの前の 24 から、4 の `FmrbConst` の 5 を引いた) | `Asterism::ROS::*` の `close`・`cancel` 5、`P5` の色 9、`GPIO.high_at?`・`low_at?`、`FmrbAsterism.saved_name`、`FmrbHw.pin_status_all`、`Hash#key?` |

済んだまとまり: クラスのコメント / 英語の追加と 1 行目 / 定数 / 長文の英語。残り: なし (指示書の 1〜5 を全部)。

## 2. クラスのコメントを新しく書いた物の根拠

| クラス | 読んだ場所 (実装・sig) |
|---|---|
| `FmrbApp` | `sig/fmrb_app.rbs` の `on_create` (「ファイルにはクラスを書くだけ、読み終えると start される」)・`on_update`・`gfx` (`@gfx`・原点)・`@gfx: FmrbGfx` の宣言・`window_width`・`theme_fg`・`set_timer`・`publish`。`lib/gems/picoruby-fmrb-app/mrblib/fmrb-app.rb:1-3` (継承して使う) |
| `FmrbGfx` | `sig/fmrb_gfx.rbs` の冒頭コメント (原点・RGB332・自分を返す)、`present` (「フレームの終わりに 1 回」)、`clear`・`fill_rect`・`draw_text`・`draw_image` の宣言 |
| `FmrbAudio` | `sig/fmrb_audio.rbs` の `self.new` (`FmrbAudio.new(self)`)・`play`・`stop`・`pause`・`resume`・`play_wav`・`note_on`・`load_fmsq`・`play_slot`・`mic_*` |
| `FmrbMidi` | `sig/fmrb_midi.rbs` の `self.device`・`sam2695_device`・`serial_device`・`register`・`now_us`、`SmfPlayer`・`MmlPlayer` のコメント |
| `MIDI` | `sig/fmrb_midi.rbs` の `MIDI::Device.new` と `FmrbMidi.device` の説明 (口は `FmrbMidi.device` から受け取る) |
| `FmrbFile` | `sig/fmrb_file.rbs` 冒頭 (パスで呼ぶ・開いたままの状態を持たない・丸ごと読み書き・読む位置を持つなら File) |
| `P5` | `sig/fmrb_p5.rbs` 冒頭 (`P5.new(gfx)`・fill/stroke)、`new` (FmrbGfx を包むだけ)、`present`・`background`・`no_fill` の宣言 |
| `Fmrb` | `sig/fmrb_fft.rbs:34`・`sig/fmrb_raycast.rbs:28` (`Fmrb::Fft`・`Fmrb::Raycast` だけを持つ) |
| `Array` | `sig/array.rbs:7` (`a[i], a[j] = a[j], a[i]` は動かない)・`each_index` などの「ブロック呼び出しは 1 回 0.4ms」・`include Enumerable` の宣言 |
| `Class` | `sig/class.rbs` (`Class#name` だけ、`Module`・`Object` が同じファイルにある) |
| `Enumerable` | `sig/enumerable.rbs` (each・each_with_index・map・to_a)、`Array`・`Hash` の `include Enumerable`、`Range < Enumerable` の宣言 |
| `FalseClass`・`TrueClass` | `sig/false_class.rbs`・`true_class.rbs` (メソッド無し)。「nil と false だけが偽」は一般の Ruby の仕様で、実装は読んでいない |
| `Float` | `sig/float.rbs` の `==` (小数どうしで比べない)・`/` (0.0 で割ると Infinity)・`to_i` (切り捨て)・`round` |
| `Hash` | `sig/hash.rbs` の `[]` (無い印は nil)・`[]=` (String と Symbol は別)・`each_key` (ブロックが要る・Enumerator 無し)・`include Enumerable` |
| `Integer` | `sig/integer.rbs` の `/` (切り捨て)・`times`・`downto` (0.4ms)・`chr` (0〜255) |
| `Kernel` | `sig/kernel.rbs` の `puts` (行はログに出る)・`print` (改行しない)・`rand` (max 無しで 0〜4294967295)・`proc`・`lambda`・`->` |
| `NilClass` | `sig/nil_class.rbs` の `nil?`・`to_i`・`to_f`・`to_s`・`to_h` |
| `Proc` | `sig/proc.rbs` の `new`・`call`、`sig/kernel.rbs:86` (Spinel は外のローカル変数を掴めない) |
| `Range` | `sig/range.rbs` の `last` (書いた方の端)・`===`・「0.4ms」・`< Enumerable` |
| `String` | `sig/string.rbs` の `length`・`[]` (位置は文字数)・`bytesize`・`getbyte`・`byteslice`・`+` と `<<`・`end_with?` 近くの「Regexp が無い」(389 行) |
| `Symbol` | `sig/symbol.rbs` の `==` (同じ印は同じ物) と、載っているメソッド 5 つ |

実装 (mrblib・C) を直接読んだのは `fmrb-assoc.rb`・`fmrb-audio.rb` (定数) と graphics-audio の `audio_commands.h`・`audio_task.c` (NSF の state・reason) だけ。クラスのコメントは主に今ある sig の説明から書いた。

定数の根拠: `FmrbAssoc::*` は `lib/gems/picoruby-fmrb-app/mrblib/fmrb-assoc.rb:33-45` (`SYS_PATH`・`USR_PATH`・`RUN = "run"`・`EDIT = "edit"`・`DEFAULT = EDIT`・`MAX_EXT_LEN = 8`)。`FmrbAudio::NSF_*` は `fmrb-audio.rb:79-86` と `fmruby-graphics-audio/main/common/audio_commands.h:57-67` (state の IDLE「何も無い・命令で止めた」・FADING「終わりを見つけて小さくしている」・ENDED「自分で終わった」、reason の SILENCE・LOOP・LIMIT)、`audio_task.c:160-165` (無音・ループを聞き終えた・時間の上限)。

## 3. 前後の欠けの数

数えたスクリプト (道具は変えていない。`tools/ti/gen_site.rb` の読み方を借りた): `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/2488ad3e-d680-4f55-88f7-804cff8c4d43/scratchpad/s7b/gaps.rb` (`-v` で名前を出す)。前の数えは同じスクリプトを指示書の直前のコミットの sig に向けたもの (`gaps_before.rb`)。

| 欠け | 前 | 後 |
|---|---|---|
| クラスのコメントが無い | 24 (`Fmrb` は 2 ファイルで 2 回数える、`asterism_ros.rbs` の `Asterism` は別ファイルにコメントがあり実際は欠けでない) | 1 (その `Asterism` の見かけだけ。サイトの道具は別ファイルの物を使うので 0) |
| クラスに英語が無い | 35 | 0 |
| クラスの 1 行目が `日本語 <<en>> English` で 111 バイト以内でない | 24 | 0 |
| 要約の無い定数 | 13 | 0 |
| 要約に英語の無い定数 | 14 | 0 |
| 定数の要約が 111 バイト超 | 4 | 0 |
| 長文が日本語だけ (コードだけの行を除く) | 24 | 0 |

## 4. 確認の結果

- `rake sig:coverage`: 「3. doc gaps」は全部 0 (no summary 0・>111 bytes 0・no `<<en>>` 0・no long form 0・no example 0・`**` across lines 0)、`vs baseline: no change`。途中で自分の英語が `**` を行にまたがせた (`FmrbGfx`・`Float`・`ApuTransport`) のを直した。`sig:coverage:update` は数が変わらないので流していない。
- `rake ti:test`: host 35 suites, 35 passed。
- `rake ti:site:test`: gen_site tests all passed。
- `rake regress`: 310 runs (82 scenes on 5 profiles), 310 passed。
- 境界の検査は落ちなかった。`sdk/` の作り直しは無し。

## 5. 直さなかった物

- `Untyped`: 書かない (指示書)。
- `String#__ljust_rjust_argcheck` (s7.md 4 章の 6): 触っていない。調べた結果:
  - 実装は `components/picoruby-esp32/picoruby/mrbgems/picoruby-mrubyc/lib/mrubyc/mrblib/string.rb:41,56` の `ljust`・`rjust` の先頭から呼ばれる内部の検査 (空の埋め草は ArgumentError、幅が Integer でないと TypeError)。定義は同じファイルの 89 行で、`# private` の後ろ。
  - アプリ・lib・flash・道具のどこからも呼ばれていない (`__ljust_rjust_argcheck` の grep は上の mrubyc の 3 か所と sig の 1 か所だけ)。型エンジンの試験 (`vendor/picoruby-ti`・`tools/ti`・`tools/sig_coverage`) もこの名前に頼っていない。`vendor/picoruby-ti/example/rbs/string.rbs:44` に上流の同じ宣言がある。
  - sig の宣言は引数 1 つ (`(Integer | String) width_or_padding`) だが、実装は 2 つ (`width, padding`)。つまり sig は実装と合っていない。上流の example から来た宣言のまま。
  - 消す・隠すかは親の判断 (sig の宣言を変えることになるので指示書の範囲外)。サイトの `site_exclude.yml` で外す手もある。この mrubyc の String がどのビルドで使われるか (Family mruby の mruby ビルドでは別の String かもしれない) は確かめていない。

## 6. 説明と実装の食い違い・確かめていないこと

- 上の `__ljust_rjust_argcheck`: sig の引数 1 つと実装の 2 つ。
- 基本クラスのコメントは、今ある sig のメソッドの説明に書いてあることだけにした。CRuby との違いのうち、実装 (mruby の `mrblib`・C) を直接読んで確かめたものは無い。例えば「`Hash` が Enumerator を持たない」は sig の `each_key` の説明のとおりで、実装は見ていない。`TrueClass`・`FalseClass` の「nil と false だけが偽」は Ruby 一般の話。
- `FmrbApp`・`FmrbGfx`・`FmrbAudio`・`FmrbMidi` の「最初に読むとよいメソッド」の選び方は、sig の説明に出てくる頻度と流れで決めた (使用実績の集計ではない)。
- `FmrbAudio` の `pause`・`resume` は NSF の曲に対するものか、WAV にも効くかを実装で確かめていない (sig は「曲」と書いている)。クラスのコメントでは NSF の曲に付けて書いた。
- `FmrbAudio::NSF_IDLE` などの意味は graphics-audio のヘッダのコメントから。`reason` の 0 (NONE) は定数が sig に無いので書いていない。
- `Fmrb` の名前空間のコメントは `fmrb_fft.rbs` に置いた (`fmrb_raycast.rbs` の `module Fmrb` には置いていない。サイトの道具はどちらかにあれば使う)。
- ブラウザ版の表示・F1・エディタのホバーでの見え方は見ていない (ビルド・サイトの生成をしない決まり)。sig を直したので、実機・sim に反映するには `rake clean` してから作り直す (sig/README.md)。

## 7. コミット

`git log sig-s7b` を参照 (件名は親への報告に並べた)。
