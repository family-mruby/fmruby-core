# Raycaster の計算を Spinel gem 化する実装計画

> **完了 (2026-08-14)。結果は `report/r1.md`。** R1-R5 全て実施。
> 実機 (Tab5/P4) で 1 フレーム 25 本のレイが **11.8ms (:ruby) → 0.23ms (:spinel)
> = 約 49 倍**。絵は同一プレイヤー状態でピクセル完全一致、抽出の忠実さも
> CRuby で 13,884 フレーム総当たり比較 (不一致 0) で確認済み。
> 「整数主体のコードでこそ Spinel が効く」という主張が数字になった
> (FFT の double 版は 11.5 倍止まりだった)。

## 目的

Spinel(Ruby → ネイティブ AOT)の効果を、実アプリで体感・計測できる形で見せます。
題材は `flash/app/game/raycaster.app.rb` の毎フレームのレイキャスト計算です。

なぜ raycaster が Spinel デモに向くか:

- 計算が**すべて固定小数点(整数)**です。Spinel の境界は Float を渡せませんが、この
  アプリは元から整数演算なので、境界設計に無理がありません。
- FFT のときに効いた「double のソフトフロート税(P4 は単精度 FPU のみ)」が**ここには
  無い**ので、mruby(バイトコード解釈)に対して Spinel(ネイティブ整数)の差がそのまま
  出ます。整数主体のコードは Spinel が明快に速くなる題材です。
- 毎フレーム同じ計算を繰り返すので、**gem として何度も呼ぶ**ユースケースになり、
  `--persistent-statics`(重い前処理をインスタンス内でキャッシュ)の意味も見せられます。

手本は `picoruby-fmrb-fft` です。同一の core を mruby(`:ruby`)と Spinel(`:spinel`)の
両方で走らせて比較する構造がそのまま流用できます。作り方の一般手順は
`doc/spinel_aot/adding_a_spinel_gem.md` を参照します。

## 現状分析(どこを切り出すか)

毎フレームの重い計算は `cast_all_rays`(`raycaster.app.rb:376`)です:

- `NUM_RAYS`(25 本)のレイについて `cast_ray`(`:281`)を呼びます。
- `cast_ray` は DDA(グリッド走査、最大 24 ステップ)で、比較はすべて整数の
  たすき掛け(除算回避)です。三角関数は起動時に作る整数 LUT(`@sin_tbl`/`@cos_tbl`、
  `:109`/`:120`)を引くだけです。
- 出力は 1 レイあたり `{ dist:, wall:, side: }` の 3 値で、`draw_frame`(`:527`)・
  `try_shoot`(`:389`)・`draw_enemies`(`:453`)が `@depth_buf[i][:dist]` 等を読みます。

入力は毎フレーム **プレイヤーの位置と向きの 3 値だけ**です(`@px`, `@py`, `@pa`)。
レイ角も fisheye 補正もこの 3 値から導けます。マップ(`WORLD_MAP`、144 要素)と
三角 LUT は不変です。

敵の処理(`try_shoot` / `draw_enemies` の `atan2_deg` / `isqrt`)は毎フレームの重い部分では
ないので、今回の Spinel 化の対象外とし、アプリ側(mruby)に残します。

## 設計方針

- **共有 core**: レイキャストのアルゴリズムを gem の `RaycastCore`(純 Ruby)に移します。
  この 1 ファイルを mruby でも Spinel でもコンパイルし、`:ruby` / `:spinel` の両バックエンドが
  同じコードを走らせます(比較が実装差で歪まないようにするため)。
- **境界は整数と binstr のみ**。Float は渡しません。三角 LUT は core の初期化時に
  `Math.sin` / `Math.cos` で作ります(**Spinel は Math を内部で使えます**。FFT core が実証)。
  この Float は core 内部だけで、境界は渡りません。
- **マップは可変**にします。core には焼き込まず、アプリが持つマップを**境界越しに
  アップロード**します(`set_map`)。LUT はマップに依存しないので core 初期化時に 1 度作り、
  マップと一緒に `--persistent-statics` でグローバルにキャッシュします(毎フレーム作り直さない)。
  マップが変わったときだけ core を作り直します(下記「マップ可変とキャッシュ」)。
- **計算モードをアプリから実行時に切り替え**できるようにします(`:ruby` ⇔ `:spinel`)。
  1 レイ束の計算時間(µs)とモード名を画面に出します。これが「Spinel の効果」を見せる
  中身です(mic spectrum アプリの `[E]ngine` 表示と同じ考え方)。将来 `:c` を足せる
  よう、モードは配列で持ちます。
- **メモリは極力 PSRAM**に置きます。インスタンスのプールはもとより、受け皿の
  ファイルスコープ static(インスタンスハンドル、マップ用バッファ、出力バッファ等)も
  `EXT_RAM_BSS_ATTR` で PSRAM に置きます。ここは esp-dsp のようなハード(アセンブリ/DMA)
  制約が無いので PSRAM で安全です。内蔵 RAM を消費しません。

## 境界設計(FFI)

エントリは引数を取れない(`int f(void)`)ので、入出力は FFI グローバル経由にします。

マップ(不変ではなく可変。変わったときだけ渡す):

- `raycast_spx_map` … `:binstr`(マップセル。`w*h` バイト、各セル 0..4)
- `raycast_spx_map_w` … `:int`
- `raycast_spx_map_h` … `:int`
- `raycast_spx_map_gen` … `:int`(世代番号。アプリが `set_map` するたび native 側で +1。
  これでエントリが「マップが変わった」を検知して core を作り直す)

入力(毎フレーム。mruby → native → Spinel エントリが読む):

- `raycast_spx_px` … `:int`(プレイヤー X、固定小数点)
- `raycast_spx_py` … `:int`(プレイヤー Y、固定小数点)
- `raycast_spx_pa` … `:int`(プレイヤー角、度)
- `raycast_spx_micros` … `:int`(計時用の現在時刻 µs)

出力(Spinel エントリ → native → mruby が読む):

- `raycast_spx_output(str, len, us)` … `:void`
  - `str` は深度バッファを詰めた binstr。1 レイ 6 バイト = `dist`(int32 LE, 4B)+
    `wall`(1B)+`side`(1B)。`NUM_RAYS`(25)本で 150 バイト。
  - `dist` は `wall_h = VP_H*CELL_SIZE/dist` の分母になり int16 に収まらない値を取りうるので
    **int32** にします。`wall`(0..4)と `side`(0..1)は 1 バイトで足ります。
  - `us` は core の実行に要した µs(Spinel 内で計測)。
- `raycast_spx_log(str, len)` … `:void`(不正入力時の診断。FFT と同じ)

binstr の組み立ては picoruby に `Array#pack` が無いので、`"\x00" * n` を作って
`setbyte` で埋めます。アプリ側は `getbyte` で読み、`{ dist:, wall:, side: }` の配列に
戻すので、既存の描画コードは無改造で動きます。マップもアプリ側で `setbyte` で
バイト列にして渡します(セル値 0..4)。

## マップ可変とキャッシュ

core(マップ + LUT)は重い前処理なので `--persistent-statics` でグローバル(`$raycast`)に
残します。ただしマップは可変なので、**変わったときだけ作り直す**必要があります。世代番号で
判定します:

```ruby
# raycast_entry.rb(概念)
gen = RaycastSpx.raycast_spx_map_gen
c = $raycast
if c.nil? || c.map_gen != gen        # nil 判定が先。short-circuit で null 参照しない
  map = RaycastSpx.raycast_spx_map
  w   = RaycastSpx.raycast_spx_map_w
  h   = RaycastSpx.raycast_spx_map_h
  c = RaycastCore.new(map, w, h, gen) # LUT もここで 1 度作る
  $raycast = c
end
c.cast_packed(px, py, pa)
```

これは FFT gem が使う「**キャッシュキーはオブジェクト自身が持つ属性**」の型です
(`c.map_gen`)。別の int グローバルに世代を覚えると、`--persistent-statics` の reset が
オブジェクトグローバルだけ nil にして int を残すため、破棄済みオブジェクトを指したまま
世代だけ一致して null 参照になります。`$raycast.nil?` を先に見るので、その事故を避けられます。

`:ruby` バックエンドは境界が無いので、`set_map` のたびにラッパが core を作り直すだけです。

## ファイル構成

新規 gem: `lib/add/picoruby-fmrb-raycast/`(手本は `picoruby-fmrb-spinel-hello` と
`picoruby-fmrb-fft`)。

作るファイル:

- `mrblib/raycast_core.rb` … 共有アルゴリズム。`class RaycastCore`。
  - `NUM_RAYS` / `FOV` / `CELL_SIZE` / `FP_*` 等の定数を持ちます(**マップは焼き込まない**)。
  - `initialize(map_bytes, w, h, gen)`: マップ(バイト列)と寸法・世代を保持し、三角 LUT を
    `Math` で構築。`map_gen` を読めるようにする(キャッシュ判定用)。
  - `map_at(mx, my)`: `map_bytes.getbyte(my*w+mx)`(範囲外は 1=壁)。
  - `cast_packed(px, py, pa)`: `NUM_RAYS` 本のレイを DDA で解き、深度バッファを binstr で返す。
    現在の `cast_ray` / `cast_all_rays` のロジックをそのまま整数で移植します。
- `mrblib/raycast.rb` … アプリ向け API。`module Fmrb; class Raycast`。
  - `new(backend:)`、`set_map(cells, w, h)`、`cast(px, py, pa)` → `[us, buf]`
    (`buf` は `{dist:,wall:,side:}` の配列)、`close`。
  - `set_map`: `cells`(int 配列 or バイト列)をバイト列に正規化して保持。`:spinel` は
    native にアップロードして世代を +1、`:ruby` は `RaycastCore` を作り直す。
  - `cast`: `:spinel` は native を呼び、`:ruby` は保持中の `RaycastCore#cast_packed` を呼ぶ。
    どちらも同じ手順で unpack(計時も同じ土俵)。
  - `open` / `close` を参照カウントで(SpinelHello / FFT と同じ)。
- `spinel/raycast_entry.rb` … Spinel エントリ。世代でマップ変更を検知し `RaycastCore` を
  `$raycast` にキャッシュ(`--persistent-statics`)、`cast_packed` を呼んで出力
  (上「マップ可変とキャッシュ」の擬似コード)。
- `spinel/raycast_ffi.rb` … 上記 FFI 宣言(`module RaycastSpx`)。
- `native/raycast_native.c` … インスタンス管理(begin/run/end)、マップ upload、毎フレーム
  I/O の static、FFI 実体。**static はすべて `EXT_RAM_BSS_ATTR`(PSRAM)**。マップ用
  バッファは上限を決めた固定サイズ(例: `RAYCAST_MAP_MAX = 64*64` バイト)。`:binstr` 長は
  `sp_net_bin_len` で公開。
- `native/raycast_native.h`
- `ports/esp32/raycast_binding.c` … mruby モジュール `RaycastNative`
  (`available?` / `begin_instance` / `set_map` / `cast` / `end_instance`)。
- `src/picoruby_fmrb_raycast.c` … gem init。
- `mrbgem.rake`

変更するアプリ: `flash/app/game/raycaster.app.rb`

- `cast_ray` / `cast_all_rays` / `build_sin_table` / `build_cos_table` を core へ移し、
  アプリからは削除します(重複コードを残さない)。`WORLD_MAP` はアプリ側に残し、
  `set_map` で gem に渡します(マップ可変)。
- `@caster = Fmrb::Raycast.new(backend: :ruby)` を作り、`on_create` で
  `@caster.set_map(WORLD_MAP, MAP_W, MAP_H)`。`on_create` / `on_update` の
  `cast_all_rays` を `@last_us, @depth_buf = @caster.cast(@px, @py, @pa)` に置換。
- **計算モードの実行時切替**: キー(例: `b`)で `:ruby` ⇔ `:spinel` をトグル。切替時は
  新しいバックエンドに現在のマップを `set_map` し直す(ラッパが世代を管理)。
- HUD に「モード名 + `@last_us` µs」を表示(既存の SCORE/ENEMY 行の隣か下)。これで
  同じ絵のまま mruby と Spinel の速度差がその場で見えます。

## 配線チェックリスト(既存ファイルに足す)

`adding_a_spinel_gem.md` の手順どおり、以下に 1 行ずつ足します。

- `lib/add/family_mruby.gembox` … `conf.gem core: "picoruby-fmrb-raycast"` を追加。
  **追加後は `rake clean`(または `clean_all`)**。しないと gem_init.c が再生成されず、
  gem が黙って未登録になります。
- `rakelib/setup.rake` … `picoruby-fmrb-raycast` の `rm -rf` + `cp -rf`(staging コピー)。
- `components/picoruby-esp32/CMakeLists.txt` … `FMRB_RAYCAST_PORTS_SRCS` の glob と参照を
  **2 か所の `set(PICORUBY_SRCS ...)` ブロック両方**に追加(FFT/SpinelHello と同じ書き方)。
- `main/CMakeLists.txt`:
  - COMPONENT_SRCS に `native/raycast_native.c` をパス参照で追加。
  - INCLUDE_DIRS に `../lib/add/picoruby-fmrb-raycast/native` を追加。
  - register の前に `prepare_ruby_spinel_source(raycast_entry ...)`。
  - register の後に `generate_ruby_spinel_command(... raycast_entry ...)` と
    `set_source_files_properties(... -DSP_MULTI_CTX -include sp_mem_override.h)`。
  - `FMRB_ANY_SPINEL` は既に常時 TRUE なので追加のフラグは不要。
- `rakelib/spinel.rake` … `raycast_entry.rb` / `raycast_ffi.rb` / `raycast_core.rb` を
  `SPINEL_SRC_DIR` へ staging し、`spinel --no-main --entry raycast_entry --persistent-statics
  -I SPINEL_SRC_DIR -c raycast_entry.rb -o gen/raycast_entry.c` を実行。

## 実装手順(段階)

- **R1: core 抽出 + mruby バックエンド**
  - `RaycastCore` を作り、アプリのレイキャストを移植。`Fmrb::Raycast`(`:ruby` のみ)を
    作ってアプリを差し替え、Linux sim で**見た目が従来と同一**なことを確認。
  - この段階では Spinel を使わないので、境界の binstr 詰め/展開だけ先に固めます。
- **R2: Spinel エントリ + 受け皿**
  - `raycast_entry.rb` / `raycast_ffi.rb` / `native/raycast_native.c` を作成。マップ upload
    (`set_map` + 世代)と毎フレーム I/O を通す。
  - 受け皿の static はすべて `EXT_RAM_BSS_ATTR`(PSRAM)。マップバッファは `RAYCAST_MAP_MAX`
    固定サイズ。プール容量は仮決め(まず 128KB。足りなければ begin が NULL を返すので
    増やす。確保先は `fmrb_sys_malloc`=PSRAM)。
- **R3: 配線 + ビルド**
  - 上のチェックリストを全て入れ、`rake clean` → `rake build:linux`。
  - `file build/fmruby-core.elf` で x86-64 を確認(stale な esp32 build に注意)。
- **R4: アプリのトグル + 計測表示**
  - `b` キーで `:ruby` ⇔ `:spinel`、HUD に µs 表示。
  - Linux sim で両バックエンドの数値が出て、絵が一致することを確認。
- **R5: 実機計測**
  - Tab5(P4)へ書き込み、`:ruby` と `:spinel` の µs を実測。整数主体なので Spinel が
    明確に速いはず。数値と手順を `doc/raycast_spinel/report/` に記録。

## 制約と落とし穴

- **Float を境界に渡さない**。入出力は int / binstr のみ。LUT 構築の Float は core 内部限定。
- **1 TU = 1 インスタンス = 1 タスク**。raycaster は 1 タスクなので問題なし。並行呼び出し不可。
- **`--persistent-statics` のキャッシュキーはオブジェクト自身**。core を `$raycast` に置き、
  `$raycast.nil? || $raycast.map_gen != gen` で判定(世代はオブジェクト自身の属性で持つ)。
  「世代を別の int グローバルに覚える」はしない(reset はオブジェクトグローバルを nil に
  するが int は残すため、破棄済みオブジェクトを指したまま int だけ一致して null 参照に
  なる)。`nil?` を先に見るので short-circuit で安全です。
- **picoruby の癖**: `Array#pack` 無し(`setbyte` で組む)、`File.binread` 無し、
  クラス内の裸 `Math`/定数は `::Math` 等で参照、`defined?` 無し。core は mruby でも
  Spinel でも通る書き方に保つ。
- **`dist` は int32**。int16 に詰めると遠距離で壊れる。
- **マップは可変・上限あり**。受け皿のマップバッファは固定サイズ(`RAYCAST_MAP_MAX`、
  例 `64*64`)にし、`w*h` がそれを超える upload は弾く(ログして拒否)。デモのマップは
  12x12 なので余裕。
- **メモリは PSRAM**。受け皿の static はすべて `EXT_RAM_BSS_ATTR`(Linux では空、
  `SpinelHello` と同型)で PSRAM に置く。ここは esp-dsp のようなハード制約が無いので
  PSRAM で安全(mic spectrum の DSP は最適化アセンブリの都合で内蔵必須だったが、本 gem は
  純粋な整数計算で該当しない)。プールも `fmrb_sys_malloc`=PSRAM。
- **gembox 追加後は `rake clean`**。native(.c)編集後も `rake clean`。linux⇔esp32 切替は
  `rake clean_all`。

## 検証と期待効果

- **まず Linux sim**(`tools/dev_run_check.sh` + スクショ)で、`:ruby` / `:spinel` の
  両方で絵が従来と一致することを確認します。
- **実機(Tab5)**で 1 レイ束(25 本)の計算時間 µs を両バックエンドで実測します。
  計時は FFT gem の `:ruby` vs `:spinel` と同じやり方(同一の µs で両者を測る)にします。
- 期待: 整数演算が主で double のソフトフロート税が無いため、**Spinel が mruby に対して
  はっきり速い**数値が出るはずです。ここが FFT(double 税で差が縮んだ)との対比になり、
  「整数主体のコードでこそ Spinel が効く」というデモの主張になります。

## 未確定事項(実装前に確認)

- mruby 側でレイキャスト時間を測る µs 源(アプリから引ける時計)。FFT gem の `:ruby`
  バックエンドが使っている経路を踏襲する。無ければ FFI で micros を mruby にも出す。
- プール容量の実測値(128KB から始めて begin の成否で調整)。
