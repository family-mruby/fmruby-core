# P3 実装指示: NARYAv4 無線・入力・音声・周辺の差し替え

> 状態: 計画済 | 更新: 2026-08-30 | 別セッション実装用のフェーズ指示。表示済みの NARYAv4 を「使える機械」にする (WiFi/リモート/入力/音/SD)

## 最初に読むもの

- doc/naryav4/plan.md の P3 節と未確定事項。
- doc/naryav4/report/p2.md — **「触っていないもの (想定内の失敗ログ)」節が
  P3 の作業対象一覧そのもの**。I2C 所有の変更 (display_p4_i2c が
  i2c_master / I2C_NUM_0 で実装し直された) も必読。
- doc/naryav4/report/p0.md の「P4-Nano の実ピン」節 (一次情報)。
- doc/reference/tab5_i2c_bus_notes.md (バス直列化の設計ルール)。

## 現在地 (P2 終了時点の実機ログから)

- **C6 は既に SDIO で上がっている** (`Base transport is set-up`,
  `Slave chip Id[12]`)。P1 のピンと esp_hosted 1.4.0 の適合は実証済み。
  WiFi の接続確認 (資格情報〜IP 取得) から先が未検証。
- 想定内で残っている失敗ログ: `es8388 codec create failed` /
  `tab5_kbd: I2C0 bus init failed` / `sdmmc_card_init failed` (カード無し)。
- USB HID はコード共通・未検証。RTC (fmrb_rtc.c の RX8130 直読み) は
  NARYAv4 にチップが無いので必ず失敗する。

## ゴールと受け入れ条件

1. **WiFi**: config/wifi.toml の資格情報で接続し IP を取る。timesync
   サービスで時刻が入る (RTC 無しの代替。plan の方針どおり)。
2. **リモート検証経路**: MCP の tab5_screenshot / tab5_input / tab5_app /
   tab5_fs が NARYAv4 に対して動く (remote desktop は system_conf で
   有効済み)。**これが通った時点から、以降の検証は自律でできる**。
3. **USB 入力**: USB-A のキーボードとマウスでデスクトップを操作できる
   (最終判定はユーザ)。
4. **音声**: APU の音がスピーカ端子から鳴り、マイク入力にレベルが入る
   (音質の官能評価はユーザ)。
5. **SD**: カードを挿せばマウントされる (ユーザにカード挿入を依頼)。
6. **ログ衛生**: NARYAv4 のブートに Tab5 固有装置の失敗ログ (es8388 /
   tab5_kbd / タッチ / RX8130) が**残らない**。crash マーカー 0。
7. **退行ゼロ**: TAB5 ビルド成功 + sim スモーク (エディタ 1 打鍵まで) +
   rake test。音声・キーボードは Tab5 実機でも壊れていないことが望ましい
   (実機がユーザ手元にあるときのみ依頼)。

## 作業ステップ (推奨順 — 2 を最初に通すと以降が自律化できる)

### step 1: Tab5 固有ドライバの除去 (ビルド分岐)

- main/CMakeLists.txt の P4 用ソース一覧にボード分岐を入れ、NARYAv4 から
  `touch_task.c` / `tab5_keyboard.c` を外す。boot.c の起動呼び出しも
  同じ条件で囲う。判定は fmrb_hw_defines.cmake が出す configure 時の
  ボード情報に合わせる (P1/P2 の流儀を踏襲)。
- **tab5_kbd が I2C_NUM_0 を取りに来る問題は「NC ピンで先に失敗する」
  という順序頼みの安全で生きているだけ** (report/p2.md)。ここで根絶する。
- タッチは Tab5 でヘッドホン検出も担っている (doc/reference/
  tab5_i2c_bus_notes.md)。NARYAv4 に HP 検出は無いので消してよいが、
  **Tab5 側の経路を巻き込まないこと**。

### step 2: WiFi 接続と remote desktop (最優先で通す)

- config/wifi.toml が置かれていることを確認 (git 外。無ければユーザに
  依頼)。ビルド → flash → シリアルで接続と IP 取得を観測。
- boot.c の modern_radio_init_task に Tab5 前提 (PI4IO / display 待ち) の
  残りが無いか確認する。P2 実機では C6 が上がっているので、大改造では
  なく「NARYAv4 で不要な待ちを外す」程度の見込み。
- timesync で時刻が入ることを確認 (services.toml は Modern 共通)。
- **罠: MCP の tab5_ip は mDNS 名で引く**。Tab5 実機が同じネットワークに
  いると**同名の別ボードへ解決し得る**。NARYAv4 のリモート検証中は
  Tab5 の電源を切る/WiFi を切るようユーザに依頼し、tab5_ip の結果が
  NARYAv4 の IP か (/status の中身などで) 確認してから使うこと。
- 通ったら tab5_screenshot で NARYAv4 の画面が取れるはず。**以降の
  検証はこの経路を主に使う** (シリアルはログ専用に)。

### step 3: USB キーボード/マウス

- コードは S3/P4 共通 (usb_task)。NARYAv4 でホスト給電 (VBUS スイッチ) が
  効いて列挙されるかの確認が主。tab5_input (注入) と物理キーボードの
  両方で操作できること。
- 動かない場合のみ調査 (usb_task の root port 電源投入フローと
  P4-Nano の VBUS 経路。report/p0.md の回路メモ参照)。

### step 4: RTC の後始末

- fmrb_rtc.c は Modern=RX8130 前提で I2C を読みに行く。NARYAv4 には
  無いので、**静かに諦めて timesync に譲る**形にする (ボード分岐か、
  1 回の失敗で以後読まない)。デスクトップの時計設定 UI
  (clock_setting.rb) 側の表示も破綻しないか一度見る。
- 失敗ログを毎回出さないこと (受け入れ条件 6)。

### step 5: 音声 (ES8311 + NS4150B)

一番の新規実装。Tab5 の audio_p4_hw.c (ES8388+ES7210、PI4IO アンプ、
I2S_NUM_1) をボード分岐し、NARYAv4 実装を足す:

- コーデックは **ES8311 1 チップ** (I2C 0x18、esp_codec_dev に公式
  ドライバあり)。録音 (マイク MIC1) も同じチップ。
- I2C は **display が所有する I2C_NUM_0 バスを借りる** (P2 で
  display_p4_i2c_* が i2c_master 化済み。直列化の作法は Tab5 と同じ:
  自前でバスを作らない)。
- I2S ピンは pin_assign の値を消費する (P1 で先置き済み:
  MCLK=13 / SCLK=12 / LRCK=10 / DOUT=9 / DIN=11)。ポートは Tab5 と同じ
  I2S_NUM_1 で全二重。
- アンプ EN は **GPIO53 を直に High** (`FMRB_PIN_AUDIO_AMP_EN`)。
  ポップ音対策でコーデック初期化後に上げる。
- サンプルレートは APU の 47160Hz を踏襲 (Tab5 の ES8388 で実績)。
  鳴らして音高がおかしければ MCLK 比の設定を疑う (確認は音階アプリを
  ユーザに聴いてもらうのが早い)。
- 受け: 起動音/アプリの音が出る + マイク入力のレベルが読める
  (mic 系アプリか、レベルのログ 1 行で可)。

### step 6: SD とログ最終確認

- ユーザにカードを挿してもらいマウント確認 (ピンは Tab5 と同一なので
  疎通確認のみの想定)。
- NARYAv4 のブートログを最初から最後まで読み、受け入れ条件 6 を満たす
  ことを確認。残った警告は report に列挙して P4 (総合検証) に送る。

## 罠と約束 (引き継ぎ)

- ポートは /dev/ttyACM1 直指定。`rake check-port` 禁止 (Tab5 リセット)。
  flash 前に .serial_port を確認。ターゲット切替は `rake clean_all`、
  終わったら .env を TAB5 に戻す。
- 実機の表示は 1280x720 実 80MHz / 64.6Hz が確定構成 (a943ef59)。
  表示回りは触らない。**要求クロックが実際に出ているか疑う教訓**は
  I2S/MCLK にも適用する (音高がずれたらまずクロック実測)。
- I2C_NUM_0 は display の所有。audio も RTC もヘルパ経由で借りる。
  自前で i2c_new_master_bus しない (GPIO31/32 の教訓とは別に、二重
  取得で必ず壊れる)。
- MCP ツールの排他: シリアル capture を開いたまま flash しない (サーバが
  管理する分は自動退避されるが、手動 cat とは競合する)。
- コミットは求められたときだけ。コメントは英語。Tab5 の挙動を変えない
  (すべてボード分岐の内側で)。

## 書き残し (フェーズ完了時)

- doc/naryav4/report/p3.md (経過・実測値・撤回した仮説・残課題)。
- plan.md 状態行更新、未確定事項の消し込み (C6 適合・ES8311 実ピン・
  47160Hz 動作・マイクゲインが対象)。
- `rake docs:index`。
- P4 (総合検証と基板反映) へ引き継ぐ事項を report 末尾にまとめる。
