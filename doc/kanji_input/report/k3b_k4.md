# K3b・K4 報告: K3 の残りと、ブラウザ版のブラウザの IME

> 状態: 完了 (実機の確認と、本物のブラウザの IME での目視は残り) | 更新: 2026-11-11 | sim の Modern の全画面 (640x360) でも変換の 1 行が出る (graphics-audio)。変換のタスクのスタックを PSRAM に移し、辞書があるときに内蔵 RAM に増えるのは TCB だけにした (sdkconfig は変えていない)。変換キーを入力の道具の名前 `henkan` に足して sim で試した。ブラウザ版は、文字を受けるアプリ (エディタ・Shell) が前にいる間だけ見えない入力欄にキーを渡し、ブラウザの IME の確定の文字をアプリへ流す。辞書が無い既定とブラウザ版の IME を使わない打鍵は今のまま (画面の基準は同じ)

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
- **変換中の文字**: 入力欄はふだん透明で、`compositionstart` から `compositionend` まで不透明 (白地に黒) になり、ブラウザが
  変換中の文字をアプリのカーソルの位置に描く。候補の Window はブラウザがその横に出す。行の高さは画面の倍率に合わせる。
  ネイティブ版と同じく、カーソルの右の文字の上に重なる。
- **かな入力はそのまま**: ページからはブラウザの IME が on か off か分からない。ネイティブ版のように「IME がいる」として
  Family mruby のかな入力と指示器を止めることはしない (IME が off の人の Ctrl+Space のかな入力が今のまま使える)。
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
- ブラウザの IME の on・off をページから知る手段は無いので、ブラウザ版ではかなの指示器を隠さない (ネイティブ版とは違う)。
  隠すかどうかはユーザの判断。
