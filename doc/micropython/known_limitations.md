# Python (MicroPython) アプリの制限事項

Family mruby 上の Python アプリでできないこと・気をつけることをまとめる。
Ruby アプリ (picoruby-fmrb-app) との差分という形で書く。

対象: MicroPython v1.28.0 を `ports/embed` 経由で取り込んだ構成。

## Python アプリの書き方 (Ruby との差分)

```python
# 共通層 (FmrbApp / FmrbGfx / FmrbAudio / SpriteImage / SpriteInstance /
# Log / ticks_ms / language) はアプリの名前空間に用意済み。import は要らない。
import _fmrb          # 低水準の口。read_file などはここから

class MyApp(FmrbApp):
    def on_create(self):
        self.gfx.set_font(FmrbGfx.FONT_JA, 12)
        self.audio = FmrbAudio(self)
        self.subscribe("mytopic")
        self.set_timer(500, self.blink)      # 一回限り。続けるなら自分で張り直す

    def on_update(self):
        return 33                            # 次に呼ぶまでのミリ秒

    def on_event(self, ev):
        super().on_event(ev)                 # 閉じるボタンの処理
        if ev.get("type") == "key_down" and ev.get("scancode") == 0x2C:
            ...

    def on_control(self, msg):               # 他のアプリからの配信など
        ...

app = MyApp()
app.start()
```

Ruby 版と違うところ:

| | Ruby | Python |
|---|---|---|
| 終わりの起動コード | `app = MyApp.new` / `app.start` | 同じ形 (`MyApp()` / `.start()`) |
| 時刻 | `Machine.board_millis` | `ticks_ms()` |
| 文字列の長さ | `String#length` は文字数 | `len()` は**バイト数** |
| 別ファイル | `require "/app/..."` | `import mymodule` (同じディレクトリのみ) |
| 別ファイルからの共通層 | 見える | **見えない** (引数で渡す) |
| タイマの callback | ブロック | 関数 (`self.blink` のように渡す) |

## 実行環境

### Python アプリは同時に 1 本だけ

MicroPython は VM の状態をすべてグローバル変数 (`mp_state_ctx`) に持っており、
mruby の `mrb_state` や Lua の `lua_State` のように複数インスタンスを作れない。
本家がその設計なので、こちらで多重化するのは改造量が割に合わない。

2 本目を起動しようとすると spawn の時点で拒否され、デスクトップに
「Another Python app is already running.」のエラーダイアログが出る。
1 本目が終われば普通に起動できる。

**Ruby / Lua / BASIC アプリとの同時実行は制限なし。**

### import できるのは自分の隣のファイルとビルトインモジュール

アプリは複数のファイルに分けて書ける。`import mymodule` は
**そのアプリと同じディレクトリ**か `/usr/lib/python` の `mymodule.py` を
読む。探すのはこの 2 か所だけで、他の場所は見に行かない。1 つのファイルの
大きさは 64KB まで。

**import した側からアプリ基盤は見えない**。`FmrbApp` / `FmrbGfx` / `Log` は
アプリ自身の名前空間で用意されるもので、module の名前空間には入らない。
分けたファイルが必要とするものは、引数で渡す:

```python
# app 側
import mypanel
mypanel.draw(self, state)      # 描画に要るものは app 経由で渡る

# mypanel.py 側
def draw(app, state):
    app.gfx.draw_text(...)     # FmrbGfx を直接名指ししない
```

組み込みモジュールとして使えるのは MicroPython 本体の `py/` にあるものと、
意図して取り込んだ `extmod/` のものだけ:

`array` / `builtins` / `collections` / `gc` / `io` / `math` / `micropython` /
`struct` / `sys` / **`random`**

`random` は取り込み時に時計から種を与えているので、起動のたびに違う目が出る。
同じ展開を繰り返したいときは `random.seed(n)` を呼ぶ。

**それ以外の `extmod/` のモジュールは使えない**: `time` / `json` / `os` /
`re` / `binascii` / `hashlib` / `heapq` / `deflate` など。組み込み用ソース
一式にこれらの実装が含まれないため。待機は `FmrbApp` の更新間隔
(`on_update` の戻り値) で行う。

要るものが出てきたら 1 つずつ足せる: `components/micropython/extmods/
micropython.mk` に .c を並べ、`port/Makefile` の複写一覧に足し、
`mpconfigport.h` で有効にして `rake micropython:gen`。自己完結した
モジュール (json / binascii など) はこれだけで入る。HAL が要るもの
(asyncio の時刻源など) はもう一段の移植が必要。

### 資産を活用したくなったときの道筋 (現状は未実装)

MicroPython 圏の資産は、種類によって取り込みやすさがまったく違う。

1. **extmod の標準モジュール** — 仕組み上の壁は無い。`random` は既に
   この手順で入れてある (extmods/micropython.mk + port/Makefile の複写 +
   mpconfigport.h)。`json` / `binascii` あたりも自己完結で軽い。`asyncio` は
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

### ファイルは読めるが書けない

`open` は builtins と `io` に存在するが、呼ぶと `OSError` を投げる。
「黙って存在しない」より「呼ぶと明示的に失敗する」ほうが分かるため、
この形にしてある。`io.StringIO` などメモリ上のものは使える。

読むほうは `_fmrb.read_file(path)` がある (丸ごと読んで `bytes` を返す。
1 ファイル 64KB まで)。大きさだけなら `_fmrb.file_size(path)`。
**書き込む手段は無い**。

## 絵

### 素材の種類で経路が違う

| 用意したいもの | 使うもの |
|---|---|
| スプライトやタイルの素材 (BMP) | `SpriteImage` + `load_bmp` |
| 一枚絵 (PNG) | `create_image` + `draw_image` |

**取り違えても例外にはならない**。BMP を `create_image` に渡すと、画面と
同じ大きさの空の画像ができて、描いても何も出ない。これは Ruby でも同じで、
Python 固有の話ではない。

素材はどちらの経路でも、まず `sync_file` で描画側へ送る必要がある
(描画側は自分のファイルシステムしか読めない)。

### 文字列はバイト列

この構成では Unicode 文字列が入っていないので、`len("日本語")` は 9
(バイト数) を返し、添字もバイト単位になる。表示幅が要るときは
`FmrbGfx#text_width` を使う (UTF-8 を走査して画素数を返す)。

日本語を出すには、`set_font(FmrbGfx.FONT_JA)` でフォントを切り替えるか、
`draw_text(..., mixed=True)` で ASCII と日本語を混ぜて描く。

### 用意していない描画

マスク、円弧、半透明の矩形、`get_pixel`、描画のまとめ送り (GfxBlock)、
合成領域、表示範囲。地図クラスも無い (`draw_tile` はあるので、並べるのは
アプリ側で書く)。

## 音

内蔵音源は `FmrbAudio` から使う。曲 (FMSQ の譜面) は主系 (MAIN)、
`note_on` / `note_off` で作る効果音は副系 (SUB) で鳴る。長い曲を主系に置き、
短い音を副系に置けば、効果音が曲を止めない。

```python
audio = FmrbAudio(self)
audio.load_fmsq_file(1, "/cache/app/mygame/bgm.fmsq")   # まず sync_file で送る
audio.play_slot(1, FmrbAudio.MAIN)
audio.note_on(FmrbAudio.CH_PULSE2, 988, 12, 2, 0)       # 効果音は副系
```

用意していないもの:

- **譜面をメッセージに直接詰める形** (`load_fmsq`)。1 メッセージの上限に
  実用的な譜面が入らない。ファイルを送って `load_fmsq_file` で読む。
- マイク入力、外部への MIDI 送出。

注意:

- 音を出す口は機械に 1 つで、アプリごとではない。**自分が鳴らした音は
  自分で止める** (終了時の `note_off` / `stop` を書く)。
- 効果音を止める時刻は `ticks_ms()` の実時間で管理する。フレーム数で
  数えると、重い描画のフレームで音が伸びる。

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

Ruby 版 (`picoruby-fmrb-app`) のサブセット。クラス名・メソッド名・
引数順・色定数は Ruby 版と同じ。

### FmrbApp

| 種別 | 提供しているもの |
|---|---|
| 属性 | `name` `platform` `window_width` `window_height` `pos_x` `pos_y` `fullscreen` `rounded_corners` `headless` `canvas` `bg_canvas` `gfx` `bg_gfx` `running` `user_area_x0/y0/x1/y1` `user_area_width/height` |
| ライフサイクル | `start` `stop` `main_loop` `destroy` / `on_create` `on_update` `on_event` `on_destroy` |
| ライフサイクル (続き) | `on_suspend` `on_resume` `on_resize` `on_quit_request` |
| 描画 | `draw_window_frame` `clear_user_area` |
| 入力補助 | `ev_ctrl` `ev_shift` `ev_alt` |
| タイマ | `set_timer` `clear_time` |
| 連携 | `subscribe` `unsubscribe` `publish` `request_run` `request_fullscreen` `toggle_fullscreen` `request_file_select` `request_reload` |
| その他 | `send_message` `set_window_position` |

`on_control(msg)` は基底クラスにはない。アプリが定義すると、カーネルからの
未知の通知 (`topic_data` / `run_result` / file_select の応答など) がそこに届く
(Ruby 版と同じ扱い)。

共通層のトップレベルには `ticks_ms()` (起動からのミリ秒) と `language()`
("ja" / "en") もある。

### FmrbGfx

`clear` `present` `set_pixel` `draw_line` `draw_rect` `fill_rect`
`draw_circle` `fill_circle` `draw_round_rect` `fill_round_rect`
`draw_ellipse` `fill_ellipse` `draw_triangle` `fill_triangle`
`draw_text` `text_width` `font_height` `set_font` `set_text_size`
`sync_file` `create_image` `draw_image` `delete_image` `draw_tile`
`delete_all_sprites`

スプライトは `SpriteImage` (`set_target` `reset_target` `load_bmp` `destroy`)
と `SpriteInstance` (`move` `set_visible` `set_frame` `destroy`)。

色定数: `BLACK` `WHITE` `RED` `GREEN` `BLUE` `YELLOW` `CYAN` `MAGENTA` `GRAY`
(RGB332、Ruby 版と同値)

### イベント

`on_event(ev)` に渡る辞書のキーは Ruby 版のハッシュと同名。

| 種別 | キー |
|---|---|
| `mouse_down` / `mouse_up` | `type` `button` `x` `y` |
| `mouse_move` | `type` `x` `y` |
| `key_down` / `key_up` | `type` `keycode` `scancode` `modifier` `character` |
| `gamepad_down` / `gamepad_up` | `type` `gamepad_id` `button` |
| `gamepad_axis` | `type` `gamepad_id` `axis` `value` |
| `kana_mode` | `type` `mode` |

文字キーの判定には `scancode` (HID Usage ID) を使う。`keycode` は
シミュレーションと実機で違う値になる。

### Ruby 版にあって Python 版に無いもの

呼ぶと `AttributeError` になる (存在だけ作って無言で何もしない、はしていない)。

| 分類 | 内容 |
|---|---|
| 地図 | タイルマップのクラス / `TileRing` (`draw_tile` はあるので自分で並べる) |
| 画像 | マスク (`draw_image_masked`) / `load_image` (`sync_file` + `create_image` + `draw_image` で書く) |
| 描画最適化 | `GfxBlock` / composite region / `set_viewport` (ハードウェアスクロール) |
| その他 | 音声 / `draw_arc` `fill_arc` `blend_rect` `get_pixel` / 追加キャンバス / スクロールバー / p5 互換層 |

音声はこれから (doc/micropython/phase8.md)。

## 見た目の差

ウィンドウ枠は Ruby 版と同じ絵になるが、描き方が違う。

- Ruby 版は `GfxBlock` で枠一式を 1 コマンドに畳んでいるが、Python 版は
  プリミティブごとに 1 コマンド送る (枠 1 回で 16 コマンド)。
  枠を描き直す頻度が低い分には問題にならないが、リサイズを多用する
  アプリでは差が出る。
- 角丸の合成領域 (composite region) 最適化は入れていない。見た目は同じで、
  graphics-audio 側の透過比較の負荷が高いだけ。
