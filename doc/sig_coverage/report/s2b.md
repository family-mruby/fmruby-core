# S2b Machine・GC・乱数と、上流由来のクラスの足りないメソッドを sig に書く

> 2026-11-24 | サブ (Sonnet) の報告。指示書は instruction_s2b.md。ブランチ `sig-s2b`。

## 1. 書いたもの

| ファイル | 内容 |
|---|---|
| `sig/machine.rbs` (新) | `Machine` の 24 メソッド |
| `sig/gc.rbs` (新) | `GC` の 20 メソッド |
| `sig/rng.rbs` (新) | `RNG` の 3 メソッド (指示書の触ってよい一覧に無いファイル名。判断が要る、3 章) |
| `sig/kernel.rbs` | `Kernel#rand` |
| `sig/object.rbs` | `Object#equal?`・`#freeze`・`#respond_to?` |
| `sig/float.rbs`・`sig/integer.rbs` | `Float#round`・`Integer#round` |
| `sig/string.rbs` | `String#rindex` |
| `sig/array.rbs` | `Array#select` |
| `sig/hash.rbs` | `Hash#key?`・`Hash#each_key` |

合計 57 メソッド (Machine 24 + GC 20 + RNG 3 + `Kernel#rand` 1 + 上流由来 9)。sig の数 857 -> 914 (+57) と合う。

素直 / つまずくの内訳 (素直 = 要約と例だけ、散文を書かなかったもの):

- 素直 9: `Machine.uptime_formatted`、`GC.interval_ratio`・`GC.step_ratio`・`GC.step_limit`・`GC.malloc_threshold`・`GC.generational_mode`・
  `GC.scheduler_driven`・`GC.debt_limit` の読み出し側、`Hash#key?`。
- つまずく 48 (型紙の順に書いた)。理由は次のどれか。単位や起算点が機種で違う (`board_millis`・`uptime_us`)。機種で振る舞いが違う・無い
  (`delay_ms`・`busy_wait_ms`・`sleep`・`unique_id`・`set_hwclock`・`get_hwclock`・`exit`・`reboot`・`debug_puts`・`posix?`・`tud_*`・`stack_usage`・
  `deep_sleep`・`bootsel_pressed?`・`wifi_available?`)。失敗が名前から分からない (例外の名前・負の値・nil)。計測用のビルドにだけある
  (`GC.reset_stat`・`GC.stat` のキー)。似た名前と違う (`rand` が 0...1 の Float でない、`equal?` と `==`、`delay_ms` と `sleep_ms`、
  `Integer#round` が自分自身を返す、`rindex` の位置が文字数)。ブロックが要る (`select`・`each_key`)。

## 2. 根拠の表 (読んだ場所)

`components/picoruby-esp32/picoruby/` の下は submodule で、lib/ から写したものが入っている (Machine の C・mrblib は
`lib/upstream/replace/picoruby-machine/` と、gc.c は `lib/upstream/patch/picoruby-mruby/lib/mruby/src/gc.c` と diff で同一を確認)。
mruby 本体は `$M` = `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby`。

| 仲間 | 場所 | 入っているビルド |
|---|---|---|
| Machine の C (`delay_ms`・`busy_wait_ms`・`sleep`・`deep_sleep`・`unique_id`・`read_memory`・`stack_usage`・`set/get_hwclock`・`uptime_us`・`board_millis`・`uptime_formatted`・`exit`・`check_signal`・`poll_signal`・`debug_puts`・`tud_*`・`bootsel_pressed?`) | `lib/upstream/replace/picoruby-machine/src/mruby/machine.c:18-538` (登録は 485-532)。`board_millis` の checkpoint のずれは 186-202、`uptime_formatted` は `ports/common/machine.c:5-22` | gembox `lib/upstream/family_mruby.gembox:16` (`picoruby-machine`)。全ビルド |
| Machine の機種差 (ESP32) | `ports/esp32/machine.c`: `delay_ms` 154-158 (`vTaskDelay(ms / ESP32_MSEC_PER_TICK)`、tick = 10 は 22 行)、`busy_wait_ms` 163-167、`sleep` 147-152、`unique_id` 175-182 (MAC)、`stack_usage` 184-189 (0)、`set/get_hwclock` 191-203、`exit` 205-209 (何もしない)、`uptime_us` 211-215、`tud_*` 119-130 | ESP32 実機 (S3・P4)。`CONFIG_FREERTOS_HZ=1000` は `config/sdkconfig.defaults.naryav3`・`.tab5`・`.naryav4` ほか |
| Machine の機種差 (POSIX) | `ports/posix/machine.c`: `delay_ms` 223-266 (10 ms 刻みの実時間待ち)、`busy_wait_ms` 268-274、`sleep` (空)、`unique_id` (`/etc/machine-id`)、`exit`・`reboot`、`uptime_us` 354-376 (`CLOCK_MONOTONIC`、emscripten だけプログラム開始から)、`tud_*` | Linux sim・ネイティブ版・ブラウザ版。`PICORB_PLATFORM_POSIX` は `lib/upstream/family_mruby_{linux,native,wasm}.rb` |
| Machine の Ruby (`posix?`・`reboot`・`signal_self_manage`・`pop_signal_self_manage`・`wifi_available?`) | `lib/upstream/replace/picoruby-machine/mrblib/machine.rb:1-36`。`RUBY_PLATFORM` は `picoruby-require/ports/esp32/platform.c` ("ESP32") と `ports/posix/platform.c` ("machine-sysname") | 同上。Watchdog の gem は `family_mruby_esp32.rb:142`・`esp32p4.rb:153` でコメントアウト |
| 整数の幅 | `MRB_INT64` が 4 つの build config 全部に (`family_mruby_{esp32:18,esp32p4:27,linux:12,wasm:36}.rb`) | |
| GC (`start`・`enable`・`disable`・`stat`・`*_ratio`・`step_limit`・`malloc_threshold`・`generational_mode`・`reset_stat`) | `lib/upstream/patch/picoruby-mruby/lib/mruby/src/gc.c`: 1847 (`start`)、1867・1889 (`enable`/`disable`)、1907-2039 (比・上限・閾値)、2041-2056・2170-2193 (世代別)、2258-2319 (`stat` のキー)、2321-2345 (`reset_stat`、`MRB_GC_PROFILE` のときだけ)、2360-2378 (登録) | 全ビルド。`MRB_GC_PROFILE`・`MRB_GC_STATS` は `FMRB_GC_PROFILE=1` の計測用ビルドだけ (`family_mruby_{esp32:52,esp32p4:61,linux:47}.rb`) |
| GC (`scheduler_driven`・`debt_limit`・`step`) | `lib/upstream/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-task/src/gc.c:19-122`。アプリ側の呼び出しは `lib/gems/picoruby-fmrb-app/mrblib/fmrb-app.rb:1299-1310` (`idle_gc=`) | `picoruby-mruby` の mrbgem.rake が `mruby-task` を依存に持つ |
| RNG (`random_int`・`random_string`) | `components/picoruby-esp32/picoruby/mrbgems/picoruby-rng/src/mruby/rng.c:4-24` (1 バイトを 4 回)、`ports/esp32/rng.c` (`esp_random`)、`ports/posix/rng.c` (`/dev/urandom` を 1 バイトごとに開く) | gembox `family_mruby.gembox:21` |
| `RNG.uuid`・`Kernel#rand` | `picoruby-rng/mrblib/rng.rb`・`mrblib/kernel.rb` (`rand(max = 0)`: 0 なら `random_int`、でなければ `random_int % max`) | 同上 |
| `Float#round`・`Integer#round` | `$M/src/numeric.c:948-994` (`flo_round`、半分は 0 から遠い方)、769-776 (`mrb_check_num_exact` が nan・無限大を FloatDomainError)、1559-1581・1676-1723 (`prepare_int_rounding`・`int_round`)、登録 2339・2373 | mruby 本体。`MRB_NO_FLOAT` は定義されていない |
| `String#rindex` | `$M/src/string.c:2445-2478` (`"S|i"`、文字位置)、`str_rindex` 915-945、登録 3552 | mruby 本体 (`MRB_UTF8_STRING` は `picoruby-mruby/mrbgem.rake` が定義) |
| `Array#select` | `$M/mrblib/enum.rb:131-141` (`find_all`)・322 (`alias select find_all`)。ブロック無しは `to_enum` で、`$M/mrblib/kernel.rb:42-44` が NotImplementedError | mruby 本体。Enumerator は gembox が入れない (`family_mruby.gembox:5-9`) |
| `Hash#key?` | `$M/src/hash.c:1948-1954`、登録 2362-2370 (`has_key?`・`include?`・`key?`・`member?` が同じ関数) | mruby 本体 |
| `Hash#each_key` | `$M/mrblib/hash.rb:87-92` | mruby 本体 |
| `Object#equal?` | `$M/src/class.c:3184-3189`、登録 4483 (BasicObject) | mruby 本体 |
| `Object#freeze` | `$M/src/kernel.c:356-366`、登録 786 (Kernel)。変更時の FrozenError は `$M/src/error.c:648-676` | mruby 本体 |
| `Object#respond_to?` | `$M/src/kernel.c:590-604`、登録 800。`mrb_obj_respond_to` (`$M/src/class.c:3207-3215`) はメソッドの有無だけ見て private を見ない | mruby 本体 |

上流由来のクラスは全部 mruby 本体 (`src/`・`mrblib/`) にあり、`mruby-string-ext` などの gem の定義は使っていない。

## 3. 書かなかったもの・判断が要るもの・見つからなかったもの

書かなかった (実装はある):

- `Machine._reboot`・`_stdin_getc`・`_stdin_gets`: `_` 始まりの内部用。
- `Module#const_defined?` (`$M/src/class.c:3827`、登録 4508) と `Module#remove_method` (`$M/mrbgems/mruby-metaprog/src/metaprog.c:619`、登録 718。
  `mruby-metaprog` は `family_mruby_esp32.rb:92` ほかに入っている): アプリが `Object.const_defined?(:X)` (3 回)・
  `self.class.remove_method(...)` (3 回)・`Digest.const_defined?` ほかで使うが、受け手が Class・Module なので書く場所は `sig/class.rbs` で、
  **指示書の触ってよい一覧に無い**。範囲外なので書かなかった。続きの一括で `class.rbs` に足す判断が要る
  (案: `const_defined?(Symbol name, ?bool inherit) -> bool`、`remove_method(*Symbol names) -> self`)。
- `Array#rindex`・`Hash#include?`・`Hash#member?`・`Hash#select`・`Object#frozen?` ほか: 実装はあるが同梱のアプリが呼んでいない。

アプリが使っているのに実装に見つからなかったもの:

- **`srand`・`Random.new`・`pack`**: 同梱アプリでは `flash/app/game/rpg_demo/generate_world.rb`・`generate_tiles.rb`・
  `flash/app/game/breakout/generate_bgm.rb` ほかの `generate_*.rb` だけが使う。これらは PC の CRuby で流す生成スクリプト
  (ファイル先頭に「Run: ruby ...」) で、端末のアプリではない。ビルドに `mruby-random`・`mruby-pack` は入っていない
  (`family_mruby_*.rb` の gem 一覧に無い) ので、端末で呼ぶと NoMethodError になる、と読んだ。**端末のアプリの不具合ではない**。
  `rake sig:coverage` はこれらのファイルも数えているので、数える道具の側で `generate_*.rb` を外すかは親の判断。
- `Machine.mcu_name`: 上流の `picoruby-machine/sig/machine.rbs` にだけ有り、C にも mrblib にも定義が無い。書かなかった。アプリも呼ばない。
- 見つからなかったものは、ほかに無い。

判断が要るもの:

- **`Machine.delay_ms` の待ち時間**。ESP32 の実装は `vTaskDelay(ms / 10)` (`ports/esp32/machine.c:157`、`ESP32_MSEC_PER_TICK` = 10 が 22 行) で、
  FreeRTOS の tick を 10 ms と決め打ちしている。ところが `config/sdkconfig.defaults.*` の `CONFIG_FREERTOS_HZ` は 1000 (tick = 1 ms)。
  読んだとおりなら、**実機では指定の約 1/10 しか待たない** (例: `delay_ms(700)` が 70 ms)。実機で測っていない。アプリでは
  `flash/app/test/busy_input.app.rb:36` (`delay_ms(700)` で「わざと忙しくする」) が影響を受けるはず。sig の長文には「約 1/10 しか待たない
  可能性がある」と、確かめていない言い方で書いた。実機で測って、実装を直すか sig を断定に直すかは親の判断。
- `Machine.exit`: ESP32 では何もしない・Linux では**本体のプロセスごと終わる**。sig にそう書いた。
- 新ファイル `sig/rng.rbs`: 指示書は新規ファイルとして `machine.rbs`・`gc.rbs` だけを挙げている。乱数の置き場が他に無いので作った。
- `Machine.busy_wait_ms` (ESP32): ソースのコメントは「まだ動かない」だが、コードは `ets_delay_us(1000 * ms)` を呼ぶ。コードの読みどおりに書いた。
- `Machine.sleep` の引数: 上流の sig は `Integer | Float` だが、C は `"i"` で整数 1 つ。`Integer` で書いた。

## 4. 数える道具と型 db の大きさ

- `rake sig:coverage` 前 -> 後: sig の 70 クラス・857 メソッド -> 73 クラス・914 メソッド。アプリが使う未定義: クラス 29 (261 回) -> 26 (92 回)、
  既知の受け手のメソッド 61 (287 回) -> 51 (118 回)、型の決まらない名前 96 (302 回) -> 89 (258 回)、受け手なし 9 (34 回) -> 8 (26 回)。
  実装にあって sig に無い: Ruby 641 -> 635、C 117 -> 83 (クラス 15 -> 13)。
  基準との差 `app_class -3, app_method -10, app_name -7, app_bare -1, impl_ruby -6, impl_c -34`。`rake sig:coverage:update` で基準を更新 (別コミット)。
  対象のうち「無い」で残るのは、`Object.const_defined?`・`Digest.const_defined?`・`MbedTLS.const_defined?`・`remove_method` (3 章の `class.rbs` の件)。
  `respond_to?` の 1 件は「動的な呼び出し」の欄に残る (`Object#respond_to?` は書いた)。
- 型 db (`rake ti:test` が使う sig/ と tools/ti/test_sig/ を重ねたもの。tidbgen を前の版 (`e2f12464`) と今の版で別々に走らせ、生成物の末尾の数を読んだ):

| 領域 | 前 | 後 | 差 | 上限 |
|---|---|---|---|---|
| 名前 | 9,170 | 9,779 | +609 | 65,535 |
| シグネチャ | 33,842 | 35,734 | +1,892 | 65,535 |
| 要約 (文書) | 65,137 | 69,262 | +4,125 | 24 ビット |
| メソッドの数 | 2,672 | 3,023 | +351 | |
| 引数の数 | 2,149 | 2,451 | +302 | |
| クラスの数 | 72 | 75 | +3 | |
| 定数の数 | 194 | 194 | 0 | |

  上限に対して 名前 15%、シグネチャ 55%。メソッドの数が書いた数 (57) より大きく増えるのは、`Object` に足した 3 つが Object を継承する
  各クラスに展開されて数えられるため、と推測した (tidbgen は読んでいない)。

## 5. 確認の結果

- `rake ti:test`: 通った (期待値の直しは不要)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass。`boundary:check` も中で通った。`sdk/` の変更は無い)。
- `rake sig:coverage`: 通った。要約 111 バイト超 0・`<<en>>` 無し 0 のまま。基準更新後は差なし。
- `rake docs:index`: 流した (`doc/README.md` が変わった。report と一緒にコミット)。

## 6. 確かめていないこと

- **ビルドも sim も動かしていない**。エディタの補完・F1・Ctrl+T・Ctrl+E に実際に出るかは見ていない (型エンジンの試験が通っただけ)。
- 動かして確かめた振る舞いは 1 つも無く、全部コードの読み。特に次は読みだけ。
  - `delay_ms` の ESP32 での実際の待ち時間 (3 章)。ブラウザ版の `delay_ms`・`busy_wait_ms` が posix の実装でどう動くか (emscripten の `nanosleep`) は読んでいない。
  - ブラウザ版・ネイティブ版で `Machine.unique_id` が nil になること (`/etc/machine-id` が無い場合) はコードの分岐の読み。ブラウザ版がこのファイルを持つかは見ていない。
  - `board_millis` が Linux sim で「ホストの起動から」であること (`CLOCK_MONOTONIC`)。WSL2 の値は見ていない。
  - Spinel のカーネルから見た `Machine` (Spinel の Integer は 32 ビットで、`ports/posix/machine.c:352-358` のコメントがその制約に触れている) は読んでいない。
    アプリは mruby の VM で動くので sig には書かなかった。
  - `RNG.random_string` に負の長さを渡したときは読んでいない。`RNG.uuid` の出力の形は mrblib の読み。
  - `Kernel#rand` に Float・Range・負の数を渡したときの結果は、`random_int % max` の読みだけ。長文には「整数だけ」とだけ書いた。
  - `Float#round` の例 (`3.14159.round(2)`・`2.5.round`・`1234.5.round(-2)`)、`Integer#round` の `1234.round(-2)` の値は、式を手で追っただけ。
  - `GC.stat` のキーの有無は `gc.c` の `#ifdef` の読み。ふつうのビルドで `:total` が無いことは動かしていない。
  - `GC.disable` 中の `generational_mode=` の RuntimeError、`scheduler_driven` 中に世代別を入れたときの RuntimeError は `gc.c:2041-2056` の読み。
  - `check_signal`・`poll_signal` が ESP32 でいつも nil、は「ESP32 のコードに `sigint_status` を立てる所が無い」(grep) だけの根拠。
  - `wifi_available?` が常に false、は `Network` の定義を grep で探して見つからなかっただけ。ほかの gem が定義する可能性は全部は見ていない。
  - `Machine.reboot` が ESP32 で NotImplementedError、は Watchdog の gem が build config でコメントアウトされているのを読んだだけ。
  - `Hash#each_key`・`Array#select` のブロック無しが NotImplementedError、は `kernel.rb` の `to_enum` の読み。
  - `Object#respond_to?` が `GC.respond_to?` のような Module 相手にも当たるかは、エディタで見ていない。

## 7. 迷った所・型紙で足りなかった所

- `Machine` は 24 個を全部書いた。アプリが使うのは `board_millis`・`uptime_us`・`delay_ms`・`set_hwclock`・`posix?` だけで、
  `tud_*`・`bootsel_pressed?`・`deep_sleep` など「このビルドでは空・必ず NotImplementedError」のものは書く価値が薄いが、数える道具の
  「実装にあって sig に無い」を 0 にするため短く書いた。不要なら消してよい。
- 型紙の「失敗のしかた」は機種ごとに違うものに合わない。`set_hwclock` は「ESP32 だけ・Linux は NotImplementedError (ScriptError なので
  `rescue => e` で受けられない)」と、1 つの文に機種差を詰めた。
- `Machine.get_hwclock` の戻りは `Array[Integer]`。タプルの書き方が tidbgen で読めるか確かめていないため。
- `GC.stat` の戻りは `Hash[Symbol, untyped]`。キーごとの型は sig では表せない。
- 続きに足す候補: `class.rbs` に `const_defined?`・`remove_method`、`Array#rindex`・`Hash#select` ほか。
