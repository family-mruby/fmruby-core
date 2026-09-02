# 報告: テーマが Python と Lua に届いていなかった

> 状態: 完了 | 更新: 2026-09-02 | 窓枠の実装は 2 か所ではなく 4 か所だった。
> MicroPython と Lua にもテーマを渡した

ユーザ報告 (2026-09-02):「システムカラーテーマが Python アプリへ継承されて
いない」。調べたら Lua も同じだった。

## 何が起きていたか

A・B・C で窓枠をテーマに繋いだとき、直したのは Ruby の 2 つ
(`picoruby-fmrb-app/mrblib/fmrb-app.rb` と Spinel 用の
`fmrb_app_base_spinel.rb`)。plan.md も「窓枠の実装が 2 か所にある」と書いて
いる。**実際は 4 か所**あった。

| 場所 | 何を持っていたか |
|---|---|
| `components/micropython/prelude/fmrb_app.py` | `TITLE_BAR_COLOR = 0xC5` `MENU_MARK_COLOR = 0xFB` `BORDER_COLOR = 0x60`、題名は `FmrbGfx.WHITE`、`clear_user_area` の既定が `BLACK` |
| `flash/app/lua/lua.app.lua` | アプリ自身が `draw_window_frame()` を持ち、同じ 0xC5 / 0xFB / 0x60 を直書き |

**0xC5 と 0x60 が動かぬ証拠**だった。Ruby 側を直したときのコメントに
「その値は classic の menu_bg と border を書き下したもので、テーマを変えても
題名バーが取り残される原因だった」と残っており、Python は**その修正前の
Ruby から書き写したまま**追随していなかった。Lua には枠を描く枠組み自体が
無く、サンプルアプリが自分で描いていた。

さらに Python には `theme_bg` / `theme_fg` などの入口が 1 つも無く、**アプリ
本体がテーマを読む手段が無かった**。

## どう繋いだか

テーマの表は mruby のヘッダ (`picoruby_fmrb_const.h`) の中にあり、Lua と
MicroPython は `mruby.h` を巻き込むそれを include できない。そこで表の定義を
**`fmrb_common/include/fmrb_theme.h` へ移した**。Lua も MicroPython も
fmrb_common を REQUIRES しているので、これで 3 つの言語が同じ 1 枚を見る。

- **移し先を fmrb_common にしたのは 2 度目の失敗の後**。最初は
  `picoruby_fmrb_const.h` から新ヘッダを include する形にしたが、
  `src/picoruby_fmrb_const.c` は **CMake ではなく rake の picoruby ビルド**が
  コンパイルしており、そちらにファームウェアの include パスは無い。
  「fmrb_theme.h: No such file or directory」で止まる。**あの .c はテーマを
  使っていない**ので、mruby 側のヘッダからはテーマの宣言を丸ごと外し、
  実際に使う 3 つ (gem の ports/esp32/const.c、fmrb_kernel.c、
  fmrb_spx_common.c) が新ヘッダを直接 include する形にした。

- **Lua**: `FmrbApp` テーブルに `THEME_*` を 9 個。サンプルアプリの
  `draw_window_frame()` と本文の色をそれに置き換えた。
- **MicroPython**: `_fmrb.theme()` を新設 (init() とは別。prelude が
  `FmrbConst` を定義する時点で読むので、アプリ生成前に呼べる必要がある)。
  prelude は `FmrbConst.THEME_*` を作り、枠の 3 色と `clear_user_area` の
  既定をそこから採り、Ruby と同じ名前で `theme_bg` ほか 5 つを生やした。
  **`modules/` を触ったので `rake micropython:gen` が要る** (qstr の再生成)。

## ついでに直したもの

Ruby 側にも同じ穴が 1 つ残っていた。閉じるボタンを押して**ボタンの外で
離した**ときの描き戻しが `CLOSE_BTN_NORMAL_COLOR`(0xFF) のままで、枠を描いた
色 (`THEME_TEXT_LIGHT`) と食い違う。light ink が白でないテーマでボタンの色が
変わる。mrblib と Spinel の両方を `THEME_TEXT_LIGHT` にした。

## 触らなかったもの

- **デモアプリの配色** (`python.app.py` の `PAPER`/`INK`、Ruby の
  `picoruby.app.rb` も同じ値)。白い紙を前提に色見本が読めるよう選んだ
  パレットで、Python 版は Ruby 版の忠実な写し。plan.md の「自分の絵を持つ
  ものはそのままでよい」に当たる。
- Lua サンプルの `FmrbGfx.RED` (0xE0) は明るく、暗い紙でも読める。ただし
  `FmrbGfx.BLUE` (0x03) は暗い紙に沈んだので、そこだけ `THEME_TEXT` にした。

## 確かめたこと

sim (NARYAv4 構成、Spinel カーネル + Spinel エディタ) で、Config から
**Dark に変えて再起動 → Python・Lua・Ruby の 3 つを並べた**。

- Python の題名バーが 0xC5 の臙脂から dark の 0x49 に変わり、Ruby のデモと
  同じ色になった。
- Lua は題名バーに加えて**紙も本文の字も**テーマに従った (window_bg 0x24 に
  白い字)。
- Light に戻して再起動し、3 つとも元の見た目に戻ることを確認。classic 相当の
  値なので、既定のテーマでは見た目が変わらない。
- `rake micropython:smoke` 通過。`rake build:linux` 通過。
