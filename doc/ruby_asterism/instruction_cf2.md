# CF2 指示書: BLE のコンソールで端末に接続の設定を書き込む

> 状態: 指示 | 更新: 2026-10-14 | 今ある Web Bluetooth の「Family mruby Console」(`tool/web/`) と端末の BLE のファイルの仕組み (`main/drivers/ble/ble_task.c`) に、接続の設定の画面を足す。WiFi・Asterism の繋ぎ先・証明書の tar を、開発用の遠隔の道具に頼らずに書けるようにする。秘密のファイルを BLE で読み出せる穴を塞ぐ

前提: plan.md の「CF: 端末に接続の設定を入れる」、report/cf1.md (`FmrbAsterism.settings`、`/home/asterism/config.toml`・tar、`/home/wifi.toml` を先に読む作り、申し送り)、doc/reference/ble_c6_web_console.md、`tool/web/` (README)、`ble_task.c` (FMRB File Service、COBS、コマンド CD/LS/RM/MKDIR/STATFS/RENAME/GET/PUT/LOG)、`main/drivers/devctl/devctl_http.c` (CF1 で `/fs/get` から秘密のファイルを外した形)。

言葉: 文書は常体、「端末」「確認」「ステップ」。技術の用語は例えに置き換えない。パスワード・バックアップなど定着したカタカナ語はそのまま。コンソールの画面は英語 (今の tool/web に合わせる)。

## 1. 決まっていること (2026-10-14 ユーザ)

- BLE のコンソールで、端末に接続の設定を書き込めるようにする。
- 書くもの: WiFi (`/home/wifi.toml`: SSID とパスワード)、Asterism の設定 (`/home/asterism/config.toml`: router・node・name)、証明書の tar (`/home/asterism/<名前>.tar`、ブラウザでファイルを選ぶ)。形は CF1 の決めたとおり。
- 鍵や WiFi のパスワードを書けるので、**端末の側の操作で「設定を受け付ける時間」を開いたときだけ**書ける。開き方は、画面のある端末 (Tab5・P4-Nano・Retro) ではデスクトップの操作 (メニューか設定の画面、数分で自動で閉じる)。画面の無い AtomS3R は CF3 (設定のモード) で扱うので、このステップでは対象外。
- **秘密のファイルは BLE で読み出せない**: BLE の GET (と LS の中身の表示) で、`/home/wifi.toml`・`/etc/wifi.toml`・`/home/asterism/*.tar` の中身を返さない (CF1 の申し送り。devctl の `/fs/get` と同じ範囲)。書き込みと削除と名前の一覧は許す。
- 書いたあと、端末は新しい設定を使う: WiFi は起動し直しで効く (または WiFi だけを繋ぎ直す口があれば使う)、Asterism はアプリの起動し直しで効く。コンソールの画面に、どうすれば効くかを出す。

## 2. 作るもの

1. **コンソールの画面** (`tool/web/`): 「Connection settings」の画面。WiFi の SSID・パスワード、Asterism の繋ぎ先 (空なら tar の中の繋ぎ先)・ノード ID (任意)・表示名、証明書の tar の選択。今の端末の設定を読んで表示する (秘密の中身は出さず、「設定あり」だけ)。受け付ける時間が閉じていれば、そう表示して書かせない。
2. **端末の側**: 「設定を受け付ける時間」(開く・閉じる・残り時間) と、その間だけ秘密のファイルの書き込みを許す判定 (`ble_task.c` の PUT・RM・RENAME で、対象が秘密のファイルなら確かめる)。秘密のファイルの GET を断る。デスクトップの開く操作 (メニューの項目か設定の画面のボタン)。
3. **文書**: tool/web の README、doc/reference/ble_c6_web_console.md、plan.md の CF2 の行 (結果)、`doc/ruby_asterism/report/cf2.md`。

## 3. 受け入れ条件

1. コンソールの画面から、受け付ける時間を開いた P4-Nano に、WiFi・config.toml・tar を書ける。閉じているときは断られる。
2. BLE の GET で、秘密のファイル (`/home/wifi.toml`・`/etc/wifi.toml`・tar) の中身が返らない。今までの普通のファイルの GET・PUT は今どおり。
3. 書いた設定で、端末が繋がる (WiFi は起動し直しのあと、Asterism はアプリの起動し直しのあと)。
4. fmruby-core の試験、tool/web の試験 (あれば)、sim の標準構成の起動、wasm のビルドが通る。内蔵 RAM の増分 0 (同じコミットで比べる)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (`feature/cf2-ble-settings`、develop から、コミットのみ、push しない)。`git commit -a` は使わない (`.env` に触らない)。コミットはファイルを指定する。sdkconfig は触らない。
- **端末**: P4-Nano (192.0.2.10、NARYAv4、/dev/ttyACM0) にアプリの区画だけ焼いてよい (全体の焼き直しはしない、`/home` を消すため)。今の P4-Nano は `/home/asterism/config.toml` と tar で VPS に TLS で繋いでいる。確認のあと、その設定で繋がる状態に戻す。AtomS3R (192.0.2.15、/dev/ttyACM1) はこのステップでは使わない (動いたままにしておく)。
- **BLE の実機の確認**: この PC (WSL) には Bluetooth が無いので、Web Bluetooth でつなぐ確認はユーザが Windows のブラウザで行う。エージェントは、端末の側の判定を試験と devctl・シリアルで確かめ、ユーザがそのまま行える確認の手順を report の最後に書く (どのページを開き、何を押し、何が出れば合格か)。コンソールのページの置き場 (今の配り方) も確かめて書く。
- ユーザに見せるファイルは `C:\Users\Public\asterism_deploy\` の下だけ (直下にフォルダを作らない)。
- **終わったら**: 起動したものを止める。fmruby-core の build/ は Linux の標準構成 (x86-64)、作業コピーは develop。
- **止まる条件**: 内蔵 RAM が増える、sdkconfig を変える必要がある、受け付ける時間の開き方を 1 章で決めきれない (案を書いて返す)。
- **report** (`doc/ruby_asterism/report/cf2.md`、常体): 作り、秘密のファイルの扱い、受け付ける時間、受け入れ条件ごとの結果、ユーザの確認の手順、残った課題 (CF3 への申し送り)。
