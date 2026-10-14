# Family mruby Console (tool/web)

Web Bluetooth でつながるブラウザ側の道具。ファイル管理、ログ、スプライト /
マップエディタ、debugd、接続の設定 (Settings)、そしてテキストエディタの**型支援**。

## 開くとき

**http で配信して開く**。

```
python3 -m http.server 8080 --directory tool/web   # か好きな静的サーバ
```

`file://` でも従来の機能は動くが、**型支援だけは動かない**。ブラウザが
`file://` から ES モジュール (js/ti.js) を読めないため。その場合はエディタの
下端にその旨が出る。

## 接続の設定 (Settings、CF2)

端末に WiFi・OpenAI の API キー・Asterism の繋ぎ先・証明書の tar を書く画面。開発用の遠隔の道具
(devctl) が無くても、BLE だけで端末を繋がる状態にできる
(doc/ruby_asterism/report/cf2.md)。

- 書く先: WiFi は `/home/wifi.toml` (`[wifi]` の `enable`・`ssid`・`password`)、
  API キーは `/home/secrets.toml` (`[openai]` の `api_key`。ファイルのほかの行は残す)、
  Asterism は `/home/asterism/config.toml` (`router`・`node`・`name`、
  `FmrbAsterism.config_text` と同じ形)、証明書は `/home/asterism/<名前>.tar`
  (ブラウザで選ぶ。既定でほかの .tar は消す。端末は名前順で最初の .tar を使うため)。
- **端末の側で「設定を受け付ける時間」を開いたときだけ書ける**。デスクトップの
  メニューの「BLE Setup」(日本語では「BLE設定受付」) で 5 分開き、もう一度選ぶと
  閉じる。開いている間はメニューバーの B の枠が黄色になり、メニューの項目に残り時間が
  出る。閉じているときは書く押しボタンが押せず、押せても端末が断る。
- **秘密のファイルの読み方**: 証明書の tar (秘密鍵) と `/home/asterism/` の中
  (`config.toml` を除く) は、端末がいつでも GET を断る (なくしたら Asterism の
  コンソールで出し直す)。WiFi のファイル (`/home/wifi.toml`・`/etc/wifi.toml`) と
  API キーのファイル (`/home/secrets.toml`・`/etc/secrets.toml`) は
  **受け付ける時間の間だけ** BLE で読める (端末の前の人が許したとき)。Files の
  一覧ではどれも `secret` と出て、ダウンロードと編集の押しボタンが無い。
  `config.toml` は読んで欄に入れる。
- **WiFi の表示**: 端末は `0x17` の答えに WiFi の要約を付ける (`"wifi":{"src","on",
  "ssid","conn","ip"}`。パスワードは入らない)。画面は SSID の欄を埋め、どのファイルか・
  今つながっているか・IP を出す。パスワードの欄は、閉じているときは空で
  「set (not shown)」、受け付ける時間が開くと端末から読んで埋める (閉じると消す)。
  パスワードを空のまま書くのは、開いていて今のパスワードを読めたときだけ (それを
  そのまま書く)。それ以外は「enter the password」で書かない。鍵の無い網には対応しない。
- **API キーの表示**: パスワードと同じ扱い。閉じているときは欄が空で、どのファイルに
  あるか (`/home/secrets.toml` か、開発ビルドが入れた `/etc/secrets.toml`) と
  set / not set だけを出す (ファイルの有無は一覧で見る。中身は読まない)。開くと端末から
  読んで欄を埋め、show で見える (閉じると消す)。空のまま書けるのは読めたときだけ
  (`/etc` のキーを `/home` に写すことになる)。引用符・バックスラッシュ・空白を含むキーは
  書かない (tts.rb は逆エスケープしないため)。tts サービスは起動時に読むので、効くのは
  端末の起動し直しのあと。
- 効く時: WiFi と API キーは端末の起動し直しのあと (メニューの Reset)、Asterism は繋ぐ
  アプリの起動し直しのあと。書いたあと画面にそう出る。
- 端末とのやりとりは新しいコマンド `0x17` (受け付ける時間の様子と WiFi の要約:
  `{"ok":true,"open":true,"left":287,"wifi":{...}}`) と、今までの LS / GET / PUT / RM / MKDIR。
  断るときの応答は `{"ok":false,"denied":true,...}` で、コンソールはやり直さない。
  古いファームでは `0x17` が知らないコマンドになり、画面は「ファームが古い」と出す。
- **設定のモード (CF3、AtomS3R)**: 画面の操作が無い端末は、WiFi の設定が無いか
  ボタンを押しながら起動すると BLE だけで起動し、受け付ける時間を起動し直すまで
  開けたままにする。`0x17` の答えに `"setup":true,"until_restart":true` が付き
  (`left` は大きな値)、画面は残り時間の代わりに「until it restarts」と出して
  「Reboot」の押しボタンを出す。押すと `0x18` を送り、端末は答えを返してから
  起動し直す (普段のモード)。`0x18` は設定のモードのときだけ受け付ける
  (doc/ruby_asterism/report/cf3.md)。

## エディタの型支援 (P7)

実機のエディタと同じ推論エンジン (picoruby-ti) を WebAssembly にしたもの。
ブラウザの中だけで完結し、**デバイスにつながっていなくても効く**。

- **Ctrl+Space** — 補完。上下で選び、Enter/Tab で確定、Esc で閉じる。
  選んだ候補の説明が一覧の下に出る。
- **マウスを重ねる** — その語の型かシグネチャ。
- **入力が止まると** — 引数の型が合わない等の指摘に赤い下線。件数は下端。
- **F1** — カーソル位置の名前の詳しい説明 (sig/ の長文ヘルプ)。
- **Docs (日本語/English)** — 説明文の言語。初期値はブラウザの言語。

元は 1 か所、リポジトリの `sig/*.rbs`。ファームウェアの型 db も F1 ヘルプも
同じ場所から作られる。

### 用意する

```
source ~/emsdk/emsdk_env.sh   # emscripten
rake ti:wasm                  # tool/web/wasm/ti.js + ti.wasm + help.json
```

生成物はコミットしない (`.gitignore`)。sig/ を直したら `rake ti:wasm` を
やり直す。

### 検証 (ブラウザ操作なしで通る)

```
node tool/web/wasm/test_wasm.mjs   # WASM モジュールの動作 (Node)
node tool/web/test/ui.mjs          # ページの操作 (Playwright headless)
node tool/web/test/settings.mjs    # 接続の設定の画面 (偽の端末を相手に)
```

`settings.mjs` は navigator.bluetooth を、同じフレームの形 (COBS + CRC32) で答える偽の
端末に置き換えて、受け付ける時間の開閉・書く中身・断られたときの表示を見る。
秘密のファイルの判定そのものは C の試験 (`rake secret:test`) で見る。

`ui.mjs` は静的サーバを立てて headless Chromium で打鍵し、スクリーンショットを
`tool/web/test/shots/` に残す。`npm i` で playwright が要る。
