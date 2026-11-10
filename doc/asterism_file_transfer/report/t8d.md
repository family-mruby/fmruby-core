# T8d: 32 ビットの端末のカーネルの msgpack (rq の数え上げ・int32 の負の数・読めない整数の理由)、Shell の amove、Window の位置のログ

> 状態: 実装済・検収待ち | 更新: 2026-11-10 | 追記 (10 章): カーネルの msgpack が 32 ビットの端末でも Float (float32・float64) を正しく読み書きするようにした (仮数を 16 ビットずつ Float で組む。-0.0・非正規化数・Infinity・NaN も)。FmrbTransfer の頼みの番号 `rq` を時計 (`Machine.board_millis`) から、アプリごとに 1 から数える番号にした。転送のアプリは `app/died` を聞いてそのアプリの pid の「最後に受けた番号」を忘れる (pid が別のアプリに渡っても新しい頼みを受ける)。カーネル (Spinel) の msgpack は整数を幅と端末の `sp_int` の大きさから先に判定し、途中の値が結果より大きくならない読み方にした: 32 ビットの端末で int32 の全域 (-2^31 を除く) と、64 ビットの値で入るものを読む。入らない整数 (uint32 の 2^31 以上など) は、送ったアプリ (PID と名前)・型・バイト列・端末の幅をログに出して、今までどおりその頼みを捨てる。別のコミットで Shell の `move` を `amove` に改名し、Window の位置を変えるたびの INFO のログ 2 行を DEBUG に下げた

決定は plan.md の「決定事項 (2026-11-10 ユーザ、32 ビットの端末の msgpack)」(A・B・C)。経緯は `t8b.md` の 7 章。`amove` は同じ日のユーザの決定 (asend・areceive と揃え、ファイルの mv と紛れないため)。

## 1. 変えたもの

| どこ | 何 |
|---|---|
| `main/prebuild_scripts/spinel/msgpack_pure.rb` | 整数 (uint8〜64・int8〜64) を `read_int` で読む。`int_bytes` (`0.size`。Spinel は `sizeof(sp_int)` に畳む、CRuby は 8) で、上の余ったバイトが符号の広がり (0x00 / 0xff) か、残りの最上位ビットが符号と合うかを先に見る。負の数は補数 (`b ^ 0xff`) を積んで `-1 - v` で返すので、`<< 8` の途中が結果より大きくならない。入らないときは `MessagePack: <型> 0x<バイト列> does not fit this machine's <N>-bit Integer` を raise。`to_signed` は消した (呼び手が無くなった) |
| `main/prebuild_scripts/kernel/fmrb_kernel.rb` | `who_sent(pid)` (`PID n (アプリの名前)`) を足し、APP_CONTROL が読めないときのログを `Failed to unpack msgpack data from PID 4 (Int Probe); the message is dropped: <理由>` にした |
| `main/prebuild_scripts/kernel/fmrb_kernel/audio_handler.rb` | 音の頼みの同じログも同じ形に |
| `flash/usr/share/transfer/fmrb_transfer.rb` | `rq` は `FmrbTransfer.next_rq` (クラスの `@last_rq` を 1 ずつ。アプリの中の FmrbTransfer 全部で 1 つの数え上げ。Shell は asend ごとに作り直すことがあっても番号は増え続ける) |
| `flash/usr/share/transfer/transfer.app.rb` | `app/died` を購読し、終わったアプリの pid を `@asked` から消す (`app_ended`)。「同じアプリからは大きい番号だけを新しい頼みとして受ける」決まりはそのまま |
| `test/msgpack/run.rb`・`cases.rb`、`rakelib/test.rake` | 新しいホストの試験 `rake msgpack:test` (`rake test` に入れた。5 章) |

### 1.1 決まりを読んだ範囲 (A)

`rq` を比べる・覚える所は 2 つだけだった: FmrbTransfer (作る・`@reqs` の鍵) と転送のアプリ (`app_request` の `@asked[pid]` との比較、`cancel_request`・`tell_app` で返すだけ)。カーネルは `rq` を見ずに Pub/Sub の中身として運ぶだけ。tools/・test/・asterism gem (`vendor/asterism`) にも `rq` を使う所は無い (gem の転送は別の id を持つ)。gem は変えていない。

時計をやめると、アプリが終わって同じ pid が次のアプリに渡ったとき、次のアプリの番号 (1 から) が前のアプリの最後の番号より小さくなり、転送のアプリが「もう受けた」と捨てる (`transfer_queued` は返るので送り手は待ち続ける)。時計のときは時間が進むので起きなかった。これを `app/died` で忘れることで塞いだ。カーネルは死んだアプリを reap した後、同じ関数の中で `app/died` を出す (`app_lifecycle.rb` の `cleanup_terminated_app`)。次のアプリがその pid で生まれて頼みを出すのはその後で、カーネルが運ぶ順も同じ列なので、転送のアプリには忘れる知らせが先に届く。転送のアプリが動いていなかったときは `@asked` が空から始まるので問題にならない。

## 2. 直す前に何が起きていたか (B・C の再現)

### 2.1 32 ビットの Spinel のプログラム (cc -m32)

`test/msgpack/cases.rb` を元の `msgpack_pure.rb` (HEAD) と一緒に Spinel で C にし、ファームと同じ実行時 (`components/fmrb_spinel_rt/spinel_rt`) と cc -m32 / -m64 で動かした。

| 入力 | 直す前 (32 ビット) | 直した後 (32 ビット) |
|---|---|---|
| int32 `d2 00000000` (0) | `shift width too big for a 32-bit Integer` | 0 |
| int32 `d2 ffff7fff` (-32769) | `integer overflow in <<` | -32769 |
| int32 `d2 80000001` (-2147483647) | `integer overflow in <<` | -2147483647 |
| int64 `d3 ffffffffffffffff` (-1) | `integer overflow in <<` | -1 |
| uint32 `ce 80000000` | `integer overflow in <<` | `MessagePack: uint32 0x80000000 does not fit this machine's 32-bit Integer` |
| `{"cmd"=>"x","v"=>-40000}` | `integer overflow in <<` | 読める |

- 32 ビットでは int32 (0xd2) は **値によらず全部** 読めなかった (`to_signed` の `1 << 32` が「幅を越えるシフト」で落ちる)。t8b の見立て「-32769 以下」より広い。mruby の msgpack-c は値に合う最小の形で詰めるので、実際に 0xd2 が来るのは -32769〜-2^31 だけで、症状としては t8b のとおり。
- 64 ビット (sim・ネイティブ版) でも int64 (0xd3) は全部読めなかった (`1 << 64`)。msgpack-c が 0xd3 を使うのは -2^31 より小さい値だけなので、ネイティブ版で -2^31 より小さい整数をカーネルに送ると捨てられていた。直した後は読む。

### 2.2 ブラウザ版 (wasm32)

使い捨てのアプリ (scratchpad の `int_probe.app.rb`。`/home` に置いて `?app=` で開く) が、カーネルに `{"cmd"=>"subscribe","topic"=>...,"n"=>N}` を 4 つ送る。読めれば `PID n subscribed to ...` が出る。

直す前 (リポジトリの HEAD のブラウザ版の bundle):

```
E (...) spx: Failed to unpack msgpack data: integer overflow in <<      (n=-40000)
E (...) spx: Failed to unpack msgpack data: integer overflow in <<      (n=-2147483647)
E (...) spx: Failed to unpack msgpack data: integer overflow in <<      (n=2147483648)
E (...) spx: Failed to unpack msgpack data: integer overflow in <<      (n=1774344617292)
```

直した後 (`rake wasm:dist` し直し):

```
I (...) spx: PID 4 subscribed to 't8d/neg40000'
I (...) spx: PID 4 subscribed to 't8d/negmax'
E (...) spx: Failed to unpack msgpack data from PID 4 (Int Probe); the message is dropped: MessagePack: uint32 0x80000000 does not fit this machine's 32-bit Integer
E (...) spx: Failed to unpack msgpack data from PID 4 (Int Probe); the message is dropped: MessagePack: uint64 0x0000019d1f2e3d4c does not fit this machine's 32-bit Integer
```

入らない値は今までどおり頼みごと捨てる (C の決定: 見える振る舞いは「断られる」のまま、理由が出る)。項目だけを差し替える道は取らなかった: 中身を変えて通すと、送った側が知らないまま別の値で動く。

## 3. 生成された C を読んだ

`rake spinel:gen` の `fmrb_kernel_combined.c` の `sp_MessagePack_s_read_int`:

- `int_bytes` は `return ((sp_int)sizeof(sp_int));` (端末ごとに C のコンパイラが決める。黙って 0 にはなっていない)。
- 読む途中は `lv_v = (sp_int_shl(lv_v, 8LL) | sp_poly_to_i(lv_b))` で、`sp_int_shl` は溢れれば raise する版のまま。判定を先に済ませているので、入る値では溢れない (5 章の 32 ビットの試験で確かめた)。
- 負の数は `sp_int_sub(-1LL, lv_v)`。補数は `sp_poly_bitop(..., 255, 2)` (xor)。カーネルでは `msg[:data]` が型の決まらない値 (`sp_RbVal`) なので `getbyte` が poly を通る。試験もバイト列を混ぜた配列から取り出して渡し、同じ `sp_RbVal` の引数の形で C になることを確かめた (`cases_spinel.c` の `read_int(sp_RbVal lv_bin, ...)`)。
- 大きな定数 (0xFFFFFFFF・-2^31) は読む側に 1 つも置いていない (32 ビットでは -1 と nil の印になる)。

## 4. -2^31 (と 64 ビットの -2^63) は入らない扱い

Spinel の `sp_int` の最小値 (`INTPTR_MIN`) は Integer の nil の印 (`SP_INT_NIL`) で、算術も比較も nil として raise する。32 ビットの端末で int32 の -2147483648 を読むと、値としては入るが使えない。だから「入らない」としてほかの値と同じ理由で断る。CRuby (試験) の 64 ビットの判定も同じにした (-2^63 と uint64 の 2^63 以上は 64 ビットでも断る)。

## 5. 確認

| 何を | 結果 |
|---|---|
| `rake msgpack:test` (新しい) | CRuby の 64 ビット・32 ビット (`int_bytes` を 4 に) で 67 件が、CRuby の整数で別に計算した期待と一致。Spinel の 64 ビットと cc -m32 の 32 ビットのプログラムが、同じ幅の CRuby と 1 行も違わない。Spinel のコンパイラか cc -m32 が無い所 (CI) は理由を出して飛ばす (CRuby の層は流れる)。実行時の .o は `test/msgpack/build/` に作り、IMPORT_INFO の commit が変わったら作り直す (初回 13 秒、2 回目から 3 秒) |
| `rake regress` | host 24 suites 全部 (msgpack を足して 23→24)、描画 229 runs 全部通る |
| `rake regress:transfer:quick` (ネイティブ版) | ok (0.7 分、9 場面。`regress:checkpoint` が作り直したネイティブ版で) |
| `rake regress:transfer:quick WEB=1` | ok (2.3 分。`rake wasm:dist` で作り直した bundle で。api の場面と Shell の `asend` を含む) |
| `rake regress:checkpoint` (BOARD= なし) | ok (6.5 分、standard・mruby・web (standard))。Shell の `amove` は 7 章 |
| `rake regress:size` | ok (NARYAv3・TAB5、基準 4e7e7e61 に対し内蔵 RAM の増加なし: DIRAM -48 / -360、IRAM・ほかの内蔵は +0。Spinel の TU の内蔵 RAM は kernel 128 B で t8b と同じ。flash の差は基準以後のほかのコミットの分を含む) |

`regress:transfer:quick` の api の場面 (xfer_probe の FmrbTransfer) と、ブラウザ版の Shell の `asend` は数え上げの番号 (1 から) で通った。同じ pid の付け替え (前のアプリの後に来たアプリの 1 番) を通しで作る場面は足していない (1.1 の順の議論と、`@asked.delete` の 1 行だけ)。

注: 最初の `regress:transfer:quick` 2 回は、ネイティブ版とブラウザ版の bundle を作り直す前に流したので古いカーネルで通っていた (道具は今あるビルドを使い、作らない)。上の表は `regress:checkpoint` (ネイティブ版を作る) と `rake wasm:dist` の後に流し直したもの。

## 6. 見立てと違った点・撤回した仮説

- **「int32 は -32769 以下が読めない」** (t8b 7 章): 読み方の誤りとしては、32 ビットでは int32 の全部、64 ビットでも int64 の全部が読めなかった (2.1)。msgpack-c が値に合う最小の形を使うので、症状は「-32769 以下 (32 ビット)・-2^31 より小さい (64 ビット)」に出る。
- **「pack 側 (`pack_int`) の `-2147483648`・`0xFFFFFFFF` の定数も 32 ビットで壊れているはず」**: 違った。cc -m32 の Spinel で `pack` → `unpack` を回すと、-32769・-40000・-2147483647 も正しいバイト列 (`d2ffff7fff` など) になる (直す前の失敗は unpack の方だった)。いったん書き換えたが、要らないので元に戻した。
- **「`0.size` は Spinel で使えないかもしれない」**: Spinel の codegen が Integer#size を `sizeof(sp_int)` に畳む (`codegen_call_recv.c`)。使えた。
- 文字列の組み立て: `hex = ""` に `<<` すると Spinel では `can't modify frozen String` (文字列の定数は凍っている)。補間で作り直す形にした。

## 7. Shell の `move` を `amove` に (別のコミット)

- `shell_commands.rb`: コマンド名を `amove` に、`move` は消した (別名は残さない)。help の行、使い方、答えの行 (`amove: PID n moved to <node> (PID m there)`・`amove: PID n stayed (...)`・`amove: asked to move ...`) も `amove:` に揃えた (`asend:` と同じ書き方)。
- 回帰の道具: `tools/regress/checkpoint_web.rb` (ブラウザ版の Shell で打つ所と答えの待ち)、`tools/regress/checkpoint_asterism.rb` (ネイティブ版の Shell で打つ所と答えの待ち)。
- 文書: `doc/checkpoint/movable_apps.md` (表と本文の 1 か所)、`doc/regression/usage.md` (asterism shell・web move の説明)。過去の report は直していない。
- 変えていないもの: 開発の道具の `fmrb_native.rb app move`、MCP の `native_app` の `move` (Shell のコマンドではない)。`doc/console_terminal/plan.md` の「`ps`・`kill`・`move`」は構想の一般名なのでそのまま。
- 確認: `rake regress:checkpoint` で、ネイティブ版 (standard・mruby) の asterism shell の場面 (`shell: moved, PID 5 there`) と、ブラウザ版の Shell の `amove <pid> rg-ckpt-a` で A へ戻す web move (`back a 20->21`) が通った。

## 8. Window の位置のログを DEBUG に (別のコミット)

アプリが自分の Window を動かすたびに出ていた INFO の 2 行 (`Set window pos_x=...`・`pos_y=...`、`lib/add/picoruby-fmrb-app/ports/esp32/app.c` の `mrb_fmrb_app_set_window_param`) を `FMRB_LOGD` に下げた (ユーザの決定 2026-11-10)。カーネルの `Start drag`・`End drag` など 1 回の操作に 1 行のものは変えていない。lib/ なので `rake clean` の後に `rake build:linux` を通した。

## 9. 残件

1. ~~**32 ビットの端末で Float が壊れる**~~ (直した。10 章。ユーザの決定 2026-11-10「直す、小数が正しくなる以外の振る舞いは変えない」): `msgpack_pure.rb` の float は整数を通して組み立てる。cc -m32 の Spinel で、読む方は `cb 3ff8000000000000` (1.5)・0.7・-1.5・float32 の負の数が `integer overflow in <<` で読めない (float64 の仮数 48 ビットを `read_be(6)` で、float32 の符号ビットを `read_be(4)` で読むため)。詰める方は **黙って違う値になる**: 1.5 が `cb 4000000000000000` (2.0)、0.7 が 1.0 (`pack_float64` の 2^52 倍の仮数が溢れる)。カーネルは publish の中身を読み直して詰め直すので、ESP32・ブラウザ版で Pub/Sub に小数を載せると、捨てられるか値が変わる。直すなら仮数を 16 ビットずつ Float で組む形になる。
2. Spinel のアプリ (デスクトップ・エディタ) の `_dispatch_control` は `MessagePack.unpack` を rescue しない。入らない整数がそこへ来たら例外がアプリの上まで上がる (直す前も `<<` の例外で同じ)。理由の文は今回のものになる。
3. `rq` の pid 付け替えを通しで作る回帰の場面は無い (5 章)。
4. `test/msgpack` の Spinel の層は CI では飛ぶ (コンパイラが無い)。CI に入れるかは別に決める (作業ブランチに CI を足さない方針)。

## 10. 32 ビットの端末の Float (9 章の残件 1 を直した)

ユーザの決定 (2026-11-10): 直す。小数が正しくなる以外の振る舞いは変えない。

### 10.1 原因

`msgpack_pure.rb` の float は、IEEE 754 のビット列を Integer に組んでから値にしていた (読む方)、値から 52 ビットの仮数を Integer で作ってから切り出していた (詰める方)。

- 読む: `unpack_float64` は仮数の下 48 ビットを `read_be(6)` で、`unpack_float32` は 32 ビット全部を `read_be(4)` で 1 つの Integer に積む。32 ビットの `sp_int` では `<<` の途中が溢れ、`integer overflow in <<` で頼みごと捨てられる。積む途中が溢れない値 (仮数の下位が 0 の 1.0・0.5、float32 の正の 1.5・0.7 など) は読めるが、カーネルが詰め直す所 (次の項) で値が変わる。
- 詰める: `pack_float64` は `(frac * 2^52).round` で仮数を Integer にする。32 ビットでは 2^52 倍が `sp_int` に入らず、黙って違う値になる (1.5 が 2.0、0.7 が 1.0)。
- 64 ビットでも直っていなかったもの: -0.0 は +0.0 に詰めた (`v == 0.0` で先に 0 を出す)、非正規化数は読みも詰めも違う値、Infinity を詰めると `while av >= 2.0` が終わらない (カーネルが止まる)、NaN を詰めると CRuby では `FloatDomainError`。

### 10.2 証拠 (直す前)

`test/msgpack/cases.rb` の float の場面を、HEAD の `msgpack_pure.rb` と一緒に Spinel で C にし、cc -m64 / -m32 で動かした (Infinity・NaN を詰める場面は終わらないので外した)。抜粋:

| 入力・操作 | 直す前 64 ビット | 直す前 32 ビット | 直した後 (両方) |
|---|---|---|---|
| 読む `cb 3ff8000000000000` (1.5) | 1.5 | `integer overflow in <<` | 1.5 |
| 読む `cb 3ff0000000000000` (1.0) を詰め直す | `cb3ff0...` | `cb4000000000000000` (2.0) | `cb3ff0...` |
| 読む `ca bfc00000` (float32 の -1.5) | -1.5 | `integer overflow in <<` | -1.5 |
| 読む `ca 3f333333` (float32 の 0.7) を詰め直す | `cb3fe6666660000000` | `cb3ff0000000000000` (1.0) | `cb3fe6666660000000` |
| 読む `cb 0000000000000001` (最小の非正規化数) | `cb0000...` (0 になる) | `cb0010000000000000` | そのまま |
| 読む `cb 8000000000000000` (-0.0) を詰め直す | `cb0000...` (+0.0) | 同じ | `cb8000...` |
| 詰める 0.7 | `cb3fe6666666666666` | `cb3ff0000000000000` (1.0) | `cb3fe6666666666666` |
| 詰める 3.14159 | 正しい | `cb4010000000000000` (4.0) | 正しい |
| 詰める 1.7976931348623157e308 | 正しい | `cb7ff0...` (Infinity) | 正しい |
| 詰める Infinity・NaN | 終わらない | 終わらない | `cb7ff0...`・`cb7ff8...` |

ブラウザ版 (wasm32) でも同じことが見えた。使い捨ての 2 つのアプリ (scratchpad。`/home` に置き `?app=` で開く): 送る側が `{"i"=>i, "v"=>値}` を値ごとに publish し、受ける側 (送る側が `request_run` で起こす) が届いたものを画面に出す。

- 直す前 (HEAD のカーネルの bundle): 1.5・0.7・-2.5・3.14159・1.0e-40・1.0e20 の 6 つは届かない (カーネルが読めず捨てる)。届いたのは `4: 0.0` (-0.0 が +0.0 に)、`7: 2.0` (1.0 が 2.0 に)、文字列だけ。値の列に Infinity を足すと、後ろの文字列も届かなくなった (カーネルが詰める所で回り続ける。ブラウザ版はカーネルとアプリが同じスレッドで順に動くので、ほかも進まない)。
- 直した後 (`rake wasm:dist` し直し): `0: 1.5`、`1: 0.7`、`2: -2.5`、`3: 3.14159`、`4: -0.0`、`5: 1.0e-40`、`6: 1.0e+20`、`7: 1.0`、`8: Infinity`、`9: "str"` が全部そのまま届いた。

### 10.3 直し方

- 読む (`unpack_float64`・`unpack_float32`): バイトを 1 つずつ取り、仮数を 16 ビットずつ Float に積む (`m = m * 65536.0 + (b * 256 + b)`。2^52 未満の整数は double で正確)。指数は 11 / 8 ビットの Integer のまま。値は共通の `float_from_parts` で: 指数が全部 1 なら仮数が 0 で ±Infinity (`1.0 / 0.0`)、そうでなければ NaN (`0.0 / 0.0`)。指数が 0 なら 0 と非正規化数 (`m / 2^mbits * 2^(1 - bias)`)、ほかは `(1 + m / 2^mbits) * 2^(e - bias)`。符号は最後に `-val` (0 なら -0.0 になる)。float32 は double に正確に入る。
- 詰める (`pack_float64`): NaN は `f != f` で見て `7ff8000000000000` (静かな NaN の 1 つの形。CRuby の `[NaN].pack("G")` と同じ)。符号は `f < 0.0`、または 0 で `1.0 / f < 0.0` (-0.0)。Infinity は `Float::MAX` より大きいかで見る。正規化数は 2^32 ずつ・2 ずつ割って (掛けて) [1, 2) に寄せる (割る・掛けるのは 2 の冪なので正確。2^32 の粗い段は大きい・小さい値で 1000 回回らないため)、仮数は `(f - 1.0) * 2^52` を Float のまま持つ。非正規化数は `f * 2^1022 * 2^52` (2 段に分けて正確)。仮数は 2^48・2^32・2^16 で割って `floor` し、16 ビット以下の Integer にしてから `append_be` に渡す。
- 途中の Integer は最大で 16 ビット (仮数の切れ端) と 11 ビット (指数)。大きな定数は Float の定数だけ (`4503599627370496.0` など。Integer の定数としては置いていない)。
- 64 ビットの振る舞い: 正規化数の読み書きは今までと同じバイト・同じ値 (CRuby の層で、乱数の double 20 万個の詰めたバイトを `Array#pack("G")` と、float32 10 万個の読んだ値を `unpack("g")` と比べて全部一致。scratchpad の使い捨ての試験)。変わるのは、今まで違っていた -0.0・非正規化数・Infinity・NaN だけ。

### 10.4 生成された C を読んだ

`rake spinel:gen` の `fmrb_kernel_combined.c` (`editor_combined.c` と、`FMRB_APP_ENGINE_DESKTOP=spinel` 相当で scratchpad に生成したデスクトップも同じ形):

- 4 つの関数とも、値は `sp_RbVal` (poly) で、演算は `sp_poly_mul`・`sp_poly_div`・`sp_poly_floor` など。型が絞れずに 0 を返す呼び出しは無い (`sp_poly_*` は Float の tag を見て double で計算する)。
- 定数は `sp_box_float(4503599627370496.0)`・`4.4942328371557898e+307` (2^1022)・`1.7976931348623157e+308`・`2.2250738585072014e-308`・`2.3283064365386963e-10` (2^-32) と、`(0.0 / 0.0)`・`(1.0 / 0.0)`。どれも C の double の定数で、`sp_int` を通らない。
- Integer の演算は `sp_int_shl(lv_sign, 7LL)`・`(lv_bexp >> 4LL)`・`sp_poly_to_i(lv_p0)` (16 ビット以下) だけ。
- `sp_poly_floor` は NaN・Infinity で raise するが、渡すのは常に有限の仮数。
- ビルドに `-ffast-math` は無い (NaN・Infinity の比較が消えない)。

### 10.5 端末ごとに持てる精度

- **カーネル (Spinel)**: `sp_float` は全端末で C の `double` (`spinel_rt/sp_types.h` の `typedef double sp_float;` に 32 ビット用の分岐は無い。poly の箱も `v.f` が double)。ESP32-S3 (倍精度の FPU は無くソフトウェアで計算)・P4・wasm32 でも float64 をそのまま持つ。よってカーネルは float64 も float32 も正確に読み、float64 で正確に詰め直す。
- **mruby のアプリ**: `family_mruby_esp32.rb`・`family_mruby_wasm.rb` に `MRB_USE_FLOAT32` は無く、Float は double。msgpack の C の束縛は `msgpack_pack_double` (0xcb) で詰める。カーネルを通っても値は変わらない (直した後)。
- **MicroPython のアプリ**: `MICROPY_FLOAT_IMPL_FLOAT` (単精度) なので、受け取った float64 は float32 の精度に丸まる。MicroPython の msgpack は Float を詰めない (送れない)。今回は変えていない。
- float32 (0xca) を送るアプリは今は無い。カーネルは読んで float64 で詰め直すので、受け手には float64 で届く (今までと同じ)。

### 10.6 試験

`rake msgpack:test` に float を足した (125 件。整数 67 件 + float 58 件)。数字の表示は Spinel と CRuby で書き方が違いうるので、Float は `to_s` を使わずバイトで見る:

- 読む場面 (`mp_fcase`): 0xcb・0xca のバイト列を読み、カーネルが詰め直すバイトと、プログラムに書いた値と同じか (NaN は NaN 同士、0 は符号まで) を出す。0.0・-0.0・1.5・0.7・-2.5・-1.5・3.14159・1.0・1 の直前・最大と最小の正規化数 (±)・最大と最小の非正規化数・±Infinity・NaN (静かな NaN と、別の payload を持つ NaN)・2147483647.0・1.0e20、float32 は 0.0・-0.0・1.5・0.7・-2.5・-1.5・3.14159・最大 (±)・最小の正規化数・1e-40 付近の非正規化数・最小の非正規化数 (±)・±Infinity・NaN。
- 頼みの形 (`mp_repack`): `{"cmd"=>"x","v"=>0.7}` (float64) と `{"v"=>1.5}` (float32) を読んで詰め直す。
- 詰める場面 (`mp_fround`): 0.0・-0.0・1.5・0.7・-2.5・3.14159・0.1・-123.456・1.0e20・6.02e23・1.0e-40・最大・最小の正規化数・最大と最小の非正規化数 (±)・±Infinity・NaN を詰めて読み戻す。
- `run.rb` は CRuby の `Array#pack("G")`・`unpack("g"/"G")` で期待を別に作り、CRuby の 64 ビット・32 ビットの判定と比べる。Spinel の 64 ビットと cc -m32 のプログラムは CRuby と 1 行も違わない。直す前の `msgpack_pure.rb` では、32 ビットの float の行がほぼ全部と、64 ビットの -0.0・非正規化数の行が落ち、Infinity・NaN を詰める場面は終わらない。

### 10.7 確認

| 何を | 結果 |
|---|---|
| `rake msgpack:test` | CRuby 64・32 ビットで 125 件、Spinel 64 ビット・cc -m32 が CRuby と同じ 125 行 |
| `rake regress` | host 24 suites 全部、描画 229 runs 全部通る |
| `rake regress:checkpoint` (BOARD= なし) | ok (6.4 min、standard・mruby・web (standard))。1 回目は standard の途中で `fmrb_native.rb app ps: [Errno 32] Broken pipe` (道具とネイティブ版の debug の接続。ネイティブ版のログに crash は無く動き続けていた)。流し直して通った。カーネルの msgpack とは関わらない所と見ている |
| `rake regress:transfer:quick` (ネイティブ版) | ok (0.7 min) |
| `rake regress:transfer:quick WEB=1` | ok (2.3 min) |
| `rake regress:size` | ok (NARYAv3・TAB5、基準 4e7e7e61 に対し内蔵 RAM の増加なし: DIRAM -48 / -360、IRAM・ほかの内蔵 +0。Spinel の TU の内蔵 RAM は kernel 128 B で変わらず。数字は 5 章と同じ) |
| ブラウザ版の Pub/Sub (10.2) | 直す前は 6 つ捨てられ 2 つ値が変わり Infinity で止まる、直した後は 10 個全部そのまま |

`regress:transfer:quick` は `regress:checkpoint` が作り直したネイティブ版と、`rake wasm:dist` で作り直した bundle で流した (道具は作り直さない)。

### 10.8 撤回した仮説・見立てと違った点

- **「Spinel の Float は 32 ビットの端末では float かもしれない」**: 違った。`sp_float` は全端末で double。カーネルは float64 を正確に持てる (10.5)。
- **「直すと 64 ビットの値が変わるかもしれない」**: 正規化数は変わらない (20 万個で一致)。変わったのは今まで違っていた所 (-0.0・非正規化数・Infinity・NaN) だけ。
- **「壊れているのは 32 ビットだけ」**: 64 ビット (ネイティブ版・sim) でも -0.0 は +0.0 になり、非正規化数は違う値で、Infinity を publish するとカーネルが止まっていた (10.1)。
- **NaN の payload**: 読んだ NaN は静かな NaN 1 つの形 (`7ff8000000000000`) で詰め直す。payload と符号は残らない (CRuby の Float も同じ扱い。payload を運ぶ使い方は無い)。

### 10.9 残件

1. ~~MicroPython のアプリは Float を msgpack に詰められない (`pack_value` に Float の枝が無い)~~ (直した。ユーザの決定 2026-11-10「MicroPython のアプリから小数を送れるようにする」): `components/micropython/modules/fmrb_module.c` の `pack_value` に Float の枝を足した。`mp_obj_get_float` の単精度を double に広げて float64 (`0xcb`、ビッグエンディアン) で詰める (広げるのは正確)。`MICROPY_PY_BUILTINS_FLOAT` で囲む。`fmrb_send_message` などで小数が `ValueError: message payload not encodable` にならなくなる。読む方 (`0xca`・`0xcb`) は前から読めていた。単精度に丸まるのは変わらない。smoke は `_fmrb` を差し替えて動くので場面は足していない (ホストで構文の確認、`rake micropython:smoke`・`rake regress` は通った)。
2. float32 (0xca) で詰める道は無い (カーネルは常に float64)。要るアプリが出たら決める。
3. 9 章の 2〜4 はそのまま。
