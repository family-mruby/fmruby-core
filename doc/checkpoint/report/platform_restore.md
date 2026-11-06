# 移したアプリの platform が書いた側の値のまま (F3・F4 の直し)

> 状態: 実装済・検収待ち | 更新: 2026-11-06 | ビルドの違う端末へアプリを移す (または復元する) と、`FmrbApp#platform` が書いた側の値を返した (ネイティブ版から ESP32 へ移したアプリが :linux)。原因は、アプリの起動時に C (`app.c` の `_init`) が `@platform` をインスタンス変数に入れ、イメージがそれを運んでいたこと。`platform` を読むたびに `FmrbConst::PLATFORM` から求める形にし、`@platform` を持たないようにした。同じ形の値を洗い出し、ファイルの関連付けの表 (`FmrbAssoc` が VM ごとに 1 回読んで控える) も復元のときに忘れるようにした。`rake regress:checkpoint` に machine の場面を足し、直す前に落ち、直した後に通ることを確認した

## 1. 症状

デモの撮影で、ネイティブ版 (:linux) から P4-Nano (ESP32) へ移したアプリが、着いた側でも `platform` に :linux を返した。同じアプリの `FmrbConst::PLATFORM` は着いた側の値 ("esp32") だった。

## 2. 原因

- `lib/add/picoruby-fmrb-app/ports/esp32/app.c` の `mrb_fmrb_app_init` (`FmrbApp#_init`) が、アプリの起動時に `@platform` を `:linux` か `:esp32` (ビルドの `CONFIG_IDF_TARGET_LINUX` で決まる) に入れ、`fmrb-app.rb` の `attr_reader :platform` がそれを返していた。
- インスタンス変数はイメージで運ばれる。復元 (`_checkpoint_restored` と、その中で C の `_checkpoint_canvases`) は Window の位置・大きさ・全画面と画面の ID を着いた側の値に入れ直すが、`@platform` は入れ直していなかった。
- `FmrbConst::PLATFORM` は着いた側の値になる。定数は VM を開いた時点の基準に入っており、イメージの変更表に出ない (gem の format_v1.md の 9 章) ので、着いた側の VM が自分のビルドから定義した値のまま。直前の枠のテーマの直し (`report/f3_frame_theme.md`) と同じ種類で、端末で決まる値を基底クラスが控えた所だけが外れていた。

## 3. 直し方

- `platform` を `attr_reader` から外し、読むたびに `FmrbConst::PLATFORM == "esp32" ? :esp32 : :linux` を返すメソッドにした。`_init` は `@platform` を入れない。`FmrbConst::PLATFORM` は `@platform` と同じ条件 (`CONFIG_IDF_TARGET_LINUX`) で決まるので、どの端末でも戻り値 (Symbol の :linux / :esp32、ブラウザ版も :linux) は前と同じ。復元のときに入れ直す形より、控えない形のほうが運ぶ・運ばないを考えずに済む。
- 洗い出し (§4) で同じ形と分かった `FmrbAssoc` の表 (`/etc/associations.toml` と `/home/associations.toml` を VM ごとに 1 回読んでモジュールのインスタンス変数 `@table` に控える) は、モジュールへの変更としてイメージに入り、書いた側の端末の表のまま着いていた。`_checkpoint_restored` の最初で `FmrbAssoc.forget` を呼び、次に引いたときに着いた側の表を読むようにした。
- ホストの試験の代役 (`tools/regress/stand_ins.rb` の `_init`) も `@platform` を入れないようにした (`FmrbConst::PLATFORM` の代役から同じメソッドが求める)。
- Spinel のアプリの基底 (`main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb` の `@platform`) は変えていない。Spinel のアプリは運べない (D16) ので、控えた値が別の端末へ行くことがない。
- イメージの形式は変わらない。前のビルドが書いたイメージに `@platform` が入っていても、もう読まれないだけ。

## 4. 洗い出し

アプリの起動時に C や基底クラスが入れる値のうち、端末・ビルド・その時の環境で決まりうるもの。(a) 移った先で直す必要がある、(b) アプリの状態なので運ぶのが正しい (または、どの端末でも同じ値)、(c) すでに着いた側で入れ直している・読むときに求めている。

| 値 | 入れる所 | 判断 | 理由 |
|---|---|---|---|
| `@platform` (`platform`) | `app.c` の `_init` | (a) 直した | ビルドで決まる。読むときに `FmrbConst::PLATFORM` から求める |
| `FmrbAssoc` の `@table` (ファイルの関連付け) | `FmrbAssoc.load_table` (VM ごとに 1 回) | (a) 直した | 端末のファイルから読んだ表。復元のときに忘れる |
| `@window_width`・`@window_height`・`@pos_x`・`@pos_y`・`@_fullscreen` | `_init` | (c) | `_checkpoint_canvases` が着いた側の context から入れ直す (F4) |
| `@_canvas`・`@_bg_canvas` | `_init` | (c) | 同上。画面の ID は着いた側で作り直す |
| `@user_area_*` | `_layout_user_area` | (c) | 大きさが変わったときに配置し直す |
| `@_name`・`@_resizable`・`@_rounded_corners` | `_init` | (b) | アプリの `.app.toml` の属性。イメージの横の `.toml` (`write_attributes`) で運び、着いた側の context も同じ値 |
| `FmrbGfx` の `@canvas_id`・`@canvas_width`・`@canvas_height` | `FmrbGfx.new` | (c) | `_checkpoint_rebind`・`_canvas_size` |
| `FmrbGfx` の `@current_font` | `set_font` | (c) | 復元で `set_font` し直し、着いた側にあるフォントを選び直す |
| 枠の `GfxBlock` (`@_frame_block`・`@_corner_clear_block`) | `_build_frame_block` | (c) | 69e0973b (`report/f3_frame_theme.md`) |
| `@_topics` | `subscribe` | (c) | 復元で `subscribe` し直す |
| `@_idle_gc` | `idle_gc=` | (b) | GC の設定そのものもイメージ (`GCS0`) で運ぶ |
| `@_closable`・`@_timers`・`@_timer_seq`・`@_attached_uis` | 基底クラス | (b) | アプリの状態 |
| `FmrbAudio` の `@native_note` | `FmrbAudio.new` | (b) | `_send_audio_note` はどの mruby のビルドにもあり、どこでも true |
| `FmrbConst::*` (テーマ・`LANGUAGE`・`BOARD`・`HW_FAMILY`・`FONT_JA`・`WHEEL_LINES` など)、`FmrbApp.language`、`FmrbApp.script_path` | const.c・C | (c) | 定数とメソッドは着いた側のもの |
| `app_settings_path`・`app_state_path`・`app_handle_name` | 基底クラス | (c) | 呼ぶたびに求める (`/home`・`/tmp`) |
| キー配列 | — | (c) | 基底クラスは控えない。キーは着いた側が変換したものが届く |
| `FmrbI18n` の `@lang` (表示の言語) | `FmrbI18n.lang` (VM ごとに 1 回) | 未決 | モジュールへの変更としてイメージに入り、書いた側の言語のまま。`FmrbApp.language` は着いた側の値なので食い違う。言語の違う端末に移ったとき、アプリの文言を変えるかは仕様なので親に返した |
| `FmrbUI` と各部品の `@bg` (既定は `C_BG` = テーマの Window の地の色) | `FmrbUI.new` | 未決 | 既定のまま作った部品は書いた側のテーマの地の色を運ぶ。ほかの色 (`C_TEXT` など) は描くときに定数を読むので着いた側の色になり、テーマの違う端末では混ざる。アプリが `bg:` で選んだ色と既定とを今は区別できないので、どう扱うかを親に返した |

## 5. 試験

`flash/app/test/ckpt_calls.app.rb` が、ログの行の終わりに基底が端末から答える値を出すようにした: `platform=` (`platform`)、`const=` (`FmrbConst::PLATFORM`)、`platform_ivar=` (アプリのインスタンス変数に `@platform` があるか)、`assoc_cached=` (`FmrbAssoc` の表を控えているか)。`on_create` で `FmrbAssoc.load_table` を呼んでおく。

- `tools/regress/checkpoint.rb` に machine の場面を足した (ネイティブ版の場面、requires の後)。A で `ckpt_calls` を起動し、移す前に `assoc_cached=true` を確認してから B へ移す。B の `on_restore` の行で、`platform` が B の `FmrbConst::PLATFORM` と同じこと、`platform_ivar=false`、`assoc_cached=false` を確認する。
- 同じ確認 (`machine_values!`) を、sim への calls の場面 (`checkpoint_sim.rb`)、Asterism で sim へ移す場面 (`checkpoint_asterism.rb`)、実機への calls の場面 (`checkpoint_board.rb`。ここだけ `platform=esp32` も求める) の `on_restore` の行にも足した。
- ネイティブ版・sim・ブラウザ版はどれも :linux なので、値が違うのは実機だけ。ネイティブ版どうしの場面は、値が着いた側で求められる形 (`@platform` を運ばないこと) を確認する。試験のためだけに着いた側の値を変える仕組みは作っていない。

直す前のコード (`fmrb-app.rb`・`app.c` を eef37deb のまま) で全部 mruby の構成を流すと、この場面だけが落ちた (`assoc_cached=true` も残っていた):

```
mruby: FAILED machine: the app keeps @platform (I (39063104) Ckpt Calls: ckpt_calls: on_restore main=3 platform=linux const=linux platform_ivar=true assoc_cached=true)
```

## 6. 確認

| 確認 | 結果 |
|---|---|
| `rake regress:checkpoint` (標準・全部 mruby) | 標準・全部 mruby とも ok (machine: `platform linux there, no @platform, associations read again`) |
| `rake regress` | ok (ホスト 21 本、描画の記録 205 回) |
| `rake regress:screens ONLY=native` | native 3/3・native-keys 4/4 ok。`screens/drivers.rb` が `FMRB_ROOT` を見ないので、そこだけ一時的に `FMRB_ROOT` を見るよう書き換えて流した (コミットしていない。f3_frame_theme.md と同じ) |
| sim・ブラウザ版・実機の場面 (`SIM=`・`WEB=1`・`BOARD=`) | 流していない。sim の道具は worktree から流せない (`FMRB_ROOT` を見ない)、実機は使わない決まり。足した確認 (`machine_values!`) は、ネイティブ版の場面で通った同じ関数を `on_restore` の行に当てるだけ。実機の場面だけが `platform=esp32` を実際に確認できる |
