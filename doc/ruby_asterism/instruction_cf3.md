# CF3 指示書: AtomS3R の設定のモード (BLE だけで起動して接続の設定を受け付ける)

> 状態: 指示 | 更新: 2026-10-14 | 画面の操作が無い AtomS3R でも、CF2 の BLE のコンソールで接続の設定を書けるようにする。S3 は WiFi と BLE を同時に動かさないので、BLE だけで起動する「設定のモード」と普段のモード (WiFi と Asterism) を起動し直しで切り替える

前提: plan.md の CF の節 (CF3 の行)、report/cf1.md、report/cf2.md (とくに 6 章「CF3 への申し送り」)、instruction_cf2.md、doc/headless_s3/、config/sdkconfig.defaults.atoms3r・system_conf_atoms3r.toml・partitions_atoms3r.csv、`main/drivers/ble/` (ble_task.c・ble_none.c)、`main/drivers/wifi/`、doc/reference/internal_ram_budget.md。

言葉: 文書は常体、「端末」「確認」「ステップ」。技術の用語は例えに置き換えない。パスワード・バックアップなど定着したカタカナ語はそのまま。コンソールの画面は英語。

## 1. 決まっていること (2026-10-14 ユーザ)

- AtomS3R は WiFi と BLE を交互に使う。同時には動かさない (coexistence は sdkconfig で切ってあり、NimBLE が不安定になる。BLE は内蔵 RAM を約 75 KB 使い、AtomS3R は WiFi と TLS の Asterism で空きが約 118 KB)。
- 設定のモードに入る条件: WiFi の設定が無い (`/home/wifi.toml` も `/etc/wifi.toml` も無い、CF2 の要約と同じ判定)、または起動のときにボタンを押している。
- 設定のモードでは WiFi を起こさず BLE だけを起こし、CF2 の受け付ける時間を開いた状態にする (`ble_settings_window_set` を起動のときに呼ぶ)。コンソールは CF2 のものをそのまま使う。
- 書き終えたら起動し直して普段のモードに入る。起動し直しは、コンソールからの指示 (足すなら) か、ボタン・電源の入れ直し。どれにするかは作りを見て決め、report に書く。
- 秘密のファイルの扱いは CF2 のとおり (tar はいつでも読めない、WiFi のファイルは受け付ける時間の間だけ読める)。
- P4・Tab5・Retro の動きは変えない。

## 2. 進め方

### Step 1: 調べて案を出す (ここで一度止まる)

AtomS3R のファームは ble_none.c (BLE 無し) で作っている。BLE を入れるには sdkconfig の変更が要る見込みで、**sdkconfig はユーザの承認なしに変えない**。次を調べて report に案として書き、実装に入らずに返す。

1. BLE を入れる構成: sdkconfig.defaults.atoms3r に足す項目 (NimBLE、coexistence は切ったまま、BLE の内蔵 RAM の割り当て)、ble_none.c と ble_task.c の切り替え (CMake・system_conf)。Retro (NARYAv3) の BLE の構成との差。
2. 内蔵 RAM の見込み: 普段のモードで BLE のコントローラを起こさないときに、静的な内蔵 RAM がどれだけ増えるか (理想は 0。増えるなら何がなぜ増えるか、減らす手)。設定のモードで BLE を起こしたときの空き。
3. 起動の分かれ道を置く場所 (WiFi と BLE の起動の順、system_conf の `wifi_auto_start`・`ble_auto_start` との関係)。
4. ボタン (GPIO と、起動のときに読めるか) と、設定のモードであることを示す手段 (AtomS3R の画面・LED・シリアルのログのどれが使えるか)。
5. flash の区画に収まるか (アプリの区画の空き)。

### Step 2: 作る (ユーザの承認のあと)

Step 1 の案が承認されたら、その範囲で作る: 構成、起動の分かれ道、設定のモードの表示、起動し直しの手段、文書。

## 3. 受け入れ条件 (Step 2)

1. WiFi の設定を消した AtomS3R が設定のモードで起動し、CF2 のコンソールで WiFi・config.toml・tar を書ける。起動し直すと普段のモードで WiFi と Asterism (VPS に TLS) に繋がる。
2. 設定のある AtomS3R を、ボタンを押しながら起動すると設定のモードに入る。押さなければ普段どおり。
3. 普段のモードの内蔵 RAM の増分 0 (同じコミットで比べる)。Asterism の TLS の接続と asterism_demo が今どおり動く。
4. P4-Nano・Retro・Linux・wasm のビルドが通り、P4-Nano の CF2 の動きが変わらない。fmruby-core の試験が通る。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (`feature/cf3-atoms3r-setup`、develop から、コミットのみ、push しない)。`git commit -a` は使わない (`.env` に触らない、ターゲットは `FMRB_HW_TARGET=ATOMS3R` の環境変数で渡す)。コミットはファイルを指定する。**sdkconfig と区画の表は Step 1 では触らない**。
- **端末**: AtomS3R (192.0.2.15、/dev/ttyACM1、`fmruby-bbbbbb`) を使ってよい。焼くのはアプリの区画だけ (全体の焼き直しは `/home` を消す)。今は `/home/asterism/config.toml` と tar で VPS に TLS で繋ぎ、asterism_demo を動かしている。WiFi の設定を消す試験の前に `/home` の設定のバックアップを取り、確認のあと元の設定で繋がる状態に戻す。P4-Nano は触らない (ビルドだけ)。
- **BLE の実機の確認**: この PC (WSL) には Bluetooth が無い。Web Bluetooth でつなぐ確認はユーザが Windows のブラウザで行う。エージェントは端末の側をシリアルと試験で確かめ、ユーザがそのまま行える確認の手順を report の最後に書く。
- ユーザに見せるファイルは `C:\Users\Public\asterism_deploy\` の下だけ (直下にフォルダを作らない)。
- **終わったら**: 起動したものを止める。fmruby-core の build/ は Linux の標準構成 (x86-64)、作業コピーは develop。
- **止まる条件**: Step 1 の終わり (必ず止まる)。内蔵 RAM が普段のモードで増える。区画に収まらない。
- **report** (`doc/ruby_asterism/report/cf3.md`、常体): Step 1 の調べと案 (sdkconfig の差分案・RAM の見込み・分かれ道・ボタンと表示)。Step 2 のあとに、作り、受け入れ条件ごとの結果、ユーザの確認の手順、残った課題。
