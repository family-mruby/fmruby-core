# APU の 2 つの音源がフィルタの状態を共有していた (60 Hz のビリビリ)

> 状態: 解決 | 更新: 2026-10-19 | 内蔵の音源 (nofrendo の APU) の低域フィルタが「直前のサンプル」を関数の中の `static` 1 つに持っていて、BGM 用 (MAIN) と効果音用 (SUB) の 2 つの音源で共有していた。1 コマごとに相手の最後の値を引き継ぐので、1 秒に 60 回の段差が出る。BGM だけの音量 (MJ12) を足して BGM を小さくしたら、それが「ビリビリ」として聞こえるようになった。値を音源ごとの構造体へ移して直した

## 1. 症状

- Tab5・ブラウザ版の麻雀で、BGM を「小」(MAIN 30%) にすると、待ち合わせの曲 (After the Rain) に**ビリビリした音**が混ざる。中・大では目立たない (2026-10-19 ユーザが耳で気づいた)。
- その前の MJ12 の sim の測定で、MAIN の音量を 0% にしても曲が鳴っているあいだは音が残っていた。

  | MAIN の音量 | ピーク / RMS (sim、`tools/fmrb_audio_probe.rb`、8 秒) |
  |---|---|
  | 100% | 2459 / 650、2185 / 349 |
  | 30% | 793 / 199、825 / 200 |
  | 0% | **456 / 8.6、547 / 10** |

  0% の行の「RMS は小さいのにピークは大きい」が手がかりだった。まばらな尖った音 (インパルスの列) の形をしている。

## 2. しくみ

### 2.1 音源の多重化は「構造体を丸ごと出し入れ」

nofrendo の APU は、もともと 1 つの音源を前提に、状態をファイルの中の `static apu_t apu;` に持つ。2 つの音源 (MAIN と SUB) は、`apu_if_p4.c` (Retro は graphics-audio の `apu_if.cpp` / `apu_if_linux.c`) が `apu_t` を 2 つ持ち、1 コマごとに入れ替えて処理する。

```c
apu_setcontext(_apu);       // apu = *_apu
apu_process(main_buf, n);   // MAIN の 1 コマ (n = 262 サンプル = 15720 Hz / 60)
apu_getcontext(_apu);       // *_apu = apu
...
apu_setcontext(_apu_sub);
apu_process(sub_buf, n);    // SUB の 1 コマ
apu_getcontext(_apu_sub);
```

`apu_t` の中にある状態は音源ごとに保たれる。**`apu_t` の外にある状態は、2 つの音源で共有される**。

### 2.2 フィルタの「直前のサンプル」が `apu_t` の外にあった

`apu_process` の低域フィルタ (既定は `APU_FILTER_WEIGHTED`) は、各サンプルを「今の値 × 3 + 直前の値」を 4 で割って作る。その直前の値が関数の中の `static` だった。

```c
void apu_process(void *buffer, int num_samples)
{
   static int32 prev_sample = 0;      // ← 2 つの音源で 1 つ
   ...
      next_sample = accum;
      accum = (accum + accum + accum + prev_sample) >> 2;
      prev_sample = next_sample;
```

処理の順は MAIN → SUB → MAIN → SUB … なので、

- **SUB の各コマの最初のサンプル**は、MAIN の最後の値を 1/4 だけ混ぜて作られる。効果音が鳴っていなくても (SUB の値が 0 付近でも)、`MAIN の最後の値 / 4` の尖った音が出る。
- **MAIN の各コマの最初のサンプル**は、SUB の最後の値を混ぜる。SUB が無音なら、MAIN の値が 1/4 欠けたへこみになる。

どちらも 1 コマに 1 回、つまり **60 Hz の繰り返し**で、大きさは曲のそのときの値で変わる。60 Hz とその倍音の、ざらついた「ビリビリ」になる。

### 2.3 BGM の音量を下げたら目立った理由

MJ12 で足した MAIN の音量は、MAIN の 1 コマを処理した**後**で MAIN の出力にだけ掛かる。

- MAIN のへこみ (MAIN の中の段差) は、音量と一緒に小さくなる。
- **SUB に漏れた尖った音は SUB の出力の一部なので、MAIN の音量が掛からない**。

100% では曲そのものに埋もれていたが、30% にすると曲は約 10 dB 小さくなり、漏れは元の大きさのまま残る。そのため「小」でだけ聞こえた。0% の測定で残った「RMS 10 前後・ピーク 500 前後」は、この漏れだけを聞いていたことになる。

この不具合自体は、2 つ目の音源 (SUB) を足したときからあった。MAIN と SUB が同じ大きさだったあいだは、曲に隠れていた。

## 3. 直し方

`prev_sample` を `apu_t` の中に移し、`apu_process` では `apu.prev_sample` を使う。`apu_setcontext` / `apu_getcontext` が構造体を丸ごと出し入れするので、これで音源ごとに別の値になる。

**構造体の大きさは変えない** (内蔵 RAM の増分 0 の決まり)。`apu_t` には `uint8 enable_reg;` と `uint8 mix_enable;` が離れて置かれていて、それぞれの後ろに 3 バイトずつすき間があった。2 つを隣に並べて 4 バイト空け、そこに `int32 prev_sample` を入れた。32 ビットで `sizeof(apu_t)` は直す前も後も 296 バイト。関数の中の `static` が無くなる分、全体では 4 バイト減る。

```c
typedef struct apu_s
{
   ...
   uint8 enable_reg;
   uint8 mix_enable; /* next to enable_reg: the two bytes share one word */
   void *buffer;
   int num_samples;
   int filter_type;
   int32 prev_sample;  /* per APU */
   ...
} apu_t;
```

同じコードは fmruby-core (`components/apu_emu`: Tab5・P4・ブラウザ版) と fmruby-graphics-audio (`components/apu_emu`: Retro の WROVER と sim) の両方にあり、両方を同じ形で直した。Tab5 は再 flash で、ユーザが耳で直ったことを確認した (2026-10-19)。

## 4. ほかの `static` も見た

同じ落とし穴がほかに無いか、`nes_apu.c` の関数の中とファイルの `static` を全部見た。

| もの | 判断 |
|---|---|
| `shift_register15` の `static int sreg` (雑音の 15 ビットのシフトレジスタ) | `REALTIME_NOISE` が無い作りでは、起動時に表 (`noise_long_lut` / `noise_short_lut`) を作るのにだけ使う。表は読み出し専用で、各音源の読み位置は `noise_t` の中にある。共有で問題ない |
| `decay_lut` / `vbl_lut` / `trilength_lut` / `duty_flip` | 定数の表。共有で問題ない |
| `debug_frame_count` ほか (`APU_DEBUG` が 0) | デバッグの数え上げだけ |
| `test_tone_initialized` | 試験用の音だけ |

## 5. 教訓

- **1 つの前提で書かれたエミュレータを「構造体の出し入れ」で多重化するときは、構造体の外の状態 (関数の中の `static`、ファイルの `static`) を全部洗う**。出し入れの外にある状態は黙って共有され、片方の音がもう片方に漏れる。
- **ピークが大きく RMS が小さい残りは、まばらなインパルスの列を疑う**。コマの長さ (ここでは 1/60 秒) の繰り返しなら、コマの境目で何かを引き継いでいる。
- **混ぜる前に片方だけ音量を下げると、それまで埋もれていた漏れが表に出る**。音量の機能を足したときは、0% にして残りを測ると、隠れた漏れが見つかる。
- 構造体に値を足すときは、先にすき間を探す。並べ替えだけで大きさを変えずに済むことがある。
