# Lua のアプリの書き方と、このディレクトリ

Lua のアプリ (`*.app.lua`) は、本体の C の殻 (`fmrb_app_shell.h`、doc/app_boundary) の上で動く。
Window・枠・ループは殻が持ち、アプリは関数を定義するだけでよい。形は Ruby のアプリ (`FmrbApp` の子)
とそろえてある。

## アプリの形

```lua
local seconds = 0

function on_create()            -- Window ができた後に 1 回
  FmrbApp.clear_user_area()     -- 区画をテーマの色で塗り、枠を描き直す
  gfx:draw_text(2, 2, "Hello", FmrbApp.THEME_TEXT)
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
  区画の大きさは `FmrbApp.user_area_width()`・`user_area_height()`、Window の大きさは
  `FmrbApp.window_width()`・`window_height()` (関数なので、呼んだ時点の大きさが返る)。
  原点は本体の側 (描き手、`fmrb_gfx_drawer.h`) が持って足す。`gfx:origin_x()`・`origin_y()`・
  `area_width()`・`area_height()`・`canvas_id()` で読める。
- `gfx` のメソッドは Ruby の `FmrbGfx` と同じ名前・同じ引数の順で、型定義 `sig/fmrb_gfx.rbs` から
  生成している (doc/app_boundary)。`gfx:fill_rect(x, y, w, h, c)`・`draw_rect`・`fill_round_rect(x, y,
  w, h, r, c)`・`draw_line(x1, y1, x2, y2, c)`・`fill_circle(x, y, r, c)`・`draw_ellipse`・
  `draw_triangle`・`draw_arc` など。`gfx:clear(c)` は canvas 全体 (枠ごと) を消す。区画だけ消すのは
  `FmrbApp.clear_user_area([c])` (枠も描き直す)。区画の隅を塗った後に枠を描き直すのは
  `FmrbApp.draw_window_frame()`。
- 省略できる引数を持つものは、小さな Lua の前置き (`fmrb_lua_gfx.c` の `gfx_prelude`) が Ruby と同じ
  既定値で包む: `gfx:draw_text(x, y, text, color [, bg_color [, mixed]])`・`gfx:present([x, y])`・
  `set_font(family [, size])`・`draw_image(id [, x, y, scale_x, scale_y])`・`draw_tile`・
  `draw_image_masked`・`sync_file(path [, dest])`・`transfer_file`・`set_composite_regions`。
  省略できない形は後ろに `_raw` が付く (`gfx:draw_text_raw(x, y, text, color, bg_color, mixed)`。
  `bg_color` が負なら背景を塗らない)。
- 記録 (`gfx:create_image(path)` の `{id=, width=, height=}` など) は表、Ruby の Symbol の選択肢は
  文字列 (`gfx:set_font("ja", 12)`)。失敗はエラーになる。
- `FmrbGfx.rgb_to_332(r, g, b)` で色を作れる。
- 色: `FmrbGfx.BLACK` などの定数と、テーマの `FmrbApp.THEME_WINDOW_BG`・`THEME_TEXT`・`THEME_BORDER` など。
- ヘッドレス (`default_window_mode = "background"`) のアプリでは `gfx` は nil。

## 音・ログ・クリップボード・時計

本体との境界から生成した関数 (Ruby と同じ名前と既定値。`_raw` は省略できない形)。

- 音: `local audio = FmrbAudio.new()`、`audio:note_on(ch, freq [, volume, duty, sweep])`・
  `audio:note_off(ch)`・`audio:play(path [, track])`・`audio:play_slot(slot [, instance])`・
  `audio:load_fmsq_file(slot, path)`・`audio:stop()`・`audio:set_main_volume(percent)`
  (Ruby の `main_volume =`)・`audio:play_wav(path)`・`audio:nsf_status()` (表か nil) など。
  定数は `FmrbAudio.MAIN`・`SUB`・`CH_PULSE1`…・`NSF_ENDED`…。
- ログ: `Log.info(msg)`・`debug`・`warn`・`error` (行の頭にアプリの名前)。
- クリップボード: `FmrbApp.clipboard([limit])`・`FmrbApp.set_clipboard(text)`・`FmrbApp.clipboard_gen()`。
- 時計: `FmrbApp.uptime_us()` (起動からの µs)・`uptime_ms()`・`FmrbApp.wallclock()` (`{year=, month=, ...}` か nil)。
- アプリと Window (本体との境界、`sig/fmrb_app.rbs`。Ruby の `FmrbApp` のメソッドが `FmrbApp.` の関数になる。
  Ruby の `fullscreen?` などは `?` を落とした名前): `FmrbApp.name()`・`pos_x()`・`pos_y()`・`fullscreen()`・
  `frameless()`・`resizable()`・`rounded_corners()`・`platform()` (`"esp32"` か `"linux"`)・`theme_bg()` など、
  `FmrbApp.request_reload()`・`request_close_confirm()`・`request_window_menu(x, y)`・`request_fullscreen(on)`・
  `toggle_fullscreen()`・`request_file_select([mode])`・`request_run(path[, prev_pid])`・`set_window_position(x, y)`、
  機械の様子 `FmrbApp.pid()`・`language()`・`script_path()`・`ps_gen()`・`audio_volume()`・`audio_muted()`・
  `wifi_connected()`・`battery_level()` (nil もある)・`battery_charging()`・`sys_pool_info()`・`gfx_stats()`・
  `crt_state()`・`linux_available()`・`notify(summary[, body])`・`set_text_cursor(x, y, h)`。
- ファイル: `FmrbFile.read(path)` (無ければ nil)・`write(path, data)`・`append`・`exist(path)`・`size` (無ければ nil)・
  `delete`・`mkdir`・`list(dir)` (名前の表、ディレクトリは後ろに `/`)・`path_read_only`・`free_bytes`。Lua の
  標準の `io` も使える。

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

- `fmrb_lua_gfx.c`: 生成した `gfx` のメソッド (`fmrb_gen/fmrb_boundary_lua.inc`) を載せる表と前置き、
  受け手の番号 (原点つき) を渡すフック、`FmrbGfx`・`FmrbApp` の表。
- `../fmrb_lua_shell.c`: 殻とのつなぎ (関数を呼ぶ・`ev` を表にする・区画を `gfx` に伝える)。
- 足すときは `../CMakeLists.txt` と `hosted/hosted.cmake` の両方に載せる。
