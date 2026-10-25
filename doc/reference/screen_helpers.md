# 画面の小物の使い分け (FmrbGfx・FmrbText・FmrbUI)

> 状態: 完了 | 更新: 2026-10-25 | アプリが窓の中を描くときの 3 つの道具の分け方。FmrbGfx = 描く (文字の詰め FmrbText とボタンの絵 draw_button を含む)、FmrbUI = 操作 (作ったら残る部品と、毎回描く画面の押せる範囲 HitAreas)。経緯は app_refactor の R5・R6

置き場はどれも `lib/add/picoruby-fmrb-app/mrblib/` (`fmrb-gfx.rb`・`fmrb-text.rb`・`fmrb-ui.rb`)。3 つのファイルの冒頭にも同じ分け方を書いてある。

## 1. 分け方

| 道具 | 役目 | 状態 | 使う画面 |
|---|---|---|---|
| FmrbGfx | **描く**。図形・文字に加え、文字の幅を測る・詰める・折り返す (FmrbText) と、角丸のボタンの絵 (`draw_button`) | 今のフォントと文字の大きさだけ | すべて |
| FmrbText | FmrbGfx に混ぜてある文字の道具 (`@gfx.fit_text` のように呼ぶ)。測る・詰める・2 行に折る | 持たない (呼ぶたびに測る) | 自分で配置する画面。FmrbUI の部品も中で使う |
| FmrbUI の部品 | **操作**。作ったら残る部品 (ラベル・ボタン・切り替え・数の増減・選択・スクロールバー・文字の欄)。フォーカス、キー、押して離したときに働く、テーマの色、汚れた物だけ描き直す | 持つ (on_create で作り、あとは id で触る) | 設定・道具の画面。Spinel のデスクトップ・エディタと共有 |
| FmrbUI::HitAreas | **操作**。毎回描き直す画面の押せる範囲。描くたびに登録し、クリックの点で引く | 一覧だけ (次の clear まで) | ゲームの卓・お絵かきボードのような、画面を毎回まるごと描く物 |

- 迷ったら: 画面を作ったら残る物で組めるなら FmrbUI の部品。毎フレーム (や毎回の変化で) 画面をまるごと描くなら FmrbGfx で描いて HitAreas で押せる範囲を持つ。
- FmrbUI の部品と HitAreas は混ぜない。部品は押して離したときに働き、押している間の見た目を自分で描く。HitAreas は描かないし、押した時点でアプリが動く。`button` は FmrbUI の部品の意味だけ。

## 2. FmrbText (FmrbGfx のメソッド)

幅はすべて px で、呼んだ時点のフォントと文字の大きさの FONT_METRICS (`text_width`) で測る。描くときと同じ数字なので、詰めた文字ははみ出さない。

| メソッド | 中身 |
|---|---|
| `fit_text(str, width, mark = "", mark_w = nil, family = nil)` | 収まればそのまま返す。収まらなければ 1 字ずつ足して、`使った幅 + 次の字 + 印の幅` が `width` を超える手前で切り、`mark` を付ける。印の幅は `mark_w` か、省けば測った値。`family` を渡すとそのフォントで測る (FmrbUI は `:default`) |
| `wrap_text(str, width)` | 最大 2 行の配列。1 行目の最後の空白で割って残りが 1 行に収まればそこで、だめなら幅で切って 2 行目を `…` で詰める |
| `draw_text_lines(x, y, str, width, color, line_h)` | `wrap_text` の行を `line_h` 間隔で描き、最後の行の下の y を返す |

- 字の区切りは UTF-8 のバイトで見る (picoruby に Regexp が無い、`String#[]` は多バイトで頭から数え直す)。
- 3 行以上に折りたいとき (オセロ (直接) のメモ) は、`fit_text` で 1 行ずつ切って残りを回す (`flash/app/test/othello.app.rb` の `wrap`)。

## 3. ボタンの絵と押せる範囲 (毎回描く画面)

```ruby
def on_create
  @hits = FmrbUI::HitAreas.new
end

def draw_screen
  @hits.clear
  # ... 画面を描く ...
  @gfx.draw_button(x, y, 60, 16, "投了", theme_accent, theme_fg_light)  # 描く
  @hits.add(:resign, x, y, 60, 16)                                    # 登録する
end

def on_event(ev)
  id = @hits.id_at(ev[:x], ev[:y]) if ev[:type] == :mouse_down
  # case id ...
end
```

| 口 | 中身 |
|---|---|
| `FmrbGfx#draw_button(x, y, w, h, label, fill, fg, radius = 3)` | 角丸の地と、中央の字 (今のフォント、`(w - text_width) / 2`・`(h - font_height) / 2`)。絵だけ |
| `FmrbUI::HitAreas.new` | 空の一覧 |
| `clear` | 描き直しの前に空にする |
| `add(id, x, y, w, h, data = nil)` | 押せる範囲を足す。`data` はクリックで返してほしい物 (麻雀の牌など) |
| `at(x, y)` / `id_at(x, y)` | 点を含む最初の `[id, x, y, w, h, data]` / その id。無ければ nil |

- 形の違うボタン (四角に枠線、字に地の色: StackChan Remote・動画の再生・MML の行) は、アプリが自分で描いて、範囲だけ HitAreas に足す。
- ボタンが動かない画面なら、範囲は on_create で 1 回足せばよい (動画の再生)。

## 4. FmrbUI の部品が長い文字を詰める

- ラベル・ボタン・切り替え (2 つの文字とも)・数の増減と選択 (値の欄) は、文字が部品の箱より広いときだけ `fit_text` で詰め、`..` を付ける。`..` は Font0 の 2 字 (6x8 の 2 マス) で、どの機種でも描ける。
- 詰める先: ラベルは幅いっぱい、ボタンと切り替えは枠線の内側 (幅 - 2)、数の増減と選択は矢印の間の値の欄の内側。
- 箱に収まる文字は前と 1 画素も変わらない (同じ文字列を同じ位置に描く)。
- アプリが渡した文字は部品が持ったまま (`Label#text` などはそれを返す)。詰めた写しは描くためだけ。詰め直すのは文字を変えたときと、`FmrbUI#move` で幅が変わったとき。flush では測らない (毎回の描き直しで割り当てをしない決まりのため)。
- 文字の欄 (text_field) は詰めない (打っている末尾を見せる物なので、末尾を切るのは合わない)。

## 5. Spinel のプログラムから

- デスクトップとエディタ (Spinel で作るとき) は `fmrb-text.rb` と `fmrb-ui.rb` を取り込む (`tool/spinel/gen_app_combined.rb`)。`fmrb-text.rb` は Spinel 版の FmrbGfx (`main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb`。text_width と FONT_METRICS は同じ) に混ざる。
- `fit_text` は渡された値に `to_s`・`to_i` をかけてから回すので、FmrbUI のように型の決まらない値を渡しても、中の繰り返しは整数と C の文字列で動く。型の決まった呼び出し元からなら 3 つとも型の決まった関数になる。
- 呼ばれないメソッドは生成されない (`draw_button` は mruby の FmrbGfx だけにある。Spinel の側では使わない)。モジュールとクラスは 1 つずつ内蔵 RAM の数え (プールの記録) を持つ。
- FmrbText に書き足すときは Spinel の制約 (doc/spinel_aot/ruby_writing_constraints.md) を守る: ブロックと Hash を使わない、文字列リテラルを直接変えない (`"".dup` から足す)。
