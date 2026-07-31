# Phase 3: FmrbApp / FmrbGfx バインディング (Ruby 版準拠) とデモアプリ

## 目的と方針

Python アプリから見た API は **Ruby 版アプリ基盤 (picoruby-fmrb-app) の構成に
準じる**。Lua 版 (fmrb_lua_gfx.c) は実装が薄く不十分なので手本にしない。
クラス名・メソッド名・引数順・属性名は Ruby 版をそのまま Python に写す。

量が多いので、このフェーズで作るのは第一段階の範囲に絞る:

- **やる**: ウィンドウ描画 (タイトルバー・枠・閉じるボタン)、文字 (既定
  フォントのみ)、簡単な図形、ライフサイクル (on_create / on_update /
  on_event / on_destroy)、閉じるボタンで終了できるところまで。
- **後続フェーズに回す** (実装しない。doc に将来課題として残す):
  スプライト、画像読み込み (create_image / load_image / BMP)、タイルマップ、
  GfxBlock、set_font / set_text_size (日本語フォント含む)、composite region
  による角丸最適化、音声、pub/sub (subscribe / publish)、file_select、
  request_run、resize、p5 互換層。

## 参照する現物仕様 (これが正)

| 層 | Ruby 版の現物 | 対応する Python 版 |
|---|---|---|
| アプリ基盤クラス | lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb (FmrbApp) | prelude の FmrbApp (Python) |
| 描画クラス | 同 mrblib/fmrb-gfx.rb (FmrbGfx) | prelude の FmrbGfx (Python) |
| C 実体 (app) | 同 ports/esp32/app.c — _init / _spin / _cleanup / _send_message。_spin が fmrb_msg_receive で HID / APP_CONTROL を受けて on_event / _handle_system_control に配送する | _fmrb C モジュールの app 系関数 |
| C 実体 (gfx) | 同 ports/esp32/gfx.c — 描画プリミティブ | _fmrb C モジュールの gfx 系関数 |

実装前に fmrb-app.rb / app.c を通読し、_init が何を ctx / spawn 設定 (toml の
ウィンドウ指定) から取ってどのインスタンス変数に入れるかを写し取ること。
canvas の生成・削除やメッセージ配送で呼ぶ下位 API (fmrb_msg / fmrb_gfx) は
Ruby 版 C 実体と同じものを使う。

## 構成 (Ruby 版と同型の 2 層)

1. **低水準 C モジュール `_fmrb`** (components/micropython/modules/):
   Ruby の ports/esp32 に相当。qstr 生成対象。関数は Ruby の C 実装と 1:1 を
   基本とする (_init 相当は dict を返す等、言語差の翻訳は可)。
2. **prelude (Python 層)** (components/micropython/prelude/fmrb_app.py 等):
   Ruby の mrblib に相当。FmrbApp / FmrbGfx クラスを Python で書く。

### prelude の読み込み機構

ファイルシステム import は無いので、**ユーザスクリプトの実行前に prelude の
ソースを同じグローバル名前空間で exec する** (Ruby で FmrbApp がトップレベルに
見えるのと同じ見え方になる)。

- rake タスク (micropython:gen に含めるか隣接タスク) が prelude/*.py を
  C 文字列化したヘッダを生成し、fmrb_mp_exec の前段で fmrb_mp が実行する。
- prelude 内の例外はユーザアプリのエラーではなくシステム側の不具合なので、
  区別できるログを出す。
- frozen bytecode (mpy-cross) は今回は使わない (道具立てが増える)。prelude の
  コンパイル時間と GC 消費を report に実測記録し、問題が出たら frozen 化を
  将来課題とする。

## 第一段階で提供する API 面 (Ruby 版のサブセット)

### FmrbApp

- `__init__`: _fmrb の init 相当を呼び、name / canvas / window_width /
  window_height / fullscreen / pos_x / pos_y を取得。canvas があれば
  FmrbGfx を構築し、user_area_x0/y0/x1/y1/width/height を Ruby 版と同じ値で
  設定 (TITLE_BAR_H = 11 等の定数も同値)。属性名は Ruby のインスタンス変数名を
  そのまま使う (self.gfx, self.window_width, self.user_area_x0, ...)。
- `start` / `stop` / `main_loop`: Ruby 版と同じ流れ
  (start -> on_create -> main_loop -> destroy)。main_loop は on_update の
  戻り値 (ms) を _spin に渡す。
- `on_create` / `on_update` / `on_event` / `on_destroy`: 既定実装も Ruby 版に
  合わせる。on_event の既定実装は閉じるボタンの押下 (mouse_down で押下色、
  mouse_up で stop) を処理する。GfxBlock 依存部分と右クリック reload は除く。
- `draw_window_frame`: GfxBlock は使わず、FmrbGfx の直接呼び出しで Ruby 版と
  同じ見た目 (タイトルバー 0xC5、ハンバーガー 3 本線、タイトル文字、
  閉じるボタン白丸、角丸枠 0x60) を描く。composite region の最適化はしない。
- `clear_user_area(color)`: Ruby 版と同じ。
- suspend / resume / reload / タイマ / pub-sub 系のメソッドは作らない
  (呼ばれたら AttributeError で落ちてよい。存在だけ作って無言で何もしない、は
  しない)。

### FmrbGfx

メソッド名・引数順は fmrb-gfx.rb / gfx.c に合わせる。第一段階の面:

- `clear(color)` / `present()`
- `set_pixel(x, y, color)` / `draw_line(x0, y0, x1, y1, color)`
- `draw_rect` / `fill_rect` (x, y, w, h, color)
- `draw_circle` / `fill_circle` (x, y, r, color)
- `draw_round_rect` / `fill_round_rect` (x, y, w, h, r, color)
- `draw_ellipse` / `fill_ellipse`、`draw_triangle` / `fill_triangle`
  (引数順は gfx.c に合わせる)
- `draw_text(x, y, str, color, bg_color=None)` — 既定 8px ASCII フォントのみ。
  set_font / set_text_size / hybrid (日本語) は後続。
- 色定数 (BLACK / WHITE / RED / ... RGB332): fmrb-gfx.rb の定義値を写す。

### イベント

- _spin (C 実装) は app.c と同じく fmrb_msg_receive で待ち、HID イベントを
  dict {"type": "mouse_down", "x": .., "y": .., "button": ..} 相当に変換して
  on_event を呼ぶ。APP_CONTROL の stop / clear_and_stop も Ruby 版
  _handle_system_control と同じ挙動にする。
- キー種別は Ruby 版のイベント hash のキー名に合わせる (type / x / y /
  button / scancode / keycode / modifier)。文字キーの判定には scancode を
  使う前提 (keycode は環境依存)。マウスは必須、キーボードは dict 化まで
  やれれば十分 (デモでは使わない)。
- **_spin の待機中も停止要求に応答する**: fmrb_msg_receive のタイムアウトを
  分割するなどして ctx->should_exit を定期確認し、立っていたら phase2 の
  停止経路 (vm abort) と同じ形で脱出する。VM フックはバイトコード実行中しか
  効かないので、C 側でブロックする箇所は自前の確認が要る。

## 作業項目

1. **_fmrb C モジュール**: components/micropython/modules/ に実装し、
   micropython.mk (SRC_USERMOD_C と、fmrb ヘッダの include パスを
   CFLAGS_USERMOD に追加) を書く。rake micropython:gen で mp_embed/ を再生成
   してコミット (qstr にモジュール名・関数名が入ったことを確認)。
   CMakeLists.txt の SRCS / REQUIRES (fmrb_msg fmrb_gfx) を更新。
2. **prelude**: fmrb_app.py / fmrb_gfx.py を書き、C 文字列化の生成を rake に
   追加。fmrb_mp 側で prelude -> ユーザスクリプトの順に実行する。
3. **デモアプリ**: flash/app/demo/python.app.py + python.app.toml。
   形は Ruby アプリと同じ (class MyApp(FmrbApp): ... 末尾に MyApp().start())。
   内容は shapes.app.rb の縮小版 (第一段階 API の図形と文字、クリックで
   ページ切替) が良い手本。phase2 の pytest.app.py はテスト用と分かる場所へ
   整理するか削除する。
4. **デスクトップ側の拡張子対応**: 分割ソース
   main/prebuild_scripts/kernel/system_desktop/ の launcher.rb (SCRIPT_EXTS に
   "py"、拡張子->表示文字に "P") と file_manager.rb に .py を追加。
   結合ファイル system_desktop_combined.rb はビルド時に自動生成される
   (report/phase2.md 気づき 6) ので分割ソースだけ編集すればよい。
   Spinel 構成のデスクトップでは rake spinel:gen の再実行も必要。
   アイコンは任意 (追加するなら rake icons)。

## 検証手順 (headless)

1. rake build:linux + tools/dev_run_check.sh --keep でデスクトップ起動。
2. ruby tools/fmrb_input.rb でランチャーから python.app を起動
   (座標はスクリーンショットで確認して決める)。
3. python3 tools/fmrb_screenshot.py で撮影し、ウィンドウ (タイトルバー・枠・
   閉じるボタン) と文字・図形の描画を画像で確認する。
4. ユーザ領域をクリックしてページ切替 (on_event が効いている確認)。
5. **閉じるボタンをクリックして終了する**ことを確認し、再度起動できることを
   確認する (排他解放の確認を兼ねる)。
6. lua.app と python.app の同時実行で両ウィンドウが描画されること
   (共存確認。Python 同士は排他)。
7. on_update の戻り値を長め (例 5000ms) にしたスクリプトで、_spin 待機中の
   kill が効くことを確認する。

## 完了条件

- 上記検証 1-7 がすべて通り、3 のスクリーンショットで Ruby アプリと同等の
  見た目のウィンドウが確認できる。
- .rb / .lua / .bas アプリに退行がない。
- 「modules/ または prelude/ を触ったら rake micropython:gen (または prelude
  生成タスク) -> ビルド」の運用が README に追記済み。
- prelude のコンパイル時間 (アプリ起動への上乗せ) と GC 消費、および
  後続に回した API の一覧が report/phase3.md に記録済み。

判定結果・実測値・実装中の気づきは [report/phase3.md](report/phase3.md)。
