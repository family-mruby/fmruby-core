# A3e 報告: ブラウザ版の「つながるまで 20 秒」と、AI ヘルパーの入力の行の日本語入力

> 状態: 1 章・3 章は直した、2 章は原因を特定できていない (候補とユーザに見てほしい手順) | 更新: 2026-11-20 | ブラウザ版はネットワークを「つながっている」と報告するようにし、AI ヘルパーの待ちは約 20 秒から 0.5 秒以内になった。Backspace の不具合の原因は mruby の文字数の数え方 (共有された部分文字列の終わりの先を読む) で、AI ヘルパーの側で避けた。エンジンの直しは範囲の外なので案として書いた。漢字変換ができない件は、道具で作れる流れでは再現しなかった。指示書は instruction_a3e.md

GPD・P4-Nano・VPS には触っていない。本物の LLM と鍵は使っていない (gem も動かしていない)。ブランチ `web-net-ime` (push していない)。

## 1. ブラウザ版が Asterism につなぐ前に 20 秒待つ

### 1.1 直す前の値 (確認した)

今の木のバンドル (`rake wasm:web`) を `web_up` で開き、`FmrbApp.wifi_info` と `FmrbApp.wifi_connected?` を画面に出すだけの
試しのアプリ (scratchpad に置き、ページの `/tmp` に put して起動) で見た。

- `wifi_info` = `{connected: false, ip: "127.0.0.1", ssid: "", hostname: ""}`、`wifi_connected?` = false。
  親の見立てどおり: `CONFIG_IDF_TARGET_LINUX` の `default_net_info` が getifaddrs でループバック以外の IPv4 を探し、wasm では
  見つからないので「機能あり・未接続」 (ip は初期値の 127.0.0.1 のまま)。
- AI ヘルパーを開くと、15 秒後もまだ「Wi-Fi につながるのを待っています…」、23 秒後には「ネットワーク (ルータ) に
  つながりません」に進んでいた (`FmrbAsterism::Link#wifi_ready?` の `wifi_wait_ms` 20 秒)。右上の印は赤い斜線。

### 1.2 直した形 (`main/kernel/fmrb_platform_sys.c`)

- `FMRB_PLATFORM_WASM` (wasm/CMakeLists.txt が core_objs に付ける) のときだけの `default_net_info`・`default_net_connected` を
  足した: `connected = true`、ip・ssid・hostname は空 (memset のまま)、`signal = -1`。嘘の値は作らない。
- sim (Linux の 3 コンテナ)・ネイティブ版・ESP32 は前の分岐のまま (wasm の分岐は Linux の分岐より前に置いた)。
- ip について: 今の Linux 版は「見つからなければ 127.0.0.1」だが、ブラウザ版ではページは自分のアドレスを知らないので空にした。
  ブラウザ版のネットワークのダイアログは `ON_WEB` の専用の文 ("The browser handles the network here. This machine has none
  of its own.") を出し、ip を表に出さないので、空が画面に出る所は無い (下の一覧)。

### 1.3 動きが変わる所 (grep した全部)

| 所 | 見ているもの | ブラウザ版での変化 |
|---|---|---|
| デスクトップの右上の印 (`system_desktop.app.rb` `draw_wifi_icon`) | `wifi_info` (有無)・`wifi_connected?` | 灰色の棒 + 赤い斜線 → 白い棒 (確認した)。クリックでダイアログが開くのは前と同じ |
| ネットワークのダイアログ (`network_dialog.rb`) | `wifi_info` | ブラウザ版は `ON_WEB` の分岐で、接続の状態も ip も出さない。変化なし (確認した) |
| Asterism の Link (`picoruby-fmrb-asterism` `link.rb` `wifi_ready?`) | `wifi_info[:connected]` | 待ちが無くなる。AI ヘルパー・Asterism を使うアプリ (麻雀・対戦ゲームなど) がすぐつなぎに行く |
| `FmrbAsterism.hostname` | `wifi_info[:hostname]` | 前も空、今も空。変化なし |
| 時刻合わせ (`services/timesync.rb`)・`services/net.rb` | `net/state`・`wifi_connected?` | ブラウザ版の services (`config/services_wasm.toml`) に net・timesync は入っていない。変化なし |
| Config の Wi-Fi のページ (`default_app/wifi.app.rb`) | `wifi_info` | `FmrbSystem.wifi_networks` が空 (ブラウザ版に Wi-Fi の表が無い) なのでダイアログから開けない。変化なし |
| 遠隔操作 (dev remote ctl・remote_guard) | ESP32 の `wifi_is_connected` | ESP32 だけ。変化なし |
| `FmrbNet.connected?` (`picoruby-fmrb-kernel` net.c) | 前から Linux の構成は true | 変化なし (今回の値と揃った) |
| ようこそ (`tool/welcome.app.rb`) | `wifi_info` | 小さい端末の起動アプリでブラウザ版では出ない。開けば「つながっている」になり ip は空 |
| 試験のアプリ (`app/test` の othello・rt_probe・step_sensor) | `wifi_info` | 表示が「つながっている」になるだけ |

困る変わり方は見つからなかった。

### 1.4 待ちの時間

| | AI ヘルパーを開いてから |
|---|---|
| 直す前 | 15 秒後も「Wi-Fi につながるのを待っています…」、23 秒後に「ネットワーク (ルータ) につながりません」 |
| 直した後 | 0.5 秒後の画面で、もう「ネットワーク (ルータ) につながりません。少し待ってからもう一度つなぎます。」 |

手元のページはルータの設定が無いので「つながりません」に進む。ルータがあれば「AI ホストを探しています…」に進むはず
(つながる先での確認はしていない)。

## 2. AI ヘルパーの入力の行で漢字変換ができない (原因は特定できていない)

### 2.1 確かめた事実

- 公開のバンドルの develop e3ff7ec4 には、A3d の入力の行のカーソルの申告 (604737ce) が入っている (`git merge-base` で確認)。
- ブラウザ版のバンドルに辞書は無く、自前のかな入力の漢字変換は動かない。これは決まった仕様 (doc/kanji_input/plan.md の
  決定: 「ブラウザ版: ブラウザの IME を使う。自前の変換は使わない」、端末の案内 kanji_ja.md 5 章も同じ)。
  よって**自前のかな入力 (Ctrl+Space、または 半角/全角 のキーが端末に届いた場合) では、ひらがなまでで、スペースを押しても
  変換されない**。ユーザの言葉 (ひらがなは入る、スペースで変換されない) は、この道の動きとちょうど合う。
- ページの IME の作りはエディタ・Shell・AI ヘルパーで同じ (wasm/web/main.js の隠しの入力欄は、焦点のあるアプリが
  カーソルを申告している間だけ焦点を持つ。違いはアプリがカーソルを申告するかどうかだけ)。
- `tools/fmrb_web.rb ime` で、次の流れのどれでも AI ヘルパーの入力の行のときは `focused: ime`・カーソル (127,202) だった:
  - パスで開く / ランチャーを DOM のクリック (`dom click`) で開く。
  - エディタ (Window) と AI ヘルパーを並べ、クリックで行き来する (エディタでは (6,24)、AI ヘルパーでは (127,202) に移る)。
  - エディタを F11 で全画面 (640x360) にした後に AI ヘルパーを開く (二重起動のエラーの Window が出ている間だけ `screen`。
    閉じて入力の行をクリックすると `ime`)。
  - 直す前のバンドルで、つながる前の待ち (「Wi-Fi につながるのを待っています…」) の間にも、入力の行のカーソルの申告は
    変わらない (`tell_text_cursor` が「なし」を送るのは問いのボタンが出ている間とプルダウンを開いている間だけ。読んで確認)。
  - 道具の `ime` で送った確定 (日本語・です・漢字) は、どの流れでも入力の行に入った。
- 本物の Windows の IME (MS-IME・Google 日本語入力) は手元に無く、変換キーの出来事の並びは作れていない。

### 2.2 推定 (確かめていない)

ユーザの言葉の 3 つ (ひらがなは入る・スペースで変換されない・タスクバーの IME の印をクリックしても反応しない) が揃うのは、
**その時ページの焦点が隠しの入力欄でなく画面 (canvas) にあり、Windows の IME が無効になっていて、打っていたのは自前の
かな入力だった**場合。Chrome は文字を打てない要素に焦点があるとき IME を無効にするので、タスクバーの印も反応しない。
候補は 2 つ:

1. **(本命) その時、AI ヘルパーのカーソルの申告がページに届いていなかった** (ページの `imeCursor.on` が 0)。どの流れで
   そうなるかは見つけられていない。道具で作れなかった違いは、公開のページ (FMRB_RELEASE=1 の dist、`?zenoh=ws/...`)・
   本物のマウスとキーボード・Windows の Chrome の焦点の扱い。
2. **Windows の IME と自前のかな入力の切り替えがずれた**: 半角/全角 のキーは Windows の IME を切り替えると同時に、
   (ページが IME の処理中のキーと見なさなければ) 端末にも「全角」のキーとして届き、自前のかな入力も切り替わる。
   両方が逆の状態になると (Windows は英字・自前はかな)、ひらがなが入り、スペースで変換されない。ただしこの場合は
   タスクバーの印は反応するはずで、エディタでも同じことが起きるはずなので、ユーザの言葉とは合いにくい。
   半角/全角 のキーで Chrome が出す keydown (keyCode 229 か 243/244 か) は手元で確かめられない。

3 章の Backspace の壊れ (下) は、どちらの入力の道でも起きる別の不具合で、2 章の原因ではない
(壊れた後に打った文字が崩れるのは 3 章の分)。

### 2.3 ユーザに見てほしい手順

公開のページ (または直した後のバンドル) で、Windows の Chrome で:

1. F12 で開発者ツールを開き、Console に `setInterval(() => console.log(document.activeElement.id), 2000)` を打つ
   (2 秒ごとに、ページの焦点のある要素の名前が出る)。
2. AI ヘルパーを開き、入力の行をクリックして、いつものように打つ。そのときに Console に出ている名前を見る。
   - `"ime-box"` なら焦点は隠しの入力欄 (ブラウザの IME が使える状態)。
   - `"screen"` なら焦点は画面 (候補 1 の状態)。そのときの手順 (何を開いて、どこをクリックしたか、エディタが全画面か) を
     そのまま教えてほしい。
3. 同じ状態で、右上のメニューバーの かな の印 (空きメモリの左) を見る。`あ`/`ア` なら自前のかな入力がオン。
   Ctrl+Space で `A` に戻してから、Windows の 半角/全角 で日本語入力にして打ち、スペースで変換されるか。
   - 変換されるなら候補 2 (切り替えのずれ)。
   - ひらがなも入らず英字が入るなら、焦点が画面で Windows の IME が無効 (候補 1)。
4. エディタで同じこと (2・3) をして、`document.activeElement.id` が何か。

## 3. 日本語を打って Backspace で消していると、途中から効かなくなる

### 3.1 再現 (確認した)

- ブラウザ版 (直す前): 自前のかな入力で `あいうえお` → Backspace 1 回目で `あいうえ` (正しい)、2 回目で `あいうえ` の後ろに
  見えない字の分の空き (カーソルが 1 字分右へ進む)、3 回目は変わらない。道具の `ime` で `日本語` `です` と確定 → Backspace
  3 回で `日本語で` + 空き、さらに 4 回押しても変わらない、続けて `漢字` を確定し `ab` を打つと `日本語で` の後ろに崩れた字 +
  `字ab`。ユーザの「途中から効かなくなり、打った文字もうまく反映されなくなる」と同じ。
- sim の Modern (64 ビット): 10 字までは正しく、12 字 (`あいうえおかきさしすせそ`) にしてから Backspace 3 回で、2 回目から
  崩れた (`あいうえおかきさしす` + 崩れた字)。
- エディタ・Shell (どちらも Spinel) では、ブラウザ版で 15 字打って Backspace 3 回、正しく 12 字になった。

### 3.2 原因 (確認した)

AI ヘルパーの Backspace は `@input = @input[0, @input.length - 1]`。読んだ経路と、試しのアプリで確かめたこと:

- mruby (`components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/string.c`) の `String#[]` は、結果が
  埋め込みの長さ (`RSTRING_EMBED_LEN_MAX`: 32 ビットで 11 バイト、64 ビットで 27 バイト) を超えると、元の文字列の領域を
  共有する (`mrb_str_byte_subseq` → `str_share`)。共有した文字列の終わりの後ろには、元の文字列の残りのバイトが続いている。
- `mrb_utf8_strlen` の内側のループ `while (NOASCII(*p))` に終わりの検査が無く、終わりの後ろのバイトが ASCII でない間
  読み続けて数える。上流の mruby は `while (p < e && NOASCII(*p))` に直っている (`utf8_strlen_check`)。
- 試しのアプリ (scratchpad のもの。リポジトリには入れていない) で、ブラウザ版: `"あいうえお"[0, 4]` は bytesize 12 で
  `length` が 7 (正しくは 4)、次の `[0, 6]` は 13 バイト (元の `お` の先頭のバイトまで入る)。リテラルでも、1 バイトずつ
  組み立てた文字列でも同じ。sim (64 ビット) では 10 字 (30 バイト) から 1 字消した 27 バイトは埋め込みで正しく、
  11 字以上で同じ崩れ方になる。
- よって: 1 回目の Backspace は元の (共有でない) 文字列からなので正しい。2 回目は共有した文字列の `length` が大きすぎ、
  `[0, n - 1]` が文字の途中で切る (見えない字の分の空き) か、終わりで切り詰められて何も変わらない (効かなくなる)。
  崩れた後ろに打った字は、崩れたバイトとつながって表示も崩れる。ブラウザ版 (32 ビット) は 4 字以上、sim・ネイティブ版
  (64 ビット) は 10 字以上で起きる。ESP32 (32 ビット) の AI ヘルパーでも同じ条件で起きるはず (確かめていない)。
- `utf8_feed` (途中のバイトの組み立て) と、カーソルの x の計算 (`text_px`・`tail_fit`、バイトで数える) は正しかった。
  ブラウザの隠しの入力欄の側 (確定の後の掃除、Backspace はページから端末へそのまま) も原因ではなかった。

### 3.3 直した形 (`flash/app/modern/ai_helper/screen.rb`)

- Backspace は `erase_char`: 最後の字の先頭のバイトを `getbyte` で探し (続きのバイト 10xxxxxx を飛ばす)、その前までを
  `"" + @input.byteslice(0, i)` で**自前の文字列**にする。`@input` が共有の部分文字列になることが無くなるので、
  `type_char` の `@input.length` (INPUT_MAX の 200 字の判定) も正しく数える。
- エンジン (mruby) は直していない (範囲の外。下の 6 章 1)。

### 3.4 直した後の確認

- sim の Modern: 自前のかな入力で 15 字 → Backspace 3 回で 12 字、さらに 13 回で空 (案内の文に戻る)、`abc ` + かな 20 字ほど +
  ` xyz` (行の幅を超えて左が切れる) → Backspace 10 回で ` xyz` と かな 6 字が消え、続けて `ね` が正しく入った。
- ブラウザ版 (直したバンドル): 道具の `ime` で `日本語` `です` → Backspace 3 回で `日本`、`漢字` を確定、`ab`、自前のかな入力で
  `あいうえお` → `日本漢字abあいうえお`、Backspace 7 回で `日本漢字`、5 回で空、`もう一度` を確定して正しく入った。
- VM プール (sim の Modern、64 ビット)。AI ヘルパーを開いて 6 秒後に debug server の gc → ps の mem_used (gem なし、
  つながらない状態):

| | 1 回目 | 2 回目 |
|---|---|---|
| 直す前 | 653,120 B | 652,672 B |
| 直した後 | 654,040 B | 653,584 B |

  増えたのは約 +0.9 KB (メソッド 1 つの命令列と記号の分)。

## 4. 回帰

- `rake regress` (直す前): host 33 suites・regress 310 runs、全部通った。
- 直した後の `rake regress`・`rake regress:screens`: 下の 4.1 に書く。

### 4.1 直した後

- `rake regress`: host 33 suites・regress 310 runs、全部通った。
- `rake regress:screens` (最後に 1 回): `screens: 28 passed / 0 failed (modern 6/6, retro 6/6, web 5/5, native 6/6,
  native-keys 5/5; 8.2 min)`。`build-linux/` は道具が Modern の標準構成に戻した。

### 4.2 後片付け

- sim は `sim_down`、ブラウザ版は `web_down`。試しのアプリはページの `/tmp` (タブが閉じれば消える) と、sim の
  `flash/tmp/` に置いたもの (消した) だけ。
- ビルドの置き場: `build-linux/` は Modern の標準構成 (TAB5)。ブラウザ版のバンドル (`wasm/build`) は今の木で作り直した。
  `.env` は NARYAv3 のままなので、`sim_up` は「426x240 but 320x240 expected」で止まる (A3d と同じ。stack は上がる)。

## 5. 確認していないこと

- 本物の Windows の IME (MS-IME・Google 日本語入力) と本物のキーボード。2 章は道具で作れる流れでしか見ていない。
- ルータのあるブラウザ版のページで、AI ホストまでつながる所 (1 章は「つながりません」へ進むまで)。公開の dist
  (`FMRB_RELEASE=1 rake wasm:dist`) は作っていない (配置は親)。
- ネイティブ版・ESP32 の AI ヘルパーの Backspace (同じ mruby なので同じく直っているはず。ESP32 は 32 ビットで 4 字から
  壊れていたはず)。
- mruby で動くほかのアプリ (3.2 の不具合はエンジンのもので、`String#[]` の結果に `length`・文字の位置で続けて触るアプリは
  どれも同じ形で崩れうる)。

## 6. ユーザに決めてほしいこと

1. **mruby の `mrb_utf8_strlen` の終わりの検査** (3.2): AI ヘルパーは避けたが、エンジンの不具合は残っている。案:
   - (推奨) `lib/upstream/patch/picoruby-mruby/lib/mruby/src/string.c` に上流と同じ 1 行 (`while (p < e && NOASCII(*p))`) の
     patch を足す (既に gc.c・vm.c の patch がある形)。全部の端末の libmruby の作り直し (`rake clean`) が要る。
   - mruby の版を上げるときに一緒に取り込む (それまでは残る)。
   どちらもこの段の範囲 (AI ヘルパー・ブラウザ版のネットワーク) の外なので、作っていない。
2. 2 章: 2.3 の手順の結果で、直す所 (AI ヘルパー・ページ・半角/全角 のキーの扱い) を決める。

## 7. コミット (ブランチ web-net-ime、push していない)

- `e4786af4` fix(wasm): the browser build reports its network as connected
- `8f32f92e` fix(ai_helper): Backspace on Japanese text stopped working after a while
- (この report と TODO.md) docs(ai_coding): report on the browser build's network state and Japanese input in the AI helper
