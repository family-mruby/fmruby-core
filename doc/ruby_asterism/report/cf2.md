# CF2 報告: BLE のコンソールで端末に接続の設定を書き込む

> 状態: 実装・確認済み。ユーザの確認 (1 回目) を受けて WiFi の表示を直した (8 章)。直したあとの Web Bluetooth の確認はユーザ待ち (7 章) | 更新: 2026-10-14 | plan.md「CF: 端末に接続の設定を入れる」の CF2。ブランチ feature/cf2-ble-settings。Web Bluetooth の「Family mruby Console」に Settings の画面を足し、WiFi (`/home/wifi.toml`)・Asterism の設定 (`/home/asterism/config.toml`)・証明書の tar を書けるようにした。書けるのは端末のメニューの「BLE Setup」で開く 5 分間だけ。秘密のファイル (WiFi の 2 つと `/home/asterism/` の tar など) は BLE でも devctl でも読めない。判定は C の 1 か所 (`main/kernel/fmrb_secret_path.c`) にまとめ、host の試験 97 項目で見る。P4-Nano で内蔵 RAM の増分 0 (同じ板・同じ設定の develop と比べた)、flash +4.3 KB

## 1. 作ったもの

| 場所 | 中身 |
|---|---|
| `main/kernel/fmrb_secret_path.c/.h` | 道を「ふつう・設定 (読める)・秘密」に分ける判定と、遠隔のファイル操作 (GET・PUT・RM・RENAME) の可否。ファイルには触らない純粋な C |
| `main/drivers/ble/ble_task.c` | 設定を受け付ける時間 (`ble_settings_window_set` / `ble_settings_window_left`)、GET・PUT・RM・RENAME の前の判定、新しいコマンド `0x17` (受け付ける時間の様子)。`ble_none.c` に同名の空の版 |
| `main/drivers/devctl/devctl_http.c` | `/fs/get` の判定を上の共通の関数に替えた (CF1 の自前の判定を消した) |
| `FmrbApp.ble_settings_window(秒)` / `FmrbApp.ble_settings_left` | mruby (`picoruby-fmrb-app` の app.c) と Spinel (`fmrb_spx_app.c`・ffi・`fmrb_app_base_spinel.rb`) の両方。sig/fmrb_app.rbs に説明 |
| デスクトップ (`system_desktop.app.rb`・i18n) | メニューの「BLE Setup」(日本語「BLE設定受付」)。開いている間はメニューバーの B の枠が黄色、メニューの項目は「Stop 4:58」(「受付終了 4:58」) と残り時間を出し、選ぶと閉じる |
| `tool/web/` | Settings のタブ (`js/settings.js`、index.html、app.css)。Files の一覧で秘密のファイルに `secret` の印 (ダウンロードと編集の押しボタンを出さない)。コマンドを 1 つずつ送る並び (後述)。断られたら繰り返さない |
| 試験 | `rake secret:test` (`rake test` に入れた)、`node tool/web/test/settings.mjs` (偽の端末を相手にした画面の試験) |
| 文書 | tool/web/README.md、doc/reference/ble_c6_web_console.md、plan.md の CF2 の行 |

### 1.1 コンソールの画面 (Settings)

- 上に受け付ける時間の様子: 開いていれば「The board is accepting settings: 4:32 left.」、閉じていれば「On the board, open the menu and choose "BLE Setup"」。2 秒ごとに `0x17` で端末に聞き、間は手元で数える。古いファームでは `0x17` が知らないコマンドになるので「ファームが古い」と出す。
- WiFi: SSID・パスワード → `/home/wifi.toml` (`[wifi]` の `enable = true`・`ssid`・`password`、TOML の文字列として `\"`・`\\`・制御文字を逃がす)。SSID は 1-32 バイト、パスワードは空 (鍵の無い網) か 8-64 文字。書いたらパスワードの欄を空にする。
- Asterism: router・node・name → `/home/asterism/config.toml`。形は `FmrbAsterism.config_text` と同じ (見出しの 1 行と、値のある鍵だけを決まった順で)。今の `config.toml` を読んで欄を埋める。router は空なら書かない (tar の中の繋ぎ先が使われる)。`tls/`・`tcp/` などで始まらなければ書かない。
- 証明書: ブラウザで .tar を選ぶ → `/home/asterism/<名前>.tar` (名前は英数字と `._-` 以外を `_` に)。既定で「ほかの .tar を消す」(端末は名前順で最初の .tar を使うので、古いものが残ると新しいものが使われないことがある)。256 KB まで。
- 端末にあるものは「設定あり」だけを出す: WiFi は `/home` と `/etc` の一覧で `wifi.toml` の有無 (`/home` があればそれ、無ければ「ファームの /etc/wifi.toml」)、証明書は .tar の名前。中身は出さない (読めない)。
- 書いたあとに効かせ方を出す: WiFi は「次の起動から (メニューの Reset)」、Asterism と証明書は「繋ぐアプリを起動し直す」。WiFi だけを繋ぎ直す口は wifi_task に無い (起動のときに 1 回読むだけ) ので、作らずに起動し直しにした。

### 1.2 コマンドを 1 つずつ送る

今までの `sendCommand` は答えを待つ口が 1 つで、答えの前に次のコマンドを送ると答えを取り違える。Settings の画面は裏で 2 秒ごとに `0x17` を送るので、ほかの操作と重なりうる。`sendCommand` を Promise の列でつないで、1 つずつ送るようにした (中身は `sendCommandNow` に移した)。

## 2. 秘密のファイルの扱い

判定は `fmrb_secret_path.c` の 1 か所。BLE と devctl の両方がこれを使う。

| 道 | 分け方 | BLE GET | BLE PUT・RM | BLE RENAME | devctl `/fs/get` |
|---|---|---|---|---|---|
| `/home/wifi.toml`・`/etc/wifi.toml` (`wifi.toml` で始まる名前、`.part` も) | WiFi (8 章) | 受け付ける時間だけ | 受け付ける時間だけ | 同じか強く隠す名前へ、時間だけ | 断る |
| `/home/asterism/` の中 (`config.toml` を除く。tar・ほかの鍵・`.part`) | 秘密 | 断る (いつでも) | 時間だけ | 秘密どうしで、時間だけ | 断る |
| `/home/asterism/config.toml`・`/home/asterism` | 設定 | 読める | 時間だけ | 時間だけ | 読める (CF1 では断っていた) |
| `/`・`/home`・`/etc` | ふつう (秘密を中に持つ) | - | 今どおり | 断る | - |
| ほか | ふつう | 今どおり | 今どおり | 今どおり | 今どおり |

- **秘密から秘密でない名前への RENAME は、時間に関係なく断る** (名前を変えれば読めてしまうため)。秘密を中に持つディレクトリ (`/home`・`/etc`・`/home/asterism`) の RENAME も同じ理由で断る。
- 道の書き方で抜けないように、空と `.` の段を飛ばし、先頭の `/flash` (file HAL の別名) を外して比べる。BLE は devctl と違って道の範囲を絞っていないので、`/flash/home/wifi.toml`・`home/wifi.toml` (相対)・`//home//wifi.toml` も同じものとして扱う。`..` を含む道は秘密とみなす (littlefs は `..` を解決するので、`/home/x/../wifi.toml` が本物に届く)。
- 指示書の範囲 (WiFi の 2 つと tar) より少し広く、`/home/asterism/` の中を `config.toml` 以外すべて秘密にした。将来この場所に鍵が増えても漏れない。逆に `config.toml` は繋ぎ先・ID・名前だけで鍵を持たないので読めるようにし、画面の欄を埋めるのに使う。devctl も同じ判定にしたので `/fs/get` の範囲は BLE と同じ (CF1 からの差は `config.toml` が読めるようになったことだけ)。
- `config.toml` も書くのは受け付ける時間だけにした。繋ぎ先を書き換えられると、平文のルータへ繋がせることができるため。
- LS と MKDIR は今どおり (名前と大きさは見える。指示書の「名前の一覧は許す」)。
- 断るときの応答は `{"ok":false,"denied":true,...}` (時間外は `"closed":true` も)。コンソールの `sendCommandRetry` はこれを見たら繰り返さない (今までは 4 回送っていた)。端末のログには `BLE GET /home/asterism/x.tar refused: holds a key` のように 1 行出る (中身は出さない)。
- 限界: BLE は暗号化もペアリングも無い (Windows の Web Bluetooth で固まるため、ble_task.c の注釈)。受け付ける時間は「端末の前の人が許したときだけ書ける」ための仕組みで、その間に近くで電波を聞かれることは防げない。

## 3. 受け付ける時間

- 開き方: デスクトップのメニューの「BLE Setup」(Tab5・P4-Nano・Retro のどれも同じ項目。sim とブラウザ版には出さない)。300 秒開く。開いている間にもう一度選ぶと閉じる。時間が来ても閉じる (ログに `settings window closed (time up)`)。
- 見え方: メニューバーの B の枠が黄色になる (毎秒の描き直しで、新しい割り当ては無い)。メニューの項目に残り時間。
- Retro では BLE が止まっていれば (ble_auto_start=false) 先に `FmrbApp.ble_start` を呼ぶ。WiFi が動いていて BLE を起こせないときは開かない (ログに `BLE is off`)。CF3 の設定のモードはこの外。
- 状態は終わりの時刻 (起動からの秒) の 32 ビット 1 語で、PSRAM に置いた (`EXT_RAM_BSS_ATTR`)。書くのはデスクトップのタスク、読むのは BLE のファイルのタスクで、1 語なので鎖錠は要らない。
- PUT は塊ごとに確かめる。時間切れの途中で大きな tar の書き込みは止まる (やり直せばよい)。

## 4. 受け入れ条件ごとの結果

### 4.1 受け付ける時間を開いた P4-Nano に書ける、閉じていれば断られる

- **端末の側の判定**: `rake secret:test` の 97 項目 (分け方 37、秘密を持つディレクトリ 12、GET 9、PUT・RM 21、RENAME 18) が通る。これは ble_task.c が呼ぶ関数そのもの。
- **画面**: `node tool/web/test/settings.mjs` の 25 項目が通る。偽の端末で、閉じているとき「not accepting」の表示と押しボタンの無効、無理に押しても端末の断りが出て何も書かれないこと、開くと残り時間が出て押せること、書いた `wifi.toml`・`config.toml`・tar の中身がバイト単位で期待どおりなこと、ほかの .tar が消えること、閉じ直すとまた押せなくなること、秘密の GET が 1 回で諦めること、ふつうの GET が今どおりなことを見る。今までの `ui.mjs` と `test_wasm.mjs` も通る。
- **P4-Nano** (app だけの焼き直し): メニューの「BLE Setup」で `ble_task: settings window open for 300 s`、もう一度選んで `settings window closed`、開いたまま置いて 300 秒後に `settings window closed (time up)` (21:19:34 → 21:24:34)。画面は B の枠が黄色、メニューに「Stop 4:58」。
- **Web Bluetooth でつないで書くところは未確認** (この PC に Bluetooth が無い)。ユーザの確認の手順は 7 章。

### 4.2 秘密のファイルの中身が返らない、ふつうのファイルは今どおり

- BLE: 上の試験 (C と画面)。実機の BLE は 7 章でユーザが確かめる。
- devctl (同じ判定の関数) を P4-Nano で: `/home/wifi.toml`・`/home/asterism/<名前>.tar`・`/home/./asterism/<名前>.tar` の `/fs/get` は 403 (`this file holds keys or passwords and is not read remotely`)、`/home/asterism/config.toml` は読めた (109 B、中身は端末に入っていたものどおり)。一覧は今どおり (config.toml 109 B と tar 8192 B)。

### 4.3 書いた設定で端末が繋がる

BLE が使えないので、**コンソールの `settings.js` の関数で作ったファイル**を devctl で置いて確かめた (BLE で運ぶところだけが違う)。

1. `settingsWifiToml` に開発用の config/wifi.toml の SSID とパスワードを入れて作った `wifi.toml` を、TOML として読み直して同じ値になることを確かめてから `/home/wifi.toml` に置いた。`settingsAsterismToml` で作った `config.toml` は、端末にあった `config.toml` (`FmrbAsterism` が書いたもの) と**バイト単位で同じ**だった。それも置いた。
2. 起動し直すと `wifi: Settings from /home/wifi.toml`、`Associated with AP`。othello を起動して `othello: fmruby-XXXXXX up (tls/..., 894 ms)` (VPS のルータに TLS で繋がった)。
3. 戻し: `/home/wifi.toml` を消して起動し直し、`wifi: Settings from /etc/wifi.toml`、othello は `up (tls/..., 710 ms)`。最後に othello を閉じた。`config.toml` と tar は元のまま (中身が同じ)。

### 4.4 試験・sim・wasm・内蔵 RAM

- `rake test`: 全部通る (secret 97、BASIC ゴールデン 103 ほか)。
- sim (標準構成、TAB5 の 426x240): 起動し、メニューに「BLE Setup」が出ない (Linux には BLE が無い) ことを確かめ、エディタを開いて 1 打鍵。
- ブラウザ版: `rake wasm:mruby wasm:mrb wasm:web` が通る。
- Retro (NARYAv3) の ESP32 ビルドも通る (ble_task.c が S3 でもコンパイルされるため)。新しい記号は `.flash.text` と `.ext_ram.bss` に入っている。
- **内蔵 RAM (P4-Nano、NARYAv4、同じ設定で develop 428a6c61 とこのブランチを比べた)**:

| | develop | CF2 | 差 |
|---|---|---|---|
| fmruby-core.bin | 6,602,976 B | 6,607,296 B | +4,320 B |
| Flash .text / .rodata | 3,677,924 / 2,820,912 | 3,679,812 / 2,823,352 | +1,888 / +2,440 |
| DIRAM (静的) | 125,372 B | 125,372 B | 0 |
| M1 `spawn:Services` | 301,452 B | 301,452 B | 0 |
| M1 `ble_ready` | 231,300 B | 231,300 B | 0 |
| M1 `wifi_ready` / `wifi_rd` | 216,976 / 212,520 B | 216,964 / 212,508 B | -12 / -12 |
| 周期ダンプの `IRAM free` | 219,060 B | 219,032 B | -28 |

`boot_start` から `ble` までの 22 行はすべて同じ値。`wifi_ready` 以降の -12 B と `IRAM free` の -28 B は、CF1 で同じコードの 2 回の起動でも出た ±30 B の揺れの範囲 (WiFi の接続の時機で変わる)。新しい状態は PSRAM の 4 B だけで、関数はどれも flash に置かれている (`riscv32-esp-elf-objdump` で確かめた)。定常の増分は 0。

## 5. 気づいたこと

- Files の一覧は、秘密のファイルを開こうとしてもダウンロードを試みず、Settings のタブへ移る。端末が断るので試みても害は無いが、何度も断られる表示を避けた。
- devctl は道の範囲を `/mnt/sd`・`/home`・`/app`・`/usr/share` に絞り `..` を断るが、`/etc` は範囲の外なので `/etc/wifi.toml` は元々読めなかった。BLE はどこでも読めるので、`/flash` の別名・相対の道・`..` まで判定に入れた。
- BLE のコンソールの公開の置き場は https://family-mruby.github.io/console/ で、サイトのリポジトリの `scripts/sync-console.sh` が fmruby-core の **main** の `tool/web` を取り込む。Settings の画面が公開の置き場に出るのは、このブランチが main に入りサイトを同期したあと。それまでは手元のファイルを開く (7 章)。

## 6. 残り (CF3 への申し送り)

- **AtomS3R** (画面が無い) はこのステップの外。BLE の判定と受け付ける時間はファームの共通の部分なので、CF3 の設定のモードでは「設定のモードで起動したら受け付ける時間を開いた状態にする」(`ble_settings_window_set` を起動のときに呼ぶ) だけで、このコンソールがそのまま使える。AtomS3R は ble_none.c (BLE 無し) で作っているので、CF3 では BLE を入れる構成が要る (内蔵 RAM の割り当ては CF3 の計画どおり交互に使う形で)。
- 設定のモードの判定に使う「WiFi の設定が無い」は、`/home/wifi.toml` と `/etc/wifi.toml` の有無で見られる (コンソールの表示と同じ)。
- WiFi だけを繋ぎ直す口 (起動し直さずに新しい `wifi.toml` を使う) は作っていない。wifi_task に足すなら、STA を止めて読み直す関数と、その間の devctl・遠隔の画面の扱いが要る。
- 受け付ける時間は 300 秒の固定。必要なら system_conf の設定にする。
- 公開の置き場への反映 (5 章) は main へのマージとサイトの同期を待つ。

## 7. ユーザの確認の手順 (Windows の Chrome か Edge)

前提: P4-Nano にはこのブランチのファームが入っている (app だけ焼き直し済み)。BLE は起動のときから動いている (名前は `Family-mruby-` で始まり、MAC の下 6 桁が続く)。

1. エクスプローラで `C:\Users\Public\asterism_deploy\cf2_console\index.html` をダブルクリックして Chrome (か Edge) で開く。(うまく開けないときは WSL で `python3 -m http.server 8080 --directory tool/web` を fmruby-core で実行し、`http://localhost:8080/` を開く)
2. 「Connect」を押し、一覧から `Family-mruby-...` を選んで「ペア設定」。上の表示が「Connected」になり、ログに `Connected (console 2026-10-14a)` が出る。
2 で出る版が `2026-10-14b` でなければ、ブラウザが古い app.js を持っている (再読み込みする)。
3. **秘密が読めないこと**: Files で `/etc` を開く。`wifi.toml` の行に `secret` の印があり、DL・Edit の押しボタンが無いこと。`/home/asterism` を開き、tar にも `secret` の印があり、`config.toml` には Edit があること (開くと router と name が見える)。
4. **閉じているときの表示 (8 章の直し)**: 「Settings」のタブを開く。上に「The board is not accepting settings...」と出て、3 つの「Write ...」が押せないこと。WiFi の「On the board」が `SSID "<今の SSID>" from the firmware's /etc/wifi.toml (...); connected, IP <端末の IP>` で、SSID の欄に今の SSID が入り、パスワードの欄は空で下に `set (not shown)` と出ること。Asterism は今の router と name、Credentials は今の tar の名前であること。
5. **開くと書けること**: P4-Nano の画面でメニュー (左上の「Family mruby」) → 「BLE Setup」。メニューバーの B が黄色になる。数秒でコンソールの上の表示が緑の「The board is accepting settings: 4:5x left.」になり、押しボタンが押せるようになる。パスワードの欄に今のパスワードが入り (「show」で見える)、下が `loaded from the board ...` になること。
6. Asterism の欄 (今の値が入っている) を変えずに「Write Asterism settings」。`Written to /home/asterism/config.toml...` と出れば合格 (中身は同じなので繋がりは変わらない)。
7. WiFi の欄は入ったまま (SSID とパスワードは端末から読んだもの) で「Write WiFi」。`WiFi written to /home/wifi.toml...` と出て、「On the board」が `SSID "..." from /home/wifi.toml; connected, ...` になれば合格。端末のメニューの Reset で起動し直し、WiFi がつながる (メニューバーの電波の印が白) こと。
   - (任意) パスワードの欄を空にして「Write WiFi」を押しても、読んであったパスワードがそのまま書かれる (開いている間だけ)。閉じているときに空のまま押すと「enter the password」と出て書かれない。
8. (任意) 証明書: 「Credentials」で今使っている .tar (Asterism のコンソールから受け取ったもの) を選び「Write credentials」。`Credentials written to /home/asterism/<名前>.tar` と出れば合格。そのあと othello などを起動して繋がること。手元に .tar が無ければ飛ばす (端末の tar はそのまま)。
9. **時間が終わると断られること**: 端末でメニュー → 「Stop m:ss」を選ぶ (または 5 分待つ)。コンソールの上の表示が「not accepting」に戻り、押しボタンが押せなくなり、パスワードの欄が空に戻れば合格。
10. 戻し (任意): 7 で書いた `/home/wifi.toml` は、同じ網の設定ならそのままでよい。消したいときは、もう一度「BLE Setup」を開いてから Files の `/home` で `wifi.toml` の Del を押し、起動し直す (`/etc/wifi.toml` に戻る)。

## 8. ユーザの確認を受けた直し (2026-10-14)

1 回目の確認で「Settings の WiFi の欄に今の設定が何も出ない」との指摘があり、続けて「パスワードは、受け付ける時間の間だけなら見せてよい」と決まった。

### 8.1 決めたこと

- SSID は秘密ではない。秘密はパスワードと証明書の tar (秘密鍵) だけ。
- WiFi のファイル (`/home/wifi.toml`・`/etc/wifi.toml`) は、**受け付ける時間の間だけ BLE で読める** (端末の前の人が「BLE Setup」を押したとき)。それ以外は断る (`"closed":true`)。devctl には受け付ける時間が無いので、`/fs/get` はいつでも断る。
- 証明書の tar と `/home/asterism/` の中 (config.toml 以外) は、**いつでも読めない** (Asterism のコンソールで出し直せるので、読み戻す必要が無い)。
- 判定の分け方を 1 つ足した (`FMRB_PATH_WIFI`)。RENAME は「同じか強く隠す名前へ」だけ通す (強さは tar > WiFi > ふつう)。tar を `/home/wifi.toml` に名前を変えて、受け付ける時間に読む、という抜け道を断つため。

### 8.2 作ったもの

- `0x17` の答えに WiFi の要約を足した: `"wifi":{"src":"/etc/wifi.toml","on":true,"ssid":"...","conn":true,"ip":"..."}`。wifi_task と同じ順でファイルを選び (`/home/wifi.toml`、無ければ `/etc/wifi.toml`)、`enable` と `ssid` の行だけを読む (`fmrb_wifi_summary_json`、純粋な C)。パスワードの行は見ず、読んだ文字の置き場 (PSRAM の 1 KB) は使ったあと 0 で埋める。ファイルから読むので、書いたばかりの `/home/wifi.toml` も起動し直す前に表示に出る。つながりと IP は wifi_task の今の様子。
- 受け付ける時間を開いたとき、同じ要約を BLE のファイルのタスクがログに 1 行出す (`settings window: wifi {...}`)。ファイルを読むのをデスクトップのスタックに載せないため、フラグを立ててファイルのタスクに任せた。
- コンソール:
  - SSID の欄を要約から埋め、「On the board」に SSID・どのファイルか・つながっているか・IP を出す。
  - パスワードの欄は、閉じているときは空で `set (not shown)`。開くと端末から WiFi のファイルを読んで埋め (`settingsTomlValue`)、閉じると消す。「show」で見える。
  - パスワードを空のまま書くのは、開いていて今のパスワードを読めたときだけ (そのまま書く)。それ以外は「enter the password」で書かない。古いパスワードを残したまま一部だけ変える書き方はしない。鍵の無い網 (パスワード無し) は対応しない。
  - 版は `2026-10-14b`。

### 8.3 確かめたこと

- host の試験 `rake secret:test` が 111 項目 (14 項目増)。WiFi のファイルは閉じていれば GET を断り開いていれば読めること、tar は開いていても断ること、tar から WiFi の名前への RENAME を断ること、要約 (SSID の取り出し、`\"`・`\\`・`\u00e9` の戻し、`#ssid` や `ssid2`・`password_ssid` を取り違えないこと、ファイルが無いとき、小さな置き場で切れても終端すること) とパスワードが要約に入らないことを見る。
- `tool/web/test/settings.mjs` が 33 項目 (8 項目増)。SSID の表示と欄、パスワードの欄が空で `set (not shown)`、閉じている間の端末の答え (偽の端末が送ったもの全部) にパスワードが無いこと、閉じているとき空のパスワードを断ること、開くとパスワードが読み込まれること、開いていても tar は読めないこと、空のまま書くと読んだパスワードが書かれること、閉じるとパスワードの欄が消えること。3 回続けて通る。`ui.mjs` も通る。
- P4-Nano (app だけ焼き直し): 「BLE Setup」を開くとログに `settings window: wifi {"src":"/etc/wifi.toml","on":true,"ssid":"<開発の網の SSID>","conn":true,"ip":"<端末の IP>"}`。IP は端末の今のアドレスと一致し、行にパスワードの文字列は無い (grep で 0 件)。閉じて `settings window closed`。devctl の tar の `/fs/get` は今どおり 403。端末はこのファーム・閉じた状態のまま残した。

### 8.4 内蔵 RAM (P4-Nano、NARYAv4、develop 428a6c61 と同じ設定で比べた)

| | develop | 直したあと | 差 |
|---|---|---|---|
| DIRAM (静的、.text / .bss / .data) | 85,300 / 25,156 / 14,916 | 85,300 / 25,156 / 14,916 | 0 |
| External RAM .bss | 18,147,292 B | 18,148,516 B | +1,224 (PSRAM、WiFi の要約の置き場) |
| fmruby-core.bin | 6,602,976 B | 6,609,760 B | +6,784 |
| M1 `boot_start` 〜 `ble` (起動 3 回) | `ble_ready` 231,300 (2 回とも) | 231,292 / 231,292 / 231,300 | 揺れ (下記) |
| 周期ダンプの最初の `IRAM free` (13 秒) | 219,060 / 219,052 | 219,036 / 219,056 / 219,032 | 揺れの範囲 |

- 1 回目と 2 回目の起動で `ble_ready` が 8 B 少なかったので、develop のファームを焼き直して 2 回目の標本を取り (231,300)、このブランチを焼き直して 3 回目を取った (231,300 で develop と同じ)。`ble_nimble_port` から `ble_ready` の間に動くのは今までの初期化だけで、この直しの関数は `0x17` か「BLE Setup」でしか動かない。この間も Services やデスクトップが並んで動くので、その時機で数バイト揺れる。
- 新しい状態は PSRAM だけ (`.ext_ram.bss`) で、静的な内蔵 RAM は 0。起動のあとしばらくした `IRAM free` は、ネットワークの様子で数百バイト動く (develop でも 208,508 → 208,528 と動いた) ので比べる数字にしなかった。
- Retro (NARYAv3) の ESP32 のビルドも通る。
