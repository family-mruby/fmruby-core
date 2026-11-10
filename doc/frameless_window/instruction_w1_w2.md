# W1・W2 指示書: 枠なしのフラグと、カーネルが持つドラッグ

> 状態: 実装中 | 更新: 2026-11-10 | 正は plan.md の「決定」と report/w0.md の 6 章 (W1・W2 の見立て)

## W1: 枠なしのフラグ

- `.app.toml` の `window_frame = "none"` (既定 `"normal"`)。`resizable` と同じ道筋で通す (report/w0.md の見立て:
  spawner の toml 読み → ctx → Window の一覧 → Spinel カーネル → mruby の app.c と Spinel の基底の `_init` → checkpoint の toml)。
- 基底 (mruby の fmrb-app.rb と Spinel の fmrb_app_base_spinel.rb の両方): 枠なしなら
  - 枠・タイトルバー・閉じるボタン・角の丸みを描かない。user area は Window 全体。
  - 地を透ける色で塗り、Window 全体を透ける区画にする。`clear_user_area` も同じ (枠を描き直さない)。
  - `_frame_event` (閉じるボタン・タイトルバーの右クリックの読み直し) をしない。
  - checkpoint で移った後も、基底が透けを掛け直す (アプリが `on_restore` で何もしなくても透けたまま着く)。
- カーネル (input_router.rb): 枠なしの Window では、上 11 行のドラッグ開始とタイトルバーの扱い、閉じる判定をしない。
- 枠つきのアプリの動きは一切変えない。

## W2: カーネルが持つドラッグ

- `window_drag = "title" | "anywhere" | "none"`。既定は枠つきなら `"title"` (今の動き)、枠なしなら `"anywhere"`。
- `"anywhere"`: カーネルが押下を保留し、4 px を超えて動いたらドラッグ (カーネルが掴み、アプリには押下を送らない)。
  超えずに離したら、押下と離しを続けてアプリへ送る (クリック)。右ボタン・ほかのボタンは保留せずそのまま送る。
- `"none"`: カーネルはドラッグしない (長押しを使うアプリ向け)。アプリは自分で `set_window_position` してよい。
- W0 で分かった不具合も直す:
  - アプリが `set_window_position` で動いたとき、カーネルの Window の一覧 (相対座標の基準) が古いままになる。
    一覧を古いと印す (世代の数など) か、その場で更新する。
  - カーネルのドラッグの後、アプリの `@pos_x` / `@pos_y` が古いまま。
- 閉じる口: 決定どおり、枠なしのアプリは Ctrl+Q と Shell の kill で閉じられる (W0 で確認済み)。基底に右クリックで
  閉じる口を 1 つ足す (枠なしのアプリだけ。アプリが右クリックを使うなら上書きできる形)。タッチだけの構成の閉じ方は
  report の残件に書く。

## 確認

- `rake regress` (描画の基準が変わらないこと)、`rake regress:screens` (sim とブラウザ版の画面。エディタの 1 打鍵を含む:
  基底に ivar を足すと Spinel の親子で並びが食い違った前例がある)、`rake regress:checkpoint` (BOARD= なし)、
  `rake regress:size` (内蔵 RAM の増分 0)。
- W0 の試作 (scratchpad/w0/app/w0_avatar.app.rb) をフラグで書き直した版で、ネイティブ版・ブラウザ版・sim の Retro に
  ついて: 透ける・上 11 行で何も起きない・どこを掴んでもカーソルについて動く (戻り・遅れが無い)・押しただけならクリックが
  アプリに届く・右クリックで閉じる・`amove` の後も透けたまま、を画面とログで示す。
- 枠つきのアプリ (エディタ・Shell など) の Window のドラッグが今までどおり。

## 触ってよい範囲

fmruby-core の基底 (lib/add/picoruby-fmrb-app の mrblib と ports、Spinel の基底)、カーネル (input_router・window_manager ほか)、
spawner・checkpoint の toml、文書 (movable_apps.md、アプリの書き方の文書に枠なしの節)。lib/ を触ったら `rake clean`。
コミットまで (develop)。push しない。

## 止まる条件

決定と違う動きが要るとき (例: 枠つきのアプリの動きを変えないと作れない、4 px の保留でほかの操作が壊れる)。
graphics-audio 側の変更が要るとき。

## report

`doc/frameless_window/report/w1_w2.md`。画面は scratchpad の w1_w2/ に置く。
