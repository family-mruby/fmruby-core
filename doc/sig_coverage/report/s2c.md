# S2c File・Dir・Time・IO と、Class の残りを sig に書く

> 2026-11-25 | サブ (Sonnet) の報告。指示書は instruction_s2c.md。ブランチ `sig-s2c`。

## 1. 書いたもの

| ファイル | 内容 |
|---|---|
| `sig/fmrb_file.rbs` | `class File < IO` を足した (クラスメソッド 12・インスタンス 3・定数 `SEPARATOR`)。既にある `module FmrbFile` は触っていない |
| `sig/dir.rbs` (新) | `Dir` の 24 メソッド |
| `sig/time.rbs` (新) | `Time` の 52 メソッド |
| `sig/io.rbs` (新) | `IO` の 7 メソッド |
| `sig/class.rbs` | `Module#const_defined?`・`Module#remove_method`、`Object.const_defined?` (写し) |

合計 101 メソッド (sig の数 914 -> 1015)。File 15 (`new`・`join`・`basename`・`dirname`・`extname`・`expand_path`・`exists?`・`file?`・
`directory?`・`size`・`delete`・`unlink`・`rename`・`path`・`path=`)、Dir 24、Time 52、IO 7 (`read`・`write`・`puts`・`print`・`flush`・`close`・`closed?`)、
Class・Module 3。

内訳 (概数。数え方は s2b と同じ「例だけ = 素直、散文あり = つまずく」):

- 素直 約 45: `Time` の取り出し (`year`・`hour` ほか) と別名、`Dir#rewind`・`pos`・`pos=`、`File.extname`・`Dir.pwd`・`IO#closed?` など。
- つまずく 約 56: 失敗のしかたが CRuby と違う (RuntimeError・nil でなく ""・複数パスを受けない)、機種で違う (`Dir#tell`・`Dir.entries`)、
  mode の読み方、`Time` の作り方と書き換え (`utc`・`localtime`)。

## 2. 根拠の表 (読んだ場所)

`lib/gems/` の 2 つの gem は `components/picoruby-esp32/picoruby/mrbgems/` にある写しと diff で同一を確認した (正は `lib/` 側)。
`$M` = `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby`。

どの実装がビルドに入るか:

- `File`・`IO`: gembox `lib/upstream/family_mruby.gembox:14-15` (`picoruby-fmrb-io`・`picoruby-fmrb-filesystem`)。全ビルド。mruby 本体の `mruby-io` は入らない
  (`lib/gems/picoruby-fmrb-io/mrbgem.rake:7` が conflict を宣言。build config にも `mruby-io` は無い)。C は `ports/esp32/` だが、
  `components/picoruby-esp32/CMakeLists.txt:67-68` が全ビルド (ESP32・sim・ネイティブ・ブラウザ) でこれを使う。下の `fmrb_hal_file_*` が機種で入れ替わる
  (ESP32 は `components/fmrb_hal/platform/esp32/fmrb_hal_file_esp32.c`、それ以外は `platform/posix/fmrb_hal_file_posix.c`)。
- `Dir`: gem `mruby-dir` (`$M/mrbgems/mruby-dir/`)。build config に全ビルド (`family_mruby_esp32.rb:96`・`esp32p4.rb:108`・`linux.rb:94`・`native.rb:89`・`wasm.rb:74`)。
  HAL は 2 通り。**ESP32 とネイティブ版は `lib/upstream/hal-esp32-dir/dir_hal.c`** (ファイル HAL の経路解決を使う。ESP32 は `components/picoruby-esp32/CMakeLists.txt:163`、ネイティブ版は
  `native/CMakeLists.txt:173` と `family_mruby_native.rb:86` の `hal-dir-fmrb`)。**Linux sim とブラウザ版は
  `lib/upstream/patch/picoruby-mruby/lib/mruby/mrbgems/mruby-dir/ports/posix/dir_hal.c`** (`conf.ports :posix`。全経路に `flash/` を前置)。
- `Time`: gem `mruby-time` (`$M/mrbgems/mruby-time/`)。`family_mruby_esp32.rb:90`・`esp32p4.rb:102`・`linux.rb:100`・`native.rb:95`・`wasm.rb:80`。
- `Module#const_defined?`: mruby 本体 `$M/src/class.c`。`remove_method`: gem `mruby-metaprog` (`family_mruby_esp32.rb:92` ほか)。
- 既にビルドした木で見た印: `components/picoruby-esp32/picoruby/build/family-mruby-linux/mrbgems/` に `mruby-dir`・`mruby-time`・`picoruby-fmrb-io`・
  `picoruby-fmrb-filesystem` があり、`mruby-io`・`mruby-errno` は無い。

| 書いたもの | 読んだ場所 (パス:行) |
|---|---|
| `File.new` (mode・RuntimeError) | `lib/gems/picoruby-fmrb-filesystem/mrblib/file.rb:18-27`、`picoruby-fmrb-io/ports/esp32/fmrb_io.c:30-53` (mode の読み。`+` は 2 文字目、その他の頭文字は `FMRB_O_RDONLY`)、`:66-82` (`IO.sysopen`。失敗は `mrb_raisef(E_RUNTIME_ERROR, "failed to open file: %s")` の 79 行) |
| 開ける数 10・全アプリ共通 | `fmrb_hal_file_esp32.c:35` (`MAX_OPEN_FILES 10`)・`:65` (静的な表)・`:655-660` (空きが無いと `FMRB_ERR_BUSY`) |
| `a+` が `r+` になる | `fmrb_hal_file_esp32.c:219-247` (`flags_to_mode`。RDWR で CREAT のみ・TRUNC 無しは `"r+"`)。`io_mode_to_flags` が `a+` を `RDWR|CREAT|APPEND` にする (`fmrb_io.c:45-48`)。posix 側も同じ (`fmrb_hal_file_posix.c:743-765`) |
| `File.open` (ブロックで閉じる) | `file.rb:30-41` |
| `File.join`・`basename`・`dirname`・`extname`・`expand_path` | `file.rb:44-89`・`:161-186` (suffix は `end_with?` の一致だけ)・`:189-216`・`:219-224`・`:92-158` (`ENV["PWD"]`・`ENV["HOME"]`、ArgumentError は 101・109 行) |
| `File.exist?`・`file?`・`directory?` | `ports/esp32/fmrb_filesystem.c:31-80` (`stat` の失敗は false。例外なし) |
| `File.size` の失敗 | `fmrb_filesystem.c:88-100` (96 行 `mrb_raisef(E_RUNTIME_ERROR, "stat failed for %s")`) |
| `File.delete`・`unlink`、戻り値 1・パス 1 つ | `fmrb_filesystem.c:109-120` (116 行に例外、`MRB_ARGS_REQ(1)` は 236-237 行)。LittleFS の `unlink` はディレクトリを `EISDIR`、開いたファイルを `EBUSY` で断る: `components/esp_littlefs/src/esp_littlefs.c:1986-2000` |
| `File.rename`、戻り値 0 | `fmrb_filesystem.c:128-139` (135 行に例外) |
| `File#read` が "" を返す・全部読む・先に確保 | `fmrb_filesystem.c:148-202` (165-181 行 length 省略=残り全部、191 行 `mrb_str_new_capa(length)`、199 行 `mrb_str_resize` で 0 バイトなら "") |
| `IO#read` (既定 1024、閉じていると RuntimeError) | `fmrb_io.c:142-178` (146 行 `length = 1024`、154・170 行) |
| `IO#write`・`flush`・`close`・`closed?` | `fmrb_io.c:187-213` (引数は `"S"`、209 行 "write failed")・`:222-237` (232 行 "flush failed"、標準ストリームは INVALID_PARAM を無視)・`:246-259` (二重 close は何もしない)・`:267-278` |
| `IO#puts`・`print` (配列を展開しない) | `lib/gems/picoruby-fmrb-io/mrblib/io.rb:11-30` (`arg.to_s`) |
| 閉じ忘れは GC で閉じない | `fmrb_io.c:25-27` (`mrb_data_type` の解放は `mrb_free` だけ)。アプリ終了時に閉じる: `main/app/fmrb_app.c:1538-1547` と `fmrb_hal_file_esp32.c:1385-1405` |
| `Dir` の失敗 (RuntimeError・メッセージはパス) | `$M/mrbgems/mruby-dir/src/dir.c:95-97` (`mrb_sys_fail(mrb, path)`)・`$M/src/error.c:593-608` (`SystemCallError` が無いと `E_RUNTIME_ERROR`)。`SystemCallError` を定義する `mruby-errno` が build config に無い |
| 閉じた Dir は IOError | `dir.c:58-60`・`:300-302`ほか、`:489` (`IOError` を定義) |
| `Dir.new`・`exist?`・`mkdir`・`delete`・`getwd`・`empty?`・`entries`・`children`・`read`・`rewind`・`seek`・`tell` | `dir.c:76-100`・`:132-144`・`:185-197`・`:111-121`・`:155-171`・`:258-278`・`:409-461`・`:292-310`・`:323-335`・`:350-368`・`:382-400`。登録は 470-487 行 |
| `Dir.open`・`foreach`・`chdir`・`each`・`each_child`・`pos`・エイリアス | `$M/mrbgems/mruby-dir/mrblib/dir.rb:15-123` (`to_enum` はブロック無しで NotImplementedError: `$M/mrblib/kernel.rb:42-44`) |
| ESP32 の `Dir#tell`・`seek` が NotImplementedError | `lib/upstream/hal-esp32-dir/dir_hal.c:200-216` (ENOSYS)、`dir.c:363-366`・`:394-397` (ENOSYS で NotImplementedError) |
| ESP32 の LittleFS は "." ".." を出さない | `components/esp_littlefs/src/esp_littlefs.c:2151` (読み飛ばす)。sim の posix は `readdir` そのまま (`ports/posix/dir_hal.c:77-83`) |
| `Dir.exist?` で "/"・"/mnt" が true | `dir_hal.c:308-336` (312-315 行、`fmrb_hal_file_virtual_children`) と `fmrb_hal_file_esp32.c:148-158` |
| 相対パスは cwd でなくルートから | `fmrb_hal_file_esp32.c:166-200` (`resolve_path` が `LITTLEFS_PATH + "/" + path`。cwd を見ない)、`dir_hal.c:98` |
| `Dir.mkdir` が既存・親なしで失敗 | `dir_hal.c:219-247` (`mkdir(2)` を直接呼ぶ)・`dir.c:193-196`。sim: `ports/posix/dir_hal.c:129-136` |
| /tmp はファイルだけ (サブディレクトリなし) | `fmrb_tmpfs_esp32.c` 冒頭のコメント (12-17 行)、`fmrb_hal_file_posix.c:1030-1036`・`:1466-1471`。容量 Retro 448 KB・Modern 8064 KB: `components/fmrb_common/include/fmrb_mem_config.h:138-141` |
| `Time.now`・`new`・`at`・`local`・`gm` | `$M/mrbgems/mruby-time/src/time.c:598-601` (`timespec_get`)・`:1216-1248`・`:634-643`・`:731-766`・`:645-728` (ArgumentError は 660・677・710 行)・登録 1722-1730 |
| `+`・`-`・`<=>`・`==` | `time.c:866-897` (TypeError は 372 行 `mrb_to_time_t`)・`:913-957` (Time を引くと Float: 921-926 行)・`:824-848`・`:799-807` |
| `zone`・`to_s`・`asctime`・`utc_offset` | `time.c:1049-1060` (UTC は "UTC"、ローカルは `%z`)・`:1471-1500`・`:1072-1100`・`:1649-1663` |
| `utc`・`localtime` が受け手を書き換える、`getutc`・`getlocal` は新しい Time | `time.c:1433-1442`・`:1285-1292`・`:1147-1160`・`:1171-1184` |
| `strftime` などが無い | `time.c:1664-1712` の `time_rom_entries` と登録 1722-1730 に無い。`mruby-time-ext` は build config に無い |
| 時差は system_conf の `timezone` | `main/kernel/fmrb_kernel.c:583-591` (`setenv("TZ")`、無ければ UTC)。出荷の値 `config/system_conf_{linux,native,wasm,naryav3,tab5}.toml` の `timezone = "JST-9"` |
| 時計の元 (RTC・ネットワーク・sim はホスト) | `main/kernel/fmrb_rtc.c:61-84`、`main/kernel/fmrb_kernel.c:1248-1252` |
| `const_defined?` | `$M/src/class.c:3827-3839` (`"n|b"`、未定義は false、名前の形が悪いと NameError は 3813-3820 行)、登録 4508 |
| `remove_method` | `$M/mrbgems/mruby-metaprog/src/metaprog.c:619-633` (戻り値 mod、凍結で FrozenError)、`$M/src/class.c:3754-3780` (無いと NameError 3779 行、継承分は対象外)、`mrb_obj_to_sym` は `$M/src/etc.c:90-95` (Symbol か String、他は TypeError) |

## 3. 書かなかったもの・判断が要るもの・見つからなかったもの

書かなかった (実装はある):

- `IO.new`・`IO.open`・`IO.sysopen`: 番号 (ファイルのハンドルを整数にしたもの) を直接扱う内側 (`fmrb_io.c:93-133`・`:66-82`)。`IO.new(fd)` は 1・2 以外の整数を
  ハンドルとして信じる。`File.new`・`File.open` が使う。判断: 書かない方針とした (io.rbs の冒頭に理由を書いた)。
- `Dir.chroot`: ESP32 は ENOSYS、sim の posix 版は `chroot(2)` を呼ぶ (`ports/posix/dir_hal.c:175-189`)。アプリが使う理由がない。
- `File.path` (クラスメソッド) とその挙動: **不具合の疑い** (6 章)。書かなかった。
- `File::Constants` の定数 (`RDONLY` ほか約 40)。`File::SEPARATOR` だけ書いた。
- `STDIN`・`STDOUT`・`STDERR` と `$stdout` (トップレベル定数・グローバル変数)。sig で書く形が決まっていないので書かなかった。
- `Time#hash` は書いたが、`Time#initialize_copy` は private で書かない。

**既にある sig と実装が食い違っているもの (既存の宣言は変えない決まりなので、直していない。判断を親に)**: `sig/fmrb_misc.rbs` の `class File`:

- `def gets: () -> String?` (`fmrb_misc.rbs` の File) の `gets` は**実装に無い** (ビルドの `File`・`IO` に `gets` は無い。呼ぶと NoMethodError)。
  長文の「行で処理するなら `gets` を回す」も誤り。
- `File.open: (String path, String mode) -> File` は mode が必須の形で、実装は省略可 (`file.rb:30`)。ブロックを渡す形も無い。
- `def read: () -> String` は length を取らず、長文の「全部が文字列で返る」は合うが、終わりでは "" (nil でない)。
- これらは本報告の長文 (`File.new`・`IO#read` ほか) に正しい形を書いたが、`File.open` と `File#read` の宣言は fmrb_misc.rbs にあるため二重にしない (重複宣言になる)
  ように、`File.open`・`File#read` は足していない。

アプリが使っているのに実装に見つからなかったもの:

- `File.binwrite`・`File.write` (`flash/app/game/breakout/generate_bgm.rb:102`・`rpg_demo/generate_bgm.rb:145`・`generate_world.rb:176`): PC の CRuby で流す生成スクリプトで、
  端末のアプリではない (s2b と同じ)。端末で呼ぶと NoMethodError。
- `File.binread`: コメントだけ (`flash/app/demo/picoruby.app.rb:607`・`lib/gems/picoruby-fmrb-midi/mrblib/fmrb-smf.rb:138` が「無い」と書いている)。実装にも無い。
- 端末のアプリで、実装に無い `File`・`Dir`・`IO` のメソッドを呼んでいるものは見つからなかった (`gets`・`each_line`・`seek` は File に対して呼ばれていない)。
- `Digest.const_defined?`・`MbedTLS.const_defined?`: 受け手の `Digest`・`MbedTLS` が sig に無いので数える道具の「無い」に残る。範囲外 (別の段)。

判断が要るもの:

- **`Object.const_defined?` の写し**: 数える道具は「クラスを受け手に呼ぶ」ものを `Module` のインスタンスメソッドへたどらないので、`sig/class.rbs` に `class Object` で
  `def self.const_defined?` を 1 つ足した (Object は Module の一種なので事実としては合う)。Class・Module の宣言を変えない決まりのため、`class Class < Module` にはしていない。
  不要なら消してよい (消すと `Object.const_defined?` 3 件が「無い」に戻る)。
- `class File < IO` は既存の `class File` (継承なし、fmrb_misc.rbs) に継承を足す形。`rake ti:test` は通った。型エンジンが 2 つの宣言を合わせて読むかは試験が通っただけで、
  エディタでは見ていない。
- `File.delete` が複数パスを受けない (CRuby と違う) のは実装 (`MRB_ARGS_REQ(1)`) のとおり書いた。

## 4. 数える道具の前後と型 db の大きさ

- `rake sig:coverage` 前 -> 後: sig の 73 クラス・914 メソッド -> 77 クラス・1015 メソッド (+101)。アプリが使う未定義: クラス 26 (92 回) -> 24 (71 回) (`Dir`・`Time`)、
  既知の受け手のメソッド 51 (118 回) -> 43 (90 回)、型の決まらない名前 89 (258 回) -> 87 (254 回)、受け手なし 8 (26 回) -> 8 (26 回)。
  実装にあって sig に無い: Ruby 635 -> 612、C 83 -> 53 (クラス 13 -> 11)。基準との差 `app_class -2, app_method -8, app_name -2, impl_ruby -23, impl_c -30`。
  `rake sig:coverage:update` で基準を更新 (別コミット)。残る `File`・`IO`・`Dir` の「無い」: `File.path` (クラスメソッド)・`IO.new`・`IO.open`・`IO.sysopen`・`Dir.chroot` (3 章)。
- 型 db (tidbgen を前の版 `127ff2c2` と今の版で別々に走らせ、`ti_builtin_*` の定数を読んだ。試験用の `tools/ti/test_sig` を重ねた):

| 領域 | 前 | 後 | 差 | 上限 |
|---|---|---|---|---|
| 名前 | 9,779 | 10,257 | +478 | 65,535 |
| シグネチャ | 35,734 | 38,769 | +3,035 | 65,535 |
| 要約 (文書) | 69,262 | 75,101 | +5,839 | 24 ビット |
| メソッドの数 | 3,023 | 3,316 | +293 | |
| 引数の数 | 2,451 | 2,776 | +325 | |
| クラスの数 | 75 | 79 | +4 | |
| 定数の数 | 194 | 195 | +1 | |

  上限に対して シグネチャ 59%、名前 16%。

## 5. 確認の結果

- `rake ti:test`: 通った (期待値の直しは不要)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass)。`sdk/` の変更は無い。
- `rake sig:coverage`: 通った。要約 111 バイト超 0・`<<en>>` 無し 0。基準更新後の差は無し。
- `rake docs:index`: 流した。

## 6. 読んでいて見つけた実装の不具合の疑い (直していない)

- **`File.path` (クラスメソッド) が常に nil を返すように読める**: `lib/gems/picoruby-fmrb-filesystem/mrblib/file.rb:227-238` は `self.instance_of?(self.class)` で分岐するが、
  `File` 自身に対しては `File.instance_of?(Class)` が true で `@path` (クラスの ivar、nil) を返す。`File.path("x")` が "x" にならない。動かして確かめていない。
- **`File.expand_path` の相対パスの基準が `Dir.getwd` でなく `ENV["PWD"] || "/"`** (`file.rb:112`)。端末で `ENV["PWD"]` が入るかは見ていない。
- **`IO#puts` が配列を展開しない** (`picoruby-fmrb-io/mrblib/io.rb:15-19`)。CRuby と違う (仕様かもしれない)。
- **ESP32 のファイル HAL は経路を 128 バイトの表に入れる** (`fmrb_hal_file_esp32.c:37`、`build_path` の `snprintf` の戻りを見ない)。`/flash` を前に付けた長い経路は
  黙って切られる読み。実機で確かめていない。
- `Dir.chdir` をしても、ESP32 のファイル操作の相対パスの基準は変わらない (`resolve_path` が cwd を見ない)。sim の posix 版は `chdir` が本当に cwd を変えるうえ、
  相対パスに `flash/` を前置するので、**sim と実機で `Dir.chdir` 後の相対パスの意味が食い違う**可能性がある。
- 既存 sig の `File#gets` (3 章)。

## 7. 確かめていないこと

- **ビルドも sim も動かしていない**。エディタの補完・F1・Ctrl+T・Ctrl+E に実際に出るかは見ていない (型エンジンの試験が通っただけ)。
- 全部コードの読み。特に次は読みだけ。
  - `a+` が追記でなく `r+` になること、`"x"` など未知の mode が読み取り専用になること。
  - 書き込みのモードで `read` したとき ("" になる、と読んだ) と、読み取りのモードで `write` したとき ("write failed" の読み)。
  - ESP32 の LittleFS が "." ".." を出さないこと (`esp_littlefs.c:2151` の読み。ビルドに入る版かは、`components/esp_littlefs` とパッチを読んだだけ)。
  - `File.rename` で別のファイルシステム (/home と /tmp) をまたぐ場合、先に同名があるときの動き。
  - ESP32 で `Dir#tell`・`seek` が NotImplementedError になること、sim で値が返ること。
  - `Time.now` の時計が合っていない端末の値 (1970 年付近、と書いたが実機で見ていない)。ネットワーク時計の合わせ方は読んでいない。
  - `Time#zone` のローカル時刻の文字列 ("+0900") は `strftime("%z")` の読み。sim の glibc で `TZ=JST-9` のときの値は見ていない。
  - ブラウザ版・ネイティブ版の `Dir`・`File` の細部 (ネイティブ版は `/mnt/root` などで読み取り専用の失敗があり、`File.delete` の失敗理由が増える。HAL の `FMRB_ERR_READ_ONLY` は読んだが、Ruby からは RuntimeError になる読み)。
  - SD (`/mnt/sd`)・/tmp の容量上限に当たったときの `write` の例外 (`tmp_smoke.app.rb:99-108` が RuntimeError 系を想定しているが動かしていない)。

親が sim のシェルで確かめるとよい項目 (打つ式 -> sig に書いた期待):

| 打つ式 | 期待 |
|---|---|
| `File.exist?("/nope")` | `false` |
| `File.size("/nope")` | RuntimeError "stat failed for /nope" |
| `File.open("/nope")` | RuntimeError "failed to open file: /nope" |
| `File.open("/tmp/s2c","w"){\|f\| f.write("ab")}` | `2` (ブロックの値) |
| `File.open("/tmp/s2c"){\|f\| [f.read, f.read]}` | `["ab", ""]` (終わりは nil でない) |
| `File.delete("/tmp/s2c")` | `1` (2 回目は RuntimeError) |
| `Dir.exist?("/nope")` / `Dir.exist?("/tmp")` | `false` / `true` |
| `Dir.children("/nope")` | RuntimeError (メッセージ "/nope") |
| `Dir.mkdir("/tmp/d")` | RuntimeError (/tmp にディレクトリは作れない) |
| `Dir.open("/home"){\|d\| d.read}` | 名前か nil (sim は "." が出ることがある) |
| `Time.at(0).utc.to_s` | `"1970-01-01 00:00:00 UTC"` |
| `Time.local(2026,11,25,9,30).to_s` | `"2026-11-25 09:30:00 +0900"` (JST-9 のとき) |
| `Time.now + Time.now` | TypeError "cannot convert Time to time" |
| `$stdout.puts([1,2])` | ログに `[1, 2]` が 1 行 |
| `Object.const_defined?(:Nope)` | `false` |
| `File.path("x")` | 疑い: nil (6 章) |

## 8. 迷った所・型紙で足りなかった所

- 型紙の「失敗のしかた」は、`Dir` のように**例外は 1 種類 (RuntimeError) で、理由はメッセージのパスだけ**という API に合わせにくい。各メソッドに「RuntimeError (メッセージはパス)」と
  繰り返したが、クラスの長文に 1 回書く方がよい (dir.rbs 冒頭に書いたうえで、メソッドにも書いた)。
- 機種で挙動が分かれるもの (`Dir#tell`・`Dir.entries` の "."・`/tmp`) は、ESP32 と sim の両方を実装から読んで 1 文に詰めた。
- 別名 (`unlink`・`rmdir`・`mktime`・`utc` ほか) は、数える道具の数に入るので、短い例だけの宣言を別に書いた。
- `File.open`・`File#read` を足さなかった (3 章) ため、F1 の長文は `File.new` と `IO#read` に分かれて出る。
