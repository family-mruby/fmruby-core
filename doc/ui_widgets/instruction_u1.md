# 実装指示書 U1: FmrbUI 本体と kamon の書き換え

対象: 実装担当セッション。作業リポジトリ: fmruby-core。
背景と設計判断の根拠は同ディレクトリの plan.md を先に読むこと。特に
「絶対条件」の 4 項目は、この指示書の全タスクに優先する。

## 進め方の約束

- fmruby-core/CLAUDE.md のルールに従う (英語コメント、Legacy を残さない、
  git 操作はユーザ確認、lib/ を編集したら `rake clean`)。
- 実装中の気づき・計測値・引継ぎは doc/ui_widgets/report/u1.md に書く。
  plan.md には確定結果だけ反映する。
- タスクは T1 → T2 → T3 → T4 の順。**T3 (計測) の数字を report に書いて
  ユーザに見せてから T4 (仕上げ) に進む**。T3 で条件を満たさなければ
  T1 に戻って直す。先へは進まない。
- 検証は Linux sim (ルートの tools/dev_run_check.sh、fmrb_input.rb、
  fmrb_screenshot.py)。Tab5 は tools/fmrb_rd_launch.rb / fmrb_rd_input.rb /
  fmrb_rd_snap.rb で遠隔検証できる (ルート CLAUDE.md)。実機検証の前には
  ユーザに一言断る。
- 罠 (既知): `rake build:linux` は stale な esp32 build/ があると Xtensa の
  まま "Linux build complete" を出す。検証前に `file build/fmruby-core.elf`
  で x86-64 を確認する。ターゲット切替は `rake clean_all`。sim は 3 コンテナ
  まとめて再起動する。`.env` の FMRB_HW_TARGET は `rake build:linux` が
  NARYAv3 に書き換えるので、作業前の値を覚えておき最後に戻す。
- 一時的な計測アプリはコミットしない。

## 書き方の規則 (fmrb-ui.rb 全体に適用)

このファイルは mruby と Spinel の両方で同じソースが使われる
(plan.md「置き場所と両エンジン」)。次を守る。違反は T3 の数字に出る前に
レビューで落とす。

- 反復は `while` のみ。`each` / `times` / `map` / `select` / `include?` /
  `find` を使わない。`Array#index` は C 実装なので可。
- `on_create` から呼ばれる生成メソッド (`FmrbUI#button` 等) 以外で、
  Hash リテラル・配列リテラル・文字列補間・キーワード引数を書かない。
  `handle` / `flush` / hit test / 各 Widget の `draw` が該当する。
- 部品の状態はインスタンス変数。Hash に詰めない (Spinel で型が確定しない
  値になり、描画メソッドに渡せない)。
- ブロックを受け取るメソッドを作らない。コールバック引数も作らない。
- `defined?(@x)` を使わない。`@x ||= v` は falsy を保存する用途に使わない。
- 文字列リテラルは frozen 前提。可変が要るなら `+""` か `dup`。
- `Log.*` をメソッド末尾に置かない (戻り値が void と推論される)。
  末尾は明示的に `nil` や値を返す。
- 整数が要る場所に型の確定しない値を渡すときは `.to_i` で固定する。
  `ev[:x]` 等は取り出した直後に `.to_i` してローカルに置く。
- 組み込みモジュールは `::` を付けずに書く。

## T1: fmrb-ui.rb

### 置き場所

`lib/add/picoruby-fmrb-app/mrblib/fmrb-ui.rb`。gem の mrblib は全ファイルが
取り込まれるので mrbgem.rake の変更は不要。Spinel 側の `libs` への追加は U2。

### 描画の下準備: `FmrbGfx#draw_text_mixed`

日本語を含むラベルのために、`draw_text(..., mixed: true)` の位置引数版
`draw_text_mixed(x, y, str, color, bg_color = nil)` を **両方の FmrbGfx** に
足す:

- `lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb` — `_draw_text_hybrid` を呼ぶ。
- `main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb` — flags に 2 を
  立てて `fmrb_spx_gfx_draw_text` を呼ぶ。

理由: mruby のキーワード引数は呼び出しごとに Hash を作るので、flush の
経路で `mixed: true` を書けない。mixed 描画なら ASCII は Font0、多バイトは
misaki_8 で出るため、kamon のように `set_font(:ja)` と `set_font(:default)`
を往復する必要がなくなる。**FmrbUI は常に draw_text_mixed で描き、フォント
切替コマンドを出さない**。文字幅は `text_width(str)` (family 省略 =
default の mixed 換算) で測る。

### API

以下をそのまま実装する。引数名・戻り値を変えるときは report に理由を書く。

```ruby
class FmrbUI
  def initialize(app)
    # app: FmrbApp。@gfx と @user_area_x0/y0/width/height を読む。
    # app 側に attr が無ければ fmrb-app.rb に attr_reader を足してよい
    # (user_area_x0 など。Spinel 基底にも同じものを足す)。
  end

  # ---- 生成 (on_create でのみ呼ぶ。ここだけキーワード引数を許す) ----
  # 座標は user area の左上が原点。内部で @ox/@oy (= user_area_x0/y0) を
  # 足して絶対座標を部品に持たせる。
  def label(id, x, y, w, h, text, align: :left)            # -> Label
  def button(id, x, y, w, h, text)                          # -> Button
  def toggle(id, x, y, w, h, text, group: nil, on: false, on_text: nil)  # -> Toggle
  def stepper(id, x, y, w, h, value, min, max, step = 1)    # -> Stepper

  # ---- 実行時 (確保しない) ----
  def handle(ev)          # -> Symbol | nil   操作が確定した部品の id
  def flush               # -> true/false    dirty を描き、描いたら present
  def invalidate_all      # 全部品を dirty に (clear_user_area / on_resize の後)
  def move(id, x, y, w, h)        # 再配置 + dirty
  def find(id)            # -> Widget | nil  (while + id 比較)
  def on?(id)             # Toggle の状態
  def set_on(id, on)      # Toggle を外から変える (group の排他も適用)
  def set_text(id, text)  # Label / Button / Toggle の文字
  def set_enabled(id, flag)
  def set_visible(id, flag)
end

class FmrbUI::Widget
  attr_reader :id, :x, :y, :w, :h          # x, y は絶対座標
  attr_accessor :dirty, :enabled, :visible
  def hit?(px, py)                          # 整数比較のみ
  def draw(gfx)                             # サブクラスで実装
end
class FmrbUI::Label   < Widget   # text, align
class FmrbUI::Button  < Widget   # text, pressed
class FmrbUI::Toggle  < Widget   # text, on_text, on, group, pressed
class FmrbUI::Stepper < Widget   # value, min, max, step, text(値の文字列), pressed(-1/0/1)
```

### 振る舞い

- `handle(ev)`:
  - `ev[:type]` が `:mouse_down` / `:mouse_up` 以外、または `ev[:button]` が
    1 以外なら何もせず nil。mouse_move は 30 Hz で来るので、この分岐を
    メソッドの先頭に置く。
  - `:mouse_down`: 後ろから hit test し、最初に当たった visible かつ enabled
    な部品を `@pressed` に置き、押下表示にして dirty。nil を返す。
    Stepper は左半分/右半分で `pressed` を -1/1 にする。
  - `:mouse_up`: `@pressed` が nil なら nil。あれば押下表示を戻して dirty、
    まだその部品の上なら確定処理 (Button: そのまま、Toggle: on を反転して
    group があれば同 group の他を off + dirty、Stepper: value を step 分
    動かして min/max で止め、変わったときだけ文字列を作り直す) をして
    id を返す。外で離したら nil。`@pressed` は必ず nil に戻す。
  - 確定しても値が変わらなかった Stepper (端で押した) は nil を返す。
- `flush`: `while` で dirty な部品を `draw` し、描いた数が 1 以上なら
  `@gfx.present` して true。0 なら何もせず false。present を呼ぶのは
  ここだけ。
- `draw` の見た目 (色は `FmrbConst::THEME_*` をモジュール定数に引く):
  - Button: 通常 = `fill_rect(THEME_BUTTON)` + `draw_rect(THEME_BORDER)` +
    中央寄せ文字 (THEME_TEXT)。押下中 = 塗りと文字色を反転。
    disabled = 文字色を THEME_BORDER。
  - Toggle: on = 塗り THEME_HIGHLIGHT、文字 THEME_TEXT_LIGHT。off は Button
    の通常と同じ。`on_text` があれば on のときそれを出す。押下中は枠を
    二重に (塗りの反転は on/off と紛らわしいため)。
  - Stepper: 左右 14px が `<` `>` の小ボタン、中央が値。押下中の側だけ
    反転。
  - Label: 背景を THEME_WINDOW_BG で塗ってから文字。align は
    :left / :center / :right。
  - 文字は `draw_text_mixed`。中央寄せの x は生成時/`set_text` 時に
    測った幅から計算して部品に覚える (`@tx`)。描画時に `text_width` を
    呼ばない。
- `set_text`: 同じ文字列なら何もしない (`String#==` は 67 µs、操作時だけ
  なので許す)。変わったら幅を測り直して dirty。
- 部品は生成順に `@widgets` (Array) へ。hit test は末尾から。

### kamon の書き換え

`flash/app/demo/kamon.app.rb` を FmrbUI に乗せる。plan.md「使い方」の形に
合わせ、以下を消す: `build_buttons`、`hit_test`、`handle_button`、
`draw_button`、`draw_value_label`、`button_selected?`、`@buttons`、
`on_event` の close ボタン回避。`MOTIFS` / `CENTERS` の並びと id の対応は
`case` で書く (Hash 引きにしない)。

- 動作の等価: 5 つの motif と 4 つの center が排他トグル、count は 1..9、
  size は 30..100% (10 刻み、表示は `"#{v}%"` を値変更時だけ生成)、
  反転はトグルで文字が `反転 切` / `反転 入`。
- 描画順: 値が変わったときだけ `draw_kamon` (絵の部分) → `@ui.flush`。
  何も変わらなければ `@ui.flush` だけ (押下表示の戻し)。
- `on_update` は従来どおり (描画はイベント駆動)。`@frame_ms` が何であれ
  on_update で描かないこと。

## T2: sim での動作確認

- `rake clean && rake build:linux` → `file build/fmruby-core.elf` で x86-64。
- `tools/dev_run_check.sh --keep` → ランチャーから Kamon を起動 → 全ボタンを
  `fmrb_input.rb click` で押し、スクリーンショットで:
  - 排他トグルが 1 つだけ on で描かれる
  - Stepper の値が動き、端で止まる
  - 押下中 (`down` だけ送って撮る) の反転が出て、`up` で戻る
  - 部品の外で `up` したとき何も起きない
  - 閉じるボタンで終了する (FmrbApp 側の処理が生きている)
- `docker logs fmruby_core | grep -i "error\|exception"` が増えていない。

## T3: 確保と描画回数の計測 (ここで一度ユーザに見せる)

計測は一時アプリで行い、コミットしない。`flash/app/test/ui_probe.app.rb`
(toml 付き) を作り、Button 3 / Toggle 4 (group 1 つ) / Stepper 2 / Label 2 を
置く。`on_update` は 1000 ms ごとに `GC.stat[:live]` と
`FmrbApp.gfx_stats[:presents]` をログに出す (この 1 行の文字列生成は計測側の
ゴミなので、**比較は GC.start 直後の live 同士**で行う。手順:
`GC.start` → live を読む → 操作 → `GC.start` → live を読む)。

取る数字 (sim と、可能なら Tab5 の両方):

| 項目 | 手順 | 合格 |
|---|---|---|
| 無操作 60 秒 | 起動後 GC.start、60 秒放置、GC.start | live の差 0、presents の差 0 |
| クリック 100 回 | Button の上で down/up を 100 回 (fmrb_input.rb を `sleep 60` 刻みで) | GC.start 後の live の差 0 |
| Stepper 100 回 | 右ボタン 50 回、左 50 回 | GC.start 後の live の差 0 (文字列は回収される)、present 100 回 |
| 外れクリック 100 回 | 部品の無い場所で down/up | live の差 0、presents の差 0 |
| 1 クリックの処理時間 | `handle` + `flush` を `Machine.board_millis` で挟んでログ (100 回の平均と最大) | sim の数字を記録。実機は報告値 |

`live` の差が 0 にならなければ、どの操作で増えるかを二分して原因を特定し、
fmrb-ui.rb を直す。「計測側のゴミ」で片付けない (GC.start 後に残るものは
参照が残っている)。

結果を report/u1.md に表で書き、**ユーザの確認を待つ**。

## T4: 仕上げ

T3 が合格してから。

- `sig/fmrb_ui.rbs` を書く (1 行目 112 バイト以内。`sig/README.md` の
  規則。sig を直したら `rake clean`)。
- kamon の先頭コメント (Layout の説明) を FmrbUI 前提に書き直す。
- report/u1.md に: API の最終形、T3 の数字、直した箇所、U2 への申し送り
  (Spinel で引っかかりそうな書き方に気づいたら必ず書く)。
- コミットは 2 つに分ける: (1) `draw_text_mixed` の追加 (両 FmrbGfx)、
  (2) fmrb-ui.rb + kamon + rbs。メッセージは英語。コミット前にユーザ確認。

## 受け入れ条件 (U1 全体)

- kamon が FmrbUI で従来と同じ操作感で動く (sim で全操作を確認)。
- T3 の 4 条件を sim で満たす。Tab5 の数字があればなお良い (無ければ
  「ユーザ確認待ち」と report に書く)。
- fmrb-ui.rb に `each` / `times` / ブロック引数 / `defined?` が無い
  (`grep -n "\.each\|\.times\|&blk\|yield\|defined?" fmrb-ui.rb` が空)。
- mruby 全構成の sim 回帰: shell / editor / monitor の起動と終了が従来どおり
  (fmrb-ui.rb は全 VM に入るので、読み込みだけで壊れていないことを見る)。
- `.env` の FMRB_HW_TARGET が作業前の値に戻っている。
