# Phase 5 完了レポート: ESP32 実機ポート (ESP32-P4 / Tab5)

## サマリ

- **目標達成**: Spinel カーネル + Spinel desktop の 2 インスタンスが **ESP32-P4 実機で
  安定動作**。ブート → desktop 描画 → メニュー/ドロップダウン → launcher の /app スキャン
  (29 apps) + アイコンスプライト 29 個 → ユーザアプリ (Kamon, Monitor) の spawn →
  ウィンドウドラッグ → アイドル、まで一通りクラッシュせず到達した。
- **ターゲットは S3 ではなく P4 になった**。Phase 5 指示書は ESP32-S3 (Xtensa) 前提だったが、
  実機検証は ESP32-P4 (riscv32) で行った。32bit ゲート (`make test32`) が幅依存の問題を
  カバーしていたため、アーキ差による追加バグは出ていない。**S3 実機は未検証のまま**。
- **fork へ ESP32 対応 6 コミット**。うち 3 件は「ホスト前提の固定バッファが MCU では
  致命的」という同型の問題で、いずれも `#ifndef` ガードによるノブ化で解決した (下記)。
- **バグ 3 件を実機で発見・修正**。3 件とも症状が原因を強くミスリードする種類のもので、
  切り分けの記録に価値があるため詳述する。
- **未解決 1 件**: 画像が 1 枚も描画されない (壁紙・起動ロゴとも)。Spinel desktop ×
  P4 でのみ発生。Phase 6 に引き継ぎ。

## 到達状態

| | |
|---|---|
| ターゲット | ESP32-P4 (NARYAv4 / M5Stack Tab5 相当), IDF v5.5.4 |
| 構成 | `FMRB_KERNEL_ENGINE=spinel` + `FMRB_APP_ENGINE_DESKTOP=spinel` (2 Spinel インスタンス) |
| bin | 0x411700 (4,265,216 B) / factory 6M の 32% free |
| fork | `origin/fmrb-dev = 94c2f89a` |

## メモリ収支 (受け入れ基準 3)

### 内部 DRAM 静的消費 (`.dram1.bss`)

| | 修正前 | 修正後 |
|---|---:|---:|
| `.dram1.bss` 全体 | 192,168 | **88,392** |
| `system_desktop_combined.c.obj` | 74,880 | 43,136 |
| `fmrb_kernel_combined.c.obj` | 74,832 | 43,088 |
| `fmrb_spx_app.c.obj` | 42,766 | 1,021 |

**103,776 バイト回収**。内訳は `SP_DYN_SYMS_MAX` 8192→256 (31,744 × 2 本) と、
`fmrb_spx_app_config` の返信バッファ 41,745 B を PSRAM へ退避 (`EXT_RAM_BSS_ATTR`)。

### 実機 IRAM free (アイドル時)

2 Spinel 構成 (kernel + desktop とも Spinel) での推移:

| 状態 | IRAM free |
|---|---:|
| 修正前 (アイドル) | 実質ゼロ (196 B の DMA 確保が失敗) |
| 修正後 (アイドル) | 76,108 |
| + ユーザアプリ 1 (Kamon) | 50,548 |
| + ユーザアプリ 2 (Monitor) | 31,316 |

`FMRB_MAX_USER_APPS = 3` に対し、ユーザアプリ 2 つで IRAM free が 31 KB まで落ちる。
3 つ目を起動する余地は乏しく、ここが次のボトルネックである。

**mruby との比較は下記「混成ビルドとの比較」の数値を使うこと。**
過去の全 mruby 構成の実機記録 (IRAM free 163,372 B) は別コミット・別日のビルドであり、
機能構成も同一とは限らないため、本フェーズの数値と直接比較してはいけない。

回収余地としては、生成 TU 1 本あたり `sp_exc_*` 20,992 + `sp_catch_*` 21,312 = 42,304 B、
**2 本で 84,608 B が例外/catch スタック**である (map からの実測値なので、
これ自体は比較に依存しない事実)。深さを下げれば回収できるが、
後述の理由で今回は見送った (Phase 6)。

### VM プール実測 (今回追加した計装より)

| VM | エンジン | used | total | 使用率 |
|---|---|---:|---:|---:|
| `fmrb_kernel` | spinel | 85,272〜107,504 | 512,000 | 17〜21% |
| `system_desktop` | spinel | 205,552〜325,440 | 819,200 | 25〜40% |
| `Kamon` | mruby | 216,416 | 512,000 | 42% |
| `Monitor` | mruby | 181,656 | 512,000 | 35% |

**ESP32 プールのサイジングは現状で妥当**と判断できる。Phase 4 で保留にした
「32bit 実測後にサイジング」はこの数値をもって解決とする。なお Linux は 64bit の
オブジェクトモデルのため desktop プールを 1.5 MB に拡大しているが、
ESP32 は 800 KB 据え置きで 40% 以下に収まっており、拡大は不要。

## fork ESP32 対応 (受け入れ基準 5)

全て `#ifndef` ガードまたは `__has_include` / 機能マクロによる汎用ガードで、
デフォルト値は不変。ホストのプリプロセス結果は同一で、64bit テストは全パスを維持。

| commit | 内容 | 分類 |
|---|---|---|
| `a03386bb` | `SP_NO_MMAN` (sys/mman.h ガード)、`sp_PolyArray_flatten_n` の 32bit truncate 修正 | upstream PR 候補 |
| `73a2083d` | `make test32` (ILP32 ゲート) 新設 | upstream PR 候補 |
| `1242ad35` | bignum のクロス TU 境界を `intptr_t` に固定 (32bit SIGSEGV) | upstream PR 候補 |
| `53941f68` | FFI 可変長引数を自然幅で渡す (32bit SIGSEGV) | upstream PR 候補 |
| `4e33a004` | MMU-less / RTOS newlib 対応 (execinfo/ucontext/ioctl/poll) | upstream PR 候補 |
| `5221ee9e` | `File#winsize` を `TIOCGWINSZ` の定義有無で判定 | upstream PR 候補 |
| `ac038886` | `SP_NO_MMAN` 時に fiber root marker を参照しない | upstream PR 候補 |
| `d0f02326` | `SP_STACK_SCRATCH_MAX` + `make check-stack` ゲート | upstream PR 候補 |
| `b8e5a02c` | `SP_EXC_STACK_MAX` / `SP_CATCH_STACK_MAX` のノブ化 | upstream PR 候補 |
| `94c2f89a` | `SP_GC_MARK_STACK_MAX` のノブ化 | upstream PR 候補 |

**通底する教訓**: Spinel ランタイムはホストのヒープとスタックを暗黙の前提に、
固定サイズのバッファを各所に持っている。MCU ではそのどれもが「プール全体や
タスクスタック全体に匹敵する」サイズになりうる。移植で潰したのは以下の 4 種で、
いずれも同じ形 (`#define` を `#ifndef` ガードにしてポートが縮める) で解決した。

- スタック上の一時バッファ (`SP_STACK_SCRATCH_MAX`, 既定 65536)
- 実行時 intern シンボル表 (`SP_DYN_SYMS_MAX`, 既定 8192 エントリ)
- 例外 / catch ハンドラスタック (`SP_EXC_STACK_MAX` 等, 既定 64 × jmp_buf)
- GC マーク作業リスト (`SP_GC_MARK_STACK_MAX`, 既定 65536 エントリ)

## 実機で発見したバグ 3 件

3 件とも**症状が原因を強くミスリードする**種類だった。切り分けの手順を残す。

### 1. 「msgpack unpack failed」は msgpack のバグではなかった

desktop が最初の canvas 作成で失敗し、display 側が
`msgpack unpack failed` を出していた。フレーム破損 (32bit 固有の
シリアライズ差など) を疑うのが自然だが、**誤り**だった。

診断ログにデコード結果と `ret` を出したところ:

```
ret=-2 len=20 first20=[94 42 04 50 c4 0e 00 00 aa 01 00 00 f0 00 00 00 fe 00 01 01]
```

- `94`=fixarray(4) / `42`=type / `04`=seq / `50`=sub_cmd / `c4 0e`=bin(14) —
  期待形式に完全一致、長さも一致、ペイロードも canvas 426x240 (`aa 01` / `f0 00`) として正しい
- `ret=-2` は `MSGPACK_UNPACK_NOMEM_ERROR`。**パースエラー (-1) ではない**

真因は内部 DRAM 枯渇。`CONFIG_SPIRAM_USE_MALLOC` が未設定のため素の `malloc` は
内部 DRAM しか返さず、そこが Spinel の `.bss` 190 KB で埋まっていた。
同じ枯渇で SDIO ドライバも 196 B の DMA バッファを取れずに落ちており、
そちらが二次被害に見えていたが、実際は**同じ原因の別症状**だった。

**教訓**: エラーコードの意味を確認せずに症状名 ("unpack failed") から
原因を推定しない。`-2` の 1 文字が全てを決めた。

### 2. GC 自身がプールの半分を要求していた

修正 1 の後、実機はアイドル数分でも、ウィンドウドラッグでも
`unhandled exception: out of memory` でリブートした。バックトレース:

```
sp_oom_die ()            sp_gc.c:74
sp_mem_malloc (n=262144) sp_ctx.c:50
sp_gc_mark_all ()        sp_gc.c:119
```

`sp_gc_mark_all` がマーク作業リストを **65,536 エントリ = 32bit で 256 KB、
単一連続ブロックで遅延確保**していた。カーネルのプールは 512,000 B なので
**プールの半分**。TLSF に live が散った後は連続ブロックとして取れず、
`sp_mem_malloc` は NULL を `sp_oom_die()` = `exit(1)` に変換する。

皮肉なことに `sp_gc_mark` には**再帰フォールバックが元から実装されている**
(`if(stack && top<MAX){push}else{h->scan(obj);}`)。作業リストは
「あれば速い」最適化であって NULL でも動く設計なのに、確保失敗が
プロセスを殺していた。

計装後の実測でカーネルプールの使用率は 17〜21% と判明し、
**live set の圧迫ではなく連続ブロックが取れなかったことが原因**と確定した。

**教訓**: OOM を見たら「総量が足りない」と「連続領域が取れない」を区別する。
プール使用率が出ていれば一目で切り分けられたので、計装を先に入れるべきだった。

### 3. 例外スタックを縮めたら GC ルートが壊れた

内部 DRAM をさらに削るため `SP_EXC_STACK_MAX` を 64→32 にしたところ、
desktop の config ダイアログ開閉で**偽の** `out of memory` が出て落ちた。
64 に戻すと同一操作で完走する。

原因は、生成 C の begin フレーム push (`sp_exc_top++`) に**境界チェックが無い**こと
(`sp_rescue_push` は溢れると `exit(1)` するのに、begin 側は素通り)。
溢れた書き込みが隣接 `.bss` の `sp_exc_rootmark` — GC のルート管理 — を破壊し、
偽のアロケーション失敗として現れていた。

**教訓 (重要)**: 「タスクスタック 24 KB / Ruby フレーム 2-6 KB だから
数段しかネストしない」という見積もりは**誤り**だった。ネスト深さは
呼び出しグラフの性質であって、スタックサイズからは導けない。
**計測せずに縮めない**。

なお、この値を Linux にも一律適用する設計にしていたため、**実機ではなく
headless テストで即座に露見した**。ポート専用フラグにしていたら実機まで
持ち越していた。dual build の分岐を作らない方針が効いた例である。

## 検証の穴 (記録)

ドラッグ時のクラッシュは **headless 検証で再現できなかった**。
入力注入 (`tools/fmrb_input.py`) のクリックでは、実マウスがドラッグ中に流す
連続的な移動イベントのレートが出ないためである。カーネルは 1 イベントごとに
確保するので、このレート差がそのままプール圧の差になる。
headless で検証できない領域として記録しておく。

## 受け入れ基準の達成状況

| # | 基準 | 状態 |
|---|---|---|
| 1 | mruby ESP32 ビルドに回帰なし | 達成 (Linux/ESP32 とも mruby 構成でビルド・動作確認) |
| 2 | 実機で安定動作、レイテンシと GC 停止時間が mruby 比で改善 | **部分**: 安定動作は達成。**数値計測は未実施** → Phase 6 |
| 3 | メモリ収支の表 | 達成 (上記) |
| 4 | 複数インスタンス同居が実機で安定、soak クリーン | **部分**: 同居は達成。**soak 未実施** → Phase 6 |
| 5 | fork の ESP32 対応が汎用ガード、64bit テスト全パス | 達成 |
| 6 | `rake spinel:doctor` clean / RTC 方式の決定 | **未** → Phase 6 |
| 7 | ホスト依存の棚卸しが backend/ガード済 | 達成 (`esp32_host_deps_sweep.md`) |

## 混成ビルド (mruby desktop + Spinel kernel) との比較

`FMRB_KERNEL_ENGINE=spinel` + `FMRB_APP_ENGINE_DESKTOP=mruby` でビルドし、
同一実機・同一表示経路で desktop エンジンだけを入れ替えた比較を取った。
**表示側 (display_p4 / PPA 合成 / 画像デコード) が完全に同一**なので、
engine のみを隔離した公平な比較になっている。

### 起動時間 (受け入れ基準 2 の一部)

| | mruby desktop | Spinel desktop | 比 |
|---|---:|---:|---:|
| `/app` スキャン 29 件 | 9.68 s | 2.76 s | **3.5x** |
| アイコンスプライト 29 個生成 | 2.19 s | 0.66 s | **3.3x** |
| カーネル起動 → 壁紙描画完了 | 17.17 s | 5.73 s | **3.0x** |

**起動が 17 秒から 6 秒になる**。体感で分かる差であり、AOT の実利として
最も説明しやすい数値。mruby 側の数値は以前に別途取った実機記録
(スキャン 9.5 秒 / スプライト 2.2 秒 / 壁紙まで約 17 秒) と一致しており、
測定の再現性も確認できている。

### 内部 RAM — 静的コストは実行時に相殺される

| | mruby desktop | Spinel desktop |
|---|---:|---:|
| `heap_init` 時点の内部 RAM | 342 KiB | 297 KiB |
| 実行時 IRAM free (アイドル) | 73,572 B | **76,108 B** |

起動時点では Spinel ビルドが 45 KiB 少ない (生成 TU の `.bss` 43,136 B が
乗っているため)。**にもかかわらず実行時は Spinel の方が 2,536 B 多く空いている**。
つまり mruby desktop VM は実行時に内部 RAM を約 47 KB 消費しており、
**Spinel の静的 `.bss` コストはそれで相殺されている**。

「AOT はメモリを食う」は 64bit Linux の live footprint 比較 (1.9 倍) から得た
結論だったが、**実機の内部 RAM 収支に限れば実質同等 (むしろ Spinel が僅かに有利)**
というのがこの計測の答えである。1 点計測なので断定はしないが、
移植の是非を判断する材料としては重要。

### VM プール — 単純比較はできない

| | used |
|---|---|
| mruby desktop | 381,472 / 381,016 (ほぼ一定) |
| Spinel desktop | 205,552 〜 325,440 (変動) |

**GC のトリガ条件が違う**ため (Spinel は pool/32 で頻繁に回す)、
この数値をそのまま live set の比較に使ってはいけない。
比較するなら両方で強制 GC 直後を採る必要がある。
ただし少なくとも **32bit 実機で Spinel が mruby より明確に大食いという
現象は起きていない**。64bit Linux で測った 1.9 倍とは異なる絵になっており、
再計測の価値がある (Phase 6)。

### タスクスタック

desktop タスクの空き: mruby 16,476 B / Spinel 11,556〜14,540 B。
**生成 Ruby メソッドのフレームが大きい**という 1.1 節の実測と整合する。

## 未解決: 画像が 1 枚も描画されない

**症状**: 壁紙も起動ロゴも表示されず、画面は `clear` の色 (白〜淡色) のまま。
リモートデスクトップ (ブラウザ) でも同じなので、合成結果に入っていない。
**mruby desktop では表示される**。**Linux の Spinel desktop でも表示される**。
つまり Spinel desktop × P4 の組み合わせでのみ発生する。

ログ上は成功している:

```
CREATE_IMAGE_FROM_FILE: /flash/boot/boot.png     -> id=1 200x200
DRAW_IMAGE: id=1 -> canvas=2 (113,20) 200x200
CREATE_IMAGE_FROM_FILE: /flash/data/bg_426x240.png -> id=2 426x240
DRAW_IMAGE: id=2 -> canvas=2 (0,0) 426x240
```

寸法は正しく返っており、`image_store_find` も成功している (失敗時は
`DRAW_IMAGE: image %u not found` が出る)。

**エンジン間で同一と確認済み (容疑者から除外)**:

- canvas 作成パラメータ: 実機ログの `transp=1/1` (透過色 1 / 透過 ON) と
  `transp=0/0` は、mruby `app.c` の `0x01` 指定と一致
- `present`: 両エンジンとも `transparent_color=0xFF` をハードコード
  (`host_task.c` が `!=0xFF` で `use_transparency` を決める)
- `TRANSPARENT_COLOR = 0x01` 定数、`clear` → `GFX_CMD_CLEAR` の割り当て
- `CREATE_IMAGE_FROM_FILE` のコマンド構築 (canvas_id / path_len / path)
- desktop の Ruby ソース (dual build で共有)

**残る仮説は 2 つ**で、両者は排他:

- (a) 画像のデコード結果が canvas 2 に書けていない (画像経路の問題)
- (b) 画像は書けているが canvas 2 が合成されていない (合成経路の問題)

「起動ロゴも壁紙も出ない」= 画像を使う描画が 2 つとも失敗し、
`clear` の色だけが見えている、という点は (a) (b) どちらとも整合する。

### 切り分け結果: 表示側は無罪、Spinel desktop 固有と確定

上記の混成ビルド (mruby desktop + Spinel kernel) で**壁紙も起動ロゴも正常に表示された**。
同一実機・同一の display_p4・同一の画像ファイル・同一のカーネルで、
**desktop エンジンだけが違う**構成なので:

- display_p4 の PNG デコード、`pushSprite`、PPA 合成、DSI 出力 — **すべて無罪**
- canvas 2 の合成そのものも正常に機能している
- **問題は Spinel desktop が送るコマンド列、またはその時点の状態にある**

つまり (b) の「canvas 2 が合成されない」は、表示側の欠陥としては否定された。
残るのは「Spinel desktop 側が canvas 2 を正しく present していない」か、
「(a) 画像経路」のいずれか。

### ログ差分から得た手がかり

両ビルドのログを突き合わせると、`DRAW_IMAGE` の行そのものは
**完全に同一** (`id=1 -> canvas=2 (113,20) 200x200`) である。
一方、以下が mruby 版にのみ存在する:

- `app: Created background canvas 2 for app system_desktop`
  — C 側が背景 canvas を生成し、その id を Ruby へ引き渡した記録
- `gfx: FmrbGfx.new called: canvas_id=2` — `@bg_gfx` が canvas 2 で生成された記録
- `display_p4_vm: DEFINE_PROG ok: id=0/1 canvas=1` — 表示側 VM へのプログラム登録

Spinel 版にはこれらに相当するログが無い。
ログを出していないだけの可能性もあるが、**`@bg_gfx` が指す canvas と
`present` の対象 canvas を確認する**のが次の一手として最も筋がよい。
`DRAW_IMAGE` が canvas=2 に届いている以上、描画先は正しいので、
疑うべきは **present 側**である。

## 引き継ぎ

- **Phase 6** (`doc/spinel_aot/phase6.md`) に未完了項目を集約:
  画像描画の解決、例外スタックの計測と縮小 (~65 KB)、性能計測、soak、
  `spinel:doctor` / RTC、S3 実機。
- fork = `origin/fmrb-dev = 94c2f89a` (push 済)。
  upstream PR 候補は `reports/fork_pr_candidates.md` を参照。
- 実機の書き込みは `rake check-port` → `rake flash` → **物理ボタンでリセット**
  (Tab5 は DTR/RTS が効かない) → `rake monitor`。
