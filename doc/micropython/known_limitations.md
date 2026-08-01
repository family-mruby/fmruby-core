# Python (MicroPython) アプリの制限事項

Family mruby 上の Python アプリでできないこと・気をつけることをまとめる。
Ruby アプリ (picoruby-fmrb-app) との差分という形で書く。

対象: MicroPython v1.28.0 を `ports/embed` 経由で取り込んだ構成。

## 実行環境

### Python アプリは同時に 1 本だけ

MicroPython は VM の状態をすべてグローバル変数 (`mp_state_ctx`) に持っており、
mruby の `mrb_state` や Lua の `lua_State` のように複数インスタンスを作れない。
本家がその設計なので、こちらで多重化するのは改造量が割に合わない。

2 本目を起動しようとすると spawn の時点で拒否され、デスクトップに
「Another Python app is already running.」のエラーダイアログが出る。
1 本目が終われば普通に起動できる。

**Ruby / Lua / BASIC アプリとの同時実行は制限なし。**

### import はビルトインモジュールのみ

ファイルシステムからの import は用意していない。`import mymodule` は失敗する。
アプリは 1 ファイルで完結させる。

使えるモジュールは MicroPython 本体の `py/` にあるものだけ:

`array` / `builtins` / `collections` / `gc` / `io` / `math` / `micropython` /
`struct` / `sys`

**`extmod/` にあるモジュールは使えない**: `time` / `json` / `os` / `re` /
`random` / `binascii` / `hashlib` / `heapq` / `deflate` など。
組み込み用ソース一式にこれらの実装が含まれないため。待機は `FmrbApp` の
更新間隔 (`on_update` の戻り値) で行う。

### 資産を活用したくなったときの道筋 (現状は未実装)

MicroPython 圏の資産は、種類によって取り込みやすさがまったく違う。

1. **extmod の標準モジュール** — 仕組み上の壁は無い。port/Makefile に
   追加コピーを足して mpconfigport.h で有効化し `rake micropython:gen`、
   という既存の手順で 1 モジュールずつ入れられる (submodule 無改変のまま)。
   `json` / `random` / `binascii` あたりは自己完結で軽い。`asyncio` は
   tick 系 HAL (mp_hal_ticks_ms 等) の実装が要るのでもう一段の作業。
   コストはフラッシュ増と qstr 再生成のみで、必要になったものから足せばよい。
2. **純 Python ライブラリ (micropython-lib 等)** — 今はアプリ 1 ファイルに
   貼り込むしかない。ファイルシステム import (mp_import_stat + reader を
   fmrb_hal_file に繋ぐ) を実装すれば `import` できるようになるが、
   多くのライブラリが上記 extmod に依存する点に注意。
3. **ESP32 ハードウェア制御 (`machine.Pin` / I2C / SPI / `network` 等)** —
   **そのままは持ち込まない**。これらは ports/esp32 のモジュールで、
   「MicroPython がチップを専有する」前提で ESP-IDF ドライバを直接叩く。
   fmruby-core では周辺資源 (ピン割当、I2C、通信リンク) を OS が所有して
   いるので、直輸入はゲストによる資源管理の迂回になる。やるなら Ruby アプリと
   同じ fmrb サービス経由で `_fmrb` ブリッジに関数を足し、その上に
   machine 風の薄い Python クラスを被せる (ドライバ実体はどうせ共通の
   ESP-IDF なので、流用する価値があるのは API の形だけ)。

### REPL なし / スレッドなし

対話実行 (REPL) は無効。`_thread` も無効 — タスクの生成は OS 側の仕事で、
ゲスト VM には渡さない。

`_thread` を将来入れない方針の理由も書いておく。MicroPython のスレッドは
VM 内の擬似スレッドではなく**ネイティブスレッドに 1:1** で、FreeRTOS 環境では
`_thread.start_new_thread()` のたびに本物の RTOS タスクが生える
(実装はポート層。py/modthread.c 自体は生成物に入っており、port 側の
スレッド層を書けば有効化はできる)。しかし (1) ゲストが fmrb_app の管理外の
タスクを生やすことになりタスクモニタ・reaper・kill の統率から見えない、
(2) 停止時に「全ゲストスレッドを終わらせてから deinit」の面倒を見ないと
解放済みヒープを触るタスクが残る、(3) GIL が既定で有効なので並列実行には
ならずインターリーブしか得られないのに、スレッドごとに内蔵 RAM から
タスクスタックを食う。アプリ内の並行処理はジェネレータ (yield) による
協調的な形が今の構成のまま動くので、そちらを使う。
方式の対比 (mruby-task = VM 内スケジューラとの違い) は
porting_comparison.md 参照。

### `open()` は必ず失敗する

`open` は builtins と `io` に存在するが、呼ぶと `OSError` を投げる。
ゲストアプリにファイルシステムを渡さない方針のため、
「黙って存在しない」より「呼ぶと明示的に失敗する」形にしてある。
`io.StringIO` などメモリ上のものは使える。

## メモリ

### GC ヒープは 1 アプリあたり 256KB 固定

アプリ用メモリプール (500KB、PSRAM) から一塊で確保する
(`FMRB_MP_HEAP_SIZE`、components/micropython/include/fmrb_mp.h)。
アプリ起動時に確保し、終了時にプールへ返す。異常終了しても
プールごと破棄されるので取り残しは出ない。

起動直後の内訳 (ESP32-S3 実機):

| 項目 | 値 |
|---|---|
| GC が管理する総量 | 258,048 B (GC 自身の管理情報が 4,096 B) |
| アプリ基盤 (FmrbApp / FmrbGfx) の消費 | 8,496 B |
| ユーザスクリプトが使える目安 | 約 244KB |

アプリ基盤は Python ソースのままファームに入っており、アプリ起動のたびに
コンパイルされる。**その分アプリの起動が 64ms 遅くなる** (S3 実機実測)。

**使い切ると `MemoryError`** が上がる。捕まえなければ未捕捉例外として
traceback がログに出てアプリが終了する (Ruby アプリのメモリ不足と同じ扱い)。

### 内蔵 RAM の消費はごく小さい

MicroPython コンポーネントの静的 RAM は **445 B** (DIRAM .bss) のみ。
GC ヒープの元になるプールは PSRAM (`EXT_RAM_BSS_ATTR`) にあるので、
内蔵 RAM 予算には効かない。

### C スタックは 16KB のタスクスタックに収まる

パーサとコンパイラが C スタックを再帰するが、S3 実機のデモアプリで
ピーク 4,660 B (アプリタスク 16,384 B の 28%)。上限は起動時の残量から
自動で決まり (実測 12,092 B)、超えると `RuntimeError: maximum recursion
depth exceeded` になる。

## 停止 (アプリの終了)

### 通常の停止

閉じるボタンやタスクモニタからの停止要求は、`_fmrb.spin()` の待機を
即座に破って `_handle_system_control` に届く。そこから `stop()` が呼ばれ、
`main_loop` が抜けて `destroy()` -> `on_destroy()` が走る。

待機は 100ms ごとに区切って停止フラグを見直しているので、
`on_update` が何秒を返していても停止要求は待たされない。

### 強制停止では `on_destroy` が走らない

Python コードが長いループを回している最中の停止は、VM のフックが
`mp_sched_vm_abort()` を呼んで**バイトコードの実行を巻き戻す**形で行う。
このとき `destroy()` も `on_destroy()` も通らない。

- キャンバスとメッセージキューは C 側の後始末が回収するので、資源は漏れない。
- ただし**アプリは `on_destroy` での後始末を当てにできない**。
  保存などが要るなら `on_update` の区切りで行う。

Lua アプリも同じ性質なので、Python に固有の制限ではない。

### 停止が効くまでの粒度

VM フックはバイトコードを実行しているときだけ回る (ジャンプ 100 回に 1 回、
停止フラグを確認)。**C の中でブロックしている間はフックが回らない**ため、
`_fmrb.spin()` は自前で 100ms ごとに確認している。
将来 C 側でブロックする API を足すときは、同じ確認を入れる必要がある。

## 提供している API

Ruby 版 (`picoruby-fmrb-app`) の第一段階サブセット。クラス名・メソッド名・
引数順・色定数は Ruby 版と同じ。

### FmrbApp

| 種別 | 提供しているもの |
|---|---|
| 属性 | `name` `platform` `window_width` `window_height` `pos_x` `pos_y` `fullscreen` `rounded_corners` `headless` `canvas` `bg_canvas` `gfx` `bg_gfx` `running` `user_area_x0/y0/x1/y1` `user_area_width/height` |
| ライフサイクル | `start` `stop` `main_loop` `destroy` / `on_create` `on_update` `on_event` `on_destroy` |
| 描画 | `draw_window_frame` `clear_user_area` |
| 入力補助 | `ev_ctrl` `ev_shift` `ev_alt` |
| その他 | `send_message` `set_window_position` |

### FmrbGfx

`clear` `present` `set_pixel` `draw_line` `draw_rect` `fill_rect`
`draw_circle` `fill_circle` `draw_round_rect` `fill_round_rect`
`draw_ellipse` `fill_ellipse` `draw_triangle` `fill_triangle`
`draw_text` `text_width` `font_height`

色定数: `BLACK` `WHITE` `RED` `GREEN` `BLUE` `YELLOW` `CYAN` `MAGENTA` `GRAY`
(RGB332、Ruby 版と同値)

### イベント

`on_event(ev)` に渡る辞書のキーは Ruby 版のハッシュと同名。

| 種別 | キー |
|---|---|
| `mouse_down` / `mouse_up` | `type` `button` `x` `y` |
| `mouse_move` | `type` `x` `y` |
| `key_down` / `key_up` | `type` `keycode` `scancode` `modifier` `character` |

文字キーの判定には `scancode` (HID Usage ID) を使う。`keycode` は
シミュレーションと実機で違う値になる。

### Ruby 版にあって Python 版に無いもの

呼ぶと `AttributeError` になる (存在だけ作って無言で何もしない、はしていない)。

| 分類 | 内容 |
|---|---|
| スプライト | `SpriteImage` / `SpriteInstance` / タイルマップ / `TileRing` |
| 画像 | `create_image` `load_image` `draw_image` `draw_tile` / BMP / マスク |
| 文字 | `set_font` (日本語含む) `set_text_size` / 混在描画 / 多バイト文字幅 |
| 描画最適化 | `GfxBlock` / composite region / `set_viewport` (ハードウェアスクロール) |
| ライフサイクル | `on_suspend` `on_resume` の呼び出し / `on_resize` / `request_reload` |
| 連携 | `subscribe` `publish` / `request_file_select` / `request_run` |
| その他 | 音声 / `draw_arc` `fill_arc` `blend_rect` `get_pixel` / 追加キャンバス / スクロールバー / タイマ / p5 互換層 |

日本語の表示ができないのは `set_font` が無いためで、当面の一番大きな差分。

## 見た目の差

ウィンドウ枠は Ruby 版と同じ絵になるが、描き方が違う。

- Ruby 版は `GfxBlock` で枠一式を 1 コマンドに畳んでいるが、Python 版は
  プリミティブごとに 1 コマンド送る (枠 1 回で 16 コマンド)。
  枠を描き直す頻度が低い分には問題にならないが、リサイズを多用する
  アプリでは差が出る。
- 角丸の合成領域 (composite region) 最適化は入れていない。見た目は同じで、
  graphics-audio 側の透過比較の負荷が高いだけ。
