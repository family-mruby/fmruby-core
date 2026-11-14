# Lua のアプリの書き方と、このディレクトリ

Lua のアプリ (`*.app.lua`) は、本体の C の殻 (`fmrb_app_shell.h`、doc/app_boundary) の上で動く。
Window・枠・ループは殻が持ち、アプリは関数を定義するだけでよい。形は Ruby のアプリ (`FmrbApp` の子)
とそろえてある。

## アプリの形

```lua
local seconds = 0

function on_create()            -- Window ができた後に 1 回
  FmrbApp.clear_user_area()     -- 区画をテーマの色で塗り、枠を描き直す
  gfx:draw_text("Hello", 2, 2, FmrbApp.THEME_TEXT)
  gfx:present()
end

function on_update()            -- 返した ms の後にまた呼ばれる (数でなければ 330)
  seconds = seconds + 1
  return 1000
end

function on_event(ev)           -- 入力 1 つごと
  if ev.type == "key_down" and ev.character == string.byte("q") then
    FmrbApp.stop()
  end
end
```

- ファイルのトップレベルは最初に 1 回だけ走る (関数と変数を定義する所)。`gfx` はまだ無い。
- `on_create`・`on_update`・`on_event` のどれも定義しないファイルは起動せず、エラーの Window が出る。
- 任意の関数: `on_destroy` (終わる前)、`on_suspend`・`on_resume` (全画面のアプリが上に来た・去った。
  中断の間は `on_update` が呼ばれない)、`on_resize(w, h)` (大きさが変わった。殻が区画を塗り直して枠を
  描いた後)、`on_quit_request` (Ctrl+Q。無ければ閉じる)。
- 関数の中でエラーになると、エラーの Window (メッセージとトレースバック) が出てアプリは終わる。
- 1 つの関数の中で長く待たない。待つのは `on_update` の返り値で。`FmrbApp.sleep(ms)` は残してあるが、
  寝ている間は入力が届かない。1 秒以上戻らない関数は、閉じるボタンと kill で打ち切られる。

## 描く

- `gfx` は殻が作った描き手。座標の (0, 0) は枠の内側 (区画) の左上 (Ruby の `@gfx` と同じ)。
  区画の大きさは `FmrbApp.user_area_width`・`user_area_height`、Window の大きさは
  `FmrbApp.WINDOW_WIDTH`・`WINDOW_HEIGHT` (大きさが変わると殻が書き直す)。
- `gfx:fill_rect(x, y, w, h, c)`・`draw_rect`・`fill_round_rect(x, y, w, h, r, c)`・`draw_round_rect`・
  `draw_line(x1, y1, x2, y2, c)`・`fill_circle(x, y, r, c)`・`draw_text(text, x, y, c [, bg])`・
  `present()`。`gfx:clear(c)` は canvas 全体 (枠ごと) を消す。区画だけ消すのは
  `FmrbApp.clear_user_area([c])` (枠も描き直す)。区画の隅を塗った後に枠を描き直すのは
  `FmrbApp.draw_window_frame()`。
- 色: `FmrbGfx.BLACK` などの定数と、テーマの `FmrbApp.THEME_WINDOW_BG`・`THEME_TEXT`・`THEME_BORDER` など。
- ヘッドレス (`default_window_mode = "background"`) のアプリでは `gfx` は nil。

## 入力 (`ev`)

Ruby のアプリの `ev` と同じ鍵と値。`ev.type` は文字列。

| `ev.type` | 鍵 |
|---|---|
| `"key_down"`・`"key_up"` | `keycode`・`scancode` (文字のキーはこちらで見る)・`modifier`・`character` |
| `"mouse_down"`・`"mouse_up"` | `button` (1 左・3 右)・`x`・`y` (区画)・`wx`・`wy` (Window) |
| `"mouse_move"` | `x`・`y`・`wx`・`wy` |
| `"mouse_wheel"` | `delta`・`x`・`y`・`wx`・`wy` |
| `"gamepad_down"`・`"gamepad_up"` | `gamepad_id`・`button` |
| `"gamepad_axis"` | `gamepad_id`・`axis`・`value` |
| `"kana_mode"` | `mode` |

タイトルバーの押しもアプリに届く (`y` が負)。閉じるボタン・タイトルバーの右クリック (Window のメニュー)・
ドラッグは殻とカーネルが受け持つので、アプリは何もしなくてよい。

## このディレクトリ

- `fmrb_lua_gfx.c`: `gfx` の描画 (原点を C で足す) と `FmrbGfx`・`FmrbApp` の表。
- `../fmrb_lua_shell.c`: 殻とのつなぎ (関数を呼ぶ・`ev` を表にする・区画を `gfx` に伝える)。
- 足すときは `../CMakeLists.txt` と `hosted/hosted.cmake` の両方に載せる。
