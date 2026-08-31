# マウスホイール対応 検討と計画

> 状態: 計画済 | 更新: 2026-08-31 | 経路は入力源 5 + 中核 4 + アプリ 3。sim だけ通せば 7 画面が一度に動く。実機 USB が唯一の難所

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
| Linux sim (SDL) | `sdl2-display/main.c` が `SDL_MOUSEBUTTON*` と `SDL_MOUSEMOTION` を送る | `SDL_MOUSEWHEEL` を足すだけ |
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

5. **`input_router.rb`** — 誰に配るか。**クリックと同じ hit test** (ポインタの
   下の窓) を採る。フォーカスではない — 見ている一覧を、クリックせずに
   送れることに値打ちがあるため。ドラッグ中・全画面時は既存の分岐に従う。
6. **アプリ基底 2 か所** — mruby は `app.c` の `HID_SET_TYPE` 群、Spinel は
   `fmrb_app_base_spinel.rb` の `_parse_hid_event`。
   `{ type: :mouse_wheel, x:, y:, delta: }` を渡す。
   **片方だけ直すと「標準構成でだけ効かない」**が起きる (窓枠の直値と同じ轍)。
7. **`FmrbUI` の scrollbar が自分で拾う** — ここが要。
   `@ui.handle(ev)` が `:mouse_wheel` を見て、ポインタが**その widget の
   属する窓の中**なら値を動かして widget の id を返す。
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

取りうる道は 3 つ:

- **(a) Report Protocol に切り替える** — descriptor を読んで x/y/wheel を
  全部そこから採る。素性は正しいが、boot 経路を捨てるので**全機種の再検証**
  が要る。
- **(b) 3 バイトを超えるレポートが来たら足を伸ばす** — 既に
  `auto_detect_mouse_report_format` が「boot を名乗るのに 6 バイト以上
  送ってくる機器」を検出して 12bit packed へ切り替える例がある。
  同じ形で「4 バイト目が来たらそれをホイールとみなす」を足す。
  **既存の動作を一切変えない**のが利点。標準的な 4 バイト boot 互換
  レポート (buttons, X, Y, Wheel) を出す機器はこれで拾える。
- **(c) TOML で指定する** — `hid_devices.toml` に `wheel` フィールドを足し、
  HID Inspector で見つけた位置を書く。**確実だが人手が要る**。

**(b) を既定、(c) を逃げ道、(a) は当面やらない**を推す。(b) で拾えない
機器は HID Inspector で位置を見て (c) に書く、という手順にする。

## 方針 (先に決めること)

- **単位は「ノッチ」**。HID の Wheel は刻みで来るので ±1 のまま運び、
  「1 ノッチ = 何行」はアプリ側 (FmrbUI の既定は 3 行) が決める。
- **向きの正**: 実測で決める。奥に回す = 上へ、を既定にする。
- **配り先はポインタの下の窓**。
- **間引かない、合算する**。
- **水平ホイール (AC Pan) はやらない**。席だけは空けておく (delta が
  1 軸で足りなくなったら型を増やせるよう、payload に予備を持たせない — 
  必要になってから型を足す方が安い)。
- **Tab5 のタッチから合成しない**。タッチは相対移動で二本指が無い
  ([[feedback-tab5-touch-is-relative]])。

## 段取り

| 段 | 中身 | 受け入れ |
|---|---|---|
| **W1** | 中核の配管 4 か所 + sim (SDL) + 注入 (`fmrb_input.rb wheel N`) + FmrbUI の scrollbar | sim で logviewer と launcher がホイールで送れる。**7 画面すべて**が触られずに動く。既存の click / move が変わらない |
| **W2** | 実機 USB。(b) の 4 バイト検出、(c) の TOML `wheel` フィールド、HID Inspector にホイール値の表示 | 手持ちのマウスで動く。**ホイールの無いマウスと、今動いている機器が全部そのまま動く** (回帰がゼロであることが本題) |
| **W3** | ブラウザ (wasm の ring + ページ) と遠隔デスクトップ (rd_input + web クライアント) | ブラウザ版でログが送れる。`fmrb_web.rb wheel N` と MCP の `web_input` で駆動できる |
| **W4** | アプリ個別: editor の本文スクロール、shell の履歴、logviewer の行送り、launcher のページ送り、file_manager / file_selector | 各画面で「見ている物が」動く。修飾キー付き (Ctrl+ホイール等) は W4 では扱わない |

W1 だけで実用になるのが要点。W2 は独立していて、失敗しても W1 の価値は
残る。

## スコープ外

- 水平スクロール、チルトホイール、高解像度ホイール (Resolution Multiplier)。
- 慣性スクロール・加速度。
- Ctrl+ホイールの拡大縮小など、修飾キーとの組み合わせ。
- ゲームパッドのスティックからの合成。

## 危険と注意

- **USB を触ると今のマウスが死ぬ**。W2 は (b) の「増えた分を読むだけ」に
  留め、SET_PROTOCOL の順序には触らない。触るなら別テーマとして立てる。
- **2 エンジン問題**。アプリ基底は mruby (`app.c`) と Spinel
  (`fmrb_app_base_spinel.rb`) の 2 か所にある。FmrbUI は 1 ファイルを両方が
  取り込むので 1 か所で済む。
- **Spinel の生成は wasm ビルドから呼ばれない** — Spinel 側を直したら
  `rake spinel:gen` が先 ([[feedback-wasm-stale-generated-sources]])。
- **注入経路を先に揃えないと検証できない**。sim の `fmrb_input.rb`、
  ブラウザの `fmrb_web.rb` / MCP `web_input`、node の `drive.js`、
  Tab5 の `fmrb_rd_input.rb` — 4 つある。W1 と W3 でそれぞれ足す。
- ホイールは**押し込みボタン (中クリック) と別物**。中ボタンは既に
  button=2 として通っている。混ぜない。
- `input_router` は mouse_move を高頻度ゆえに落とすことがある。
  **wheel を同じ扱いにしない** (落ちると「回したのに動かない」になる)。

## 受け入れ条件 (テーマ全体)

- sim・ブラウザ・実機の 3 つで、ログとランチャーがホイールで送れる。
- ホイールの無いマウス、既存のクリック・移動・ドラッグが**一切変わらない**。
- 標準構成 (Spinel カーネル + Spinel エディタ) と全 mruby の両方で確認。
- 新しい入力型を注入から出せる (4 つの道具すべて)。

## 未確定事項

- 向きの正 (実測で決める)。
- 1 ノッチあたりの行数の既定 (3 行を仮置き)。アプリごとに変えられるように
  するか、FmrbUI の定数 1 つで足りるか。
- スクロールバーを持たない画面 (editor の本文) をどう扱うか。W4 で個別に。
- `hid_devices.toml` の `wheel` 記法 (offset / size / relative の 3 つで
  足りるはず)。
