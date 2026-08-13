# 実装報告 トラック A: FFT バックエンド選択 gem (Stage 1-3)

対象: `impl_plan.md` Stage 1 / Stage 2 / Stage 3 / 実施: 2026-08-13 /
ブランチ: `feature/mic-spectrum-fft` (develop から分岐、コミット前)

## 結果

**Stage 1-3 完了。4 者を Tab5 実機で計測した**。Stage 2 の本命だった
「同居 Spinel インスタンス」は成立 (フォールバック不要)。Stage 4 (マイク) は
未着手。

### Tab5 実機 (ESP32-P4) の 4 者 — 本命の数字

| backend | avg us | min us | peak bin | 一致 | C 比 |
|---------|-------:|-------:|---------:|------|-----:|
| `:dsp` (esp-dsp) |     354.1 |     350.1 | 8 | 一致 | 0.82x |
| `:c` (素朴)      |     430.8 |     418.4 | 8 | 基準 | 1.00x |
| `:spinel`        |   9,948.2 |   9,767.9 | 8 | 一致 | 23.1x |
| `:ruby` (mruby)  | 115,558.9 | 113,784.4 | 8 | 一致 | 268x |

- 512 点 / K=20 / 5 回 / 8 周期の正弦波。**4 者ともピーク bin = 8、振幅も
  bin ごとに ±1 以内**で一致 (画面の `ok` 列が全部 yes)。
- 環境: M5Stack Tab5 (ESP32-P4)、`feature/mic-spectrum-fft` の同一ビルドを
  flash して remote desktop からデモを起動、シリアルログで採取。
  ブートは Guru/abort ゼロ、IRAM 空き 121KB → デモ実行中 94KB。
- 操作も remote desktop 経由 (ファイルマネージャをキーボードで辿って起動)。

読み取れること:

- **手最適化 (esp-dsp) は素朴な C より 1.2 倍速いだけ**。天井は思ったより近い。
  素朴な C でもマイク可視化には十分 (512 点 0.43ms)。
- **同じ Ruby ソースが Spinel だと mruby の 11.6 倍速い** (115.6ms → 9.9ms)。
- **ただし Spinel は C の 23 倍遅い**。ホスト (下記) では 7 倍だったので、
  **実機で差が開く**。→ この差の主因は下の「追記」で訂正。当初の
  「Float の箱が PSRAM に散る」という見立ては**生成コードの確認で否定された**。
  **切り分けは次の課題** (C を double でビルドして精度を単離、固定小数点版)。
- 実時間用途の判断: 30fps (33ms) 予算に対し **C 0.43ms / esp-dsp 0.35ms は
  余裕、Spinel 9.9ms は 1 フレームの 30%、mruby 115ms は不可**。
  マイク可視化のトラック B は plan どおり esp-dsp (または C) で回す。

### Linux sim での 3 者比較 (先に正しさを固めた段。同一ビルド・同一入力)

| backend | avg us | min us | peak bin | 一致 |
|---------|-------:|-------:|---------:|------|
| `:c`      |   2.3 |   2.3 | 8 | 基準 |
| `:spinel` |  16.2 |  15.4 | 8 | 一致 |
| `:ruby`   | 590.6 | 544.4 | 8 | 一致 |
| `:dsp`    | — | — | — | このビルドに無い (Linux) |

- 512 点 / K=20 / 5 回。入力は 8 周期の正弦波 (`Fmrb::Fft.sine`)。
  **peak bin = 8 = 入力周波数**で 3 者とも一致、振幅も bin ごとに ±1 以内。
- 環境: WSL2 上の docker Linux ビルド (x86-64)。**この数字は相対比較のためだけ**
  で、実機の絶対値ではない。
- 2 回目の起動でも同傾向 (c 2.9/2.5、ruby 540.0/519.4、spinel 14.4/14.1)。

読み取れること (Linux ホストでの相対値):

- **同じ Ruby ソースが、Spinel だと mruby の約 37 倍速い** (590.6 → 16.2)。
- **Spinel は C の約 7 倍遅い**。C との差は Float の箱と配列アクセスの経路で、
  ここが Spinel の伸びしろ。
- mruby は C の約 250 倍。桁の話なので、可視化のような実時間用途に
  mruby VM の Float FFT は使えない、という plan の前提が数字になった。

## 作ったもの

| ファイル | 役割 |
|---|---|
| `main/kernel/fmrb_fft_bench.c/.h` | 素朴 radix-2 の C 実装 + `fmrb_fft_micros` + esp-dsp 版 (ガードつき) |
| `main/kernel/fmrb_fft_spinel.c/.h` | **同居 Spinel インスタンス**の生成/呼び出し/破棄と FFI の受け皿 |
| `main/prebuild_scripts/spinel/fft_spinel.rb` | Spinel の entry (top-level が entry 本体) |
| `main/prebuild_scripts/spinel/fmrb_fft_ffi.rb` | この entry 専用の FFI 宣言 6 本 |
| `lib/add/picoruby-fmrb-fft/` | gem 本体 (`mrblib/fft_core.rb` / `mrblib/fmrb-fft.rb` / `ports/esp32/fft_binding.c` / `src/picoruby_fmrb_fft.c`) |
| `flash/app/demo/fft_bench.app.rb` | 4 者を順に走らせて画面とログに出すデモ |

配線: `rakelib/setup.rake` (gem コピー)、`lib/add/family_mruby.gembox`、
`components/picoruby-esp32/CMakeLists.txt` (binding のビルド)、
`main/CMakeLists.txt` (`FMRB_FFT_SPINEL`、既定 on)、`rakelib/spinel.rake`
(`fft_spinel.c` 生成)、`Rakefile` (フラグをコンテナへ渡す)。

## Stage 2: 同居 Spinel インスタンスは成立した

**mruby アプリのタスクの中で Spinel ランタイムのインスタンスを作り、AOT した
FFT を関数のように呼んで、壊さずに畳める**ことを実機同等の経路 (Linux sim) で
確認した。ログ:

```
I fft_spx: Spinel FFT instance up (196608 bytes, size 512)
I FFT Bench: FFT spinel: avg=16.2us min=15.4us peak=8 (expected 8) agrees=true
```

成立の条件として効いたもの:

- インスタンスは**タスクごとの状態**なので、mruby の `mrb_state` と同居できる
  (`fmrb_spinel_instance_begin` が呼び出しタスクの current にする)。
- entry は `int f(void)` なので、入出力は**全部 FFI のグローバル経由**。
  サンプルは `:binstr`、振幅は `:str` + 長さ、時間は `:int`。
  **Float は境界を渡らない** (チェックリストどおり)。
- K 回のループは Spinel 側 (entry の中) に置いた。C と同じく「繰り返しが
  計測区間の内側」。
- `begin` / `run` × N / `end` を 1 回のベンチで回し、**アプリを 2 回起動しても
  再現**した (プールの取り直しも含めて破綻なし)。

実装で決めたこと:

- **プールは 192KB** (`FFT_POOL_BYTES`)。fft_core.rb が 512 点で作る 6 本の
  配列と、境界を渡る String に対して GC が回る余裕を見た値。**内蔵 RAM 逼迫の
  ある S3 では実測して詰め直すべき値** (Linux では潤沢なので今回は上限確認を
  していない)。閾値はカーネルと同じ `pool/32`。
- entry は呼ばれるたびにテーブル (窓・twiddle・bit 反転) を作り直す。
  entry をまたぐ状態を持たないため。**計測区間の外**なので数字には影響しない
  (1 回の `run(K)` につき 1 回)。

## 公平性 (チェックリストの確認)

- [x] `:ruby` と `:spinel` は**同一の `fft_core.rb`**。gem 側が原本で、
      `rake spinel:gen` が Spinel のソース木へコピーしてから compile する
      (コピーは gitignore)。片方だけ直る事故が起きない。
- [x] twiddle・窓・bit 反転は 3 実装とも事前計算。内側ループに `Math.sin` は無い。
- [x] 3 者とも同じ入力・同じ長さ・同じ K。**ピーク bin 一致 + 振幅 ±1 以内**を
      デモが毎回検査し、画面とログに `agrees=` で出す。
- [x] C 関数は mruby binding と Spinel FFI の両方から呼べる形 (mrb 非依存の ABI)。
- [x] Float は FFI 境界を渡らない。
- [x] `begin`/`end` は対。`Fmrb::Fft.close` が参照カウントで畳む。
- [x] esp-dsp は Linux ビルドに混ざらない (`__has_include` + ターゲットガード。
      `dsp_available?` が false を返し、デモは "not in this build" と表示)。

## つまずいた点 (次に効く知見)

- **gem の C binding を `ports/esp32/` だけに置くと初期化が呼ばれない**。
  mruby のビルドは「その gem に C ソースがあるか」で `gem_init.c` に
  init 呼び出しを出すかを決めるので、**`src/` にも 1 本必要**
  (`src/picoruby_fmrb_fft.c` が `ports` 側の `mrb_fmrb_fft_init` を呼ぶ形。
  picoruby-fmrb-log / -const と同じ作り)。これが無いと Ruby から
  `NameError: uninitialized constant FftNative` になる (最初にこれを踏んだ)。
- `FmrbGfx` に `width` / `height` は無い。アプリ側は `@user_area_width` /
  `@window_height` を使う。
- 段階検証が効いた: `FMRB_FFT_SPINEL=0` で Stage 1 を先に通してから 1 に
  してので、Spinel 側の失敗と gem 側の失敗を切り分けられた。

## Stage 3: esp-dsp の追加とビルド

`main/idf_component.yml` に `espressif/esp-dsp` を足し、**Tab5 (esp32p4) の
ビルドが通ることを確認した**。

```
NOTICE: [2/12] espressif/esp-dsp (1.8.2)
fmruby-core.bin binary size 0x481e40 bytes ... 0x17e1c0 bytes (25%) free
nm: fmrb_fft_c / fmrb_fft_dsp / fmrb_fft_spinel_entry / dsps_fft2r_fc32_arp4_
```

- **4 者ともファームウェアに入っている** (C / esp-dsp / Spinel entry / mruby)。
  `fmrb_fft_dsp` はスタブではなく実体 (`__has_include("esp_dsp.h")` が当たった)。
- flash 残は 25%。
- 依存は `target in [esp32p4, esp32s3]` に限定。Linux ビルドには入らない。

## 残り

- **Stage 4 (マイク可視化)**: ES7210 の bring-up は実機必須。ここからは
  マイク HW を触るので、推測でドライバを書かず未着手のままにしてある。
- **Spinel が実機で C の 23 倍になる理由の切り分け**: 下の「追記」で主仮説を
  精度/FPU に更新済み。C を double でビルドして精度を単離 (E1) → 固定小数点版。
- 固定小数点版 (plan の「第 2 幕」) は未着手。

## 追記: 23 倍の主因の訂正 (生成コード確認後)

当初「Float の箱が PSRAM に散ってキャッシュに乗らない」を主因としたが、
**生成された `main/prebuild_scripts/spinel/gen/fft_spinel.c` を見ると `sp_FloatArray`
を 45 箇所使い、汎用ボックス配列 (`sp_RbVal`) は 4 箇所だけ**。fft_core.rb の
`@re = []; @re << 0.0` は Spinel では**連続 double バッファ** (`mrb_float* data`) に
落ちている。→ **boxing も「箱の散在」も主因ではない**。この当初仮説は否定。

**新しい主仮説は精度と FPU**:

- `mrb_float` は **`double` (8 バイト、`sp_types.h` でハードコード)**。Spinel/mruby の
  FFT は全部 double 演算。
- C と esp-dsp は **`float` (32bit)** (`fmrb_fft_bench.c` の `s_re` 等 / esp-dsp fc32)。
- **ESP32-P4 の FPU は単精度のみ (RV32IMAFC の "F"、"D" 無し)** → **double は
  ソフトウェア・エミュレーション**。C の float はハード FPU。
- これがホスト 7 倍 → 実機 23 倍の開きを説明する。ホスト (x86) は double もハードで
  速いので差は表現オーバヘッドの 7 倍だけ。実機は C=ハード単精度 / Spinel=ソフト
  倍精度で 1 演算が何倍もの命令になる。**ホスト計測では隠れ、実機でだけ出る差**。

**注意 (フェアネス)**: 今の比較は精度が揃っていない (C=float32 vs Spinel=double64)。
ピーク bin・振幅は一致するが、時間比較は精度差込み。

**次の切り分け**:

- **E1 (最優先・安い)**: C ベースラインを `double` でビルドして実機計測。精度/
  ソフトフロートの寄与を単離。C-double が Spinel 側へ近づけば主因確定。同時に
  「同じ精度なら Spinel は C の何倍か」というフェアな数字も得る。
- **E4 (実務の本命)**: 固定小数点 (Q15 整数) 版。整数は Spinel で即値 (箱なし) +
  整数 ALU で FPU 精度問題を丸ごと回避。実機で一番効く見込み。
- (任意) Spinel の単精度化 (`mrb_float` を float32 に) は fork 作業で重い。

