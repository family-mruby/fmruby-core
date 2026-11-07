# 報告: 小さい端末の起動アプリ welcome (StickS3・AtomS3R)

> 状態: 完了 (検収待ち) | 更新: 2026-11-07 | 3.0.0 で Web Installer が StickS3・AtomS3R のファームを配り始めるのに合わせ、起動アプリを試しのアプリ (step_sensor・atoms3r_hello) から状態を見せる welcome (`/app/tool/welcome.app.rb`) に替えた。Family mruby と版・機種、Wi-Fi (off / connecting / connected と IP・mDNS 名)、次の手 (ボタンを押しながら電源を入れて Console から BLE で Wi-Fi を書く)、動いている印 (回る四角と経過時間) を出す。C は足していない。描画の記録に 2 つの大きさの場面を足し、StickS3 の実機で起動を確認した

## 1. 作ったもの

- `flash/app/tool/welcome.app.rb` + `.app.toml` (fullscreen、ランチャーに出さない、single_instance)。
  - 版は `FmrbConst::OS_VERSION`、機種は `FmrbConst::BOARD` (sticks3 / atoms3r は「M5Stack StickS3」などの名前に。幅が足りなければ「M5Stack」を外す)。
  - Wi-Fi は `FmrbApp.wifi_info` (既存。`connected`・`ip`・`ssid`・`hostname`) を 1 秒ごとに読む。nil か SSID が空なら off、SSID があってつながっていなければ connecting、つながれば connected で IP と `<hostname>.local`。変わったときだけその区画を描き直し、ログに 1 行出す (`welcome: Wi-Fi connected <ip> <host>.local`)。
  - 次の手の文: つながっていれば「Wi-Fi を変えるには、ボタンを押しながら電源を入れて Family mruby Console (BLE) で」、つながらない・off なら「ボタンを押しながら電源を入れ、ブラウザで Family mruby Console を開いて BLE で Wi-Fi を設定」。ボタンは StickS3 が front button (KEY1、G11)、AtomS3R は画面 (G41、押すとボタン)。どちらもカーネルの設定のモードの判定に使うピン (`FMRB_PIN_BUTTON_ENTER`)。
  - 動いている印: 題の帯の右の 2x2 の四角が 250 ms ごとに時計回りに 1 つずつ光る (変わる 2 つだけ描く) のと、右下の経過時間 (`up m:ss`、1 時間を超えると `h:mm:ss`)。
  - 大きさで組む: 幅 200 px 以上 (240x135) は題・Wi-Fi の行・IP を 2 倍の文字、128x128 は全部 1 倍。文は 6x8 の字の桁数で語の切れ目で折る。英語のみ (この 2 機種は font_ja が無い)。入力は取らない。
- `config/system_conf_sticks3.toml`・`config/system_conf_atoms3r.toml` の `startup_app` を `/app/tool/welcome.app.rb` に。
- 置き場所は `flash/app/tool/` (2 機種の storage_exclude は app/tool を外していない。ビルドのログで `Adding File: app/tool/welcome.app.rb` を確認)。ファイルは 7.8 KB。

## 2. 見立てと違った点: BLE のモードではアプリが動かない

- 指示書は「BLE のモードのときは BLE の名前と Console の案内を出す」だったが、BLE のモード (= 設定のモード、cf3) ではカーネルが**アプリを起動しない** (内蔵 RAM を BLE に回すため。`fmrb_kernel.rb` の `Setup mode: no app started`)。代わりにカーネルの C (`fmrb_kernel_show_setup`) が紺の画面に「SETUP MODE / 理由 / BLE name: Family-mruby-xxxxxx / Connect the Family mruby Console, write the settings, then press the button or click Reboot.」を出す。
- よって BLE の名前と Console の案内はその画面が受け持ち、welcome は普段のモード (Wi-Fi) の側だけを出す。普段のモードでは BLE は起きていないので、welcome から BLE の名前は読めない (読むなら C が要る)。設定のモードでもアプリを動かすのは動きの変更なのでしていない。
- 設定の画面の文言に「in a browser」を足すかどうかは C の変更なので触っていない (必要なら親・ユーザの判断)。

## 3. 確認

### 3.1 rake regress

- `tools/regress/scenes/welcome.rb` を足した (mini の 2 つの組だけ): connected / connecting → 1.2 秒後につながる / off (wifi_info が nil) / 1 時間後。
- 基準 8 件は別コミット (4394ffae、`added welcome/{connected,connecting,off,long_uptime}@mini-{240x135,128x128}`)。追加後の `rake regress`: `host: 21 suites, 21 passed` / `regress: 213 runs (77 scenes on 4 profiles), 213 passed, 0 failed`。
- 記録を画素に起こした絵 (6x8 の字は LovyanGFX の glcdfont、3 倍に拡大。作業用の描き起こしで、本物の合成とは別):
  - `tmp/welcome_app/connected_mini-240x135.png`、`tmp/welcome_app/connected_mini-128x128.png`
  - `tmp/welcome_app/connecting_before_join_mini-240x135.png`、`tmp/welcome_app/connecting_before_join_mini-128x128.png` (つながる前)
  - `tmp/welcome_app/connecting_mini-240x135.png`、`tmp/welcome_app/connecting_mini-128x128.png` (つながった後)
  - `tmp/welcome_app/off_mini-240x135.png`、`tmp/welcome_app/off_mini-128x128.png`
  - `tmp/welcome_app/long_uptime_mini-240x135.png`、`tmp/welcome_app/long_uptime_mini-128x128.png`

### 3.2 StickS3 の実機

- 端末の区画表は新しい方 (factory 5632K・storage 0x590000 から 2496K) であることを esptool の read_flash で確かめてから書いた。
- `/etc/system_conf.toml` は config/ から生成されてイメージに入るだけで、devctl の `/fs/put` は /etc を受け付けない (`path outside /mnt/sd, /home, /app, /usr/share`)。そこで storage は次の手で書いた:
  1. storage 区画を read_flash で丸ごと退避 (`~/asterism_keys/backup_sticks3_20261107/storage.bin`、鍵を含むので 700/600。460800 bps は途中で落ちたので 115200)。
  2. littlefs-python で開き、ビルドの `build/storage_root` と比べた。違いは /etc/system_conf*.toml と /home だけ (端末の storage は他は HEAD と同じだった)。
  3. `build/storage_root` の /home を退避した /home (wifi.toml・証明書の tar・config.toml・step_sensor.toml・music) に差し替えたイメージを作り、開き直して中身が一致することを確かめた。
  4. `FMRB_HW_TARGET=STICKS3 rake build:esp32` の app (0x10000) と、このイメージ (0x590000) だけを esptool で書いた (bootloader・区画表・nvs は書いていない)。
- 起動ログ (抜粋。SSID は伏せる):

```
boot: Family mruby OS version 3.0.0
kernel: Boot mode: app (/app/tool/welcome.app.rb, 3 retries)
kernel: setup mode: not entered (WiFi settings present, button up)
spx: Single-app boot: /app/tool/welcome.app.rb (retries=3)
fmrb_app: USER_APP pool: idx=4 mempool_id=4 (ordinary, 500 KB)
fmrb_app: [Welcome] VM pool after compile: total=512000 used=307304 free=204304 largest_free=115528 peak=396568 frag=89
Welcome: welcome: sticks3 240x135 v3.0.0
Welcome: welcome: Wi-Fi connecting <SSID>
Welcome: welcome: Wi-Fi connected <IP> fmruby-xxxxxx.local
fmrb_app: Welcome           mrb   316520   195088   512000   396568   116544   75%      -
```

- VM プール (通常の 500 KB): 定常 316-320 KB (62%)、山は 396,568 B (77%、コンパイルの時)。3 回の周期ダンプで増えていない。step_sensor は large_memory (1 MB) で定常 386 KB だったので、プールは半分で済む。
- `IRAM free: 132,740 B` (3 回同じ)。`Guru|abort` 0 件。
- 最初の版は全画面への resize の描き直しで「Wi-Fi connecting」を 2 回ログに出していたので直した (cda0ddff)。直した版は devctl の `/fs/put` で `/app/tool/welcome.app.rb` を置き換え、シリアルを開き直して (= 再起動) 上のログを取った。
- 画面: devctl の `/fb` (rgb565le) を PNG にした `tmp/welcome_app/sticks3_device.png`。記録の絵 (3.1) と同じ配置で、IP と mDNS 名が実際の値で出ている。
- 終わりの状態: StickS3 は welcome を動かしている。シリアルの capture は `/dev/ttyACM0` (reset false) で開いたまま。

## 4. 残件

- 歩く演出のデモに使う StickS3 は、`/etc/system_conf.toml` の `startup_app` を `/app/test/step_sensor.app.rb` に戻す (config を書き換えてビルドするか、storage を作り直す。/etc は devctl の `/fs/put` の対象外)。step_sensor と atoms3r_hello は flash/app/test/ に残してある。
- `rake regress:boot BOARD=sticks3` の基準は step_sensor の起動の時のもの (プールの表の名前が変わる)。基準の更新は親が行う。
- AtomS3R は実機が無く、描画の記録だけで確認した。
