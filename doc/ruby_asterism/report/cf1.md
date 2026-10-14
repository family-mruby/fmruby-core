# CF1 報告: 端末の Asterism の設定を /home/asterism/ にまとめる

> 状態: 実装・確認済み (AtomS3R の焼き直しだけ未了: 機械が PC にも WiFi にもつながっていなかった) | 更新: 2026-10-14 | plan.md「CF: 端末に接続の設定を入れる」の CF1。ブランチ feature/asterism-cf1 (fmruby-core 08c0cc30・75aa52f8・23c0617f、親 811e0c8)。繋ぎ先・ノード ID・表示名・証明書の tar を `FmrbAsterism.settings` の 1 か所で決め、古い 3 つのファイルは 1 回だけ `config.toml` に書き写す (消さない)。WiFi は `/home/wifi.toml` を先に読む (読む側だけ)。P4-Nano は app だけの焼き直しで古いファイルから書き写し、VPS の TLS のルータへ今までどおり繋がった。flash は P4 +4.5 KB・AtomS3R +4.4 KB、静的な内蔵 RAM は増分 0

## 1. 結果の要約

- **決め方を 1 か所にした**: `FmrbAsterism.settings` (lib/add/picoruby-fmrb-asterism/mrblib/fmrb-asterism.rb) が `Settings` (`router`・`node`・`name`・`credentials`・`error`・`zenoh_config`) を返す。Asterism と Zenoh の試しのアプリ 14 本 (othello・asterism_demo・asterism_rover・rover_brain・rover_cam・rover_pilot・ros2_drive・ros2_service・ros2_talker・ros2_types・rt_probe・zenoh_echo・zenoh_burst・zenoh_nodes) が、各自の `first_line` / `conf_value` を捨ててこれを使う。
- **置き場所**: `/home/asterism/config.toml` (`router`・`node`・`name`。書いた鍵だけが効く) と、今のままの `/home/asterism/<名前>.tar`。
- **古いファイル**: `config.toml` が無いときだけ読み、1 回だけ `config.toml` に書き写す。**消さずに残す** (3 章。最初は消す作りにして、P4-Nano で古いアプリが繋ぎ先を失った)。
- **WiFi**: `/home/wifi.toml` (同じ形) があればそれだけを読み、無ければ今の `/etc/wifi.toml`。開発用の遠隔の道具 (`/fs/get`) は両方の WiFi のファイルを渡さない (4 章)。
- **確かめたところ**: host のテスト 54 項目 (`rake asterism:config_test`)、sim (config.toml だけ・古いファイルだけ・名前の保存・ほかのアプリ 3 本・エディタ 1 打鍵)、ブラウザ版 (ページの ID のまま)、P4-Nano の実機 (古いファイルから書き写して VPS に TLS で繋がる)。

## 2. 決め方

### 2.1 ファイル

```
/home/asterism/config.toml     router = "tls/203.0.113.5:7447"
                               node   = "alice"      (任意)
                               name   = "Alice"      (機械の名前、プロフィールの name)
/home/asterism/<名前>.tar      console が渡す証明書 (G2a のまま)
```

- 1 行に `鍵 = "値"`。値の中では `\"` と `\\` だけを戻す (picoruby に Regexp が無いので、今の `toml_string` をそのまま使う)。ほかの行 (注釈など) は読み飛ばす。
- 書くのは `FmrbAsterism.write_config` (名前の保存と書き写し)。書くときは 3 つの鍵のうち持っているものだけを、決まった順で書く。手で書いた注釈は名前の保存で消える。
- `/home/asterism/` は G2a から `/fs/get` で読めない (鍵があるため)。`config.toml` も同じ扱いになる。置く・一覧・消すはできるので、遠隔で確かめるときは一覧の大きさで見る (P4-Nano では 109 B = 見出し 55 + router 39 + name 15 で、中身どおり)。

### 2.2 順

| 項目 | 順 |
|---|---|
| ルータ | `config.toml` の `router` → 設定 `zenoh_router` (system_conf.toml) → tar の `asterism.json` の `router` → `tcp/zenohd:7447` |
| ノード ID | `config.toml` の `node` → mDNS の名前 → 設定 `asterism_node` → 機種の名前。**TLS で繋ぐときは証明書の名前** (違えばログに 1 行) |
| 名前 | `config.toml` の `name` (無ければアプリはノードの ID を出す、今のまま) |
| 証明書 | ルータが `tls/` のときだけ、`/home/asterism/` の tar (名前順で最初)。無い・読めないときは `error` に理由が入り、アプリが繋ぐときに出す |

- 計画の表では「tar の次に config.toml が勝つ」だけだった。設定 `zenoh_router` を tar より前に置いたのは、これを持つのがブラウザ版だけだから (sim・端末の system_conf には無い)。ブラウザ版は TLS の口を持たず、ページが決めたルータ (`?zenoh=`・Network の枠) を使うべきなので、ブラウザに tar が置かれていても `ws/` のルータが勝つ。端末では今までどおり config.toml → tar → 既定で決まる。
- 計画の「tar から名前を取る」は、tar の `asterism.json` の `name` = 証明書の名前 = ノードの ID と読んだ。表示名 (`name`) は tar からは取らない (asterism.json に表示名の欄は無い)。ノードの ID は G2a のとおり証明書の名前が必ず勝つ (ルータの ACL がその名前で機械を見分けるので、違う ID では断られる)。
- 平文の Zenoh だけを開くアプリ (ros2_*・rover_cam・zenoh_*) にも、TLS のときは `Session.open(..., config: 証明書の zenoh の設定)` を渡すようにした。ただし G0 の ACL は `asterism/<ID>/...` の外を通さないので、ROS 2 や `fmrb/test/*` のキーは VPS の上では使えない (今までどおり)。
- zenoh_nodes の名前は `/home/zenoh_node.txt` (Z3 の試しの名残) をやめ、`settings.node` にした (既定が機種の名前から mDNS の名前に変わる)。このファイルは書き写さない (Z3 の試しにしか使っていない)。
- `FmrbAsterism.saved_name` は VM の起動のたびに呼ばれる (プロフィールの名前)。ここで `config` を読むので、**書き写しはその機械で最初に mruby のアプリが起動したときに起きる** (Asterism を使わないアプリでも)。

### 2.3 ブラウザ版の道

ページは `--fmrb-conf=asterism_node=wasm-xxxxxx` (保存した ID かリンクの ID) と `zenoh_router` を渡す。ブラウザの `/home/asterism/config.toml` は普通は無いので、ノード ID は mDNS (ブラウザでは無い) を飛ばして設定 `asterism_node`、ルータは設定 `zenoh_router` になる。古いファイルも普通は無いので、何も書かれない (`/flash/home` が空のままを確かめた)。O1 で残っていた「リンクで開いたのに `wasm-<16 進>` で出た」件は、`/home/asterism_node.txt` があればリンクより勝つ順だったことと矛盾しない。この順は `config.toml` の `node` に引き継いだ (変えていない)。

## 3. 古いファイルの書き写し (残すことにした理由)

- `config.toml` が無く、古いファイル (`/home/zenoh_echo.txt` の 1 行目・`/home/asterism_node.txt` の 1 行目・`/home/asterism_profile.toml` の `name`) が 1 つでもあれば、値のあるものだけを `config.toml` に書き、ログに `asterism: copied ... into /home/asterism/config.toml` を 1 行出す。古いファイルが 1 つも無ければ何も書かない。
- `config.toml` ができた後は古いファイルを読まない。アプリが設定を読むたびに、残っている古いファイルの名前をログに 1 行出す (`asterism: /home/zenoh_echo.txt, ... not read (/home/asterism/config.toml is)`)。
- **残す理由**: app だけの焼き直し (開発の普通の道) は FmrbAsterism (ファームの中) を新しくするが、storage の中のアプリは古いまま。古いアプリは自分で `/home/zenoh_echo.txt` を読む。最初の作りは「読み直して同じなら消す」だったが、P4-Nano で app だけ焼き直すと、デスクトップの起動で書き写しと削除が起き、古い othello が `tcp/zenohd:7447` に落ちて繋がらなかった (`othello: fmruby-aaaaaa down (tcp/zenohd:7447, 32 ms)`)。古いファイルは `/fs/put` で戻し、残す作りに直した (23c0617f)。残しても新しいコードは読まないので、決め手は 1 つ (`config.toml`) のまま。古いファームに戻したときも今の設定で繋がる。
- 残すことの欠点: 古い手順で `zenoh_echo.txt` を置き直しても効かない。そのことは上のログの行で分かる。文書と手順 (browser.md 5 章、game.md、親の docker/mujoco/README.md、tools/fmrb_zenoh.rb の注釈) は `config.toml` に直した。全部の機械が新しいアプリになったら、古いファイルは手で消してよい (消すコードは入れていない)。

## 4. WiFi の置き場所

### 4.1 今の作り

`/etc` と `/home` は同じ littlefs (`storage` の区画) の中にあり、どちらも動いている間に書ける (HID Inspector は今も `/etc/hid_devices.toml` を書いている)。違いは意味と更新のされ方:

- `/etc` は**ビルドが作るもの** (config/ から生成。`config/wifi.toml` があれば開発のビルドが `/etc/wifi.toml` に焼き込む。リリースのビルドには無い)。storage を焼き直すと、ビルドの中身に戻る。
- `/home` は**使う人のもの** (空で出荷、CLAUDE.md の約束)。app だけの焼き直しでは残る。今は全部の焼き直しで消えるが、いずれ「/home を残して storage を更新する」道を作るなら、そこに入るものは自然に残る。

### 4.2 決めたこと: /home の下へ移す (`/home/wifi.toml`)

- WiFi の SSID とパスワードは、その機械を使う人の設定で、Asterism の繋ぎ先や証明書と同じ種類のもの。`/etc` に書くと、ビルドの作るものと使う人の書いたものが同じファイルで混ざり、開発のビルドの storage を焼くと黙って上書きされる。
- CF2 (BLE のコンソール) が書く先は `/home/wifi.toml` にする。CF3 の「WiFi の設定が無ければ設定のモード」も、このファイルの有無 (と `/etc/wifi.toml`) で決められる。
- 置き場所は `/home/asterism/` ではなく `/home` の直下にした (WiFi は Asterism の設定ではない)。ファイル管理やエディタで見えるのは、使う人のものなので良しとした。

### 4.3 入れたもの (読む側だけ)

- `wifi_task.c`: `/home/wifi.toml` が `fmrb_hal_file_stat` で見つかればそれだけを読み (中身が悪くても `/etc` に戻らない。どちらを読んだか分かりにくくなるため)、無ければ `/etc/wifi.toml`。起動のログに `wifi: Settings from /etc/wifi.toml` の 1 行を足した。形は同じ (`[wifi]` の `enable`・`ssid`・`password`・`hostname`)。
- `devctl_http.c`: `/fs/get` が渡さないものを `/home/asterism/...` に加えて `/home/wifi.toml` と `/etc/wifi.toml` にした (どちらもパスワードを持ち、このサーバは LAN の誰でも呼べる。`/etc/wifi.toml` は今まで読めていた)。置く・一覧・消すはできる。
- config/wifi.toml.example に `/home/wifi.toml` のことを書いた。
- CF2 への申し送り: BLE のファイルの仕組み (`ble_task.c` の GET) は、今は `/home/asterism/` の鍵も WiFi のパスワードも読める。設定を受け付ける時間の作りと一緒に、読める範囲を決める必要がある。

## 5. 確かめたこと

### 5.1 host のテスト

`rake asterism:config_test` (test/asterism/run.rb、54 項目、全部通る)。gem のファイルを、道のりを一時ディレクトリに置き換えて CRuby で動かし、証明書は asterism の checkout の `Asterism::Credentials` で、ここで作った tar から読む。何も無い・config.toml・名前だけ・system_conf の設定・古いファイル 3 つの書き写し (残ること、2 回目は書かないこと、後で古いファイルを変えても読まないこと)・tar だけ (繋ぎ先と証明書の名前)・tar と config.toml の組み合わせ・壊れた tar・TLS なのに tar が無い・名前の保存 (ほかの鍵を残す、空で消す、古いファイルからの書き写しを先にする) を見る。asterism の checkout が要るので `rake test` には入れていない (asterism:test と同じ扱い)。

### 5.2 sim (標準構成、TAB5 の 426x240)

- `/home/asterism/config.toml` だけ (`node = "cf1sim"`、`name = "CF1 sim"`): othello が `cf1sim up (tcp/zenohd:7447)`、画面の名前が「CF1 sim」。
- 古い 3 つのファイルだけ: `asterism: moved ...` (消す作りのとき) のあと `cf1old up`、`config.toml` に 3 つの鍵。名前の欄で「New」を保存すると `name` だけが変わり、`router`・`node` は残った。消さない作りは host のテストと P4-Nano で確かめた。
- zenoh_echo・ros2_talker (ROS のノード名 `fmruby_talker_cf1old`)・asterism_demo が同じ設定で繋がった。エディタを開いて 1 打鍵。

### 5.3 ブラウザ版

`rake wasm:mruby wasm:mrb wasm:web` が通る (最後の作りで 2 回目も)。`?app=/app/test/othello.app.rb` で開くと名前の欄がページの ID (`wasm-4bd85c`) になり、`/flash/home` には何も書かれない。ws/ のルータは立てていないので繋がりはしない (この確認の範囲外)。

### 5.4 P4-Nano (192.0.2.10、NARYAv4、app だけの焼き直し)

焼き直す前の `/home`: `zenoh_echo.txt` (`tls/silentworlds.info:19296`)・`asterism_profile.toml` (`name = "nano4"`)・`asterism/fmruby-aaaaaa.tar`。`asterism_node.txt` は無い。音は消したまま。

1. 新しいファーム (消さない作り) を焼き、`config.toml` を一度消し、新しいアプリを送って othello を起動: `asterism: copied /home/zenoh_echo.txt, /home/asterism_profile.toml into /home/asterism/config.toml`、続いて `othello: fmruby-aaaaaa up (tls/silentworlds.info:19296, 778 ms)`。画面は名前「nano4」、ID `fmruby-aaaaaa (tls/silentworlds.info:19296)`。`/home/asterism/` は `config.toml` (109 B) と tar (消していない)。古いファイルも残っている。
2. WiFi は `wifi: Settings from /etc/wifi.toml` で今までどおり繋がった (`/home/wifi.toml` は置いていない)。
3. 最後は othello を閉じ、デスクトップとサービスだけの状態に戻した。

### 5.5 AtomS3R (未了)

ATOMS3R のファーム (このブランチ) はビルドが通り、写しを `C:\Users\Public\asterism_deploy\cf1_atoms3r\fmruby-core.bin` に置いた。ただ AtomS3R は 192.0.2.15 に応答せず (No route to host)、usbipd にも出ていなかった (Windows に繋がっていない) ので焼けていない。P4-Nano と同じく、焼き直しの後も古いファイルは残るので、storage の古いアプリでも新しいアプリでも今の TLS の設定で繋がるはず。繋いだあとの手順:

```
# fmruby-core で (.env は触らない。P4 の build/ からなら先に rake clean_all)
rake attach                                    # AtomS3R が /dev/ttyACM1 に出る
FMRB_HW_TARGET=ATOMS3R rake build:esp32
# DOCKER_CMD_PRIVILEGED (Rakefile) の形で idf.py -p /dev/ttyACM1 -b 115200 app-flash
```

焼いた後に見るもの: シリアルの `wifi: Settings from /etc/wifi.toml`、asterism_demo を起動して `asterism: copied ...` と TLS の `up`、`/home/asterism/` の一覧に `config.toml` と tar。

## 6. 数字

同じ板・同じ設定で develop (eb90a91a) とこのブランチを比べた (idf.py size)。

| | develop | CF1 | 差 |
|---|---|---|---|
| P4 (NARYAv4) fmruby-core.bin | 6,598,512 B | 6,602,976 B | +4,464 B |
| P4 DIRAM (静的) | 125,372 B | 125,372 B | 0 |
| AtomS3R fmruby-core.bin | 4,158,624 B | 4,162,992 B | +4,368 B |
| AtomS3R Flash .rodata / .text | 1,588,520 / 2,464,928 | 1,592,808 / 2,465,004 | +4,288 / +76 |
| AtomS3R DIRAM (静的) | 111,039 B | 111,039 B | 0 |

- 増えた分のほとんどは libmruby (gem の Ruby のバイトコード、約 +3.8 KB)。C (wifi・devctl) は約 +150 B。
- P4-Nano の起動時の内蔵 RAM (M1 の行): develop のファームとこのブランチで `spawn:Services` 301,452 B と同じ、`wifi_ready` 216,988 → 216,956 B、`wifi_rd` 212,532 → 212,500 B (同じ C のコードの 2 回の起動でも ±30 B ほど揺れる範囲)。定常の増分は 0 とみてよい。
- 書き写しはアプリの起動のときの 1 回だけで、起動のたびのファイルの読みは `config.toml` 1 つと古いファイルの有無 3 つ (今までは古いファイルを 2-3 個読んでいた)。

## 7. 気づいたこと

- **作業の途中で、fmruby-core の checkout の枝が別の手で `develop` に切り替わった** (reflog 19:39:05 「moving from feature/asterism-cf1 to develop」。この作業のコマンドではない)。そのとき両方の枝が同じコミットで、変更はまだコミットしていなかったので変更はそのまま残り、最初の 2 つのコミットが develop に載った。気づいた後に `develop` を eb90a91a に戻し、`feature/asterism-cf1` を 75aa52f8 に合わせた (どちらも push していない)。そのせいで最初の P4-Nano の焼き直しは develop のファームだった (WiFi の新しいログが出ないことで気づいた) ので、焼き直した。上の数字は取り違えを直した後の対応。
- app だけの焼き直しは storage のアプリを新しくしない。設定の読み方のように「ファームとアプリの両方が同じファイルを触る」変更は、古いアプリが残っている間も動く形にしておく (3 章)。

## 8. 残り

- AtomS3R の焼き直しと確認 (5.5)。
- CF2: BLE のコンソールで `/home/asterism/config.toml`・tar・`/home/wifi.toml` を書く。BLE の GET が読める範囲を決める (4.3)。
- 古いファイルを消す時期 (全部の機械のアプリが新しくなったら手で消す。コードでは消さない)。
