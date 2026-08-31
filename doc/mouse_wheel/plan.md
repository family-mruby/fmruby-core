# マウスホイール対応 検討と計画

> 状態: 進行中 | 更新: 2026-08-31 | **W1 完了** (report/w1.md、ブラウザで実測)。残りは W2 後半 (遠隔) / W3 (実機・白名簿) / W4 (アプリ個別)

ログとランチャーをホイールで送りたい、という要望 (2026-08-31)。
入力の一番下 (USB HID の report descriptor) から一番上 (アプリの
`on_event`) まで通す話になるので、全部の継ぎ目を数えてから決める。

## 現状: ホイールはどこにも無い

`wheel` を名乗るものは、コメントアウトされた nanoKVM 用レイアウトの中に
一度出てくるだけで、**経路のどの層にも通っていない**。

### 入力源 (5 つ)

| 源 | 今あるもの | ホイールをどう採るか |
|---|---|---|
| USB HID マウス (実機 S3 / P4) | `hid_report_parser` が descriptor から buttons / x / y **だけ**を抜く | Usage 0x38 (Wheel) を足す。**最大の難所、後述** |
| Linux sim (SDL) | `sdl2-display/main.c` が `SDL_MOUSEBUTTON*` と `SDL_MOUSEMOTION` を送る | `SDL_MOUSEWHEEL` を足す。**加えて中継 graphics-audio `input_linux/input_handler_ipc.c` の型 switch にも足す** (知らない型を捨てる。W1 で踏んだ) |
| 注入 (`tools/fmrb_input.rb` → UNIX ソケット) | move / click / key | `wheel N` を足す |
| ブラウザ (wasm) | `input_wasm.c` の ring に 4 種 | ページの `wheel` イベント → ring 型 5 |
| 遠隔デスクトップ (Tab5, `rd_input.c`) | move / button / key の 3 メッセージ | 型を 1 つ足す (ブラウザ側の JS も) |

### 中核 (4 つの継ぎ目)

1. **`main/include/fmrb_hid_event.h`** — sim と WROVER の線を流れる wire 形式。
   型 `HID_EVENT_MOUSE_WHEEL = 0x12` と構造体を足す。
   **長さ前置のプロトコルなので、旧い受け手は読み飛ばすだけ** — 片側だけ
   新しくしても壊れない。
2. **`host_task.c` の入口** — `fmrb_host_send_mouse_wheel(x, y, delta)` を
   足す。`fmrb_host_send_mouse_move` にある 33ms の間引きには**乗せない**
   (移動は最後の 1 つが正しければよいが、ホイールは 1 つ 1 つが意味を持つ)。
   代わりに**同方向の連続分を合算**して 1 メッセージにする。
3. **`components/fmrb_msg/fmrb_hid_msg.h`** — カーネルへ渡す subtype。
   `HID_MSG_MOUSE_WHEEL = 10` と payload (subtype, delta(int8), x, y)。
4. **`components/fmrb_msg/fmrb_hid_event.h`** — デコード済み構造体。
   `int8_t wheel` を足す (`value` は gamepad 軸のもので、共用すると読めない)。

### アプリまで (3 か所)

5. **`input_router.rb`** — 誰に配るか。**鍵と同じ経路 (フォーカスのある窓)**
   を通す。クリックのような hit test はしない (方針を参照)。
   ドラッグ中・全画面時は既存の分岐に従う。
6. **アプリ基底 2 か所** — mruby は `app.c` の `HID_SET_TYPE` 群、Spinel は
   `fmrb_app_base_spinel.rb` の `_parse_hid_event`。
   `{ type: :mouse_wheel, x:, y:, delta: }` を渡す。
   **片方だけ直すと「標準構成でだけ効かない」**が起きる (窓枠の直値と同じ轍)。
7. ~~**`FmrbUI` の scrollbar が自分で拾う**~~ — **この案は外れた** (W1 で
   判明。どの画面もスクロール位置を自分で持ち、widget へは描画のたびに
   書き込むので、widget 内で動かした値は上書きされて消える)。実際には
   アプリ基底の `wheel_rows(ev)` が ノッチ × WHEEL_LINES を返し、各画面が
   自分の刻みで動かす。以下は当初の見立て:
   `@ui.handle(ev)` が `:mouse_wheel` を見て、その窓に届いた以上は自分の
   値を動かし、widget の id を返す (窓の中のどこにポインタがあるかは見ない
   — 1 つの窓にスクロールバーは 1 本しか無い)。
   これだけで既に scrollbar を使っている **7 画面**が動く:
   file_manager / logviewer / launcher / file_selector / shell /
   nsf_player / smf_player。

## 最大の難所: 実機の USB マウスは、今のままではホイールを送ってこない

**Boot Interface のマウスには `SET_PROTOCOL(Boot)` を投げている**
(usb_task.c:1361)。Boot Protocol のマウスレポートは**3 バイト固定
(buttons, X, Y) でホイールの席が無い**。つまり descriptor を読む以前に、
**線の上にホイールのデータが乗っていない**。

しかもこの SET_PROTOCOL の呼び出し位置は、**過去に列挙の不具合を出した
場所そのもの**である (低速デバイスで EP0 が競合し STATUS ACK を落とす件。
`doc/archive/` の USB 列挙の記録と [[project-usb-lowspeed-enum-issue]])。
ここを触るのは「ホイールが増える」以上に「今動いているマウスが動かなく
なる」危険がある。

### 採る道: 白名簿 (ユーザ判断 2026-08-31)

**`hid_devices.toml` に `wheel` を書いた機器だけがホイールを持つ**。
自動判別 (レポートが 3 バイトを超えていたら 4 バイト目をホイールとみなす)
も、Report Protocol への全面切替もやらない。

この選び方は**既存の仕掛けにそのまま乗る**ことが分かった:

- `usb_task.c:1360` は `if (!toml_matched && is_boot_device ...)` で
  SET_PROTOCOL を出している。つまり **TOML に載った機器は最初から
  Boot Protocol に落とされない** — 白名簿に書くこと自体が「この機器は
  素のレポートのまま扱う」の意思表示になる。新しい分岐は要らない。
- レポートの読み出しは既に TOML のレイアウト (buttons/x/y) 駆動なので、
  `wheel` を 1 フィールド足すだけで同じ経路を通る。
- `hid_devices.toml` には既に **"Byte 4 is wheel (ignored)"** と書かれた
  実機の例がある (汎用 OEM マウス、5 バイト・12bit packed)。**位置は
  分かっていて、読んでいなかっただけ**。最初の 1 台はこれで通せる。

代償ははっきりしている: **書いていないマウスではホイールが動かない**。
そのぶん「今動いている機器の挙動は 1 バイトも変わらない」ことが構造で
保証される。載せ方は HID Inspector で dump を見て `wheel` の offset を
書く、という既存の手順そのまま。

## 方針 (先に決めること)

以下はユーザ判断 (2026-08-31)。

- **単位は「ノッチ」**。HID の Wheel は刻みで来るので ±1 のまま運ぶ。
  アプリ側には 2 つの読み方がある: `wheel_rows` (文字行 = ノッチ ×
  wheel_lines) と `wheel_notches` (刻みそのもの)。**行の高さが文字行でない
  一覧 (ランチャーのタイル) は後者**。
  **「1 ノッチ = 何行」はシステム設定 (`system_conf.toml`) で変えられる**
  ようにする。`mouse_scale_x/y` の隣に `wheel_lines`(既定 3) を置き、
  Config ダイアログにも行を足す。
  値をアプリまで届ける道は 1 本増える: テーマ色と同じく
  **`FmrbConst::WHEEL_LINES`** として VM に入れる (C の loader、
  `picoruby-fmrb-const`、Spinel の定数生成器の 3 か所。テーマ色が通っている
  経路をなぞるだけ)。
- **向きは「奥に回すと上」**。指を前へ押し出すと、見ているものが上へ動く
  (= 一覧の前の方へ戻る) 側を正とする。
- **配り先はフォーカスのある窓**。ポインタの下ではない — 煩雑さを避ける
  ため。**代償は「まず窓をクリックしてから回す」必要があること**。
  ルータ側は鍵と同じ経路を通るだけなので、後で「下の窓」に変えたく
  なったら 1 か所の差し替えで済む。
- **間引かない、合算する**。
- **水平ホイール (AC Pan) はやらない**。席だけは空けておく (delta が
  1 軸で足りなくなったら型を増やせるよう、payload に予備を持たせない — 
  必要になってから型を足す方が安い)。
- **Tab5 のタッチから合成しない**。タッチは相対移動で二本指が無い
  ([[feedback-tab5-touch-is-relative]])。

## 段取り

| 段 | 中身 | 受け入れ |
|---|---|---|
| **W1** | 中核の配管 4 か所 + sim (SDL) + 注入 (`fmrb_input.rb wheel N`) + アプリ側の受け | **完了 2026-08-31** (report/w1.md)。log / shell / デスクトップの一覧がホイールで動く。**FmrbUI 案は成り立たず `wheel_rows` に変更**、sim の経路は書いたが未実行 |
| **W2** | ブラウザ (wasm の ring + ページ) と遠隔デスクトップ (rd_input + web クライアント) | **前半 (ブラウザ) は W1 と一緒に完了** — 検証がこの経路のため。後半 (遠隔デスクトップ) が残り |
| **W3** | 実機 USB。`hid_devices.toml` の `wheel` フィールド (白名簿) と HID Inspector でのホイール値表示 | 白名簿に書いた機器で動く。**書いていない機器と、今動いている機器は 1 バイトも変わらない** (回帰ゼロが本題) |
| **W4** | アプリ個別 | **大半は W1 で済んだ** (editor 本文・shell 履歴・logviewer・launcher・file_manager / file_selector)。残りは nsf_player / smf_player と、修飾キー付き (Ctrl+ホイール等) |

W1 だけで実用になるのが要点。実機 (W3) を最後に置いたのは、W1-W2 が
**私が自分で端から端まで検証できる経路**で、実機は人手が要るため
(ユーザ判断で W2 と入れ替えた)。W3 が遅れても W1-W2 の価値は残る。

## スコープ外

- 水平スクロール、チルトホイール、高解像度ホイール (Resolution Multiplier)。
- 慣性スクロール・加速度。
- Ctrl+ホイールの拡大縮小など、修飾キーとの組み合わせ。
- ゲームパッドのスティックからの合成。

## 危険と注意

- **USB を触ると今のマウスが死ぬ**。W3 は白名簿の 1 フィールドに留め、
  SET_PROTOCOL の順序には触らない。触るなら別テーマとして立てる。
- **2 エンジン問題**。アプリ基底は mruby (`app.c`) と Spinel
  (`fmrb_app_base_spinel.rb`) の 2 か所にある。FmrbUI は 1 ファイルを両方が
  取り込むので 1 か所で済む。
- **Spinel の生成は wasm ビルドから呼ばれない** — Spinel 側を直したら
  `rake spinel:gen` が先 ([[feedback-wasm-stale-generated-sources]])。
- **注入経路を先に揃えないと検証できない**。sim の `fmrb_input.rb`、
  ブラウザの `fmrb_web.rb` / MCP `web_input`、node の `drive.js`、
  Tab5 の `fmrb_rd_input.rb` — 4 つある。W1 と W2 でそれぞれ足す。
- ホイールは**押し込みボタン (中クリック) と別物**。中ボタンは既に
  button=2 として通っている。混ぜない。
- `input_router` は mouse_move を高頻度ゆえに落とすことがある。
  **wheel を同じ扱いにしない** (落ちると「回したのに動かない」になる)。

## 受け入れ条件 (テーマ全体)

- sim・ブラウザ・実機の 3 つで、ログとランチャーがホイールで送れる。
- ホイールの無いマウス、既存のクリック・移動・ドラッグが**一切変わらない**。
- 標準構成 (Spinel カーネル + Spinel エディタ) と全 mruby の両方で確認。
- 新しい入力型を注入から出せる (4 つの道具すべて)。

## 決まったこと (2026-08-31 追加)

- **`wheel_lines` はシステム設定 1 つだけ**。アプリ別の上書きはやらない
  (意図が薄い)。
- **スクロールバーの無い画面もホイールで動かす**。editor の本文がまさに
  それで、W4 の主目的。
- 白名簿に無い機器のログは**簡単なら 1 回だけ出す**。マウスの接続時に
  レイアウトを決める場所で 1 行なので、W3 で入れる。

## 未確定事項

- `hid_devices.toml` の `wheel` 記法 (offset / size / relative の 3 つで
  足りるはず。既存の x / y と同じ書き方に揃える)。

## 検証の都合 (W1 の進め方)

W1 の受け入れは本来 sim だが、**sim は docker の Linux ビルドが要り、
ターゲット切替を挟む**。ブラウザ経路 (W2 の前半) は同じ中核を通り、
`web_*` ツールで端から端まで自分で駆動できるので、**W1 と一緒に
wasm の入力源だけ先に足して、そこで通しの確認をする**。sim (SDL) の
経路は同じ形で書いて、ユーザの手元での確認に回す。
