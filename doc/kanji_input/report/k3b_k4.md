# K3b・K4 報告: K3 の残りと、ブラウザ版のブラウザの IME

> 状態: 完了 (実機の確認と、本物のブラウザの IME での目視は残り) | 更新: 2026-11-11 (K4b: 変換中の文字の欄を文字の幅に合わせ、ブラウザの IME が変換を始めたらかなの指示器を隠す) | sim の Modern の全画面 (640x360) でも変換の 1 行が出る (graphics-audio)。変換のタスクのスタックを PSRAM に移し、辞書があるときに内蔵 RAM に増えるのは TCB だけにした (sdkconfig は変えていない)。変換キーを入力の道具の名前 `henkan` に足して sim で試した。ブラウザ版は、文字を受けるアプリ (エディタ・Shell) が前にいる間だけ見えない入力欄にキーを渡し、ブラウザの IME の確定の文字をアプリへ流す。辞書が無い既定とブラウザ版の IME を使わない打鍵は今のまま (画面の基準は同じ)

## K3b

### 1. sim の Modern の全画面でも 1 行を出す (graphics-audio)

- `main/graphics/graphics_handler.cpp` の `screen_mode_render_frame` (Linux の全画面の高解像度の描画、`CONFIG_IDF_TARGET_LINUX` の中だけ) で、
  持ち主の絵を写した後、z が 30000 (`SCREEN_MODE_SYSTEM_OVERLAY_Z`、core の `FMRB_GFX_Z_SYSTEM_OVERLAY` と同じ値) 以上の
  見えている Canvas を重ねる。透明色のある Canvas は透明色で抜く。マウスのカーソルはその上。WROVER の経路は変えていない。
- display_local (Tab5・NARYAv4・ネイティブ版) の K3 の直しと同じ考え方。`EXPORT_FRAME` の絵 (`screen_mode_picture`) には
  入れていない (保存する絵は持ち主の絵のまま)。
- sim (TAB5 の Linux ビルド、辞書を `/home` に置いた) の全画面エディタで `kanji` → 変換キー: 640x360 の下端に
  `かんじ 1感じ 2幹事 3漢字 ...  1/6` が出る (modern_hires_candidates.png)。変換キー 2 回 → Enter で 漢字 が入り、1 行は消える
  (modern_hires_committed.png)。

### 2. 変換のタスクのスタックを PSRAM に

- `FMRB_KANJI_TASK_FLAGS` を `FMRB_TASK_FLAG_PSRAM` にした (`xTaskCreateWithCaps(..., MALLOC_CAP_SPIRAM)`)。辞書があるときに
  内蔵 RAM に増えるのは TCB (`StaticTask_t`、IDF が内蔵 RAM に置く。数百バイト) だけになる。辞書が無ければ今までどおり 0。
- **sdkconfig は変えていない**。`xTaskCreateWithCaps` に外の RAM を渡すのに要る `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` は
  IDF 5.5.4 の Kconfig で S3・P4 の既定が y (ESP32 無印だけ n)。config/sdkconfig.defaults.* はどれもこれを書いていない。
- K2b で内蔵に置いた理由 (SD のドライバがスタックの上の構造体を DMA に渡す) への答え: このタスクが呼ぶファイルの関数
  (open・read・seek・size・close) は、`fmrb_hal_file_esp32.c` が「今のスタックが PSRAM なら hw_proxy (内蔵 RAM のスタックのタスク)
  に代わってもらう」形になっている (`hw_proxy_needs_proxy`)。このタスクは PSRAM のスタックで SD・flash に直接触らない。
  ほかにすることは二分探索・タスク通知・待ち行列への送り・ログで、flash も DMA も使わない (doc/reference/internal_ram_budget.md の
  軸 B の安全境界を満たす)。
- 合わせて直した 2 つ:
  - `hw_proxy_needs_proxy` は S3 の PSRAM の番地 (0x3C000000-0x3DFFFFFF) しか見ていなかった。P4 の PSRAM (0x48000000-) の
    スタックでは代行されず SD に直接行くところだった。IDF の `esp_ptr_external_ram` (チップごとの範囲を知っている) に替えた。
    S3 では同じ範囲。今まで PSRAM のスタックのタスクは無かったので、ほかの動きは変わらない。
  - `fmrb_task_delete_ex`: PSRAM のスタックのタスク (WithCaps で作る) を `vTaskDelete` で消すと、TCB (内蔵 RAM) とスタックが
    解放されない。`vTaskDeleteWithCaps` で消すようにした (自分を消すときは IDF が片付けのタスクを一時的に作る)。辞書が開けない
    とき、変換のタスクは自分を消すのでここを通る。
- **確認は組み立てまで** (`rake regress:size` が S3・P4 のファームを作る)。sim は Linux の FreeRTOS で、スタックの置き場所の
  旗を見ない。実機で確かめること (下の「実機での確認」)。

### 3. 変換キーの名前

- `tools/fmrb_input.rb` の `SPECIAL_KEYS` に `"henkan" => [138, 0]` (International4、HID 0x8A)。`fmrb_web.rb` はこの表を
  使うので、ブラウザ版の `key henkan` も通る。Tab5 の遠隔の `tools/fmrb_rd_input.rb` の `SCAN` にも `"henkan" => 0x8A`。
  (この 2 つの道具は family-mruby の直下のリポジトリにある。)
- sim で `key henkan` が変換になり (1 の確認はこのキーで流した)、もう 1 回で次の候補に進む。

## K4: ブラウザ版のブラウザの IME

### 形

| 段 | すること |
|---|---|
| アプリ | 今までどおり `FmrbApp.set_text_cursor(x, y, h)` (エディタ・Shell。L12b で入れた口)。アプリは変えていない |
| host task (`host_task.c`) | ネイティブ版だけだった「アプリごとの文字カーソル」「確定の文字 (`HOST_MSG_HID_TEXT`)」「`fmrb_host_send_text`」を、ブラウザ版にも入れた (`HOST_TEXT_IN`)。20 ms ごとに前のアプリのカーソル (フレームの座標) を出す |
| 入力の口 (`wasm/backend/input_wasm.c`) | 入力のリングに種類 6 (テキスト、1 件 12 バイト、`a` が長さ、`b`〜`d` が UTF-8) を足した。カーソルは `fmrb_wasm_text_cursor()` の共有の場所 (番号が奇数の間は書き途中) |
| ページ (`wasm/web/main.js`) | 見えない `<textarea>` (`#ime-box`) を画面の箱の中に置く。前のアプリが文字カーソルを出している間だけ、キーの焦点を画面からそこへ移し、カーソルの位置に動かす。`compositionend` の確定の文字をテキストの種類で流す |

- **キー**: 画面と入力欄に同じキーの処理を付けた。違うのは、ブラウザの IME が取ったキー (`isComposing` か `keyCode` 229) を
  端末へ送らないことと、その離しも送らないことだけ。画面 (canvas) は IME の変換をしないので、画面に焦点があるときは
  一字も変わらない。入力欄でも IME を使わない打鍵は同じ関数が同じものを送る (preventDefault も同じ)。
- **焦点**: 前のアプリが文字カーソルを出していない (ゲーム・デスクトップ・カーソルを `h = 0` で消したエディタ) 間は画面に
  焦点があり、IME はキーを見ない (ネイティブ版の L12b と同じ線)。変換の途中では焦点を動かさない。焦点がページのほかの所
  (設定の欄など) にあるときは奪わない。
- **変換中の文字**: 入力欄はふだん透明で、`compositionstart` から `compositionend` まで不透明 (K4b からは画面の色。下の K4b) になり、ブラウザが
  変換中の文字をアプリのカーソルの位置に描く。候補の Window はブラウザがその横に出す。行の高さは画面の倍率に合わせる。
  ネイティブ版と同じく、カーソルの右の文字の上に重なる。
- **かな入力はそのまま**: ページからはブラウザの IME が on か off か分からない。ネイティブ版のように「IME がいる」として
  Family mruby のかな入力を止めることはしない (指示器は K4b から推測で隠す) (IME が off の人の Ctrl+Space のかな入力が今のまま使える)。
  両方を同時に on にすると、ブラウザの IME が変換中のキーは端末に届かず、確定の文字だけが届く。
- **画面のキーボード**: 入力欄に `inputmode="none"`。タッチの端末で焦点を移したときに画面のキーボードが出ない (今と同じ)。
- 入力欄に IME 以外の文字 (端末のキーの無い字、端末の求めていない貼り付け) が入っても、その場で消す (端末へは送らない。
  今の画面と同じ)。
- 辞書と自前の変換 (K2b・K3) は今までどおりブラウザ版では使わない (`kanji_lookup.c` はスタブ)。

### 確認 (ヘッドレスのブラウザ)

`tools/fmrb_web.rb` に `ime` を足した。引数なしで焦点とカーソルと入力欄の位置を返す。`ime "日本語" --updates に,にほ,...` は
入力欄に、ブラウザが送るのと同じ並び (IME の取った押下 → compositionstart → update ごとの input → compositionend → 離し) を
DOM の出来事として送る。本物の IME の Window は動かせない (合成の KeyboardEvent には keyCode を付けられないので、
`isComposing` で代わりにした)。

| 確認したこと | 結果 |
|---|---|
| デスクトップ | `focused: screen`、カーソルなし |
| エディタを開く | `focused: ime`、カーソル (7,35) 行 12、入力欄も (7px,35px) 高さ 12px (倍率 1) |
| `ime "日本語"` (途中で 4 回の update) | 途中は入力欄が不透明。エディタに 日本語、Col 4 (IME が取った押下は端末に届いていない)。web_editor_ime.png |
| `ime "、漢字変換のテストです。"` (33 バイト = テキスト 3 件) のあと `key a` | 続けて入り、a も入る (Col 17)。web_editor_long.png |
| Shell を開いて `ime "漢字"` | カーソル (20,64)、プロンプトに 漢字。web_shell_ime.png |
| Shell を閉じる | `focused: screen`、カーソルなし |

### ユーザに目視を頼む手順 (本物の IME)

1. `rake wasm:serve` (か公開前のブラウザ版) をふだんの Chrome で開き、Start を押す。日本語の IME (Windows の MS-IME・Google 日本語入力
   など) は**英字のまま**にしておく。
2. デスクトップからエディタを開き、英字で `abc`、Enter、Ctrl+S などを打つ。今までどおり効くこと (IME が英字の間は何も変わらない)。
3. IME を日本語にして `nihongo` と打つ。エディタのカーソルの位置に白地の「にほんご」が出て、スペースで変換、候補の Window が
   その近くに出ること。Enter で確定し、エディタに 日本語 が入ること。確定の Enter で改行が入らないこと。
4. 長い文 (`kyouhaiitenkidesu` → 今日はいい天気です) を確定して、全部入ること。変換中に BS・矢印・Esc を押しても、
   エディタの文が動かないこと (キーは IME のもの)。
5. IME を日本語のまま、エディタで矢印・Ctrl+S・Ctrl+Q (変換していない間) が効くこと。
6. Shell でも 3 と同じく打てること。
7. ゲーム (FlappyDemo・Breakout など) を前にして、IME を日本語にしたままでもキーがそのまま効くこと (IME が入らない)。
8. 拡大 (Zoom 2x・Fit)・全画面 (Full screen) でエディタを開き、変換中の文字と候補の Window がカーソルの近くに出ること。
   Modern の全画面エディタ (F11、640x360) でも同じ。CRT の見た目を on にすると、曲がりの分だけ位置がずれる (見込み。許容か見てほしい)。
9. IME を英字に戻し、Ctrl+Space の Family mruby のかな入力 (ローマ字 → かな) が今までどおり使えること。

## 確認 (回帰)

| | 結果 |
|---|---|
| `rake regress` | host 25/25、描画の記録 229/229 |
| `rake regress:screens` | 33/33 (retro 9・modern 11・web 6・native 3・native-keys 4)。エディタの 1 打鍵を含め画素で同じ。ブラウザ版の 6 場面は入力欄に焦点がある形で通った |
| `rake regress:size` | ok。NARYAv3 DIRAM 125,419 (-40)・IRAM +0、TAB5 DIRAM 129,346 (-352)。K2b・K3 のときと同じ数 (この変更の内蔵 RAM の静的な増分 0)。基準 (4e7e7e61) が古いので flash の差にはほかの変更が入っている |

- 1 回目の `regress:size` は S3・P4 とも組み立てで落ちた (`hw_proxy.c` の `volatile uint8_t local` の番地を渡す形が
  `-Werror=maybe-uninitialized`)。`__builtin_frame_address(0)` を渡す形に直して通した。この直しの後で
  `regress`・`regress:screens` は流し直していない (変わったのは ESP32 の分岐の中だけで、Linux・ブラウザ版の組み立てには入らない)。

画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/k4/` (リポジトリの外)。

## 撤回した仮説

- 「PSRAM のスタックのタスクのファイルは hw_proxy が代わる」は S3 だけ本当だった (番地の範囲が S3 の固定値)。P4 では
  代わっていなかった。
- 「WithCaps のタスクも `vTaskDelete` で消せる」: 消えるが、静的に渡した TCB とスタックは誰も解放しない。

## 残件

- **実機での確認** (辞書を SD に置いた S3 と P4): 起動ログの `M1|kanji|internal=..` が直前の `host_ready` から TCB の分
  (数百バイト) しか減らないこと、`kanji: ... open in N us` と変換の引きが今までどおりであること、10 秒ごとの `fmrb_task:` の
  `fmrb_kanji` の行が `PSRAM` で Free が残っていること。辞書の壊れた・版違いのファイルを置き、タスクが自分を消しても落ちない
  こと (`not opened ... kanji conversion off`)。
- 本物のブラウザの IME での目視 (上の手順)。Safari・Firefox は試していない (合成の出来事の並びは Chrome に合わせた。
  Safari は compositionend が確定の Enter の keydown より先に来るが、その keydown は keyCode 229 なので送らない見込み)。
- ブラウザの IME の on・off をページから知る手段は無い。K4b で推測して指示器を隠すようにした (下の K4b)。

## K4b: 変換中の文字の欄と、かなの指示器

ユーザの指摘 2 つへの直し。

### 1. 変換中の文字の欄が右へ伸び続けた

- 原因: 変換中に `imePlace` (アニメーションのフレームごとに呼ぶ) が幅を `scrollWidth + 2` にしていた。`<textarea>` の
  `scrollWidth` は自分の幅より小さくならないので、呼ぶたびに 2 px ずつ広がり、白い帯が Window の外まで伸びた。
- 直し (`wasm/web/main.js`):
  - 幅は見えない `<span>` に同じ字の大きさで変換中の文字を入れて測る (文字の幅 + カーソルの分 4 px)。
  - 画面 (Family mruby の canvas) の右端を越えない。収まるならカーソルの位置、収まらないときは収まるだけ左へずらす。
    画面の幅より長いときは画面の幅で、中で横に送る (カーソルが見えるように末尾へ)。
  - 色は画面から採る。`compositionstart` のとき、フレームのカーソルの行の左右 48 px (カーソルの字の分は除く) で
    いちばん多い色を地、地と明るさが十分違う色のうちいちばん多いものを文字の色にする。エディタ (サイバーパンクの見た目) では
    黒地に灰の文字、Shell では黒地に緑。見つからなければ地の明るさの逆 (白か黒)。文字に下線。字の大きさは行の高さ × 倍率
    × 0.85、行の高さを欄の高さに揃えた。

### 2. ブラウザの IME が日本語でも指示器が変わらなかった

ページは IME の状態を読めないので推測する。入力欄に焦点があるとき (エディタ・Shell が前にいるとき) だけ。

| 出来事 (入力欄で) | 推測 |
|---|---|
| `compositionstart`、または IME が取ったキー (`isComposing`・`keyCode` 229) | IME は日本語 |
| IME が取らなかった印字できるキー (`key` が 1 文字、Ctrl・Alt・Meta なし) | IME は英字 (か off) |

- 推測は入力のリングの新しい種類 7 (`a` = 1 か 0) で端末へ送る (`wasm/backend/input_wasm.c`。番号 `seq` つき)。
  host task (`ime_tick`、ブラウザ版) は新しい推測が来たら `s_ime_on` をそれにし、日本語の間はネイティブ版の Linux の IME と
  同じ `FMRB_KANA_MODE_IME` (3) をアプリとデスクトップに知らせる。**この値の表示は「指示器を隠す」** (デスクトップの
  メニューバーのセル、エディタのステータス行の `[A]`。ネイティブ版と同じ。新しい表示は作っていない)。前に出たアプリへの
  知らせ直し (`s_ime_told_gen`) はネイティブ版の処理を `ime_tell_focused` に出して共有した。
- 印字できるキーで英字と推測したときは、キーより先に送る (そのキーでかなの指示器が変わるとき、端末自身の表示になっている)。
- 焦点が画面 (canvas) に移ったときは、端末には「英字」と知らせる (IME はキーを見ないので)。推測そのものは覚えておき、
  入力欄に戻ると「日本語」を知らせ直す。
- **ブラウザ版で変えたのは指示器だけ**。ネイティブ版と違い、Family mruby のかな入力は止めない (ローマ字の合成は今までどおり、
  指示器のクリックも効く)。
- **両方が効くときの勝ち負け**: 後から来た方が勝つ。Ctrl+Space (かなのキー) が端末に届いたとき、または指示器のクリックで
  `FmrbApp.set_kana_mode` が来たときは、ブラウザ版では推測を「英字」にして、指示器は Family mruby のかなの状態
  (`[あ]` など) を出す。次に変換が始まると (`compositionstart` は毎回知らせ直す) また隠れる。IME が日本語で変換中のキーは
  端末に届かないので、Family mruby のかな入力が同時に文字を作ることはない。
- 推測が外れる場合: IME を日本語にしただけでまだ打っていない間は、指示器は前のまま (最初の 1 字で隠れる)。IME を英字に
  戻しただけの間も、次の印字できるキーまで隠れたまま。

### 確認 (ヘッドレスの Chromium)

`tools/fmrb_web.rb ime` に `--hold` (変換を開いたままにする)・`ime --end` (確定)・`ime --key a` (入力欄に IME が取らない
キー) を足した。答えに欄の幅と色、推測 (`guess`) と端末へ知らせた値 (`told`) も出す。入力欄はページの DOM なので、
欄の写真は Playwright の Chromium を `--remote-debugging-port` で開き、DevTools の `Page.captureScreenshot` で撮った
(倍率 2 の 852x480)。

| 確認したこと | 結果 |
|---|---|
| エディタで `にほんご` を変換中 | 欄は幅 86 px (文字の分だけ)、黒地に灰、下線、カーソルの位置。メニューバーの `A` とステータス行の `[A]` が消える (compose_short.png) |
| 確定 → `ime --key b` | 確定の後も隠れたまま、b で両方に `A` が戻り、b が入る (pair.png) |
| 右端近く (カーソル x=229) で 24 字の変換 | 欄は画面の右端で止まり、収まるだけ左へずれる (compose_long_edge.png) |
| 画面より長い 54 字の変換 | 欄は画面の幅 852 px、中で末尾まで送られる (compose_wider_than_screen.png) |
| 変換 → Ctrl+Space → 変換 → `k` `a` → Ctrl+Space | 隠れる → `[あ]` (かなの方が勝つ) → 隠れる → か が入り `[あ]` → `[A]` (who_wins.png) |
| 日本語の推測のままデスクトップをクリック → エディタに戻る | 画面に焦点 (`told: 0`) でメニューバーに `A`、戻ると隠れる (focus_pair.png) |
| Shell で `かんじ` を変換中 → `l` `s` | 黒地に緑の欄、指示器が隠れ、l s で戻る (shell_compose.png・shell_after.png) |

- 54 字 (162 バイト) を一度に確定したとき、端末に入ったのは 35 字までだった。K4 の確定の文字の経路 (1 バイトずつの
  キーの出来事をアプリの待ち行列へ、待ちは 200 ms) で落ちたと見ている。ヘッドレスで裏のタブはフレームが遅い (40 秒ほどで
  98 フレーム) ので、本物のブラウザで起きるかは未確認 (下の目視の 3)。この変更では触っていない。
- `rake regress`: host 25/25、描画の記録 229/229。host_task.c はネイティブ版の組み立ての旗で `-fsyntax-only` を通した
  (ネイティブ版・sim・ESP32 の組み立ては、build/ に P4 のファームがあるので流していない)。`rake regress:screens` も同じ理由で
  流していない。

画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/k4b/` (リポジトリの外)。

### ユーザに目視を頼む手順 (本物の IME)

1. `rake wasm:serve` をふだんの Chrome で開き、Start。エディタを開く。IME は英字。メニューバー右と、エディタの
   ステータス行の右端に `A` / `[A]` が出ていること。
2. IME を日本語にして `nihongo` と打つ。変換中の文字がカーソルの位置に、エディタの地の色・文字の色に近い色で、文字の
   幅だけの欄に出ること (白い帯が右へ伸びないこと)。打ち始めで `A` と `[A]` が消えること。
3. 長い文 (`kyouhaiitenkidesuneashitamoharerudeshou`) を、行の右端の近くで変換する。欄が画面の右端を越えず、
   長いときは左へずれ、さらに長いときは欄の中で送られること。確定して全部入ること (ヘッドレスでは長い確定の後半が
   落ちた。上の注)。
4. IME を英字に戻して `a` を打つ。`A` と `[A]` が戻り、a が入ること。
5. IME を日本語のまま Ctrl+Space を押す (Windows の IME が Ctrl+Space を取る設定なら、指示器のクリックで代える)。
   `[あ]` が出ること。続けて日本語で打つと再び隠れること。
6. Zoom 1x・Fit・全画面で 2 と 3 を見て、欄の字の大きさが画面の字と大きく違わないこと。
