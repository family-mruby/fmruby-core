# W3b: 当たり判定を申告した形に置き換える・長押しで閉じる

> 状態: 報告 | 更新: 2026-11-11 | W3 の画素の問い合わせをやめ、枠なしの Window はアプリが申告した形 (矩形の一覧・ビットのマスク・印を付けた sprite の和) で当たるようにした。申告が無ければ Window の四角。形は C の表 (PSRAM、1 アプリ 2 KB まで) に置き、カーネルは押下のときにそれだけを見る (描画側へ往復しない)。形はアプリの状態として `amove` で運ばれ、着いた側で基底が申告し直す。`window_drag = "anywhere"` の枠なしの Window を動かさずに 1 秒押し続けると `on_frameless_right_click` (既定は `on_quit_request`) が呼ばれる。見本のアバターは絵の文字列からマスクを作って申告し、吹き出しを押すと吹き出しを閉じる。ネイティブ版 (標準・mruby のカーネル)・ブラウザ版・sim の Retro で確かめ、回帰の道具 (regress・screens・checkpoint・spinel・size) は通った。graphics-audio は変えていない

指示書は `doc/frameless_window/instruction_w3b.md`、計画は `plan.md`、前段は `report/w3.md`・`report/w4.md`。

## 1. 外したもの

| 所 | 内容 |
|---|---|
| `main/app/fmrb_app.c`・`fmrb_app.h` | `fmrb_app_window_see_through_at` (押した 1 画素を GET_PIXEL で問う口) |
| `components/fmrb_gfx/fmrb_app_canvas.c/.h` | `fmrb_app_canvas_key` (W3 の判定のためだけにくくり出した手伝い)。W3 の前の形に戻した |
| `main/kernel/fmrb_spx_kernel.c`・`fmrb_spx.h`・`fmrb_ffi.rb`・`fmrb_kernel_base_spinel.rb`・`lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c` | カーネルの `_window_see_through?` (Spinel と mruby)。`_window_hit?` に置き換えた |

残したもの: display_local の `GET_PIXEL` の色の直し (W3 の 3 章。アプリの `get_pixel` のための正しい直し)。

## 2. 足したもの

### 2.1 形の置き場と判定 (C)

| 所 | 内容 |
|---|---|
| `components/fmrb_common/include/fmrb_app.h` | 形のバイト列の決まり (`FMRB_HIT_*`)、`fmrb_app_set_hit_shape(pid, data, len)`・`fmrb_app_hit_shape_place_sprite(pid, sprite_id, x, y, visible)`・`fmrb_app_window_hit_at(pid, x, y)` |
| `main/app/fmrb_app.c` | `g_hit_shape[FMRB_MAX_APPS][2048]` と長さ (どちらも `FMRB_EXT_RAM_BSS_ATTR` で PSRAM、`g_ctx_lock` の下)。申告のときに全部の項目を検査し、壊れた・長すぎるものは断る。slot を渡し直すとき (`reset_ctx_run_state`) に長さを 0 に戻す。判定は 枠なし・全画面でない・申告あり のときだけ項目を歩き、ほかは true (四角) |
| `lib/add/picoruby-fmrb-app/ports/esp32/app.c` | `FmrbApp._set_hit_shape(str)`・`FmrbApp._hit_sprite_place(id, x, y, visible)` (クラスメソッド。呼んだタスクの ctx に効く)。APP_CONTROL `"long_press"` を `_handle_system_control` へ |
| `main/app/fmrb_spx_app.c/.h`・`fmrb_app_ffi.rb` | Spinel の同じ口 `fmrb_spx_app_set_hit_shape`・`fmrb_spx_app_hit_sprite_place` |
| カーネルの呼び口 (`fmrb_spx_window_hit`、mruby の `_window_hit?`) | `fmrb_app_window_hit_at` を返すだけ |

形のバイト列 (1 項目 12 バイト + マスクの行。リトルエンディアン):

| バイト | 内容 |
|---|---|
| 0 | 種類 (1 矩形 / 2 マスク)。0x80 が立っていれば隠れている (当たらない) |
| 1 | 1 マスの画素数 (マスク。矩形は 1) |
| 2..5 | x, y (int16、Window の中の画素) |
| 6..9 | w, h (矩形は画素、マスクはマス) |
| 10..11 | sprite の id (sprite の項目でなければ 0) |
| 12.. | マスクだけ: h 行 × (w + 7) / 8 バイト、左のマスが上位ビット。立っているマスが当たる |

決め (指示書の「形は report で決める」): **マスクは行ごとのビット列 + 拡大率**にした。ドット絵を 3 倍で描くアバターの 48x48 の当たりが 16 行 × 2 バイト = 32 バイトで済み、画素単位 (拡大率 1) もそのまま書ける。

### 2.2 基底の手伝い (mruby の `fmrb-app.rb`・新しい `fmrb-hit-mask.rb`、Spinel の基底に同じ形)

| 口 | 内容 |
|---|---|
| `set_hit_shape([[x, y, w, h], ...])` | 矩形の一覧を置き換える (`[]` で外す) |
| `set_hit_mask(mask, x = 0, y = 0)` | マスク 1 つを置く (nil で外す) |
| `hit_mask_from_art(rows, transparent: ".", scale: 1)` | ドット絵の文字列 (1 文字 1 マス、ASCII) から `FmrbHitMask`。`transparent` に含まれる文字は透ける (複数文字可) |
| `add_hit_sprite(sprite, mask = nil)` / `remove_hit_sprite(sprite)` | sprite に印を付ける・外す。印の付いた sprite の四角 (mask を渡せばその形を sprite の左上に) が当たりに入る |
| `clear_hit_shape` | 全部外して四角に戻る |
| `FmrbHitMask` | `new(width, height, bits, scale = 1)`、`from_art`、`set?(cx, cy)`、`width`・`height`・`scale`・`bits` |

- 基底は 3 つの一覧を ivar (`@_hit_rects`・`@_hit_mask` と位置・`@_hit_sprites`) に持ち、変わるたびに全体をバイト列に詰めて申告する (詰めるのは大きさを先に数えて `"\x00" * n` + `setbyte`。Spinel でも同じ書き方)。
- **sprite は位置を自分で覚える**ようにした (`SpriteInstance#x`・`y`・`width`・`height`・`visible?`、`fmrb-sprite.rb`)。印の付いた sprite の `move`・`visible=`・`destroy` は `FmrbApp._hit_sprite_place` で C の表の該当項目の位置と隠れの印だけを書き換える (全体を申告し直さない。印の無い sprite は何もしない)。sprite は基底を知らないので、口をクラスメソッドにした (Spinel のデスクトップも `fmrb-sprite.rb` を使うので、sprite から基底への参照を持たせない形にした)。
- 判定は和: 矩形のどれか・マスクの立っているマス・見えている印付き sprite のどれかに入れば当たり。何も申告していなければ (一覧が全部空なら) 四角。

### 2.3 移るとき (checkpoint)

一覧は ivar なのでイメージに入る。`_checkpoint_restored` で、`clear_user_area` などの後・`on_restore` の前に、**sprite の印を捨ててから**申告し直す。sprite は移るときに忘れられ (`_checkpoint_forget`)、アプリが `on_restore` で作り直すので、印もそこで付け直す決まりにした (`movable_apps.md` 3.3、spec.md 3.4)。着いた側の C の表は新しい slot なので空から始まる。

### 2.4 長押し (カーネル `input_router.rb`・`app_lifecycle.rb`)

- `window_drag = "anywhere"` の押下を預かるとき (`:press`)、押した時刻 `@press_ms` と、長押しにしてよいか `@press_long_ok` (枠なし かつ Ruby の基底を持つアプリ = vm が mruby か native) を控える。
- `tick_process` の `tick_long_press`: `:press` のまま 1000 ms (`LONG_PRESS_MS`) たったら `:long` にし、アプリへ APP_CONTROL `{"cmd" => "long_press", "x", "y"}` (押した点、Window の中) を送る。預かっていた押下は捨てる。
- `:long` の間の move はアプリへ送らず、ドラッグにもならない。離しも送らない (押下も離しもアプリには届かない)。左を押している間の別のボタンの離しは今までどおり送る。
- 1 秒の間に 4 px より動けば今までどおりドラッグ、1 秒より前に離せば今までどおりクリック。
- 基底 (mruby・Spinel): `"long_press"` を受けたら、枠なしで全画面でなければ `on_frameless_right_click({type: :long_press, button: 1, x:, y:})`。既定は `on_quit_request` (既定は閉じる)。右クリックと同じ口なので、アプリが上書きすれば両方が変わる。区別したいアプリは `ev[:type]` を見る。

新しい kernel の ivar は `@press_ms` (int) と `@press_long_ok` (bool)。`:long` は `@capture_mode` の値が 1 つ増えただけ。

### 2.5 見本のアバター (`flash/app/demo/avatar.app.rb`)

- 絵を描いたら (`paint_avatar` が何か描いたとき)、画面に出ている 16 行 (`@shown`) から `hit_mask_from_art(@shown, transparent: ".", scale: 3)` を作り、ビット列が前と違えば `set_hit_mask(mask, AV_X, AV_Y)`。変わるのは芽の揺れのときだけ (顔は輪郭を変えない)。
- 吹き出しを描いたら、吹き出しと尻尾の矩形を `set_hit_shape([rect])`、消したら `set_hit_shape([])`。
- 吹き出しを押したら (W4 の残件)、なでたことにせず吹き出しを閉じる。

### 2.6 文書

- `doc/app-distribution/spec.md` 3.4: 透けた画素の判定の項を「当たりの形」の項に書き換え、長押し、見本の 1 行を足した。
- `doc/checkpoint/movable_apps.md` 3.3: 形が運ばれ、sprite の印は `on_restore` で付け直す。
- `sig/fmrb_app.rbs`: `set_hit_shape`・`set_hit_mask`・`hit_mask_from_art`・`add_hit_sprite`・`remove_hit_sprite`・`clear_hit_shape`、`on_frameless_right_click` の説明に長押し。`sig/fmrb_sprite.rbs`: `SpriteInstance#visible=`・`visible?`・`x`・`y`・`width`・`height`、`FmrbHitMask`。
- リポジトリの外: family-mruby の `.claude/skills/fmrb-app-new/SKILL.md` の枠なしの節 (コミットしていない)。

## 3. 確かめたこと

試験のアプリは `hit_test` (リポジトリの外: scratchpad の `w3b/homeA/home/hit_test.app.rb`)。140x100 の枠なし、(120,60) に置く。矩形 2 つ (赤 (10,10,40,30)・緑 (10,60,20,20)) を `set_hit_shape`、青い 16x16 の sprite (80,10) に印 (四角)、黄色いひし形の sprite (80,60) に印 (8x8 の文字列から作ったマスク、拡大率 2)。左クリックの場所をログに出し、赤の左上 (rel x,y < 30) を押すと青を 40 px 右へ動かし、ひし形の見え隠れを切り替える。画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/w3b/` (以下ファイル名だけ)。

### 3.1 ネイティブ版 (標準構成、CLI、run dir `/tmp/fmrb-w3b-a`・`-b`、利用者の場所は scratchpad の `w3b/homeA`・`homeB`)

| 確かめたこと | 結果 | 画面 |
|---|---|---|
| Kamon の上の hit_test: 赤・緑の矩形、青 (sprite の四角)、ひし形の真ん中 (sprite のマスク) | 4 つともアプリへ (`click at (30,25)`・`(20,70)`・`(88,18)`・`(88,68)`)。sprite の上がすり抜けない | n8_hit_test.png |
| 青を動かした後、元の場所 | 後ろへ抜け `Brought 'Kamon' to front` | n9_sprite_moved.png |
| 青の新しい場所 | アプリへ (`click at (128,18)`) | — |
| 隠したひし形の場所・何も無い所 | アプリへ届かない (後ろへ) | n10_no_kamon.png |
| ひし形を見せ直して、角 (透ける) と真ん中 | 角は抜け、真ん中はアプリへ (`(88,68)`) | — |
| アバター (W4) が Kamon の上: 絵の外の透けた所 (Kamon の「大きさ <」の上) | 抜けて Kamon が 70% → 60% に | n5_avatar_over_kamon.png、n6_through_to_kamon.png |
| アバターの胴 | pat 1 | n2_pat.png |
| 吹き出しを押す | 吹き出しが消え、pat は増えない | n3_bubble_click.png |
| 胴を長押し (途中で 2 px 動かした) | `Long press: PID 4` → `App Avatar quit request` → 閉じた | n7_long_press.png |
| 胴を掴んでドラッグ (W2) | 動いた (長押しにならない) | — |
| `app move` で A → B (アバター、34,500 バイト) | B の Kamon の上に着き、胴で pat 2、吹き出しを押すと閉じ、透けた所 (Kamon の円の上) は Kamon の前面化へ | b1_arrived.png、b2_pat.png、b3_bubble_closed.png、b4_through_kamon.png |
| `app move` で A → B (hit_test、24,729 バイト。`on_restore` は何もしない) | 基底が申告し直した矩形で当たり (`click at (30,25)`)、矩形の外は Kamon へ抜けた | b5_hittest_arrived.png |

### 3.2 ネイティブ版 (mruby のカーネル、`--engine mruby`、run dir `/tmp/fmrb-w3b-m`)

Kamon の上の hit_test で、赤の矩形・ひし形の真ん中がアプリへ、ひし形の角は抜けて Kamon が前面へ。Kamon を閉じてから赤を長押しして `fmrb_kernel: Long press: PID 6` → `quit request`。m1_mruby_kernel.png。

### 3.3 sim の Retro (NARYAv3、320x240)

置いた `flash/home/w3b/` の hit_test で、Kamon の上の 4 つ (矩形 2・sprite の四角・sprite のマスク) がアプリへ。青を動かした後の新しい場所はアプリへ、隠したひし形の場所は抜けて Kamon が前面へ。長押しで `Long press` → `quit request`。アバター: 胴でなでて (pat 1)、吹き出しを押すと閉じ (pat は増えない)、長押しで閉じた。s1_retro_bubble_closed.png。graphics-audio は変えていない。`flash/home/w3b/` は消した。

### 3.4 ブラウザ版

`rake wasm:web` で作り直し、`/flash/home` に hit_test を置いて Shell の `run` で起動 (Shell の上)。赤の左上を押すと青が動き (矩形が当たる)、動いた先の青を押してもアプリが前のまま (当たる)、隠したひし形の場所を押すと Shell が前面に来た (抜ける)。Shell を閉じ、赤を長押しすると閉じた。置いた 2 つは消し、`/flash/home` は元の 4 つ (asterism/・mahjong.toml・walk_scroller/・walk_scroller.toml) のまま。

### 3.5 回帰の道具

| コマンド | 結果 |
|---|---|
| `rake regress` | host 25/25、描画 229/229 (sig を足した後に `rake clean` してもう一度流して同じ) |
| `rake regress:screens` | 33/33 (modern 11・retro 9・web 6・native 3・native-keys 4、エディタの 1 打鍵を含む)。基準は変わらない |
| `rake regress:checkpoint` (BOARD= なし) | standard・mruby・web (standard) とも ok |
| `rake regress:spinel` | 6 プログラムとも生成できた |
| `rake regress:size` | (下の 3.6) |

Spinel の ivar の並び: 生成 C で `sp_EditorApp` は `sp_FmrbApp` の 37 フィールドの先頭一致、`sp_SystemDesktopApp` は 31 フィールドの先頭一致 (デスクトップの生成 C では Spinel が使われない形の ivar を落としていて、`iv__hit_*` が無い。一致は保たれている)。

### 3.6 内蔵 RAM

`rake regress:size` (基準 4e7e7e61): NARYAv3 DIRAM 125,411 (−48)・IRAM 16,384 (±0)、TAB5 DIRAM 129,338 (−360)。W1/W2・W3 の測定と内蔵の数字は同じ (基準との差は前のコミットの分)。Spinel の TU の static 内蔵 RAM も kernel 128 B・editor 92 B のまま。形の表 (14 KB) と長さは `FMRB_EXT_RAM_BSS_ATTR` で PSRAM に置いた。**内蔵 RAM の増分は 0**。

## 4. 仮決め・見立てと違った所

- 仮決め: 形の上限は 1 アプリ 2 KB (`FMRB_HIT_SHAPE_MAX`)。超える申告は基底が警告して断り (`false`)、前の形のまま。PSRAM に 7 × 2 KB = 14 KB を static に取る (ctx の pool と同じ置き方)。足りなければ拡大率を上げる。
- 仮決め: 申告が全部空 (矩形・マスク・sprite が無い) なら四角。印付きの sprite が全部隠れているときは「申告はあるが当たる所が無い」で、Window 全体が抜ける。
- 仮決め: 長押しは `window_drag = "anywhere"` の枠なしの Window だけ (カーネルが押下を預かる形だけが時間を測れる)。`"none"` のアプリには押下と離しがそのまま届くので、長押しは自分で扱う。Lua・BASIC・MicroPython のアプリには送らない (押し続けても、離せば今までどおりクリック)。
- 仮決め: 長押しの後に動かしてもドラッグにしない。離しも送らない。
- 指示書の「既定は『閉じますか』の問い」は、右クリックの既定と同じ `on_quit_request` (Ctrl+Q と同じ、アプリへの終了の頼み) と読んだ。`on_quit_request` を上書きしていないアプリ (アバターを含む) は**問いの画面を出さずにそのまま閉じる**。確認の画面を出す口は足していない (足すなら、右クリックと長押しの既定を両方変えることになる)。
- 見立てと違った所: sprite の位置は基底でなく C の表で追う形にした。`SpriteInstance` から基底へ参照を持たせると、同じファイルを使う Spinel のデスクトップに型の推論の分かれ目が増えるため、`FmrbApp._hit_sprite_place` (クラスメソッド、呼んだタスクの ctx に効く) を sprite が呼ぶ形にした。
- Spinel の基底の手伝いは生成・コンパイルまで確かめた (エディタの生成 C に入っている)。Spinel で動く枠なしのアプリは今無いので、実行では確かめていない。

## 5. 残件

- **確認の画面を出す既定**: 長押し・右クリックで「閉じますか」の画面を出したい場合は、デスクトップの confirm_dialog を使う口を基底に足す (ユーザの判断)。
- **sprite のコマ**: sprite の印の形は 1 枚目の絵の大きさ (か渡したマスク 1 つ) で、`frame=` でコマを替えても形は替わらない。コマごとに形が違う sprite は `add_hit_sprite(sp, mask)` を付け直す。
- Tab5・S3 の実機では確かめていない。長押しの 1 秒と 4 px は Tab5 の指 (タッチは相対移動) では測っていない。
- `tool/spinel_poc/harness_input_router.rb` の偽物は `_window_hit?` (常に true) に替えた。CRuby で全件が `NoMethodError` になるのは W3 の前から同じで、直していない。

## 6. 後片付け

ネイティブ版 3 つ (run dir `/tmp/fmrb-w3b-a`・`-b`・`-m`) は止めた。利用者の場所は scratchpad の `w3b/homeA`・`homeB`。~/.local/share/family-mruby-dev には触れていない。sim は止め、`flash/home/w3b/` を消した。ブラウザ版は止め、置いた 2 つを消した。ネイティブ版の run dir は消した。build/ は Retro (NARYAv3) の Linux ビルド (x86-64、regress:size が元に戻したもの)。native/build・native/build-mruby・ブラウザ版のバンドルは今のソースで作ったもの。

## 7. 追補 W3c: 長押しは問いを出してから閉じる (2026-11-11)

ユーザの判断: タッチだけの端末で枠なしの Window を 1 秒押し続けたら「<名前> を閉じますか?」と聞き、はいのときだけ閉じる。4 章の読み (長押しは問わずにそのまま閉じる) と 5 章の残件「確認の画面を出す既定」をこれで片付けた。右クリックの既定は変えていない (そのまま `on_quit_request`)。

### 7.1 変えたもの

| 所 | 内容 |
|---|---|
| 基底 (mruby `fmrb-app.rb`・Spinel `fmrb_app_base_spinel.rb`) | `on_frameless_right_click` の既定が `ev[:type] == :long_press` なら新しい `request_close_confirm`、ほか (右クリック) は今までどおり `on_quit_request`。`request_close_confirm` はカーネルへ APP_CONTROL `{"cmd" => "close_confirm"}` を送るだけ |
| カーネル `fmrb_kernel.rb` | `"close_confirm"`: 送り手の名前 (`info[:name]`) と pid を付けてデスクトップへ `{"cmd" => "close_confirm", "pid", "name"}`。`"close_confirmed"`: デスクトップから来たときだけ、その slot に同じ名前のアプリがまだいれば `{"cmd" => "quit_request"}` を送る (Ctrl+Q が組み込みのアプリに送るのと同じもの) |
| デスクトップ `system_desktop.app.rb` | `"close_confirm"` を受けて、今ある確認の画面 (`open_confirm_dialog`) を `close_app_question(name)` の文で開く。はいの答えは `"close_confirmed"` と `close_pid`・`close_name` (電源の項目・reload と同じ形でカーネルへ戻る)。いいえ・Esc・外のクリックは何もしない |
| `system_desktop/i18n.rb` | `close_app_confirm`: "Close %s?" / "%s を閉じますか?" (`%s` は `fmgr_delete_question` と同じく手で差し込む) |
| `sig/fmrb_app.rbs`・`doc/app-distribution/spec.md` 3.4 | 長押しの既定の説明、`request_close_confirm` |

はいの後の終わり方は前と同じで、アプリの `on_quit_request` が呼ばれる (上書きしていなければ閉じる)。アプリが `on_frameless_right_click` を上書きすれば長押しも今までどおりアプリのもの。アプリが自分で `request_close_confirm` を呼んでもよい。

### 7.2 確かめたこと

ネイティブ版 (標準構成、run dir `/tmp/fmrb-w3c`、利用者の場所は scratchpad の `w3c_home`) でアバター (`/app/demo/avatar.app.rb`)。画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/w3c/`。

| 確かめたこと | 結果 | 画面 |
|---|---|---|
| 胴を長押し | `Long press: PID 4` の後、デスクトップに「Close Avatar?」 | 2_long_press_question.png |
| いいえ | 閉じない (ps に Avatar が残る) | 3_no_keeps.png |
| アバターを確認の画面の真下へ動かして長押し | 確認の画面が Window の上に出る (重なりの順は問題なし) | 4_question_over_window.png |
| はい | `Close confirmed: quit request to pid=4` → `App Avatar quit request` → 閉じた | 5_yes_closed.png |
| 長押しの後、確認の画面の外をクリック | 閉じない | 6_outside_click_keeps.png |
| 右クリック | 問わずにそのまま閉じた (`quit request` だけ、`Close confirmed` は出ない) | 7_right_click_closed.png |
| language = "ja" | 「Avatar を閉じますか?」 | 8_question_ja.png |

回帰の道具: `rake regress` (host 25/25、描画 229/229、`rake clean` の後)、`rake regress:screens` 33/33 (エディタの 1 打鍵を含む)、`rake regress:spinel` 6/6。Spinel の生成 C で、基底の既定は `ev[:type]` を記号で比べる分岐になっていた (黙って 0 になる形ではない)。全部 Spinel の構成のデスクトップも `close_app_question` を含めて生成できた。

### 7.3 残り

- mruby のカーネル (`--engine mruby`) とブラウザ版・sim では動かしていない (カーネル・デスクトップの Ruby は同じソース)。
- keys のデスクトップ (`system_desktop_keys.app.rb`) は確認の画面を持たないので、そこでの長押しは何も起きない。ポインタの無い構成なので長押しも来ない見込み。
- ネイティブ版は止め、run dir は消した。~/.local/share/family-mruby-dev には触れていない。
