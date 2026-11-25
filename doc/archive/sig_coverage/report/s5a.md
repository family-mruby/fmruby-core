# S5a データ・機器・絵の計算・storage のライブラリを sig に書く

> 2026-11-25 | サブ (Sonnet) の報告。指示書は instruction_s5a.md。ブランチ `sig-s5a`。

## 1. 結果

実装にあって sig に無かったクラスのうち、指示書の範囲 (データ・機器・計算・storage のライブラリ) を書いた。
`rake sig:coverage` の差は `app_class -11, app_constant -2, app_method -23, app_name -7, impl_ruby -70, impl_c -33`
(基準との差。基準は別コミットで更新)。sig は 81 → 104 クラス、1114 → 1244 メソッド。書き方の欠け (「3. doc gaps」) は 0 のまま。

| クラス | 書いた数 | ファイル |
|---|---|---|
| `JSON` | 2 + 例外 4 クラス | `sig/json.rbs` (新) |
| `JSON::Digger` | 3 (`.new`・`dig`・`parse`) | 同上 |
| `MessagePack` | 4 (`pack`・`unpack`・`dump`・`load`) | `sig/message_pack.rbs` (新) |
| `FmrbToml` / `FmrbToml::Raw` | 7 / 2 | `sig/fmrb_misc.rbs` (追記) |
| `FmrbColors` | 11 + 定数 1 | 同上 |
| `Digest::SHA256` | 6 (`.new`・`.hexdigest`・`update`・`<<`・`hexdigest`・`digest`) | `sig/digest.rbs` (新) |
| `FmrbHw` | 4 + 定数 12 | `sig/fmrb_hw.rbs` (新) |
| `FmrbNet` / `FmrbNet::Request` | 6 / 7 | `sig/fmrb_net.rbs` (新) |
| `I2C` | 4 + 定数 2 | `sig/i2c.rbs` (新) |
| `GPIO` / `GPIO::Error` | 16 / 1 + 定数 7 | `sig/gpio.rbs` (新) |
| `Fmrb::Fft` | 14 + 定数 2 | `sig/fmrb_fft.rbs` (新) |
| `Fmrb::Raycast` | 14 | `sig/fmrb_raycast.rbs` (新) |
| `FmrbReceiveConf` | 16 + 定数 4 | `sig/fmrb_lib.rbs` (新) |
| `Unit8Encoder` | 13 + 定数 3 | 同上 |

素直 (例だけ) とつまずく (型紙の順) の数は数えていない。例だけにしたのは、`Digest::SHA256` の `digest`・`update`・`<<`、`FmrbColors.palette_size`・
`name_at`、`FmrbHw.pin_status_all`、`I2C` の定数、`GPIO` の `pin`・`high?`・`low?`・`high_at?`・`low_at?`・`pull_*_at`、
`Fmrb::Fft.q15?`・`size`、`Fmrb::Raycast#rays`・`side`・`fp_cos`、`Unit8Encoder` の読み取りと LED、など。残りは失敗のしかた・使えるビルド・省略値を書いたので型紙の順。

足したファイル名 (サイトの領域分け `AREAS` に入れるのは親): `json.rbs`・`message_pack.rbs`・`digest.rbs`・`fmrb_hw.rbs`・`fmrb_net.rbs`・`i2c.rbs`・
`gpio.rbs`・`fmrb_fft.rbs`・`fmrb_raycast.rbs`・`fmrb_lib.rbs` (`fmrb_misc.rbs` は既存で領域は変わらない)。
領域の候補: `fmrb_hw`・`i2c`・`gpio` は「本体」、`fmrb_net` は「ネットワーク」、`json`・`message_pack`・`digest`・`fmrb_lib` は新しい「データとライブラリ」、`fmrb_fft`・`fmrb_raycast` は「音」か「描画」。

## 2. 直した既存のシグネチャと長文

なし。既存の宣言には触っていない (`fmrb_misc.rbs` は末尾に足しただけ)。

## 3. 根拠の表

パスは `fmruby-core/` から。picoruby の gem は、Family mruby が差し替えたものは `lib/upstream/patch/...` の写しを読んだ (`components/picoruby-esp32/picoruby/` の側と一致を `diff` で確認: json・i2c)。
どのビルドに入るかは `lib/upstream/family_mruby.gembox` (全ビルド共通) と `family_mruby_{esp32,esp32p4,linux,native,wasm}.rb`。

### json.rbs (`lib/upstream/patch/picoruby-json/mrblib/json.rb`、gembox:`picoruby-json` で全ビルド)

| 書いたもの | 実装 | 失敗・形の根拠 |
|---|---|---|
| `JSON.parse` | json.rb:104-106、`Parser` 438-466 | 値でない字は `ParserError` "Unexpected character at index N": 463-464。空文字列も同じ (448 の `@json[0]` が nil)。閉じない文字列: 537-539。**構造の崩れは親の `JSONError`**: `expect` 63-68 (`Expected ',' ...`)。後ろのごみは読まない: `parse` は値を 1 つ返して終わる 446-466。キーは `parse_string` の String: 477・482。`\u`: 570-588 (`[code].pack('C*')`)。小数は手計算: 679-718。数: 607-658 |
| `JSON.generate` | 108-110、`Generator` 336-436 | Symbol・String は `generate_string` 349-350。その他は `to_s` の文字列: 359-360。Hash のキーも `generate_string`: 373。NaN は `to_s`: 433-435。`GeneratorError` は定義だけで `raise` が無い (15、ファイル全体を grep)。空白を入れない: 366-388。日本語・`/` はそのまま、0x20 未満だけ `\u00XX`: 414-423 |
| `JSON::Digger.new`・`dig`・`parse` | 143-146・150-178 | 負の位置は ArgumentError: 156-158。型違い: 164-166。キー無し `DiggerError`: 268-270。**範囲外の位置は `raise` が無い**: 330-332 (`JSON::DiggerError.new(...)` だけ)。形違いは `expect` の `JSONError`: 63-68 (CRuby で同じファイルを動かして `dig(0)` を `{...}` に・`dig("a")` を `[...]` に当てると "Expected '[' ..." / "Expected '{' ..."、mruby では未確認)。掘ると `@json` が切り出し部に置き換わる: 167-169 |
| 例外 4 クラス | 13-16 | `JSONError < StandardError`、ほか 3 つは `JSONError` の子 |

### message_pack.rbs (`lib/gems/picoruby-fmrb-msgpack`、gembox:`picoruby-fmrb-msgpack`、ESP32 は `ports/esp32/msgpack.c`、hosted は `hosted/hosted.cmake:209-212,482`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `pack` | src/picoruby_fmrb_msgpack.c:9-28、ports/esp32/msgpack.c:41-134 | 型: 46-131 (Integer は int64、Float は double、Symbol は str として: 66-74、バイナリ印の String は bin: 76-91)。書けない型は `-1` → RuntimeError "Failed to pack object to msgpack": c:17-19 |
| `unpack` | c:32-46、msgpack.c:176-255 | 空・途中切れ・余り・ext・2^63 以上は FALSE: 176-181・205-212・225-227・本体 163-171。RuntimeError "Failed to unpack msgpack data": c:41-43。bin はバイナリ印の String: 29-36・213 付近の `MSGPACK_OBJECT_BIN` |
| `dump`・`load` | mrblib/msgpack.rb:17-24 | 別名 |

### fmrb_misc.rbs の FmrbToml・FmrbColors (`lib/gems/picoruby-fmrb-app/`、gembox:`picoruby-fmrb-app`、全ビルド)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `FmrbToml.parse`・`load`・`error` | mrblib/fmrb-toml.rb:37-72、ports/esp32/toml.c:128-177 | nil と理由: 37-72。ファイル無しはログ無し・読めない/TOML でないはログ 1 行: 56-71。型の写し: 冒頭コメント 7-17 |
| `FmrbToml.update` | 81-122・240-283 | 行ごとの書き換え・表.キー: 201-206・240-283。nil で削除: 242・273。TOML でないファイルに触らない: 88-92。読み戻し検査: 103-120 |
| `FmrbToml.raw`・`Raw` | 24-34・23 | 引用符なし: 127 |
| `FmrbToml.dump_value`・`dump_key` | 126-142・181-191 | ArgumentError "no TOML for ...": 141 |
| `FmrbColors.shade` | mrblib/fmrb-colors.rb:138-155 | アプリ: image_viewer.app.rb:38・ai_helper.app.rb:117-119 |
| `by_name`・`name_of`・`name_at`・`palette_size`・`palette_value` | 69-122 | 小文字だけ (`downcase` を避けている: 24-26 のコメント・`lookup` 75-85) |
| `to_color` | 191-222 | `0x`・10 進・名前、他は nil: 195-221 |
| `to_text`・`set`・`clear`・`section` | 280-284・233-259・160-175 | `set` の戻り: 233-236。`clear`: 239-276 (節の見出しから次の見出しまでを捨てて全体を書き直す)。`section`: 160-175 |
| 使っているアプリ | main/ruby/default_app/ の editor/const.rb・palette_ui.rb・shell_commands.rb | `section`・`set`・`clear`・`to_color`・`to_text` |

### digest.rbs (`lib/gems/picoruby-fmrb-sha256/src/sha256.c`、ブラウザ版だけ: `lib/upstream/family_mruby_wasm.rb:100`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `.new`・`update`・`<<`・`digest`・`hexdigest`・`.hexdigest` | sha256.c:191-199 | 状態を変えない: 94-116 (`sha256_final` が複製を使う)。`S` の引数で TypeError: 146・179。他のビルドは `MbedTLS::Digest`: flash/app/modern/chat/stamps.rb:57-75 |

### fmrb_hw.rbs (`lib/gems/picoruby-fmrb-const/ports/esp32/const.c`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| メソッド 4 つ | const.c:76-108・609-612 | 範囲外は UNUSED/false: `components/fmrb_hal/platform/esp32/fmrb_hal_pin_manager_esp32.c:96-103・146-152`。`pin_status_all` の長さ: const.c:94-102 |
| `PIN_*` の用途 | 589-598 | 値は `components/fmrb_hal/fmrb_hal_pin_manager.h:27-36` |
| `pin_count` 49/55 | fmrb_hal_pin_manager.h:19-23 | sim の表は共有: 同 22 のコメント。hosted は `CONFIG_IDF_TARGET_ESP32P4` を定義しない |
| `PIN_I2C*` が実機だけ | const.c:600-606 (`#ifndef CONFIG_IDF_TARGET_LINUX`)、`hosted/hosted.cmake:521` (hosted も定義) | 値: `components/fmrb_common/include/fmrb_pin_assign.h` の Tab5 33-39、NARYA v4 127-132、AtomS3R 227-231、StickS3 303-307、既定 (S3) 415-418。`GPIO_NUM_NC` は -1 |
| システム専用のピンの登録 | fmrb_hal_pin_manager_esp32.c:24-94 | posix 側は何も登録しない: platform/posix/fmrb_hal_pin_manager_posix.c:7-9 |
| MIDI の TX が I2C2 SDA | lib/gems/picoruby-fmrb-midi/ports/esp32/fmrb_midi_serial.c:37 | |

### fmrb_net.rbs (`lib/gems/picoruby-fmrb-kernel/ports/esp32/net.c`・`mrblib/fmrb-net.rb`、gembox:`picoruby-fmrb-kernel`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `connected?`・`ip_address`・`hostname`・`ssid`・`wait_for_ip` | net.c:222-262・264-278 | Linux (`CONFIG_IDF_TARGET_LINUX`、sim・hosted 共通) は固定値: 223-224・233-234・244-245・268。WiFi 無しは false・"0.0.0.0"・""。`wait_for_ip` の既定 10000・負は 0: 267-271。実機の WiFi (`FMRB_HAS_WIFI`): 12-14 |
| `request`・`Request` | fmrb-net.rb:124-126・39-120 | 板は `fetch_now` で止まる: 107-119・26-30。ブラウザ版は `start_in_page`: 51-54・100-105。`done?`・`ok?`・`cancel`・`error` の文: 58-98 |
| `Net::HTTP` の在処 | family_mruby_{esp32,esp32p4,linux,native}.rb の `picoruby-net-http` | wasm に無い: family_mruby_wasm.rb:87-93 |
| 使うアプリ | main/ruby/default_app/appstore.app.rb:77・273 | weather.app.rb は `FmrbNet.connected?` だけ |

### i2c.rbs・gpio.rbs (`picoruby-i2c`・`picoruby-gpio`、ESP32 実機の S3 と P4 だけ: `family_mruby_esp32.rb:100-102`・`family_mruby_esp32p4.rb:112-114`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `I2C.new` | mrblib/i2c.rb:7-10 (components/picoruby-esp32/picoruby/mrbgems/picoruby-i2c/)、patch の i2c.c:182-200 | `unit:` 必須 (Ruby の引数)。unit 名 0/1 だけ: lib/upstream/picoruby-i2c-proxy/i2c.c:5-13。失敗は全部 `I2C_ERROR_INVALID_UNIT` → "Invalid I2C unit": 同 15-19・patch i2c.c:191-198。別アプリが持つ・ピン固定・画面の準備前: main/drivers/hw_proxy/hw_proxy_i2c.c:41-96 (init の結果が FAILED/BUSY/INVALID でも `I2C_gpio_init` は 1 種類に畳む) |
| `write` | patch i2c.c:11-124 | 型: 18-31・73-75。8 ビット切り捨て: 58・63。戻り値は長さ: 80-87・123 (hal は `len`: components/fmrb_hal/platform/esp32/fmrb_hal_i2c_esp32.c:40-51)。"I2C write failed": 119-121 |
| `read` | patch i2c.c:126-172 | 前置の送りは nostop: 149-155。"I2C read failed" と `len` 0: 167-171。最短 10 ms: hw_proxy_i2c.c の `if (timeout_ms < 10)` (handle_read 付近) |
| `scan`・`close` | mrblib/i2c.rb:12-31、patch i2c.c:174-180 | `puts` と nil: i2c.rb:18-30。close は所有だけ手放す: hw_proxy_i2c.c:180-199 |
| 定数 | i2c.rb:4-5 | |
| `GPIO.new`・`setmode`・`pin` | mrblib/gpio.rb:35-53・46 | 例外: 40-42・78・96。整数だけのピン: src/mruby/gpio.c:8-38 (proxy の `GPIO_pin_num_from_char` は常に -1: gpio-proxy/gpio.c:5-9)。台帳に取る (acquire) が結果を見ない: gpio-proxy/gpio.c:11-14・hw_proxy_gpio.c:12-21 |
| `read`・`write`・`high?`・`low?` | gpio.c:164-209 | 失敗 -1: components/fmrb_hal/platform/esp32/fmrb_hal_gpio_esp32.c:37-50。0/1 以外は ArgumentError: gpio.c:204-206 |
| `*_at` | gpio.c:51-159 | `GPIO.new` を通さない |
| OPEN_DRAIN・ALT・HIGH_Z | gpio-proxy/gpio.c:16-34・56-60 | open_drain と set_function は何もしない。HIGH_Z は入力扱い (`default:`) |
| `GPIO::Error.peripheral_error` | gpio.rb:13-32 | |
| 同梱のアプリは `GPIO` を使わない | flash/ を grep | I2C は i2c_kbd・encoder8・imu・services/timesync.rb が使う |

### fmrb_fft.rbs (`lib/gems/picoruby-fmrb-fft`、gembox:`picoruby-fmrb-fft`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `new` | mrblib/fmrb-fft.rb:458-477 | 例外: 459-464。Spinel の取り合いと fallback: 470-473・449・636-646 |
| `available?`・`q15?`・`micros`・`spinel_total_us` | 479-509 | `:dsp` は `FMRB_FFT_HAVE_ESP_DSP`: native/fmrb_fft_bench.c:527-533。Spinel は `FMRB_FFT_SPINEL`: native/fmrb_fft_spinel.c:112・232 |
| `forward`・`run` | 512-541 | C 側の検査: ports/esp32/fft_binding.c:28-52 (`need %d int16 samples`・iters >= 1)。Ruby 側の穴埋め 0: mrblib/fft_core.rb:76-91。窓と正規化: 同 37・166-173、頭打ち 171 |
| `sine`・`bin`・`peak_bin`・`bench`・`close` | 546-625 | 範囲外の `bin` は 0: 566。同値は小さい番号: 576-585。`bench` の Hash: 605-615 |
| 使うアプリ | flash/app/modern/mic_spectrum.app.rb:136-168、debug/fft_bench.app.rb | |

### fmrb_raycast.rbs (`lib/gems/picoruby-fmrb-raycast`、gembox:`picoruby-fmrb-raycast`)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `new`・`available?`・`backend`・`micros` | mrblib/raycast.rb:26-84 | 例外: 27-29・211。取り合い: 57-60・208-218 |
| `set_map`・`cast` | 92-129 | 例外: 99・119。受け付ける形: 167-179。ネイティブ側の検査: ports/esp32/raycast_binding.c:56-59 |
| `rays`・`dist`・`wall`・`side` | 132-157 | 6 バイト: raycast_core.rb:36 |
| `fp_sin`・`fp_cos` | 64-70・43-52 | 度 mod 360、FP_ONE 256: raycast_core.rb:24 |
| 地図の意味・視野・光線の数・歩数 | raycast_core.rb:24-32・80-100・113-170 | 地図の外は壁 (値 1): 91-92。0 以外が壁: 150-151。MAX_STEPS 24。深さの最小 1 と直角距離: 164-173 |
| `unpack`・`close` | raycast.rb:185-198・159-163 | |

### fmrb_lib.rbs

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `FmrbReceiveConf` の 16 メソッド | flash/usr/share/transfer/receive_conf.rb | `path` 97-101、`enabled?` 137-140、`send_enabled?` 144-147、`install_mode` 152-156、`view` 224-241、`set_enabled` 366-372 (TOML でないと `hand_for_edit` 549-554 が ArgumentError)、`set_send_enabled` 376-382、`set_install_mode` 159-172、`allow` 387-399、`deny` 405-428、`tell_transfer` 434-444、`node_ok?` 299-301、`allows_anyone?` 524-526、`allows?` 244-252、`senders` 211-218、`lines` 464-498。使うアプリ: flash/app/tool/receiving.app.rb、main/ruby/default_app/shell/shell_commands.rb:1510-1560、transfer.app.rb:228 |
| 定数 | 73-75・85・87 | |
| `Unit8Encoder` | flash/lib/unit8encoder.rb:4-109 | 範囲検査なし (レジスタ番号の計算だけ: 27・35・50・57・74)。使うアプリ: flash/app/debug/encoder8.app.rb:6-30 |

## 4. 載せない候補 (内側。親が ignore.yml に入れる)

| クラス | メソッド | 理由 |
|---|---|---|
| `JSON::Digger` (9) | `dig_array`・`dig_number`・`dig_object`・`dig_string`・`dig_value`・`pop_stack`・`push_stack`・`reset`・`skip_whitespace` | `dig` の中身 (json.rb:180 の `# private` 以降)。`JSON::Parser`・`Generator`・`Common` は ignore.yml に入っている |
| `FmrbToml` (8) | `.dig`・`.dump_float`・`.header_name`・`.line_key`・`.quote`・`.read`・`.set_line`・`.split_name` | `update`・`dump_value`・`dump_key` の下回り (fmrb-toml.rb:144-283、「inside」の節) |
| `FmrbColors` (3) | `.lookup`・`.read`・`.write` | 表引き・ファイル読み書きの下回り (fmrb-colors.rb:75-85・177-187・261-276)。アプリは `by_name`・`set`・`clear`・`section` を使う |
| `FmrbNet::Request` (4) | `.new`・`fetch_now`・`release`・`start_in_page` | 入口は `FmrbNet.request`。3 つは fmrb-net.rb:92-96 の「API でない」のコメントつき。`Request.new` は `request` が呼ぶだけで、同梱のアプリは直接作らない |
| `GPIO` (4) | `open_drain`・`set_dir`・`set_pull`・`set_function` | `setmode` の中身 (gpio.rb:55 の `# private` 以降) |
| `Fmrb::Fft` (2) | `.spinel_open`・`.spinel_close` | `new`・`close` が数える (fmrb-fft.rb:627-653) |
| `Fmrb::Raycast` (3) | `.open`・`.close` (クラス側)・`.pack_map` | 同じく `new`・`close`・`set_map` の中身 (raycast.rb:165-225)。`.close` は「クラスとインスタンスで種類が違う」と道具に出る |
| 裏側 | `FftNative`・`FftCore`・`FftCoreQ15`・`RaycastNative`・`RaycastCore` | 指示書のとおり (ignore.yml に入っている。`FftCoreQ15` も入っている) |
| `FmrbReceiveConf` の下回り | `parts`・`mark_at`・`hand`・`hand_senders`・`managed`・`at_ok?`・`checked_list`・`write_managed`・`block`・`save_hand`・`set_key` ほか | 道具の数に入っていない (storage のファイル)。`write_managed`・`checked_list`・`at_ok?`・`parts` は transfer.app.rb と shell が呼ぶが、console の `policy_set` の受け口と shell 内部なので書かなかった (迷ったもの) |

## 5. 迷ったもの (書いていない。親が決める)

- **`FmrbReceiveConf.write_managed`・`checked_list`・`at_ok?`・`parts`**: 転送アプリ (transfer.app.rb) と shell の `areceive` が呼ぶ。console が送る一覧を書く道なので、アプリが自分で呼ぶ API かの判断が要る。
- **`BMI270`・`RMT`**: 同梱のアプリ (imu.app.rb・led_matrix.app.rb) が使う機器のクラス。指示書の一覧に無かったので書いていない。`BMI270.new(i2c)`・`address`・`init`・`read_accel`・`read_gyro`・`read_temperature`・`error` と `RMT.new(pin, t0h_ns: ...)` は読めば書ける (次の一括で)。
- **`Net::HTTP`・`URI`・`MbedTLS`・`MbedTLS::Digest`**: weather・chat のアプリが使う。上流の gem (Net::HTTP.get_response、URI.parse、`MbedTLS::Digest.new(:sha256).update(..).finish`) で、実装の読みが別に要る。
- **`ArgumentError`・`RuntimeError`・`Exception`**: 道具の「アプリが使う未定義」に出るが、メソッドの無い例外クラスなので、書くなら標準の例外を並べた sig を新しく作る話になる。
- **`FmrbMidi::NoteScheduler`**: 前の一括 (s4b) から持ち越し。
- **`Digest::SHA256` がブラウザ版だけ**: 書いたとおり。実機・sim で SHA-256 を使いたいアプリ向けに `MbedTLS::Digest` を書くかは上と同じ。
- **`gpio.rbs` の置き場**: `tools/ti/test_sig/gpio.rbs` (型エンジンの試験の見本) と同じ名前なので、試験のときは試験用が `sig/gpio.rbs` を置き換える (Rakefile:522-529 の `cp`)。試験は通った。`sig/README.md` の「`gpio.rbs` は sig/ には無く」と、test_sig/gpio.rbs の冒頭「GPIO は Family mruby の API ではない」は古くなった (どちらも触っていない)。親が README を直す。

## 6. アプリが使っているのに実装に見つからなかったもの

この範囲では無い。次は定義が実機の gem にあり、このリポジトリのソースに無い (上流の gem): `BMI270` は `lib/gems/picoruby-bmi270` にあった。`Net::HTTP`・`URI`・`MbedTLS` は picoruby の gem (上の「迷ったもの」)。
`FmrbApp._get_last_error`・`FmrbAsterism.*`・`Asterism.profile_defaults`・`TableClient` は S5b の範囲。

## 7. 数える道具の前後と型 db の大きさ

- `rake sig:coverage` 前 → 後: sig 81 クラス・1114 メソッド → 104 クラス・1244 メソッド。アプリが使う未定義: クラス 22 → 11 (69 → 15 回)、定数 8 → 6、受け手のわかるメソッド 36 → 13 (72 → 24 回)、型の決まらない名前 67 → 60。
  実装にあって sig に無い: Ruby 395 → 325、C 48 → 15。基準との差 `app_class -11, app_constant -2, app_method -23, app_name -7, impl_ruby -70, impl_c -33`。基準は別コミットで更新。
  書き方の欠け 0 のまま (要約 111 バイト超 0・`<<en>>` 無し 0・長文なし 0・例なし 0・強調が行をまたぐ 0。`<<en>>` の無い長文 0)。
- 型 db (tidbgen を前の版 (`HEAD~2` の sig) と今の sig で別々に走らせた。どちらも `tools/ti/test_sig` を重ねた):

| 領域 | 前 | 後 | 差 | 上限 |
|---|---|---|---|---|
| 名前 | 11,586 | 12,883 | +1,297 | 65,535 (20%) |
| シグネチャ | 43,858 | 48,317 | +4,459 | 65,535 (74%) |
| 要約 (文書) | 82,199 | 91,270 | +9,071 | 24 ビット |
| メソッドの数 | 3,555 | 4,277 | +722 | |
| 引数の数 | 3,046 | 3,636 | +590 | |
| クラスの数 | 83 | 104 | +21 | |
| 定数の数 | 221 | 245 | +24 | |

  シグネチャの領域が 67% → 74%。**S5b (Asterism・ROS・卓のゲームの部品、約 200 メソッド) を足すと 80% を超える見込み** (1 メソッドあたり約 35 バイト → +7,000 ほどで 55,000 台)。次の段の前に領域の手当て (plan 2.4) を決める必要がある。
  メソッドの数が +722 になるのは、クラスごとに継承した Object のメソッドが数に入るため (新しいクラス 21 × 約 30)。

## 8. 確認の結果

- `rake ti:test`: 通った (期待値の直しは不要。`sig/gpio.rbs` は試験用に置き換えられる)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass)。
- `rake sig:coverage`: 上のとおり。ビルドと sim は流していない。`ruby tools/boundary/gen.rb` は流していない (境界の印を付けたクラスは足していない)。

## 9. 親が sim で確かめるとよい項目 (sim の irb に打つ式 → 期待)

1. `JSON.parse('[1,2] x')` → `[1, 2]`
2. `JSON.parse('')` → JSON::ParserError
3. `JSON.parse('{"a" 1}')` → JSON::JSONError (ParserError でない)
4. `JSON.generate({a: :b, 1 => nil})` → `{"a":"b","1":null}`
5. `JSON::Digger.new('{"a":[10,20]}').dig("a",1).parse` → 20
6. `JSON::Digger.new('[1]').dig(-1)` → ArgumentError
7. `MessagePack.unpack(MessagePack.pack({a: 1}))` → `{"a"=>1}`
8. `MessagePack.unpack(MessagePack.pack(1) + "x")` → RuntimeError
9. `FmrbToml.parse("a = 1\n[s]\nb = black")` → `{"a"=>1, "s"=>{"b"=>"black"}}`
10. `FmrbToml.parse("a =")`、続けて `FmrbToml.error` → nil、理由の String
11. `FmrbColors.by_name("black")`・`by_name("Black")` → 0・nil
12. `FmrbColors.to_color("0x1C")`・`to_color("zzz")` → 28・nil
13. `FmrbHw.pin_count`、`FmrbHw.pin_available?(99)` → 49 (sim)、false
14. `FmrbHw::PIN_I2C1_SDA`、`I2C`、`GPIO`、`Digest` → どれも NameError (sim)
15. `FmrbNet.connected?`、`FmrbNet.ip_address` → true、"127.0.0.1"
16. `Fmrb::Fft.new(size: 100)` → ArgumentError ("FFT size must be a power of two")
17. `Fmrb::Raycast.new.cast(1,1,0)` → RuntimeError ("set_map has not been called")

## 10. 読んでいて見つけた実装の不具合の疑い (直していない)・確かめていないこと

- **`JSON::Digger#dig` の配列の位置が範囲外でも例外が出ない**: json.rb:330-332 は `JSON::DiggerError.new("Array index out of range")` を作るだけで `raise` が無い。冒頭のコメント (136) は例外が出ると書いている。CRuby で同じファイルを動かすと `dig(3, ...)` の後の `parse` が nil になった (mruby では未確認)。上流の gem のバグと見える。
- **`JSON.parse` の `\uXXXX`**: `[code].pack('C*')` (json.rb:587) は 1 バイトに丸める。`Array#pack` は family_mruby のビルドのうちブラウザ版 (`mruby-pack`、family_mruby_wasm.rb:95) にしか入れていないように見え、ほかのビルドでは `\u` を読むと NoMethodError になる可能性がある。**実機・sim では確かめていない** (sig には「当てにならない」とだけ書いた)。
- **`GPIO.new` はピンの取得 (acquire) の失敗を見ない**: gpio-proxy/gpio.c:11-14 は `fmrb_hal_gpio_reset` の戻り値を捨て、hw_proxy_gpio.c の config・read・write は台帳の持ち主を確かめない。システム専用のピンに書けてしまうように読める。実機では確かめていない。
- **`GPIO#read` の失敗 -1 を `high?` が true と答える**: gpio.c:164-172。`GPIO.read_at` も同じ。
- **`I2C.new` の失敗の文が 1 種類**: i2c-proxy/i2c.c:18 が全部を `I2C_ERROR_INVALID_UNIT` にし、利用者には "Invalid I2C unit" と出る (別アプリが持っている・ピン固定の場合も)。
- **`JSON.generate` の NaN・無限大**: `to_s` のまま JSON として正しくない字が入る (json.rb:433-435)。
- **`gpio_viewer.app.rb` の `PIN_COUNT = 49` の直書き**: P4 の機種は `FmrbHw.pin_count` が 55。
- 確かめていない: `FmrbHw::PIN_I2C*` がブラウザ版・ネイティブ版で本当に未定義か (const.c の `#ifndef CONFIG_IDF_TARGET_LINUX` と hosted.cmake:521 からの読み)。`FmrbNet.ip_address` の "127.0.0.1" の実際の出力。`Fmrb::Fft`・`Fmrb::Raycast` の `:spinel` が入るビルド (`available?` で調べるよう書いた)。`FmrbColors` は Spinel の側に別の写しがあり (fmrb_app_base_spinel.rb)、そちらは読んでいない。JSON の例外の型・メッセージは CRuby で同じ Ruby を動かして確かめた分を含む (mruby ではない)。
- 迷った所: `FmrbHw::PIN_I2C1_SDA` の値を機種ごとに長文へ書いた (fmrb_pin_assign.h の読み)。機種が増えると古くなる。

## 11. コミット

- `a854f80a` feat(sig): JSON, MessagePack, TOML, colours, SHA-256, FmrbHw, FmrbNet, I2C and GPIO signatures
- 次のコミット: Fmrb::Fft, Fmrb::Raycast, FmrbReceiveConf and Unit8Encoder signatures
- `test(sig_coverage)`: 基準の更新
- `docs(sig_coverage)`: この報告
