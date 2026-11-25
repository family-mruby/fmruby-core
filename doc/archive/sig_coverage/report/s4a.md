# S4a UI の部品・FmrbApp の残り・アプリ間のメッセージ・FmrbI18n・FmrbAssoc を sig に書く

> 2026-11-25 | サブ (Sonnet) の報告。指示書は instruction_s4a.md。ブランチ `sig-s4a`。

## 1. 結果

実装にあって sig に無かった 44 メソッド (Ruby 40・C 4) を書いた。数える道具の差は `impl_ruby -40, impl_c -4, app_name -2, app_bare -6`。
書き方の欠け (「3. doc gaps」) は 0 のまま。

| クラス | 書いた数 | 内訳 |
|---|---|---|
| `FmrbUI` | 5 | `scrollbar`・`set_range`・`direction`・`bg`・`bg=` |
| `FmrbUI::Widget` | 2 | `bg`・`bg=` |
| `FmrbUI::Stepper` | 1 | `set_range` |
| `FmrbUI::TextField` | 1 | `set_field_text` |
| `FmrbUI::Scrollbar` (新しいクラス) | 7 | `value`・`total`・`visible_count`・`active?`・`set_value`・`set_range`・`hit?` |
| `FmrbApp` | 16 | `closable=`・`resize_corner`・`draw_resize_mark`・`wheel_rows`・`wheel_notches`・`app_settings`・`save_app_settings`・`app_settings_path`・`app_handle_name`・`app_state`・`save_app_state`・`app_state_path`・`detach_ui`・`request_early_update`・`idle_gc=`・`sync_file` |
| `Kernel` | 4 | `send_message`・`receive_message`・`broadcast_message`・`message_queue_exists?` (`def self.`、`sig/kernel.rbs`) |
| `FmrbI18n` | 3 | `strings`・`forget`・`half_kana?` |
| `FmrbAssoc` (新しい `sig/fmrb_assoc.rbs`) | 5 | `resolve`・`extension_of`・`load_table`・`forget`・`merge_file`。定数 `RUN`・`EDIT`・`DEFAULT`・`SYS_PATH`・`USR_PATH`・`MAX_EXT_LEN` も宣言 |

素直 (例だけ) 6、つまずく (型紙の順の散文つき) 38。素直なのは `FmrbUI#bg`・`Scrollbar` の 3 つの読み取り・`app_settings_path`・`app_state_path`。
`FmrbApp` の新しいメソッドは全部 Ruby で書かれているので `%a{fmrb.internal}` を付けた。境界の検査は通り、`sdk/` の差分は無い (`tools/boundary/gen.rb` は流していない)。

## 2. 直した既存のシグネチャ

| 宣言 | 前 | 後 | 根拠 |
|---|---|---|---|
| `FmrbUI.new` | `(FmrbApp app)` | `(FmrbApp app, ?bg: Integer, ?text_size: Integer, ?bg_painter: untyped)` | fmrb-ui.rb:973 (長文は元から bg:・bg_painter:・text_size: を説明していた) |
| `FmrbUI#enum` | `(..., Array[String] options)` | `(..., Array[String] options, ?index: Integer, ?text_size: Integer)` | fmrb-ui.rb:1036。アプリでの使用は無し |
| `FmrbUI::HitAreas#add` | `(untyped id, x, y, w, h)` | `(untyped id, x, y, w, h, ?untyped data)` | fmrb-ui.rb:905。長文は元から 6 番目の data を説明していた。麻雀の牌が使う |
| `FmrbApp#send_message` | `-> void` | `-> bool` | fmrb-app.rb:1254-1257 は `_send_message` の戻り値を返し、app.c:948-989 は成功で true・送れなければ false。長文も型紙の順に書き足した (要約は不変) |
| `FmrbI18n.truncate_to` | `(String str, Integer max_px)` | `(String str, Integer max_px, ?String ellipsis)` | fmrb-i18n.rb:114 (`ellipsis = ".."`) |

長文の直し (シグネチャ以外): `FmrbI18n.t` の例 `btn.set_text(FmrbI18n.t(:save))` は部品の `set_text(text, gfx)` を 1 引数で呼ぶ形で、実装に無い呼び方
(fmrb-ui.rb:268)。コンテナ経由の `@ui.set_text(:save, FmrbI18n.t(:save))` に直した。

## 3. 根拠の表

行は `lib/gems/picoruby-fmrb-app/mrblib/` 以下 (`fmrb-ui.rb`・`fmrb-app.rb`・`fmrb-i18n.rb`・`fmrb-assoc.rb`・`fmrb-assoc-file.rb`) と、
`ports/esp32/app.c` (= `lib/gems/picoruby-fmrb-app/ports/esp32/app.c`)。アプリの欄は使い方を写した所。

### fmrb_ui.rbs

| 書いたもの | 実装 | アプリ・失敗のしかた・場面の根拠 |
|---|---|---|
| `FmrbUI#scrollbar` | fmrb-ui.rb:1030-1033 (`Scrollbar.new` 659-671) | 例は `flash/app/tool/receiving.app.rb:120`・`nsf_player.app.rb:142`。押した瞬間に id: `handle` 1060-1068 (`fires_on_press?` 684 と `activate` 718-721)、離したときは返さない: 1085-1089。端で返らない: `set_value` 696-702。全部見えている間は描かず当たりも無い: `active?` 675・`hit?` 679・`draw_widget` 723-726。ホイールに応えない: fmrb-app.rb:238-248 のコメント。位置はアプリが持つ: fmrb-ui.rb:656-657。on_create 前提は 993 のコメント。on_resize で作り直す例 `main/ruby/default_app/logviewer.app.rb:372-380` |
| `FmrbUI#set_range` | 1292-1297 | `Stepper#set_range` 511 は (min, max)、`Scrollbar#set_range` 687 は (total, visible)。基底は何もしない 215。id が無いと nil 1294。例は `nsf_player.app.rb:178`・`:266`、`receiving.app.rb:249` |
| `FmrbUI#direction` | 1261-1264 | 例は `main/ruby/default_app/logviewer.app.rb:320`。0 の返りは 1263 と `release` 710 |
| `FmrbUI#bg`・`#bg=` | 966-971 | `bg=` が `@bg` と `@bg_theme` だけを書き、既存の部品に写さない・dirty を付けない: 同行。部品が `add` で写す: 1344・126-130。移送で戻らない: `_checkpoint_ground` 1164-1166。例 `main/ruby/default_app/shell/shell_commands.rb:251` |
| `FmrbUI::Widget#bg`・`#bg=` | 116-121 | 使う部品: `Label#draw_widget` 297 / `Stepper#draw_widget` 581 / `Scrollbar#draw_widget` 725・733。Button・Toggle・TextField の `draw_widget` は `@bg` を読まない (352・453・868)。例 (読み取り) `flash/app/test/ckpt_window.app.rb:76` |
| `FmrbUI::Stepper#set_range` | 511-517 | 値の引き戻しは 515 (`set_value`)。同じ範囲は何もしない 512 |
| `FmrbUI::TextField#set_field_text` | 826-831 | nil は空: 828。dup: 同行 |
| `FmrbUI::Scrollbar#value`・`total`・`visible_count` | 663 | 例は書き方の手本 (`Stepper#value`) に合わせた |
| `Scrollbar#active?`・`#hit?`・`#set_value`・`#set_range` | 675・679-682・696-702・687-694 | `hit?` の例 `logviewer.app.rb:348`。`set_value` の clamp 747-751 |
| 直し: `FmrbUI.new`・`enum`・`HitAreas#add` | 973・1036・905 | 2 章 |

### fmrb_app.rbs

| 書いたもの | 実装 | アプリ・失敗のしかた・場面の根拠 |
|---|---|---|
| `closable=` | fmrb-app.rb:118 | 見るのは閉じるボタンのクリックだけ: `_frame_event` 816。例 `main/ruby/kernel/system_desktop.app.rb:385` |
| `resize_corner` | 105-110 | 0 の条件 106-107、定数 `RESIZE_MARK` 46 (+1 = 9)。例 `flash/app/modern/chat/screen.rb:306` |
| `draw_resize_mark` | 228-232 | `window_gfx` に描く 231。例 `main/ruby/kernel/linux_window.app.rb:109`。`draw_window_frame` が自分で呼ぶ 198 |
| `wheel_rows`・`wheel_notches` | 249-255・260-265 | nil の条件と符号: 同行。`WHEEL_LINES` は system_conf.toml の `wheel_lines`: `picoruby_fmrb_const.h:18-19`。例 `flash/app/modern/chat/screen.rb:418`・`main/ruby/kernel/system_desktop.app.rb:2217` |
| `app_settings`・`save_app_settings`・`app_settings_path`・`app_handle_name` | 287-296・355-376 | 戻り値 (読み戻した Hash か nil、TOML でないファイルは触らない) は `FmrbToml.update` (fmrb-toml.rb:81-121)・`FmrbToml.load` (54-72)。例 `flash/app/demo/weather.app.rb:189`・`:275`、`nsf_player.app.rb:100` |
| `app_state`・`save_app_state`・`app_state_path` | 315-351 | 値の型の絞り込み 318-320・330、変わらなければ書かない 336、書けなければ false 341-345。例 `main/ruby/kernel/system_desktop/file_manager.rb:93`・`:106` |
| `detach_ui` | 421-431 | 外さないと古い座標で描き直す: 411-417 のコメントと `_invalidate_attached_uis` 433-441。例 `logviewer.app.rb:377`・`shell.app.rb:575` |
| `request_early_update` | 1108-1111 | 場面 (callback の中だけ): 1085-1103 のコメント。例 `main/ruby/default_app/services.app.rb:159`・`flash/usr/share/transfer/transfer.app.rb:904` |
| `idle_gc=` | 1299-1314 | 世代別が切れる・小分け・失敗時のログ: 1280-1298 のコメントと 1311-1313。例 `flash/app/demo/midi_apu.app.rb:56` |
| `sync_file` | 1250-1252 | `_sync_file` は bool: app.c:967 以降 (`fmrb_kernel_sync_file == FMRB_OK`)。アプリの例は `@gfx.sync_file` のもの (`flappy.app.rb:209`) で、このメソッドを直接呼ぶ同梱アプリは無い |
| 直し: `send_message` | 1254-1257・app.c:948-989 | 届かないとき false (`fmrb_msg_send` が NOT_FOUND・TIMEOUT): fmrb_msg.c:246-251・257-290。上限 176: `FMRB_MAX_MSG_PAYLOAD_SIZE` (fmrb_msg.h:27)・app.c:974。受け手: app.c:572-700 (`"cmd"` が String の Hash だけ、土台の cmd は on_control に届かない、それ以外は 697-701、型 APP_CONTROL 以外は 709 で未処理のログ)。送り手の pid は入らない: 中身は `data` を MessagePack に詰めただけ 1255。例 `flash/app/test/ai_editor_probe.app.rb:64` |

### kernel.rbs (Kernel の C 4 件)

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `Kernel.send_message(task_id, msg_type, data, ?timeout_ms)` | `lib/gems/picoruby-fmrb-kernel/src/picoruby_fmrb_kernel.c:40-69` | 範囲 0-15 と 64 バイトの ArgumentError: 49-57。既定 100: 44。false になる条件 (queue 無し・満杯・上限 9): `fmrb_msg_send` fmrb_msg.c:236-251・257-290、`FMRB_MAX_APPS` = 9 (`components/fmrb_common/include/fmrb_limits.h:28`) |
| `receive_message(task_id, ?timeout_ms)` | 同 86-114 | 既定 1000: 90。nil は TIMEOUT: 105。queue 無し・範囲外は RuntimeError: 107-109 (fmrb_msg.c:297-318 が NOT_FOUND・INVALID_PARAM を返す)。自分の queue を `_spin` と取り合う: app.c:572-573 が同じ `fmrb_msg_receive(ctx->app_id, ...)` |
| `broadcast_message(msg_type, data, ?timeout_ms)` | 同 128-154 | 既定 10: 132。戻りは入った queue の数: fmrb_msg.c:341-390 |
| `message_queue_exists?(task_id)` | 同 166-180 | 範囲外は ArgumentError: 172-174。9 以上は常に false: fmrb_msg.c:400-405 |

### fmrb_misc.rbs (FmrbI18n) と fmrb_assoc.rbs

| 書いたもの | 実装 | 根拠 |
|---|---|---|
| `FmrbI18n.strings`・`.forget`・`.half_kana?` | fmrb-i18n.rb:28-35・61-64・104-110 | `forget` を土台が呼ぶ: fmrb-app.rb:662-663。`half_kana?` の位置はバイト・`n` は bytesize: `text_width` 76-100 の呼び方 (92)、3 バイト残らなければ false: 106 |
| 直し: `truncate_to` | 114-147 | `ellipsis` が付く分も予算に入る: 116-118。入るときはそのまま: 115 |
| `FmrbAssoc.resolve`・`.extension_of`・`.load_table`・`.forget` | fmrb-assoc.rb:47-54・63-73・76-84・88-91 | 答えの 3 形: 1-18 のコメント。`""` になる条件: 65-71 (点なし・末尾・ディレクトリ内・8 文字超)。例 `main/ruby/kernel/system_desktop/file_manager.rb:352`・`default_app/shell/shell_commands.rb:973` |
| `FmrbAssoc.merge_file` | fmrb-assoc-file.rb:107-117 | TOML の誤りは丸ごと読まない: 1-5 のコメントと 108-109 (`FmrbToml.load` が nil)。Spinel 版は `main/ruby/spinel/fmrb_assoc_spinel.rb:8-30` (1 行ずつ) |
| 定数 | fmrb-assoc.rb:33-45 | `RUN`・`EDIT` を `main/ruby/kernel/system_desktop/file_manager.rb:834`・`:838` が使う |

## 4. 載せない候補 (内側。親が ignore.yml に入れる)

合計 73 (クラス#メソッド)。理由は 1 つにまとめられるものが多い。

| クラス | メソッド | 理由 |
|---|---|---|
| `FmrbUI::Widget` (29) | `.new`・`take_ground`・`follow_theme_ground`・`shown`・`shown=`・`dirty=`・`text_h`・`draw_widget`・`relayout`・`press`・`release`・`activate`・`fires_on_press?`・`fit_widget_text` | `FmrbUI` が部品を回すために呼ぶ内側 (`flush`・`handle`・`add`・`move`・`_checkpoint_ground`)。`draw_widget`・`fit_widget_text` は fmrb-ui.rb:187-209 のコメントのとおり Spinel の呼び出しのために名前を決めた内部の形 |
| 〃 | `enabled=`・`visible=` | `FmrbUI#set_enabled`・`set_visible` が dirty も立てて呼ぶ (1299-1317)。直接書くと描き直しの印が付かず、隠した跡が塗られない |
| 〃 | `value`・`set_value`・`on?`・`set_on`・`group`・`set_range`・`set_text`・`option_text`・`field_text`・`set_field_text`・`focus`・`type_key`・`direction` | 基底の何もしない形 (fmrb-ui.rb:196-225)。コンテナが部品の型を問わずに呼ぶための受け皿で、アプリは `@ui.value(id)` などのコンテナ側か、`Stepper`・`Toggle`・`TextField` のそれぞれの宣言を使う。`type_key`・`focus(on)` は部品に直接渡すとコンテナの `@focus` と食い違う |
| `FmrbUI::Label` (5) | `.new`・`draw_widget`・`fit_widget_text`・`relayout`・`set_text` | 同上。`.new` は gfx と text_size を取る内部の組み立てで、アプリは `FmrbUI#label` を使う。`set_text` は迷ったものへ |
| `FmrbUI::Button` (8)・`FmrbUI::Toggle` (8) | `.new`・`draw_widget`・`fit_widget_text`・`relayout`・`press`・`release`・`activate`・`set_text` | 同上 |
| `FmrbUI::Stepper` (8) | `.new`・`draw_widget`・`fit_widget_text`・`relayout`・`press`・`release`・`activate`・`arrow_w` | 同上。`arrow_w` は幅の計算の内部 |
| `FmrbUI::Scrollbar` (7) | `.new`・`draw_widget`・`press`・`release`・`activate`・`fires_on_press?`・`direction` | 同上。`direction` は迷ったものへ |
| `FmrbUI::TextField` (5) | `.new`・`draw_widget`・`activate`・`type_key`・`focus` | 同上 |
| `FmrbUI::Enum` (1) | `.new` | 同上 |
| `FmrbApp` (2) | `.new`・`attach_ui` | `attach_ui` は `FmrbUI.new` が自分で呼ぶ (fmrb-ui.rb:988)。同梱のどのアプリも呼ばない |

迷ったもの (書いていない。親が決める):

- `Label#set_text`・`Button#set_text`・`Toggle#set_text` と `Widget#set_text`: `(text, gfx)` の 2 引数で、アプリは `@ui.set_text(id, text)` を使う。部品に直接呼ぶ同梱アプリは無いが、
  `FmrbI18n.t` の古い例はこの形を誤って 1 引数で呼んでいた。
- `Scrollbar#direction` (`Widget#direction`): 向きの読み取りは `FmrbUI#direction(id)` を書いた。部品に直接読む同梱アプリは無い。
- `Widget#value`・`#set_value` など基底の受け皿: `@ui.find(id)` は `Widget?` を返すので、`@ui.find(:x).value` は今の宣言では指摘される。
  同梱のアプリは `find` の結果に `.w`・`.hit?` しか呼んでいない (`config_dialog.rb:230`・`logviewer.app.rb:348`)。

## 5. アプリが使っているのに実装に見つからなかったもの

この 5 つの範囲で、見つからないものは無い (同梱のアプリ・カーネル・見本の `@ui.xxx` の呼び出しと、`FmrbApp`・`FmrbAssoc`・`FmrbI18n` の呼び出しを grep で引いた)。
関連: `FmrbApp._get_last_error` (`flash/app/modern/ai_helper/editor.rb`、`_` 始まり) は決定 19 のとおり載せていない。

## 6. 数える道具の前後と型 db の大きさ

- `rake sig:coverage` 前 -> 後: sig の 77 クラス・1014 メソッド -> 79 クラス・1058 メソッド (+44。クラスは `FmrbUI::Scrollbar`・`FmrbAssoc` の 2 つ増えた)。アプリが使う未定義: 受け手なし 8 (26 回) -> 2 (4 回)、型の決まらない名前 87 (254 回) -> 85 (247 回)。
  実装にあって sig に無い: Ruby 612 -> 572、C 53 -> 49。基準との差 `app_name -2, app_bare -6, impl_ruby -40, impl_c -4`。`rake sig:coverage:update` で基準を更新 (別コミット)。
  書き方の欠け 0: 要約 111 バイト超 0・`<<en>>` 無し 0・長文なし 0・例なし 0・強調が行をまたぐ 0。
- 型 db (tidbgen を前の版 `4f10dca7` の sig と今の sig で別々に走らせ、`tools/ti/test_sig` を重ねて `ti_builtin_*` を読んだ):

| 領域 | 前 | 後 | 差 | 上限 |
|---|---|---|---|---|
| 名前 | 10,302 | 10,794 | +492 | 65,535 |
| シグネチャ | 39,165 | 41,121 | +1,956 | 65,535 |
| 要約 (文書) | 75,107 | 78,062 | +2,955 | 24 ビット |
| メソッドの数 | 3,315 | 3,442 | +127 | |
| 引数の数 | 2,797 | 2,916 | +119 | |
| クラスの数 | 79 | 81 | +2 | |
| 定数の数 | 195 | 201 | +6 | |

  上限に対して シグネチャ 63%、名前 16%。(S2c の報告の「後」の値と前の列が違うのは、その後の変更が入っているためと思うが、確かめていない。)

## 7. 確認の結果

- `rake ti:test`: 通った (期待値の直しは不要)。
- `rake regress`: 通った (host 34 suites 全部 pass、描画の記録 310 runs pass。境界の検査 `boundary:check` を含み、`sdk/` の変更は無い)。
- `rake sig:coverage`: 通った (上のとおり)。
- ビルドと sim は流していない。

## 8. 親が sim で確かめるとよい項目 (エディタで Ctrl+E で指摘の有無、Ctrl+T で型)

アプリの `on_create` の中か `FmrbApp` を継ぐクラスの中に打つ。`@ui = FmrbUI.new(self)` が前にある前提。

1. `@ui.scrollbar(:sb, 0, 0, 10, 80, 20, 5)` → 指摘なし (scroll は省ける)
2. `@ui.scrollbar(:sb, 0, 0, 10, 80)` → 引数が足りないと指摘
3. `@ui.set_range(:sb, 20, 5)` → 指摘なし (3 引数)
4. `@ui.direction(:sb)` で Ctrl+T → Integer
5. `FmrbUI.new(self, bg: 0, text_size: 2)` → 指摘なし (前は引数過多)
6. `@ui.enum(:m, 0, 0, 90, 14, ["a"], index: 1)` → 指摘なし
7. `@hits.add(:t, 0, 0, 8, 8, tile)` → 指摘なし (6 引数。前は過多)
8. `send_message(pid, 0, { "cmd" => "x" })` で Ctrl+T → bool
9. `app_state("fm")` で Ctrl+T → `Hash[String, untyped]`
10. `save_app_state({ "d" => 1 }, "x")` → 指摘なし
11. `wheel_rows(ev)` で Ctrl+T → `Integer?`
12. `Kernel.send_message(0, 1, "x", 100)` → 指摘なし、引数を 2 つにすると指摘
13. `FmrbAssoc.resolve("/a.md")` で Ctrl+T → String、`FmrbAssoc::RUN` が補完に出る
14. `FmrbI18n.truncate_to("abc", 60, "")` → 指摘なし (前は引数過多)
15. `self.closable = false` と `self.idle_gc = true` → 指摘なし
16. `sync_file("/a", dest: "/b")` → 指摘なし。F1 で `FmrbUI#scrollbar` の長文が開く

## 9. 読んでいて見つけた実装の不具合の疑い (直していない・動かして確かめていない)

- **`Kernel.send_message`・`receive_message` の C 結びの `fmrb_msg_t` が本体の型と別物に読める**:
  `picoruby_fmrb_kernel.c:19-23` は自前で `{ uint32 type; uint32 size; uint8 data[64]; }` (72 バイト) を定義しているが、本体は
  `{ type; src_pid; size; data[176] }` (`components/fmrb_msg/fmrb_msg.h:40-45`) で、queue の 1 件の大きさは後者 (`fmrb_msg_create_queue` の既定 `sizeof(fmrb_msg_t)`)。
  `receive_message` は queue から 1 件ぶんを 72 バイトの局所変数へ書くので、**局所変数を越えて書く (スタックを壊す) ように読める**。`send_message` は
  逆に読み過ぎで、受け手に渡る `size`・`data` も 4 バイトずれる。同梱のアプリ・カーネルのどれもこの 4 つを呼んでいない (README.md の例だけ) ので、
  今は潜在。sig の長文は「アプリ間の伝言には `FmrbApp#send_message`」と書き、こちらの形は保証していない。
- **`FmrbUI#bg=` は既存の部品の色を変えない**: `bg=` は `@bg`・`@bg_theme` だけを書く (fmrb-ui.rb:968-971)。部品は `add` で色を写して持つので、Label・Stepper・
  Scrollbar の中の色は `bg=` のあとも古いまま。シェルの `apply_colors` (`shell_commands.rb:248-252`) は `@ui.bg =` と `invalidate_all` だけを呼ぶ。そこの部品は Button だけで
  `@bg` を使わないので目に見えない可能性がある。sig には事実のとおり書いた。
- **`FmrbI18n.add` の sig の長文「すでにある印を消さない / Existing keys are left alone」は読み方で外れる**: 実装 (fmrb-i18n.rb:37-43) は同じ印があれば**上書き**する
  (他の印を消さない、の意味なら正しい)。既存の宣言で、触っていない。
- **`FmrbGfx#sync_file` の宣言が `(String path)` のまま**: 実装は `sync_file(path, dest: nil)` (fmrb-gfx.rb:345) で、アプリは `dest:` を渡している
  (`flappy.app.rb:209`・`shooter.app.rb:180`・`blockgame.app.rb:604`)。決定 22 のとおり `?dest: String` を足すとよい。`sig/fmrb_gfx.rbs` は今回の範囲外なので触っていない。
- **Spinel の基底には `app_settings`・`save_app_settings`・`app_settings_path` が見つからなかった**: `main/ruby/spinel/fmrb_app_base_spinel.rb` に `def` が無い
  (`app_state` 系・`wheel_*`・`closable=`・`idle_gc=` ・`request_early_update`・`detach_ui` は有る)。Spinel のアプリがこれを呼ぶと動かないはず。sig は mruby の基底のとおり書き、
  この差は書いていない。
- **`Kernel.*` の task_id の範囲の検査 (0-15) と本体の上限 `FMRB_MAX_APPS` = 9 が合っていない**: 9-15 は検査を通り、`send_message` は false、`receive_message` は RuntimeError になる
  (sig はそのとおり書いた)。
- 部品の mrblib の注釈 (fmrb-ui.rb:993) は「作るのは on_create だけ」に読めるが、logviewer・shell は `on_resize` で作り直している。sig は「前提」と書いた。

確かめていないこと: 上の `Kernel.*` の局所変数の大きさ (sizeof の実測)、`FmrbUI#bg=` の見え方、Spinel の `app_settings`、エディタでの新しい宣言の補完と F1 の見え方
(`rake ti:test` が通っただけで、sim のエディタでは見ていない)。

## 10. コミット

| ハッシュ | 件名 |
|---|---|
| 4bf09a0f | feat(sig): FmrbUI scrollbar, set_range, direction, bg and the Scrollbar widget |
| f5677438 | feat(sig): FmrbApp settings, state, wheel, early update, idle GC and detach_ui |
| f30f89a4 | feat(sig): Kernel task-queue messages, FmrbAssoc and the rest of FmrbI18n |
| 616bc5df | test(sig_coverage): baseline after the UI, FmrbApp, message, FmrbI18n and FmrbAssoc entries |
| (この文書) | docs(sig_coverage): report for the widgets, the rest of FmrbApp, messages, FmrbI18n and FmrbAssoc |
