# T8b: ブラウザ版のアプリから送ると msgpack の integer overflow で落ちる

> 状態: 修正済・検収待ち | 更新: 2026-11-10 | 原因はブラウザ版の時計。Emscripten の pthreads 版では `CLOCK_MONOTONIC` が `performance.timeOrigin + performance.now()` (Unix 時刻のミリ秒) で、`Machine.board_millis` が起動からの時間でなく約 1.79e12 になっていた。FmrbTransfer は要求の id (`rq`) にこれを使うので、Pub/Sub の要求に 64 ビットの整数 (uint64) が乗り、32 ビット整数のカーネル (Spinel、wasm32 では `sp_int` = 32 ビット) が読む途中で `<<` が溢れて要求ごと捨てていた。picoruby-machine の POSIX の時計と Spinel の `fmrb_spx_board_millis` を、ブラウザ版だけプログラムの開始からの時間にした。`regress:transfer:quick WEB=1` に api の場面と Shell の `asend` を足して通した

T8 の残件 1 (`t8.md` 8 章・9 章)。FmrbTransfer は `t2.md`。

## 1. 症状

- ブラウザ版のアプリが FmrbTransfer で送ると (Shell の `asend`、xfer_probe の `api`)、毎秒 `E (...) spx: Failed to unpack msgpack data: integer overflow in <<` が出て、20 秒後に `the transfer app did not take it in 20 s` で終わる。ネイティブ版・sim では起きない。
- 再現: `regress:transfer:quick WEB=1` の場面に `api` を足して流すと (2 章)、修正前のビルドで `api: FAILED api: xfer_probe: api done failed: the transfer app did not take it in 20 s`、ページのログに上の行が 1 秒ごと (FmrbTransfer の再送の周期) に出た。

## 2. 経路を読む

1. FmrbTransfer#send_file は要求 `{"cmd"=>"send", "rq"=>rq, "to"=>..., "path"=>...}` を `@app.publish("transfer/ctl", req)` で出す。`rq` は `Machine.board_millis` (`new_rq`)。
2. `publish` はカーネルへの APP_CONTROL `{"cmd"=>"publish", "topic"=>..., "data"=>req}`。送るのは mruby のアプリ (Shell・xfer_probe) で、ブラウザ版の mruby は `MRB_INT64` (`lib/add/family_mruby_wasm.rb`)。pack は C の msgpack-c で、`rq` が 2^32 以上なら uint64 (`0xcf` + 8 バイト) になる。
3. カーネル (`main/prebuild_scripts/kernel/fmrb_kernel.rb` の `handle_app_control`) は Spinel で、`MessagePack.unpack` は `main/prebuild_scripts/spinel/msgpack_pure.rb`。uint64 は `read_be(bin, pos, 8)` で `v = (v << 8) | byte` を 8 回。
4. Spinel の整数 `sp_int` は `intptr_t` (`sp_types.h`) で、wasm32 では 32 ビット。`sp_int_shl` (`spinel_rt.h`) は raise の設定で、結果が収まらないと `RangeError: integer overflow in <<` を投げる。カーネルはこれを拾って `Failed to unpack msgpack data: ...` と出し、要求を捨てる。転送のアプリには届かない。
5. ネイティブ版・sim は 64 ビットなので `sp_int` も 64 ビットで、同じ値でも溢れない。ESP32 は `sp_int` が 32 ビットだが、`board_millis` は起動からのミリ秒で 2^31 よりずっと小さい (2 章の 6)。

ここまでで「`rq` が 32 ビットに収まらない」ことは言えるが、なぜブラウザ版だけ大きいかが残る。

6. `Machine.board_millis` (picoruby-machine、`src/mruby/machine.c`) は `Machine_uptime_us() / 1000` (+ checkpoint の差分)。ブラウザ版は `PICORB_PLATFORM_POSIX` で、`ports/posix/machine.c` の `Machine_uptime_us` は `clock_gettime(CLOCK_MONOTONIC)` をそのまま返す。
7. 出来上がった `core_web.js` の時計は `_emscripten_get_now=()=>performance.timeOrigin+performance.now();`。pthreads の版では worker ごとに `performance.now()` の原点が違うので、Emscripten は `timeOrigin` を足して全スレッドで揃えている。その結果 `CLOCK_MONOTONIC` は Unix 時刻 (ミリ秒で約 1.79e12) になる。

## 3. 証拠

- ページのログの時刻 (hosted の `esp_log_timestamp`、同じ `CLOCK_MONOTONIC` を `uint32_t` に切ったもの) が `E (3284939443) spx: Failed to unpack ...`。同じ瞬間の Unix 時刻 (ミリ秒) は 1,794,286,303,574 で、その 2^32 の剰余は 3,284,941,142。ずれは 2 秒以内で、時計が Unix 時刻であることと合う (ブラウザ版の起動からの時間なら数十秒のはず)。
- したがって mruby のアプリの `board_millis` は約 1.79e12 (2^40 程度) で、`rq` は uint64 で送られる。カーネルの `read_be` は 4 バイト目の時点で `v` が 2^24 を超え、次の `<< 8` で 32 ビットを溢れる。
- 修正後は `Failed to unpack` が 0 行、api の場面と Shell の `asend` が通る (5 章)。

## 4. 直し方

`board_millis` は「起動からのミリ秒」(`fmrb_spx.h` の説明、ESP32 の値) なので、ブラウザ版の時計をそれに合わせた。カーネルの msgpack を直す道は取らない: 1.79e12 は 32 ビットの `sp_int` に入らないので、読み方をどう変えても値を正しく受けることはできない。

| どこ | 何 |
|---|---|
| `lib/replace/picoruby-machine/ports/posix/machine.c` | `__EMSCRIPTEN__` のときだけ、`Machine_uptime_us` がプログラムの開始 (constructor で main の前に取る基準。全スレッドで同じ) からの時間を返す。ほかの POSIX (sim・ネイティブ版・host の試験) は今までどおり |
| `main/kernel/fmrb_spx_common.c` | `__EMSCRIPTEN__` のときだけ、`fmrb_spx_board_millis`・`fmrb_spx_board_micros` が `Machine_uptime_us` を読む。Spinel のアプリ・カーネルの `board_millis` が mruby のアプリと同じ値になる。ESP32 と sim の分は変えていない (前処理の後は同じ) |
| `tools/regress/transfer_quick.rb` | WEB=1 の場面に `api` を足した (最後。ブラウザ版の probe はページの `?app=` なので、job を `/home` に置いてページを読み直す)。続けて、ページを Shell で開き直し (プロファイルを残すので `/home` もそのまま)、`asend rg-xfer-pc /home/inbox/same.txt` を打って、この側に置かれた中身がページのファイルと同じこと・`Failed to unpack` が無いことを見る |
| `tools/regress/transfer.rb` | `web_up` に `app:` と `fresh:` (プロファイルを消すか) を足した |

ついでに直ったもの: ブラウザ版の Spinel のアプリの `board_millis` は、今まで Unix 時刻を `uint32_t` に切って 32 ビットの `sp_int` に入れていたので、負の値になったり、49.7 日ごとの折り返しが読み込みの時刻次第でいつでも起きたりした。`Machine.uptime_formatted` も 2 万日を超える値を出していた。どちらも起動からの値になる。checkpoint でブラウザ版から運んだアプリの `board_millis` も、運んだ先で大きな値のまま続くことがなくなる (D9 の差分は運んだ元の `board_millis` を引き継ぐ)。

ESP32 のビルドで変わるコードは無い (`fmrb_spx_common.c` の追加は全部 `__EMSCRIPTEN__` の中、`ports/posix/machine.c` は ESP32 では使わない) ので、内蔵 RAM は増えない (`regress:size` は 5 章)。

## 5. 確認

| 何を | 結果 |
|---|---|
| `rake regress:transfer:quick WEB=1` (修正前) | `api: FAILED ... the transfer app did not take it in 20 s`、ページのログに `Failed to unpack msgpack data: integer overflow in <<` が毎秒 |
| `rake regress:transfer:quick WEB=1` (修正後) | ok (2.3 分)。api: `same.txt` が置かれ、`sd: true` が届き、1 MiB を 172,032 B で取り消してこの側の片のファイルが消えた。Shell の `asend` で `same.txt` がこの側に `same(1).txt` (22 B、ページのファイルと同じ) として置かれた。`Failed to unpack` は 0 行 |
| `rake regress:transfer:quick` (ネイティブ版) | ok (0.7 分、9 場面) |
| `rake regress` | host 23 suites 全部、描画 229 runs 全部通る |
| `rake regress:checkpoint` (BOARD= なし) | ok (6.4 分、standard・mruby・web) |
| `rake regress:size` | ok (NARYAv3・TAB5、基準 4e7e7e61 に対し内蔵 RAM の増加なし: DIRAM -48 / -360、IRAM・ほかの内蔵は +0。flash の差は基準以後のほかのコミットの分) |

Shell の `asend` は修正前のビルドでは流していない。Shell は mruby のアプリで、`asend` は xfer_probe の `api` と同じ FmrbTransfer#send_file を通る (`shell_commands.rb` の `cmd_send`) ので、修正前に落ちる理由は api と同じ。

## 6. 撤回した仮説・見立てと違った点

- **「ブラウザ版の mruby の Integer が 31/32 ビット」** (T8 の時点の見立ての一部): 違う。ブラウザ版の mruby は `MRB_INT64` + `MRB_NO_BOXING` で 64 ビット。32 ビットなのは Spinel (カーネル) の `sp_int` の方。値を作る側は正しく 64 ビットで送っていて、受ける側に入らない。
- **「`board_millis` が 32 ビットを超えるのは、長く動いたから・wasm で桁が溢れたから」**: 違う。ブラウザ版の時計が初めから Unix 時刻だった (Emscripten の pthreads の `CLOCK_MONOTONIC`)。ページを開いた直後から 2^40 程度。
- **「カーネルの msgpack の読みを直せば済む」**: 取らなかった。`read_be` の書き方を変えても 1.79e12 は 32 ビットの `sp_int` に入らないので、例外の代わりに値が壊れるだけ (隠すことになる)。
- **ログの時刻**: ページのログの `E (3284939443)` は別の時計 (hosted の `esp_log_timestamp`、sim と共有) で、今も Unix 時刻の下 32 ビットのまま。表示だけで値は運ばないので触っていない (残件 2)。

## 7. 残件

1. カーネル (Spinel) の msgpack は 32 ビットの端末 (ESP32・ブラウザ版) で、`sp_int` に入る値でも読めないものがある: int32 (`0xd2`、-32769 以下の負の数) は `read_be` の `<< 8` と `to_signed` の `1 << 31` (= `SP_INT_NIL`) で溢れ、uint32 で 2^31 以上は入らない。アプリが APP_CONTROL にそういう整数を載せると、その要求ごと捨てられる。ESP32 でも起動から 24.8 日を過ぎると `board_millis` が 2^31 を超え、FmrbTransfer の `rq` が同じ理由で読めなくなる。直すなら、読める範囲を `sp_int` に合わせる直し (int32 の負の数) と、`rq` を時計でなく数え上げにするかどうか (FmrbTransfer の決まり: 同じアプリから大きい id だけを受ける) は、仕様の選択を含むので親が決める。
2. hosted のログの時刻 (`fmrb_log_port_hosted.c` の `esp_log_timestamp`) はブラウザ版で Unix 時刻の下 32 ビット。起動からの時間にするかは、sim・ネイティブ版と揃えるかを含めて別に決める。
