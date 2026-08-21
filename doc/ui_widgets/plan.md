# 汎用 UI 部品 (FmrbUI) の計画

FmrbApp で画面を持つアプリは、いまボタンを個別に描き、当たり判定を自分で
計算している。同じものが kamon / midi_apu / monitor / デスクトップの
config・network・storage ダイアログに 6 回書かれ、押下中の見た目・
mouse_down と mouse_up が別の場所に落ちたときの扱い・閉じるボタンとの衝突・
窓の大きさ変更後の座標計算を、それぞれが少しずつ違う形で持っている。

これを `FmrbUI` として一つにまとめる。**画面のあるツールを短く書けること**が
目的で、見た目の派手さは目的ではない。

実装の気づき・計測値は `report/` に置く。本書には確定した設計だけを残す。

## 絶対条件

この環境は picoruby の VM が遅く、GC がプール固定の PSRAM ヒープを舐める
(doc/gc_monitoring.md)。UI 部品が**毎フレーム**何かを確保したり描いたりすると、
GC の回数が増えて操作感がそのまま悪くなる。次の 4 つは設計の前提であって、
後から直すものではない。

1. **定常状態では確保しない**。部品は `on_create` で作り、以後のフレームと
   イベントで新しいオブジェクトを作らない。Hash リテラル・配列リテラル・
   文字列補間・キーワード引数 (mruby では呼び出しごとに Hash になる) を
   描画経路とイベント経路から締め出す。確保が許されるのは「利用者が操作して
   値が変わった瞬間」の文字列 1 本だけ。
2. **毎フレーム描かない**。部品は `dirty` を持ち、立っているものだけ描く。
   `ui.flush` は何も dirty でなければ present もしない。
3. **`each` / `times` / `include?` を使わない**。反復は `while`、探索は添字。
   ブロックを渡す呼び出しは 1 回 0.4 ms で、部品 10 個の hit test に `each`
   を使うと 1 クリック 4 ms が消える。
4. **保存して後から呼ぶブロックを持たない**。Spinel では外側のローカルを
   捕まえられず黙って壊れる。部品は「押された部品の id (Symbol)」を返し、
   アプリが `case` で受ける。

判定はコードレビューではなく数字で行う。`GC.stat[:live]` が、合成イベントを
100 回流す前後と、1 Hz 再描画を 60 秒回す前後で**増えていない**こと。

## 使い方 (アプリから見た形)

kamon を書き直すとこうなる。これが API の仕様書を兼ねる。

```ruby
def on_create
  @ui = FmrbUI.new(self)            # @gfx と user area を受け取る
  x = PANEL_X
  y = 14
  # 排他グループ。group が同じ Toggle は一つしか on にならない
  @ui.toggle(:m_circle,  x,      y,      50, 16, "円", group: :motif, on: true)
  @ui.toggle(:m_diamond, x + 58, y,      50, 16, "菱", group: :motif)
  @ui.toggle(:m_flower,  x,      y + 18, 50, 16, "花", group: :motif)
  # ...
  @count_st = @ui.stepper(:count, x, 130, 108, 14, 5, 1, 9)   # value, min, max
  @size_st  = @ui.stepper(:size,  x, 160, 108, 14, 70, 30, 100, 10)
  @ui.toggle(:invert, x, 176, PANEL_W, 12, "反転 切", on_text: "反転 入")
  draw_kamon
  @ui.flush                         # 初回は全部 dirty なので全部描いて present
end

def on_event(ev)
  super(ev)
  id = @ui.handle(ev)               # 押下の見た目の更新もここで dirty になる
  case id
  when :m_circle  then @motif = :circle
  when :m_diamond then @motif = :diamond
  when :count     then @count = @count_st.value
  when :invert    then @inverted = @ui.on?(:invert)
  when nil        then return @ui.flush
  end
  draw_kamon                        # アプリ自身の絵
  @ui.flush                         # dirty な部品だけ描いて present 1 回
end
```

- `handle` が返すのは「操作が確定した部品の id」。mouse_down は押下表示に
  するだけで nil を返し、mouse_up が同じ部品の上で起きたときに id を返す。
  外で離せば nil (押下表示は戻す)。
- 閉じるボタンは基底の `FmrbApp#on_event` が先に処理し、部品は user area の
  中にしか置けないので、kamon が手で避けていた `close_btn_x` の判定は不要に
  なる。
- Stepper の値はアプリが `value` で読む。文字列の作り直しは値が変わった
  ときだけ部品の中で起こる。
- 座標は **user area の左上が原点**。基底の `@user_area_x0/y0` の足し算は
  `FmrbUI` が部品を置くときに 1 回だけ行う。
- 暗い背景のアプリは `FmrbUI.new(self, bg: COLOR_BG)` を渡す。部品が自分の
  後ろを塗る色 (Label の箱、Stepper の値の欄、`set_visible(false)` にした
  部品のあと) がこれになる。既定は `THEME_WINDOW_BG`。

### 大きさ変更に追従する

resizable なアプリは `on_resize` でこの 4 手を踏む。**部品は作り直さない**。

```ruby
def on_resize(new_width, new_height)
  return unless @ui
  @ui.set_origin(@user_area_x0, @user_area_y0)   # user area の原点が動いた
  x = @user_area_width - PANEL_W - 4             # 右端に貼り付く列
  @ui.move(:m_maru, x, 14, 50, 16)
  # ... 追従させる部品の分だけ move
  clear_user_area(FmrbGfx::WHITE)
  draw_window_frame                              # resizable は毎 redraw 必要
  @ui.invalidate_all                             # 全部 dirty に
  draw_kamon                                     # 自分の絵
  @ui.flush
end
```

`set_origin` を忘れると、`move` に渡した user area 相対の座標が古い原点で
足される。`invalidate_all` を忘れると `clear_user_area` で消えた部品が
戻ってこない。

## 設計

### 置き場所と両エンジン

- 本体は `lib/add/picoruby-fmrb-app/mrblib/fmrb-ui.rb` 1 ファイル。純 Ruby で
  `FmrbGfx` の描画メソッドしか呼ばないので、**mruby では gem の mrblib として
  全アプリ VM に入り、Spinel では `tool/spinel/gen_app_combined.rb` の `libs`
  に加えるだけで同じソースが通る**。fmrb-i18n.rb / fmrb-audio.rb と同じ
  扱いで、エンジンごとに書き分けない。
- したがって書き方は doc/spinel_aot/ruby_writing_constraints.md に従う。
  特に、型が確定しない値 (Symbol キーの Hash の値) を描画メソッドに渡さない
  ため、**部品は Hash ではなくクラスのインスタンス変数で状態を持つ**。
  kamon の `{ id:, x:, y: ... }` の形は採らない。
- 型支援のため `sig/fmrb-ui.rbs` を書く (1 行目 112 バイトの制約に注意)。

### クラス

```
FmrbUI                 部品の入れ物。hit test、押下の追跡、dirty の一括描画
  Widget               共通: id, x, y, w, h, dirty, enabled, visible, bg
    Label              文字だけ。set_text で変わったときだけ dirty
    Button             押している間だけ反転。離すと id を返す
    Toggle             on/off。group があれば排他。on_text で表示を変える
    Stepper            "< 値 >"。value/min/max/step。左右どちらでも id を返す
```

- `FmrbUI` は部品を `Array` に持ち (追加は `on_create` のみ)、hit test は
  `while` で後ろから (後に置いたものが上)。部品数は多くて 20 程度なので
  線形で十分。
- `handle(ev)` は `ev[:type]` が `:mouse_down` / `:mouse_up` 以外なら即 nil
  (mouse_move は 30 Hz で来るので何もしないことが大事)。キー操作は段 2。
- 押下中の部品は `@pressed` (Widget か nil) で追跡。mouse_up では
  `@pressed` だけ hit test し直す。
- `flush` は `while` で dirty を描き、1 つでも描いたら `@gfx.present`。
  戻り値は描いたかどうか。アプリが自分の絵も描いた後に呼べば present は 1 回。
- `invalidate_all` は `clear_user_area` や `on_resize` の後に使う。
- `move(id, x, y, w, h)` で再配置。`on_resize` で `set_origin` -> `move` の
  順に呼ぶ (上の「大きさ変更に追従する」)。レイアウト機構は段 1 では
  入れない。添字から座標を出すアプリは `move` を回せば足りる。
- **部品の描画メソッドは `draw` ではなく `draw_widget`**。`flush` は基底型の
  受信に対して呼ぶので、Spinel が候補を**メソッド名で**引く際に
  `GfxBlock#draw` (`**kwargs`。コンパイル不能) まで巻き込んでビルドが落ちた。
  poly 受信で呼ぶ名前は一意にする (doc/spinel_aot/ruby_writing_constraints.md)。

### 描画

- 色は `FmrbConst::THEME_*` をモジュール定数に引いて使う。部品ごとに色を
  持たせない (持たせると `on_create` の Hash が増えるだけで、統一感も落ちる)。
  例外は背景色で、これは `FmrbUI.new(app, bg:)` から全部品に配る (アプリの
  地の色に合わせないと、隠した部品のあとが白く残る)。
  既定テーマは `THEME_BORDER == THEME_BUTTON` なので、**枠色で押下を
  表さない** (見えない)。押下中の内枠はラベル色で描く。
- 文字幅は `set_text` の時点で `@gfx.text_width(str, :default)` を呼んで
  部品に覚える。描画のたびに測らない。**family を省略しない**: 省略すると
  アプリが直前に選んだフォントで測ってしまう。
- **文字の大きさは部品を作ったときに固定する** (`text_size:`、既定 1)。
  アプリは 2 回の flush の間に自分の絵のために size を変えるので (PicoRuby
  デモはページごとに 1〜4 を使う)、その時々の size に従うと、測ったときと
  描くときで倍率が変わって崩れる。
  - 測るときは `text_width(...) / current_text_size * 自分のサイズ` で
    アプリの倍率を割り落とす (生成時にアプリが size 3 でも影響しない)。
  - `flush` は入口で `current_text_size` を控え、部品ごとに必要なときだけ
    `set_text_size` を出し、**最後にアプリの size へ戻す**。全部が同じ
    サイズなら発行されるコマンドは 0。
  - Stepper の `< >` の幅もサイズに比例する。size 2 の Stepper は幅を
    倍くらい取らないと値の欄が潰れる。
- 日本語を含むラベルもフォントを切り替えずに描く。位置引数版の
  `draw_text_mixed` (ASCII=Font0 6px / 多バイト=misaki_8 8px) を使うので、
  `set_font(:ja, 8)` と `set_font(:default)` の往復も、ja の部品をまとめて
  描くための 2 周目の while も要らなくなった。
- GfxBlock 化 (押下/通常の切替をレジスタ値だけ送る) は段 1 ではやらない。
  flush の 1 部品は fill_rect + draw_rect + draw_text の 3 コマンドで、
  コマンド数が問題になってから測って決める。

### 確保しないための具体策

| 場所 | やらないこと | 代わりに |
|---|---|---|
| `handle` | `ev` から Hash を作る、`[x, y]` を返す | 整数をローカルに取り出す。戻り値は Symbol か nil |
| hit test | `each`、`rect = [x, y, w, h]` | `while` とインスタンス変数の比較 |
| Stepper の表示 | 毎 flush の `"#{@value}"` | 値が変わったときだけ `set_text` (String 1 本) |
| Toggle の表示 | `on ? "入" : "切"` の補間 | `text` と `on_text` の 2 本を生成時に持つ |
| `flush` | `dirty.select`、`present` の無条件呼び出し | while + 描いた数の整数 |
| 生成 | キーワード引数 | 生成は `on_create` の 1 回なので許す。描画・イベント経路では使わない |

### 段 2 の候補 (段 1 では入れなかったもの)

段 1 は kamon / midi_apu / monitor を載せて終わった。次に足すならこの順で、
いずれも**実際に要るアプリが出てきてから**着手する。

- **Slider**。ドラッグ = `mouse_move` を処理するので、`handle` の先頭で
  mouse_move を捨てている今の形を変える必要がある。押している部品だけに
  move を配る形にすれば、他の部品の代償は無い。
- **キーボード焦点と Tab 移動**。ダイアログを移植するなら要る。
- **自動レイアウト (縦積み・格子)**。`move` で足りている間は入れない。
  monitor の行と midi_apu の列は添字計算で足りた。
- **デスクトップのダイアログ群の移植** (config / network / storage /
  file_selector / confirm / clock)。6 か所で最も重複が大きい。ただし
  `FMRB_APP_ENGINE_DESKTOP=spinel` が通っていないので、先にそれを
  片付けてから (report/u2.md の残件)。
- **テキスト入力とリスト**。shell / editor が独自に持っているものを
  一般化しない — 移植先が 2 つ以上になってから考える。

## 段階

### U1: 本体と kamon

1. `fmrb-ui.rb` に FmrbUI / Widget / Label / Button / Toggle / Stepper。
2. kamon.app.rb を書き換える (`build_buttons` / `hit_test` / `draw_button` /
   `draw_value_label` を消す)。見た目は現状と同じにする。
3. 計測用に一時アプリ (コミットしない) で `GC.stat[:live]` を取る:
   - 合成 mouse_down/up を 100 回 (fmrb_input.rb) → 前後の `:live` の差が 0
     (Stepper の文字列分は GC 後に戻るので、GC.start を挟んで比べる)
   - 操作せず 60 秒 → `:live` の差が 0、`present` が 0 回 (GFX STATS の
     presents/s で見る)
4. sim で kamon の全ボタンを押して絵が変わること、閉じるボタンが効くことを
   確認する。結果と数字を `report/u1.md` に書く。

### U2: midi_apu と Spinel

1. midi_apu.app.rb のボタン列を Button / Toggle に置き換える。キー操作
   (1-7) は従来どおりアプリが受け、同じ処理に流す。
2. `gen_app_combined.rb` の `system_desktop` の `libs` に `fmrb-ui.rb` を足し、
   `FMRB_APP_ENGINE_DESKTOP=spinel` で**コンパイルが通る**ことを確認する
   (現状この構成は `_send_audio_note` の欠落で壊れているので、先にそれを
   Spinel 基底に足す。これは本計画と独立の修正として単独コミットにする)。
3. `rake spinel:doctor` の指摘を 0 にする。

### U3: monitor と大きさ変更

1. monitor の Tasks ページの `[X]` を Button にする (行ごとに作り直さず、
   最大行数分を `on_create` で作って `visible` で出し入れする)。
2. resizable なアプリでの `on_resize` → `move` → `invalidate_all` の手順を
   確認する。対象は editor ではなく、一時的に resizable にした kamon。
3. `sig/fmrb-ui.rbs` を書き、editor の補完に出ることを確認する。
4. doc/fmrb-app-new (アプリの書き方) に FmrbUI の節を足す。

### 完了条件

- kamon / midi_apu / monitor が FmrbUI で動き、自前の hit test と
  draw_button が残っていない。
- 上の `GC.stat[:live]` の 2 条件を、kamon で sim と Tab5 実機の両方で満たす。
- 実機で押下の反転が目に見える (遅延が感じられたら報告して判断を仰ぐ)。
- mruby 全構成と Spinel カーネル構成の sim 回帰が通る。

## ルール

- fmruby-core/CLAUDE.md に従う (英語コメント、Legacy を残さない、git 操作は
  ユーザ確認)。
- 計測用の一時アプリはコミットしない。
- 段ごとに `report/uN.md` を書き、U1 の数字を見せてから U2 に進む。
