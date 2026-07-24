# Phase 4 完了レポート: system_desktop の Spinel 化 (Linux)

## サマリ

- **目標達成**: system_desktop (~4,200 行 + 13 mixin) を Spinel AOT で C 化し、
  mruby カーネルの下で NATIVE インスタンスとして駆動。**混成構成 (mruby kernel +
  Spinel desktop) と 2-Spinel 構成 (kernel も desktop も Spinel) の両方が 1 プロセスで
  同居・完全動作**を Linux headless で実証。壁紙/メニュー/時計描画、クリックでの
  ドロップダウン展開、クロスインスタンス HID ルーティング、launcher の /app スキャン
  (29 apps) + アイコン描画、二行ラベル、例外/ヒープ破壊ゼロ。
- **エンジン切替はアプリ単位**: `FMRB_KERNEL_ENGINE` と `FMRB_APP_ENGINE_DESKTOP` は
  独立。mruby-only 構成に回帰なし。
- **性能**: desktop の Ruby 実行時間は mruby 比で明確に短縮 (draw あたり ~2.0ms vs
  ~4.8ms)。転送 (present) は両者無視できる = ボトルネックは Ruby compute → AOT 投資は正当。
- **メモリ**: Spinel の live footprint は mruby の ~1.9x (64-bit object model)。launcher
  の boot 時 OOM を計測 (churn 支配) して every-icon GC + Linux のみ pool 1.5MB で解決。
- **fork へ 5 件のコンパイラ改善を報告・修正** (全て汎用 = upstream PR 候補)。Ruby 側の
  正当な回避と fork 弱点を都度切り分けた。
- **shell (T4-4) は未着手 (見送り)**、**T4-1 step3 (mruby 一本化) は延期**。下記
  「未完了・引き継ぎ」参照。soak は計画から除外 (ユーザ決定)。

構成フラグ:

| 構成 | FMRB_KERNEL_ENGINE | FMRB_APP_ENGINE_DESKTOP | ELF | 状態 |
|---|---|---|---|---|
| mruby-only | mruby | mruby | 5.8 MB | 回帰なし (基準) |
| 混成 | mruby | spinel | 10.8 MB | 完全動作 |
| 2-Spinel | spinel | spinel | 12.7 MB | 完全動作 (T4-0 acid test 合格) |

## タスク内訳

### T4-0: fork — マルチプログラム同一バイナリリンク対応 (完了・push 済)

生成 TU が entry 以外に ~26 グローバルを emit するため、kernel + desktop の 2 プログラムを
1 ELF にリンクすると多重定義になる問題を解消。全て sp_runtime.h 由来 (codegen 無改修)、
Phase 3 vtable と同じ per-ctx 間接化で対処。fork `fa2b38aa` (step1 `3d9774a1` データ、
step2 `e0fd1816` 関数)。**acid test**: 生成 TU 2 つが各々 entry シンボル 1 個のみ
non-static global として公開、交差ゼロ → 2-Spinel 構成で多重定義なしを実リンク+nm で確認。

### 事前作業: import + MC 配線 + estalloc + kernel 配線 (完了)

- MC 配線: `-DSP_MULTI_CTX -include sp_mem_override.h` を fmrb_spinel_rt に **PRIVATE のみ**
  (INTERFACE は main 全体に波及して FFI 境界の malloc が estalloc に誤ルート → crash)。
  生成 C だけ `set_source_files_properties` で per-file 付与。
- host shim: `components/fmrb_spinel_rt/fmrb_spinel_host.c` (spinel_rt 外なので import で
  消えない)。`fmrb_spinel_instance_begin/_end` の plain-C API。sp_ctx を隔離。
- estalloc: タスク mempool を `est_init` → `ctx->est` → cfg フックに `est_calloc/realloc/free`
  (zero-fill で alloc 契約充足)。ps は既存 `mrb_get_estalloc_stats` 経路で統計取得。

### T4-1: FmrbApp / FmrbGfx の FFI シム (完了)

mrb 非依存の C ABI シム。`main/app/fmrb_spx_app.c` (26 メソッド) + `fmrb_spx_gfx.c`
(FmrbGfx 描画 API)。canvas_id は int 引数、構造化戻り値は固定 LE `:binstr` レコード
(バイト長 `sp_net_bin_len`、空 String = nil)。gfx cmd は host_task GFX queue へ。

### T4-2: Spinel 用ベースクラス (完了)

`fmrb_app_ffi.rb` (FFI 宣言: FmrbSpxApp 26 / FmrbSpxGfx 44) + `fmrb_app_base_spinel.rb`
(~1,200 行)。Log/Machine/SpxBytes/FmrbGfx (描画+font cache+text_width+sprite)/GfxBlock
(即時モード)/FmrbApp (lifecycle + _spin poll 化 + HID bytes→event hash 手パース +
APP_CONTROL dispatch + set_timer + ps/heap/config/wallclock/wifi/usb_devices を binstr パース)。

### T4-3: system_desktop の Spinel ビルド + spawn 統合 (完了)

`gen_app_combined.rb` で combined 生成、`FMRB_APP_ENGINE_DESKTOP` を kernel engine と独立に
end-to-end 配線。`fmrb_app_spawner.c` に spinel_desktop_native (estalloc instance) + engine
switch。`fmrb_spx_common.c` に `sp_net_bin_len` + board_millis + log_write を移動し **混成でも
desktop がリンク可**に。混成 → 2-Spinel の順で実証。

## FmrbApp / FmrbGfx メソッド対応表

シム関数 (FFI 宣言) 数: **FmrbSpxApp = 26, FmrbSpxGfx = 44**。ベースクラスにはこれに加えて
純 Ruby 実装 (色変換 `rgb_to_332`/`hsv_to_rgb`、font width/caching、HID event パース等)
がある。方式の別:

| 種別 | 実装方式 | 例 |
|---|---|---|
| 下層 API 直呼び | C シム (mrb 非依存 ABI) | canvas 作成/描画/present/get_pixel/mask/image、recv_message、send_message、ps/heap/config/wallclock/wifi/usb |
| 純 int 演算 | C シム省略 → base で純 Ruby 再実装 | `rgb_to_332`, `hsv_to_rgb`, `text_width` |
| framework | base で Ruby 実装 | lifecycle (on_create/on_update/on_control)、_spin poll ループ、set_timer (@_timers)、HID event 手パース |
| esp32 専用枝 | Linux で `#ifdef` 除外 (esp32 は Phase 5) | hw_proxy/heap_caps/wifi 実体 |

未使用で省略: なし (desktop が実際に使う API は全て結線済)。T4-1 step3 (mruby 版
`fmrb_app.c`/`gfx.c` を `fmrb_spx_*` へ一本化) は回帰回避で**延期**。

## fork へ報告/修正したコンパイラ問題 (Ruby 正当 vs fork 弱点の切り分け)

Phase 4 の落とし穴どおり、コンパイルエラー毎に「Ruby の正当な曖昧さ (回避が正しい)」と
「fork の推論/codegen 弱点 (fork 修正が正しい)」を判定した。

### fork で修正した弱点 (全て汎用・upstream PR 候補)

| commit | 問題 | 修正 |
|---|---|---|
| `fa2b38aa` | 生成 TU の非 entry グローバルが 2 プログラムで多重定義 (T4-0) | per-ctx 間接化 (TU 関数/データを sp_ctx へ) |
| `9474d92` / `8a298cb` / `286de9b` | **poly 値が `const char*` (`:str`) 引数へ coercion 無しで流入** (sprintf/format/File.open/string op-write)。GC-root 圧下では初期化子に GC-root 文が落ちて壊れた C | `:str` 引数位置で `emit_str_expr` 経由 `sp_poly_to_s` coercion + format expr を temp を開く前に emit |
| `44e2d57` | `source_references_set` がコメント/文字列リテラル内の `Set` も検出し、壊れた bundled set.rb を暗黙 require | コメント/引用符をスキップする state machine (`#{...}` 補間は code 走査) |
| `e2497db` | Spinel の File/Dir が生 POSIX パスを使い fmrb HAL 仮想パス解決を通らない (launcher /app scan 不能) | sp_ctx に VFS backend フック (open/read/write/seek/tell/close/stat + opendir/readdir/closedir)。NULL=POSIX default で byte 同一 |
| `d26c1f9` | **poly レシーバの `String#byteslice` が dispatch されず NoMethod raise** (launcher 二行ラベルでクラッシュ)。ljust/rjust/center (U-1) と同族の gap。`.to_s` は結果に付いていて受信側 poly を直せていなかった | `is_strjust` と同型の SP_TAG_STR pre-arm + TY_STRING 戻り推論。test 追加 |

全コミットで `make test` (1991→1993 pass / 1 fail=既知 nilclass_bool_ops), `make bench` 58/0
を通過。fork HEAD = **d26c1f9** (origin/fmrb-dev, push 済)。

### Ruby 側で回避 (Ruby の正当な記法・dual-safe)

`.to_s`/`.to_i` 型固定 (poly→具体型が必要な箇所)、`Integer#chr`→`setbyte` (sp_str_chr 未実装)、
bare `Set` 回避 (自動 require 回避)、poly `Array#delete`→明示ループ、void-return メソッドの
末尾 `nil`、`partition`→明示ループ、`SpxBytes.name`→`read_name` (組み込み `Module#name` 衝突)、
`on_control` 既定 no-op 定義、GfxBlock の escaping-proc ローカルキャプチャ回避 (即時直描き)。
詳細は `ruby_writing_constraints.md`。

## 性能比較

### desktop draw 内訳 (T4-5 first pass、Ruby 側 vs present を分離計測)

| 構成 | Ruby compute / draw | present (gfx submit) | max draw |
|---|---|---|---|
| mruby desktop | ~4.8 ms | ~0.05 ms | 14 ms |
| Spinel desktop | **~2.0 ms** | ~0.03 ms | **3 ms** |

**確定知見 (workload 非依存)**: (1) present は両者無視できる = 転送はボトルネックでなく
**Ruby compute 支配 → Spinel 投資は正当** (計画の懸念は該当せず)。(2) Spinel の max-draw が
3ms vs mruby 14ms = **遅延分散が小さい** (mruby の spike は GC pause 由来、AOT が有利)。

### メモリ (launcher 29-icon load、estalloc `used` を forced-GC 前後で分離)

| 指標 | mruby | Spinel | 比 |
|---|---|---|---|
| true live (全 29 icon 後、forced GC) | — | 678 KB (平坦、+9KB) | — |
| baseline live (非 forced) | 389 KB | 728 KB | **1.9x** |
| churn / GC サイクル | 6 KB | 66 KB | **11x** |

**判定=churn 支配**: true live は 678KB で平坦 (pool に収まる)。OOM は icon parse/draw の
transient garbage が headroom を burst 超過するもの。headroom が薄い根因は Spinel の 1.9x
live overhead (64-bit object header/pointer)。**修正**: (a) `ensure_icon_sprites` の icon loop
で毎 icon 明示 GC (churn 対策、pool 非拡大)、(b) Linux のみ SYSTEM_APP pool を 1.5MB へ
(ESP32 800KB 据え置き)。両者で live 678KB が pool の ~45% = mruby 並み headroom に回復。
詳細 `memory_model_findings.md`。

## 検証 (Linux headless)

`tools/dev_run_check.sh` (ヘッドレス compose) + `fmrb_screenshot.py` (SHM→PNG) +
`fmrb_input.py` (合成マウス/キー注入) で自律検証。確認したシナリオ:

- boot → 壁紙/メニューバー/時計描画 (混成・2-Spinel 両方)
- メニュークリック → ドロップダウン展開 (入力ルーティング)
- **クロスインスタンス messaging** (Spinel kernel → Spinel desktop HID routing)
- launcher: /app scan で 29 apps 検出 (VFS 経由、mruby parity)、アイコンスプライト 29 生成、
  二行ラベル (poly byteslice) が全 long-label で正常
- boot 5+ 回 OOM ゼロ、例外/ヒープ破壊ゼロ、`Resources cleaned up=0`

スクリーンショットは検証時に scratchpad へ都度生成 (ephemeral)。`dev_run_check.sh --keep`
+ `fmrb_input.py click 20 5 sleep 700 click 30 14` で再現可能。

## shell (T4-4) の扱い

**未着手 (見送り)**。desktop までで Phase 4 のコア目標 (kernel + desktop 2 インスタンス、
エンジン切替、性能/メモリ計測) は達成済み。shell の Spinel 化は T4-1/T4-2 に shell 用シム
追加が必要で、`$stdout`/`$LOAD_PATH` 等グローバルの poly 化リスクもある。**shell は mruby の
まま**とし、Spinel 化するか否かは Phase 5 以降にユーザ承認を得て判断する (受け入れ基準 6 は
「見送り + 根拠」として本レポートに記録)。

## 受け入れ基準の充足状況

| # | 基準 | 状況 |
|---|---|---|
| 1 | kernel+desktop 2 インスタンス + 混成起動 | **達成** (混成・2-Spinel 両方) |
| 2 | shell の IRB/スクリプトが従来同等 | shell は mruby 維持 (回帰なし)。Spinel 化は見送り |
| 3 | desktop Ruby 時間が mruby 比で短縮 (数値) | **達成** (2.0 vs 4.8 ms、内訳計測済) |
| 4 | エンジン切替がアプリ単位 + mruby 回帰なし | **達成** |
| 5 | shell の扱いと根拠をレポート化 | **達成** (上記、見送り + 根拠) |

(旧基準 4「soak 30 分クリーン」は計画から除外 = ユーザ決定。)

## Phase 5 (ESP32) への引き継ぎ

- **fork import**: SPINEL_PIN = d26c1f9 (origin/fmrb-dev)。ESP32 ビルドでも
  `-DSP_MULTI_CTX -include sp_mem_override.h` を **snapshot ビルドと生成 C の両方**に
  一致させる (片側だけは silent ABI break)。SPINEL_PIN と IMPORT_INFO の commit 一致必須
  (`spinel:gen` が警告)。
- **メモリ (ESP32 サイジングは別途)**: Linux は 64-bit で ~1.9x live のため SYSTEM_APP を
  1.5MB にしたが、**ESP32 (32-bit) は 800KB 据え置き**。32-bit 実機での live 実測 +
  `sp_gc_hdr` スリム化を経てサイジングすること。every-icon GC (launcher) は engine 非依存で
  残置 (ESP32 のタイト pool でむしろ必要)。
- **root_stack**: default `SP_GC_STACK_MAX` は idle でも予約されるため ESP32 で要縮小
  (Phase 3.5 の follow-up)。
- **VFS**: fmrb HAL backend は ESP32 littlefs にそのまま接続 (Linux で設計・実証済)。
  ESP32 に POSIX FS が無いため VFS フックは Phase 5 の必須基盤。
- **バイナリサイズ**: 2-Spinel 12.7MB (Linux)。ESP32 flash では要計測 (Phase 5 で対策検討)。
- **未計測**: 実機 HID latency、NTSC 実出力、音声。ヘッドレスで確認不能なものはユーザ確認。

## ツールチェーン注意 (Phase 4 で判明)

- `rake spinel:setup` の **stale vendor 更新が壊れている** (`git fetch origin <短縮SHA>` が
  "couldn't find remote ref" で失敗)。回避 = `cd vendor/spinel && git fetch origin fmrb-dev
  && git checkout --detach <pin> && make`。fresh clone 経路は動く。**修正候補**: Rakefile の
  fetch を branch fetch 化。
- plain build (SPINEL_DIR 無し) は vendor を優先し、vendor が pin より古いと**黙って旧
  compiler で生成 → バグ C** (divergence は WARNING 止まりで abort しない)。vendor を pin へ
  更新するか `SPINEL_DIR=tmp/spinel` を明示すること。

## Phase 4 判定

**完了 (ユーザ決定)**。desktop が mruby の drop-in 置換として Spinel で全機能動作、混成と
2-Spinel 両構成を Linux headless で実証。性能 (Ruby compute 短縮 + 低遅延分散) とメモリ
(1.9x overhead を計測して pool/GC で解決) を数値化。fork へ 5 件の汎用修正。残 =
shell 化判断、mruby 一本化 (T4-1 step3)、および Phase 5 (ESP32 実機ポート)。
