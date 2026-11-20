# A3d 報告: AI ヘルパーの入力の行を、PC・ブラウザの IME に対応させる

> 状態: 完了 (GPD の本物の環境での目視は残り) | 更新: 2026-11-19 | AI ヘルパーの入力の行が文字のカーソルを申告する (`FmrbApp.set_text_cursor`) ようにした。見立てどおり、申告が無いと PC の IME はアプリに対して無効にされていた。ネイティブ版 (`linux_pc_wl`、コンテナの中の本物の fcitx5 + Mozc) で、入力の行に変換中の文字・候補が出て 日本語 が入ることを確認した。sim の自前のかな入力・漢字変換と、ブラウザ版の IME の経路 (道具で送る確定) も通った。指示書は instruction_a3d.md

GPD・P4-Nano・VPS には触っていない。本物の LLM は使っていない (fake だけ)。

## 1. 読んだ経路と原因

| 段 | 申告が無いとき |
|---|---|
| host task (`host_task.c` の `ime_publish_focused_cursor`) | 前のアプリのカーソルを 20 ms ごとに出す。申告が無いアプリは「カーソルなし」 |
| コンポジタ (`native/wlr/wl_ime.c` の `update`) | Family mruby に焦点があるとき、カーソルがあるときだけ input-method を activate する。**カーソルなしなら deactivate** (fcitx5 はキーを見ない) |
| SDL の Window (`native/output/output_sdl.c`) | カーソルなしなら `SDL_StopTextInput` (「ゲームのキーはそのまま」の作り) |
| host task のかな入力 (`ime_tick`) | PC の IME がいる間 (`s_ime_on`) は、自前のかな入力を止め、指示器のクリックも断る |
| ブラウザ版のページ (`wasm/web/main.js`) | カーソルなしなら焦点は画面 (canvas) のまま。ブラウザの IME はキーを見ない |

- 原因は**親の見立てどおり**: 申告が無いので IME は無効にされ、しかもネイティブ版では自前のかな入力も止まっているので、
  日本語を打つ手が無かった。文字が来て捨てていたのではない。
- 確定の文字の届き方: `HOST_MSG_HID_TEXT` → `host_text_pump` が、自前のかな入力と同じ「1 バイトずつのキーの出来事
  (scancode 0・keycode 0、`character` にバイト)」でアプリへ送る。AI ヘルパーの `utf8_feed` (UTF-8 の組み立て) で
  そのまま受けられる。受け口の直しは要らなかった。

### 1.1 見立てに無かったこと: 座標の基準

`FmrbApp.set_text_cursor` の座標は **canvas の座標** (Window の左上から。sig/fmrb_app.rbs と host task が Window の
位置を足すだけの作り)。AI ヘルパーの `@gfx` は利用者の区画 (タイトルバーの下) が原点なので、そのまま渡すと
タイトルバーの分 (11 px) 上にずれた (最初の試しで、変換中の文字が入力の行の上の状態の行に重なった)。
`@gfx.origin_x`・`origin_y` を足して渡すようにした。

**エディタも同じずれを持っているように見える** (範囲の外なので直していない)。ネイティブ版で `edit /home/t.rb` のエディタに
焦点があるとき、`comp ime` のカーソルはフレームの (6,24) で、エディタのカーソルの升は画面で (7,38) あたり (タイトルバー
と少し分、上)。Shell も `input_y` を区画の座標のまま渡している (コードを読んだだけ。動かしていない)。K4 の wl_ime_check は
「候補がカーソルの近く (±40 px)」で見ているので通っている。下の 6 章でユーザに決めてほしいこととして書いた。

## 2. 直した形 (`flash/app/modern/ai_helper.app.rb`・`ai_helper/screen.rb`)

- `draw_input` が入力の行のカーソルの x を `@in_cx` に覚え (空なら 4、打った文があれば末尾。描く線は今までと同じ位置)、
  `tell_text_cursor` を呼ぶ。`draw_buttons` (問いのボタン・プルダウンの開け閉めで必ず呼ばれる) も呼ぶ。
- `tell_text_cursor`: 入力の行が文を受けるとき (`@choices` が無く、プルダウンが閉じている) は
  `(origin_x + @in_cx, origin_y + @in_y + 1, LINE_H=13)`、そうでなければ `(0, 0, 0)`。前に送ったものと同じなら送らない
  (x と y を 1 つの整数 `@tc` にまとめて比べる)。`on_resize` で `@tc` を空にし、大きさを変えた後は必ず送り直す。
- Window が後ろにいる間は、host task が前のアプリのカーソルしか使わないので、アプリの側では何もしない (エディタ・Shell と同じ)。
- 答えの欄のスクロールでは入力の行は動かないので、送り直しは要らない。
- 問いのボタンが出ている間・プルダウンを開いている間の打鍵の扱い (入力の行に文字が入る・入らない) は変えていない。
  変わるのは、その間 PC・ブラウザの IME が働かないこと (キーはそのまま届く) だけ。
- かなの指示器: AI ヘルパーは自分の指示器を持たない。デスクトップのメニューバーの指示器は host task が今までどおり出す
  (ネイティブ版の確認で `PID 4 told an IME types (its kana indicator shows the IME's mark)` と、英字に戻すと `A`)。
- 端末の案内 (`flash/usr/share/doc/ai_helper_ja.md`・`ai_helper.md`) の「かなと漢字」の行に、ブラウザ版と PC (Linux) の版では
  PC の日本語入力でも打てることを足した。
- ESP32・sim では `set_text_cursor` は何もしない (`HOST_TEXT_IN` はネイティブ版とブラウザ版だけ)。

## 3. 受け入れ条件

| # | 結果 | 根拠 |
|---|---|---|
| 1 | 通った (コンテナの中の本物の fcitx5 で) | 3.1 |
| 2 | 通った | 3.2 |
| 3 | 通った (道具で送る IME の確定まで) | 3.3 |
| 4 | 通った (VM プールは +1.6 KB、下の注) | 3.4 |
| 5 | 通った | 3.5 |

### 3.1 ネイティブ版 (`linux_pc_wl`)

`rake native:wl` で作り直したプログラムを、`test/native/wl/wl_ime_check.rb` と同じ形 (Ubuntu 26.04 のイメージ、ヘッドレスの
wlroots、`dbus-run-session` の中で fcitx5 + Mozc、1280x800 に 640x400 の画面) で上げ、会話の場所のスクラッチに置いた
小さな操作の台本で動かした (回帰の道具は変えていない)。gem は動かしていない (つながらないまま。入力の行は使える)。

| 確認したこと | 結果 |
|---|---|
| AI ヘルパーを開く前 | `serves=none cursor=off` |
| AI ヘルパーを開く | `serves=fmrb cursor=127,202,13` (`IME serves Family mruby`)。入力の行の左端 |
| Mozc を切ったまま `nihongo ` | 仮想キーボードで返ったキーが入り、カーソルが 6 px × 8 字進む (169 → 175) |
| Ctrl+Space (Mozc) → `nihongo` | 変換中の文字が入力の行のカーソルの位置に出る (preedit `100x28+348+402`、カーソルは出力の (350,404))。候補の Window がその下 (b3_composing.png) |
| Space Space → Up → Enter | `IME committed 9 bytes to Family mruby`、入力の行が `nihongo 日本語`、カーソルが 36 px 進む (b5_committed.png) |
| 「編集」のプルダウンを開く | `serves=none cursor=off`。Esc で閉じると `serves=fmrb` に戻る |
| エディタで `# AI:` の行のある文書を開き、「AIコメント削除」で問いのボタン (消す・キャンセル) を出す | `serves=none cursor=off`。Esc (キャンセル) で戻る |
| 指示器 | Mozc を on にすると host が `the PC's IME types Japanese: kana indicator shows the IME's mark` |

- 直す前の版 (座標に区画の原点を足す前) で 1 回通したとき、変換中の文字が入力の行の 11 px 上 (状態の行) に出た。
  これが 1.1 のずれ。足した後は入力の行に重なる。
- `--machine sdl` (SDL の Window の IME) では試していない。SDL の側も同じカーソルで `SDL_StartTextInput` するだけの作り。

### 3.2 sim の Modern (標準構成)

Modern の標準構成 (`FMRB_HW_TARGET=TAB5`、Spinel のカーネルとエディタ、mruby のデスクトップ) を `rake clean:linux` →
`rake build:linux`。a2.md と同じく `sim_up` は「426x240 but 320x240 expected」で止まるが stack は上がっていて、そのまま使った。
漢字変換を試すため、sim の `flash/home/` に辞書を置き、`flash/etc/system_conf.toml` に `kanji_dict` を書いた
(確認の後、辞書を消し、設定は元の写しで戻した)。gem は `--llm fake --cost-per-day 1000`。

- 「質問」に切り替え、入力の行をクリック → Ctrl+Space → `kanjiha` → 変換キー → Enter で 感じ が入り、続けてかなを打ち、
  Ctrl+Space で英字に戻して `?`、Enter。「(質問、文書なし) 感じはっていつみるの?」が送られ、偽物の答えが返った
  (`答えが届きました`、sim_kana_ask.png)。直した後の最後の版でも Ctrl+Space → `kana` → かな が入る。
- `は` が残ったのは自前のかな入力の決まり (変換キーの前の読みの区切り) で、この変更とは関係ない。

### 3.3 ブラウザ版

今の木で `rake wasm:web` (emsdk を読み込んで) → `web_up`。ページの IME の状態は `tools/fmrb_web.rb ime` で見た。

| 確認したこと | 結果 |
|---|---|
| AI ヘルパーを開く | `focused: ime`、カーソル (127,202) 高さ 13、入力欄も同じ位置 |
| `ime "日本語" --updates に,にほ,にほん,にほんご` | 変換中は入力欄が不透明 (幅 48 px)。確定で入力の行に 日本語、カーソル 163 |
| 自前のかな入力 (Ctrl+Space → `kana` → Ctrl+Space) | かな が続けて入る (`日本語かな`) |
| 「編集」のプルダウンを開く | `focused: screen`、カーソルなし。Esc で閉じて戻る |
| Window を動かす | カーソル (65,202) に付いてくる |
| 角をつかんで大きさを変える | カーソル (65,204) (区画の高さが変わった分)。続けて `ime "漢字"` が入る (web_after_resize.png) |

- 本物のブラウザの IME (Windows の MS-IME など) では試していない。ページの側の直しは要らなかった。

### 3.4 回帰と VM プール

- `rake regress`: host 33 suites・regress 310 runs、全部通った。
- `rake regress:screens`: 28 passed / 0 failed (3.6)。
- VM プール (sim、64 ビット、プール 1.5 MB、Modern の標準構成)。AI ヘルパーを開き、gem に hello が通った後の `ps` の mem_used。

| | 値 |
|---|---|
| 直す前 (2 回) | 669,352 B / 669,352 B |
| 最初の形 (x・y・h を別々の 3 つの ivar で覚える) | 671,328 B (+1,976 B) |
| 1 つの整数で覚える形 (2 回) | 670,848 B / (原点を足した最後の版) 671,000 B |

  - 増えたのは +1.6 KB (プールの 0.1%)。メソッド 1 つ (命令列と記号) と ivar 2 つの分。値は起動ごとに同じだったので、GC の
    揺れではなく本当の増分。「KB 単位で増えない」の境目に近いので、比べた数をそのまま書いた。縮めるならメソッドを
    `draw_input` に入れて `draw_buttons` から `draw_input(false)` を呼ぶ形があるが、描く量が増えるので採らなかった。

### 3.5 取りこぼしと後片付け

- `git status` は前からある 3 つ (`components/esp_littlefs`・`components/picoruby-esp32/picoruby`・`wasm/vendor/`) だけ。
- sim は `sim_down`、ブラウザ版は `web_down`、ネイティブ版のコンテナは消した。gem のプロセスは止めた (`pgrep` で 0)。
- sim の `flash/home/` に置いた辞書は消し、`flash/etc/system_conf.toml` は元の写しで戻した。ネイティブ版の試験の文書は
  スクラッチの run の場所の中だけ。
- ビルドの置き場: `build-linux/` は Modern の標準構成のまま (前は Retro だった。次に Retro の sim を使う人は `rake build:linux`
  で作り直す)。`native/build-wl/`・ブラウザ版のバンドルは今の木で作り直した。

### 3.6 rake regress:screens

最後に 1 回: `screens: 28 passed / 0 failed (retro 6/6, modern 6/6, web 5/5, native 6/6, native-keys 5/5; 8.1 min)`。
`build-linux/` は道具が元の Modern の標準構成に戻した。

## 4. GPD で見てほしいこと

1. AI ヘルパーを開き、入力の行で Ctrl+Space (fcitx5 の切り替え) → `nihongo` → 変換 → Enter。変換中の文字が入力の行の
   カーソルの位置に、候補がその下に出て、日本語 が入ること。メニューバーの指示器が `漢` になること。
2. 「編集」のプルダウンを開いている間・問いのボタン (はい・いいえ など) が出ている間は IME が働かず、閉じると戻ること。
3. Window を動かす・大きさを変えた後も、変換中の文字が入力の行に出ること。
4. エディタ・Shell で変換中の文字がカーソルより上 (タイトルバーの分) に出ていないか (1.1。出ているなら 6 章の 1)。

## 5. 確認していないこと

- GPD の本物の環境 (Kubuntu の Plasma・ユーザの fcitx5 の設定)。手元はコンテナの中の fcitx5 + Mozc (ヘッドレス)。
- `--machine sdl` の SDL の Window の IME、本物のブラウザの IME。
- P4-Nano・Tab5 (`set_text_cursor` は何もしないので動きは変わらない見込み)。
- 全部 mruby の構成。`rake regress:wl` (コンポジタの回帰の道具一式) は流していない (道具も C も変えていないため)。

## 6. ユーザに決めてほしいこと

1. **エディタ・Shell のカーソルの申告のずれ** (1.1): 直し方の案は 2 つ。
   - (推奨) エディタ・Shell の側で `@gfx.origin_x`・`origin_y` を足す (sig の「canvas の座標」に合わせる。AI ヘルパーはこの形)。
   - `set_text_cursor` を「区画の座標」に変え、境界 (C) で原点を足す。この場合 AI ヘルパーの足し算を外す。
   どちらもこの段の範囲 (AI ヘルパー) の外なので、作っていない。

## 7. コミット (ブランチ ai-helper-ime、push していない)

- `604737ce` feat(ai_helper): the input line takes a PC's or the browser's IME
- (この report) docs(ai_coding): report on the AI helper's input line with a PC or browser IME
