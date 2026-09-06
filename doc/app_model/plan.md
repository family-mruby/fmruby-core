# FmrbApp の基底クラスを締める (計画)

> 状態: 計画済 | 更新: 2026-09-06 | 継承は変えない。**忘れられる契約を 1 つ
> 消し、基底が予約する名前を 13 個減らす**。アプリの書き換えは 0 本

継承のままでよいという判断は `inheritance_vs_delegation.md`。そこで「ゼロから
作るなら最初からこうする」と書いた 2 点を、既存に当てる。

**どちらもアプリ側の変更を必要としない**のが採用の理由である。

## 目的

- **A. `super` を呼ぶ規則をなくす。** 忘れると閉じるボタンが死ぬ契約が 1 つだけ
  あり、43 本中 41 本がそれを守るために `super(ev)` を書いている。
  守れている規則だが、要らない規則の方が良い。
- **B. 基底が予約する ivar を減らす。** 基底は 30 個持ち、名前空間はアプリと
  共有している。**実測では衝突ゼロ**だが、公開していないものまで一般名で
  占めている理由がない。

## A. 既定の `on_event` を改名する

### 形

```ruby
def _dispatch_event(ev)     # native / Spinel の呼び出し先をこれに変える
  _frame_event(ev)          # 今の on_event の中身
  on_event(ev)              # アプリの hook
end

def on_event(ev)            # 空。アプリが上書きする
end
```

**アプリは 1 本も触らない。** 既存の `super(ev)` は空の `on_event` を呼ぶ
無害な呼び出しになるので、41 本はそのままで動く。掃除は後からでよい。

### 触る箇所

| ファイル | 内容 |
|---|---|
| `mrblib/fmrb-app.rb` | 既定 `on_event` → `_frame_event`、`_dispatch_event` を足す、空の `on_event` |
| `ports/esp32/app.c:379` | `mrb_funcall(..., "on_event", ...)` → `"_dispatch_event"` (**1 行**) |
| `spinel/fmrb_app_base_spinel.rb` | 同じ改造 + `_dispatch_message` の呼び先 (**別実装なので独立に要る**) |
| アプリ 76 本 | **0** |

### 順序は問題にならない (実測)

`super` を呼ぶ 41 本のうち **39 本は冒頭**。残り 2 本も安全である。

- `mic_spectrum` — 3 行のコメントを挟んでいるだけで実質冒頭。
- `inspector` — **キー以外のときだけ** `super`。基底はマウスしか見ないので、
  キーで走らせても何もしない。

### この案に必ず付いてくる修正

基底には閉じるボタンのほかに**タイトルバー右クリックの再読み込み**があり、
そちらは `!@fullscreen` で守られていない。

```ruby
if ev[:type] == :mouse_up && ev[:button] == 3 && ev[:y] < 11
  request_reload if _is_file_app
end
```

**今 picorabbit が安全なのは `super` を呼ばないからである。** 基底を必ず
走らせると、**全画面の picorabbit で上端を右クリックすると再読み込みが走る**
(`default_window_mode = "fullscreen"` かつファイル由来のアプリなので条件が
揃う)。

全画面のアプリにタイトルバーは無く `y < 11` はただの絵の上端なので、
**そもそも入っているべき条件**である。閉じるボタンの側には最初から入っている。
この案を採らなくても直す価値がある。

## B. 公開していない ivar に `@_` を付ける

### どれを変えるか (アプリ 76 本 + mixin 34 本を実測)

**直接触られているものは変えない。**

| 変えない | 参照数 |
|---|---|
| `@gfx` | 68 |
| `@user_area_x0` / `_y0` / `_width` / `_height` | 55 / 54 / 42 / 38 |
| `@window_width` / `@window_height` | 6 / 5 |
| `@running` | 4 |
| `@user_area_x1` / `@user_area_y1` | 3 / 3 |
| `@name` | 2 |
| `@bg_gfx` | 1 |

**変える (どこからも直接触られていない 13 個)**

```
@suspended  @rounded_corners  @param  @idle_gc  @fullscreen
@frame_block  @corner_clear_block  @composite_region_w  @composite_region_h
@close_btn_pressed  @closable  @canvas  @bg_canvas  @attached_uis
```

(`@_timers` `@_timer_seq` `@_spin_break` は既に付いている。)

**`@scroll` は基底の持ち物ではない** — コメントの中にしか無く、5 本のアプリが
自分の変数として使っている。触らない。

### 罠: アクセサの生え方

`attr_reader :fullscreen` は `@fullscreen` を読む reader を作る。**ivar を
改名するとアクセサが壊れる**ので、対象のうちアクセサを持つ 3 つは手書きに
置き換える。

```ruby
attr_reader :fullscreen        # ↓
def fullscreen; @_fullscreen; end
```

対象: `fullscreen` `rounded_corners` (attr_reader)、`closable` (attr_accessor)。
**メソッド名は変えない**ので、`app.fullscreen` を呼ぶ側は無傷である。

### mixin も同じ名前空間にいる

`system_desktop/*.rb`、`editor/*.rb`、`shell/*.rb` などは include されるので
ivar を共有する。34 本を調べ、**該当は `editor/menu.rb` の `@fullscreen` 1 件**
だけだった (`view_toggle_label(@fullscreen, :m_full)`)。ここは `fullscreen`
メソッドに書き換える。

## 段取り

| | 内容 | 受け入れ |
|---|---|---|
| **M1** | A の実装 (基底 2 つ + C 1 行) と `!@fullscreen` の追加 | sim と実機で、窓アプリの閉じるボタンが効く。**全画面の picorabbit で上端を右クリックしても再読み込みされない** |
| **M2** | B の改名 13 個 + アクセサ 3 つの手書き化 + `editor/menu.rb` | 標準構成でデスクトップ・エディタ・シェルが起動し、全画面の出入りができる |
| **M3** (任意) | アプリ 41 本から不要になった `super(ev)` を消す | 掃除だけ。挙動は変わらない |

M1 と M2 は独立で、順序はどちらでもよい。M3 は急がない。

**Spinel の基底 (`fmrb_app_base_spinel.rb`) は別実装**なので、M1・M2 とも
2 か所に同じ変更が要る。片方だけ直すと、engine を替えたときだけ壊れる。

## 検証

- 標準構成 (Spinel カーネル + Spinel エディタ) と全 mruby の 2 点。
- **エディタを起動して 1 打鍵まで**含める (基底の ivar を触るため。
  `doc/spinel_aot/reports/editor_ivar_layout_bug.md`)。
- 窓アプリ (shell) の閉じるボタン、全画面アプリ (picorabbit) の上端右クリック、
  全画面の出入り (Ctrl+Tab / Ctrl+,)。

## やらないこと

- 移譲への作り替え (`inheritance_vs_delegation.md` の結論)。
- `@gfx` をメソッド化すること。描画の輪で効く。
- `on_create` / `on_update` など**中身の無い hook を template method 化する**
  こと。守るべき契約が無いので、増やす理由がない。
