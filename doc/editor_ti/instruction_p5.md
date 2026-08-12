# 実装指示書 P5: 実機対応 (arena の PSRAM 化 + Tab5/S3 実機検証)

対象: 実装担当セッション。前提: P4.5 完了 (PIN = fmrb-dev 39ad30c、
補完/ホバー/診断/signature help/F1 ヘルプが sim 両構成で動作)。plan.md と
report/p1.md〜p4_5.md を先に読むこと。

report は doc/editor_ti/report/p5.md へ。タスクごとにコミット。

## P5 のゴール

型支援を実機で使える状態にする。具体的には:

1. **エンジンの 16KB 作業 arena を PSRAM へ移す** (内蔵 RAM を食わない)
2. **Tab5 実機で P3/P4/P4.5 の全機能を検証** (P2 補完は実機確認済み)
3. **S3 (NARYAv3) で載るか判断** — 内蔵 RAM 予算に収まるか実測して決める

## 前提の整理 (P5 で確定済み)

- **flash は S3 のボトルネックではない**。S3 は factory 4M / storage 5M
  (config/partitions_n16r8.csv)、ti 込みイメージは ~3.6MB で 14% 余裕。
  flash/help は 20KB で storage の 5M に対し誤差。
- **残る S3 の懸念は内蔵 RAM の 16KB arena だけ** (エンジンの
  `static uint8_t arena_bytes[16KB]`、src/base/picoruby_ti_arena.c)。
  これを PSRAM に逃がせば S3 でも内蔵 RAM は増えない。
- arena は **CPU アクセスのみ** (DMA しない) ので PSRAM 常駐で問題ない。
  [[project_psram_dma_issue]] は DMA 対応バッファの話で、ここには当たらない。

## T1: fork — arena の格納属性を host 注入可能にする

作業場所: tmp/picoruby-ti (fmrb-dev)。上流に返せる形を保つこと
(esp32/IDF 固有物を engine に直書きしない)。

src/base/picoruby_ti_arena.c の arena を、host がセクション属性を差し込める
形にする。移植性を壊さないため既定は現状 (ただの static):

```c
/* Host builds may place the arena in external RAM by defining these; the
   default keeps it an ordinary static array (portable, host tests). */
#ifdef TI_ARENA_INCLUDE
#include TI_ARENA_INCLUDE
#endif
#ifndef TI_ARENA_ATTR
#define TI_ARENA_ATTR
#endif
TI_ARENA_ATTR static uint8_t arena_bytes[TI_ARENA_SIZE];
```

- host_test / Linux ビルドは何も定義しないので従来どおり (回帰確認)。
- コミットは 1 本、上流説明を書く (「大きな作業領域を外部 RAM に置きたい
  組み込み host のためのフック。既定不変」)。push して PIN を更新。

## T2: fmruby-core — esp32 ビルドで arena を PSRAM に置く

- picoruby-ti の esp32 コンパイルに対して
  `TI_ARENA_INCLUDE="esp_attr.h"` と `TI_ARENA_ATTR=EXT_RAM_BSS_ATTR` を
  定義する。定義箇所は ti のソースをコンパイルしている経路
  (rake の mruby ビルド側。P1 報告のとおり ti は普通の mrbgem なので
  CMake の PICORUBY_SRCS には無い)。**linux ビルドには定義しない**
  (EXT_RAM_BSS_ATTR は IDF 専用)。
- 定義の入れ方は既存の MRB_* defines や他 gem の cc.defines の作法に合わせる
  (family_mruby_esp32.rb / esp32p4.rb か mrbgem.rake の該当箇所)。
  linux 用 build_config には入れないこと。
- **測定**: 定義前後で S3 (NARYAv3) と P4 (TAB5) の内蔵 RAM を比較する。
  ブートログの `M1|...|internal=..` 隣接差分か idf.py size で、
  **arena の 16KB が内蔵 RAM から PSRAM (.ext_ram.bss) へ移ったこと**を
  数字で示す。report に両ターゲットの前後を表で残す。

## T3: Tab5 (P4) 実機で全機能検証

実機は remote desktop で操作できる (root CLAUDE.md「Tab5 実機のリモート UI
操作」)。**シリアルを開くと Tab5 はリセットするので、ログ採取を先に掴んで
から UI 操作**すること。IP は毎回変わるので mDNS かブートログで取得。

標準構成 (Spinel エディタ) で、スクリーンショット付きで report へ:

1. 補完 (P2 の回帰): `class MyApp < FmrbApp` の def 内 `@gfx.dr` + Tab。
2. ホバー (P3): `@gfx` に Ctrl+T -> 型、メソッド上で Ctrl+T -> シグネチャ。
3. 診断 (P3): 誤引数を書いて保存 -> 行マーカー + `[!N]` バッジ。
   Ctrl+E で次エラーへ。直して保存 -> `no problems`。
4. 定数 (P4): `FmrbGfx::` + Tab -> 色定数。診断に型が流れること。
5. signature help (P4.5): `@gfx.draw_text(10,` -> `>>y: Integer<<`。
6. F1 ヘルプ (P4.5): 補完候補選択中に F1 -> [Help] ページが開き、
   編集キーが弾かれ、Esc で元のバッファ・カーソル・色分けまで戻る。
7. **実機レイテンシ**: 20KB 程度の文書で補完/診断を叩き、`ti_lat` の
   実測値を記録 (sim は 20.5KB で 34ms、64bit。実機 32bit の値を取る)。
8. **ログの静けさ**: 補完のたびに fmrb_alloc の使い捨てヒープログが
   出ないこと (fmrb_mem_create_handle_quiet で対処済み、実機で確認)。

## T4: S3 (NARYAv3) 実機で載るか判断

- `.env` を NARYAv3 に、`rake clean_all` -> `rake build:esp32` ->
  `FLASH_BAUD=115200 rake flash`。ブート健全性 (Guru/abort 0、
  `IRAM free:` が想定内)。
- **内蔵 RAM の実測が判断の核**: T2 の PSRAM 化を入れた状態で、
  ブートログの M1 マーカーと `fmrb_task: IRAM free` を [[project_internal_ram_budget]]
  の予算と突き合わせる。arena が PSRAM に居れば ti の内蔵 RAM 増は
  ほぼ 0 のはず。db は rodata (flash) なので内蔵 RAM に乗らない。
- S3 は remote desktop が無い (BLE のみ) ので UI 操作は不可。**補完が
  動くことの確認は shell か debugd 経由**、または Linux sim で機能確認済み
  として、実機では「ビルド通過 + ブート健全 + 内蔵 RAM が予算内」を
  受け入れ条件にする (root CLAUDE.md の Retro 検証制約どおり)。
- **判断**: 内蔵 RAM が予算内なら S3 でも ti を有効のまま。もし何か
  (arena 以外の理由) で内蔵 RAM が足りないなら、**ti を Modern 限定に
  ゲートする** 逃げ道を用意する:
  - gembox かビルド設定で picoruby-ti の取り込みを esp32p4 (Modern) の
    ときだけにする。エディタ側は「ti 無効時はブリッジ呼び出しをしない」
    (P2 で入れた 32KB 上限や候補 0 と同じ、機能が黙るだけ) で耐える。
  - この分岐は**入れる場合のみ**。まず実測して、要らなければ作らない
    ([[feedback_new_config_silent_holes]]: 分岐を足したらその構成を必ず
    一度ビルドして通す)。

## T5: 残件の片付けと記録

- flash/help が実機の storage に載り、F1 でページが開くこと (T3-6 で確認)。
- **診断の桁範囲** (start_x..end_x) はブリッジまで来て UI 未使用 (v1 仕様)。
  変更不要、report に「据え置き」と記載。
- report/p5.md に: T2 の内蔵 RAM 前後表 (S3/P4)、T3 の実機レイテンシと
  スクリーンショット、T4 の S3 判断 (載せる/Modern 限定) とその数字、
  PIN (T1 の commit)。

## 受け入れ条件

1. T1 の PIN 更新後、rake ti:test green / linux ビルド通過 (arena 属性の
   既定不変を確認)。
2. T2: S3・P4 とも esp32 ビルド通過。**arena 16KB が内蔵 RAM から
   PSRAM へ移ったことを数字で示す**。
3. T3: Tab5 実機で 1-6 が動作 (スクリーンショット)。実機 ti_lat 記録。
4. T4: S3 ビルド通過 + ブート健全 + 内蔵 RAM が予算内 (または Modern 限定
   ゲートを入れて S3 は ti 無効でビルド通過)。判断と数字が report に。

## やらないこと (P5 の範囲外)

- ディレクトリ 1 クリック移動 (別件・保留中)。
- 全 API の長文ヘルプ執筆 (P4.5 の範囲どおり、以後は随時)。
- PC 側 LSP/MCP (P6)、WebConsole 展開 (P7)。
- 上流 PR (fork には返せる修正が貯まっているが、出すのは別判断)。
- arena を「外から渡す実行時 API」への作り替え (今回は属性注入で足りる。
  将来サイズを可変にしたくなったら再検討)。
