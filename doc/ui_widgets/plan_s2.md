# 段 2 の計画: Enum / Scrollbar / TextField とデスクトップのダイアログ

段 1 (U1-U4) で kamon / midi_apu / monitor / NSF / PicoRuby が FmrbUI に乗り、
**自前の当たり判定はツリーから消えた**。段 2 は「まだ手描きで残っている型」を
片付ける。対象と非対象はユーザ決定 (2026-08-22)。

- **やる**: Enum (選択肢の送り)、Scrollbar、TextField、
  デスクトップのダイアログ 5 本の移植。
- **やらない**: List。**アプリごとの性格に深く依存していて汎用化が難しい**
  (行の中身がファイル名+曲数 / アイコン+名前 / ログ行 / シェルの履歴と
  バラバラで、共通なのはスクロールと選択だけ)。共通部分は Scrollbar として
  切り出すので、残るのは各アプリ固有の行描画になる。
- Slider は段 1 から候補に挙げていたが、**ツリーに手描きの Slider が 1 つも
  無い**ので着手しない。

段 1 の「絶対条件」(定常状態で確保しない / 毎フレーム描かない / while /
ブロックを持たない) と、`draw` のような ありふれた名前を poly 受信で呼ばない
規則は段 2 でもそのまま適用する。

## S1: Enum -- 選択肢を左右で送る

### なぜ

`config_dialog.rb` の設定行がこの形で、**Stepper が Integer しか扱えない**
ために自前で書かれている。

```ruby
{ key: :language, type: :enum, options: ["en", "ja"] },
{ key: :theme,    type: :enum, options: ["light", "dark", "classic"] },
{ key: :timezone, type: :enum, options: ["JST-9", "UTC", "EST5", ...] },
```

### 設計

```ruby
@ui.enum(id, x, y, w, h, options, index: 0, text_size: nil)  # -> FmrbUI::Enum
@ui.value(id)        # -> 選ばれた添字 (Integer)
@ui.text(id)         # -> options[index] の文字列
@ui.set_value(id, i) # 添字で設定 (範囲外は端で止める)
```

- **Stepper の派生**として実装する (`< 値 >` の見た目・押下反転・`arrow_w` を
  そのまま使う)。違いは表示する文字が `options[@index]` になる点だけで、
  **値の文字列を作り直さない** (options は生成時に渡された Array をそのまま
  読むので、操作時の確保が 0 になる。Stepper より軽い)。
- `options` は `on_create` で渡す Array。**部品は中身を書き換えない**。
- 端で押しても `handle` は nil を返す (Stepper と同じ)。巡回はしない
  (`language` が en -> ja -> en と回ると誤操作しやすいため。必要なアプリが
  出たら `wrap: true` を足す)。
- 幅が足りないとき: 最長の選択肢に合わせるのはアプリの責任。部品は
  はみ出した分を切らない (切ると何が選ばれているか分からなくなる)。

### 確認

`config_dialog` の 6 行が置き換わること。`GC.stat[:live]` が 100 回の送りで
増えないこと (Stepper と違い文字列を作らないので、こちらは厳密に 0)。

## S2: Scrollbar -- FmrbUI に統合する

### なぜ統合するのか (ユーザ提起)

いま `FmrbApp#draw_scrollbar` / `#scrollbar_hit` として基底にある。調べたら
**段 1 でボタンについて直したのと同じ問題**を抱えていた。

1. **同じ矩形を 2 か所に書く**。描画と当たり判定が別メソッドで、呼び出し側が
   同じ 4 つの座標を両方に渡す。ずれれば当たり判定だけ狂う。
   ```ruby
   draw_scrollbar(@scroll, @lines.size, vis, x0, log_y0, w, log_h)
   sb = scrollbar_hit(x, y, @user_area_x0, log_y0, @user_area_width, log_h)
   ```
2. **描画のたびに確保する**。mruby 版は `@scrollbar_blocks[[x, y, w, h]]` と
   **配列リテラルをキーにした Hash 引き**をしていて、呼ぶたびに Array を作る。
3. **実装が 2 つある**。mruby 版は GfxBlock、Spinel 版は即時描画の写しで、
   同じ絵を別々に保守している (Spinel は proc を保存できないため)。
   FmrbUI は純 Ruby で `FmrbGfx` しか呼ばないので、**1 つで両エンジンを賄える**。

### 設計

```ruby
@ui.scrollbar(id, x, y, w, h, total, visible, scroll = 0)  # -> FmrbUI::Scrollbar
@ui.value(id)                    # -> 現在の scroll (先頭の行番号)
@ui.set_value(id, scroll)        # -> 変わったら true
@ui.set_range(id, total, visible) # 行数が変わったとき (Stepper と同じ名前)
```

- `handle` が返すのは「scroll が動いた」ときだけ。アプリは `value` を読んで
  自分の一覧を描き直す。
- クリックの扱いは既存の `scrollbar_hit` と同じ: 上下ボタン、つまみより上/下
  でページ送り。**つまみのドラッグは入れない** (mouse_move の処理が要る。
  Slider と同じ理由で段 3 送り)。
- `total <= visible` のときは自動的に描かない (今の `draw_scrollbar` と同じ)。
  `set_visible` を使わなくてよい。
- 絵は Spinel 版の即時描画をそのまま移植する (GfxBlock は使わない)。

### 基底の 2 メソッドは段 2 の中で消す

当初「List を移植しないので基底は残る」と書いたが、**これは前提が
間違っていた**。呼び出し元 7 箇所を読み直すと、**すべてが既に
`(scroll, total, visible)` の 3 整数を自分で計算して渡している**:

```
nsf_player     @scroll,               @files.length,               vc
smf_player     @scroll,               @files.length,               vc
logviewer      @scroll,               @lines.size,                 vis
shell          @scroll,               total,                       avail
file_manager   @file_manager_scroll,  @file_manager_entries.size,  max_visible
file_selector  @file_selector_scroll, @file_selector_entries.size, ...
launcher       @launcher_scroll,      launcher_total_rows,         launcher_visible_rows
```

**スクロールバーは一覧の中身を知らない。** launcher は 2 次元の格子だが、
`launcher_total_rows` / `launcher_visible_rows` と**行に畳んで**渡すので
同じ形になる。戻り値の使い道も 3 通りしかなく、どれも「1 単位動かす」だけ:

```ruby
sb == :up ? scroll_up : scroll_down                    # logviewer
sb == :up ? launcher_scroll_up : launcher_scroll_down  # launcher
handle_file_manager_scroll(sb == :up ? -1 : 1)         # file_manager
```

したがって **行の描画・選択・キー操作をアプリに残したまま 7 箇所すべてを
移せる**。List 部品は要らない。

- **S2**: 軽い 4 つ (nsf_player / smf_player / logviewer / shell) を移す。
- **S4**: デスクトップ側の 3 つ (file_selector / file_manager / launcher) を
  ダイアログと一緒に移す (同じ座標系の問題に当たるため)。
- **S4 の最後に、基底の `draw_scrollbar` / `scrollbar_hit` を両エンジンから
  削除する**。段 2 の完了条件に入れる。S2 と S4 の間だけ両方が存在するが、
  それは同一段の作業途中であって、残る状態ではない。

## S3: TextField -- 1 行の文字入力

### なぜ

`file_selector` の保存ファイル名の欄が自前で持っている (カーソル線を
`draw_line` で引いている)。editor の検索欄も同型。**shell と editor 本体の
入力は対象外** — あれはアプリの機能そのもので、一般化すべきではない。

### 設計上の大きな変更: `handle` がキーを見る

段 1 の `handle` は `:mouse_down` / `:mouse_up` 以外を即 nil で捨てている。
TextField はキーが要るので、**焦点を持つ部品があるときだけ `:key_down` を
処理する**形に広げる。

```ruby
@ui.text_field(id, x, y, w, h, text, max: 32)  # -> FmrbUI::TextField
@ui.focus(id)      # 焦点を当てる (nil で外す)
@ui.focused        # -> id か nil
@ui.text(id)       # 打たれた文字列
```

- `handle` の先頭は変えない: **焦点が nil なら `:key_down` は今までどおり
  即 nil**。焦点があるときだけ分岐に入る (mouse_move を捨てる速さは保つ)。
- クリックで焦点が移る。焦点のある欄はカーソル (縦線) を点滅**させない**
  (点滅は毎フレーム描くことになるので、絶対条件に反する。線は出しっぱなし)。
- 受け取るキー: 印字可能文字 (`ev[:character]`)、Backspace、Enter。
  Enter で `handle` が id を返す = 確定。Esc は焦点を外して nil。
  **矢印キーとカーソル移動は入れない** (欄の途中に挿入したい要求が出てから)。
- 文字列は `@text` を破壊的に伸ばす (`+""` で可変にして `<<` / `chop!`)。
  **1 打鍵につき確保 0 を目指す**が、mruby の String 伸長は内部で再確保する
  ので、ここは「操作時のみ」の確保として許す (段 1 の Stepper と同じ扱い)。
- Tab による焦点移動は入れない。欄が 1 つのダイアログしか対象にないため。

### 確認

`file_selector` の保存モードで名前が打てること。`GC.stat[:live]` が
**キーを打っていない間**は増えないこと (焦点があっても mouse_move や
on_update で確保しない)。

## S4: デスクトップのダイアログ 5 本

`main/prebuild_scripts/kernel/system_desktop/` の

| ファイル | 自前ボタンの箇所 | 要る部品 |
|---|---|---|
| `storage_dialog.rb` | 27 | Button / Toggle |
| `config_dialog.rb` | 18 | **Enum** / Button / Stepper |
| `network_dialog.rb` | 10 | Button / TextField (SSID/パス) |
| `clock_setting.rb` | 3 | Stepper (年月日時分) |
| `confirm_dialog.rb` | 2 | Button |

- **新しい部品は S1-S3 で足りる**。ここで足すものは無い見込み。
- ダイアログは**デスクトップの上に重ねて描く**ので、`FmrbUI.new(self, bg:)` の
  `bg` は各ダイアログの地の色 (`THEME_WINDOW_BG`) にする。閉じたときに
  `set_visible(false)` で消すのではなく、**ダイアログごとに FmrbUI を持ち、
  開いている間だけ `flush` する**。
- 座標はダイアログの左上ではなく **user area 原点**が基準になる。ダイアログは
  画面中央に置かれ位置が動くので、開くときに `set_origin` + `move` で
  貼り直す (U3 の resize と同じ手順)。**ここが一番はまりそうな箇所**。
- デスクトップは `closable = false`、`FMRB_APP_ENGINE_DESKTOP=spinel` でも
  ビルドが通ること (別セッションの `a4f7073` で通るようになった) を確認する。

## 段取りと確認

| 段 | 内容 | 確認 |
|---|---|---|
| S1 | Enum | host harness に追加 + config_dialog を 1 行だけ試験移植して sim |
| S2 | Scrollbar | host harness + NSF / smf_player を移植して sim |
| S3 | TextField | host harness + file_selector の保存モードを sim |
| S4 | ダイアログ 5 本 | sim で全ダイアログを開閉、Tab5 で 1 本 |

- **各段ごとにコミットし、S1 が終わった時点で一度見せる** (Enum の見た目と
  API がその後 3 段の土台になるため)。
- ホストの harness (`ui_harness.rb`) は段 1 では scratchpad 止まりだったが、
  **段 2 では `rake test` に組み込む** (部品が 4 つ増え、docker 無しで回せる
  回帰の価値が上がるため)。これも S1 で行う。
- `sig/fmrb_ui.rbs` と skill の追記は各段の中で行う (後回しにしない)。
- report は `report/s1.md` .. `report/s4.md`。

## 見えているリスク

1. **S2 と S4 の間だけ Scrollbar の実装が 2 つある**。段の外へは持ち越さない
   (S4 の最後に基底を削除)。完了条件で縛る。
2. **`handle` がキーを見るようになる**。焦点が nil のときの経路が段 1 と
   1 命令も変わらないことを、harness で明示的に確認する。
3. **ダイアログの座標系**。デスクトップは fullscreen で user area 原点が
   (0,0) なので、窓アプリと違って `set_origin` の効きが分かりにくい。
   S4 の最初にここだけ小さく試す。
4. **Spinel**。新しい部品も poly 受信で呼ばれるので、`text` / `focus` の
   ような名前が他クラスと衝突しないか、生成 C を見て確かめる
   (段 1 の `draw` の件と同じ)。**`text` は GfxBlock::Recorder が
   `draw_text` の別名として持っている** (引数 4 個)。部品側は 0 引数なので、
   `FmrbUI#text(id)` のような**poly 受信の `text` 呼び出しを作らない**。
   Enum は `option_text`、TextField は `field_text` のように固有の名前に
   する。段 1 の `draw` -> `draw_widget` と同じ回避。

## 完了条件 (段 2)

- Enum / Scrollbar / TextField が FmrbUI にあり、rbs と skill に載っている。
- デスクトップのダイアログ 5 本が FmrbUI で動く。
- **`FmrbApp#draw_scrollbar` / `#scrollbar_hit` が両エンジンの基底から
  消えている** (呼び出し元 0)。
- ホストの harness が `rake test` で回る。
- mruby 全構成と Spinel 構成の sim 回帰、Tab5 で 1 本。
