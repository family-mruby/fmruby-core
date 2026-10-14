# CF3 報告: AtomS3R の設定のモード (BLE だけで起動して接続の設定を受け付ける)

> 状態: Step 2 の最初の計測で止めた (普段のモードの内蔵 RAM が +2,016 B、ユーザの判断待ち) | 更新: 2026-10-14 | 10 章が Step 2 の計測。 instruction_cf3.md の Step 1。AtomS3R に BLE を入れる sdkconfig の差分案を 2 つ作り、試しのビルドで静的な内蔵 RAM と flash を測った。Retro と同じ構成 (案 A) は普段のモードで静的な内蔵 RAM が +16,816 B、BLE のコントローラを flash に置く構成 (案 B) は +3,676 B。案 B に、普段のモードの起動で BLE の領域を heap に返す手 (`esp_bt_controller_mem_release`) を足すのを推す。それでも IDF の IRAM のコード約 1.9 KB が残る見込みで、増分 0 になるかは ROM の BLE の領域の大きさ次第 (端末でしか測れない)。設定のモードの空きは約 150 KB の見込み (推定)。sdkconfig・区画の表・ソースは変えていない

## 1. 要約

- **構成**: sdkconfig.defaults.atoms3r の `CONFIG_BT_ENABLED=n` を、Retro (n16r8) と同じ NimBLE の項目に替え、`CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y` を足す (案 B)。coexistence は切ったまま。CMake は変えなくてよい (main/CMakeLists.txt が `CONFIG_BT_ENABLED` を見て ble_task.c と ble_none.c を選ぶ)。system_conf は `ble_auto_start = false`・`wifi_auto_start = true` のまま、設定のモードを許す鍵を 1 つ足す。
- **内蔵 RAM (測った、静的)**: 今 111,039 B → 案 A 127,855 B (+16,816) / 案 B 114,715 B (+3,676)。`idf.py size` の DIRAM。
- **内蔵 RAM (推定、実行時)**: 普段のモードは案 B と領域を返す手で「+約 2.0 KB − ROM の BLE の領域」。設定のモードは WiFi・devctl・アプリを起こさず BLE だけなので、空きは約 150 KB。
- **分かれ道**: 判定は `read_system_config` (fmrb_kernel.c、system_conf を読んだ直後) の 1 か所で、`boot_mode` を新しい値 (設定のモード) にする。無線は boot.c の Retro の無線の節がこの値を見て BLE か WiFi を起こす。画面はカーネルの Ruby が startup_app の代わりに出す。
- **ボタンと表示**: ボタンは G41 (押すと Low、10 kΩ で引き上げ、strap ではない)。system_conf を読む時点 (電源から約 1.5 秒) で読める。表示は画面 (カーネルの止まった画面と同じ仕組み) とシリアルのログ。使える LED は無い。
- **区画**: アプリの区画 5 MB の空きは、今 1,077,984 B (20.6%) → 案 A 873,376 B (16.7%) / 案 B 781,248 B (14.9%)。収まる。

## 2. 測り方

- このブランチ (1d23e17c、中身は develop と同じ) の作業コピーを一時のディレクトリに写し、そこで `rake clean_all` → `FMRB_HW_TARGET=ATOMS3R rake build:esp32` を 3 回 (今の構成・案 A・案 B) 行った。違いは写しの側の `config/sdkconfig.defaults.atoms3r` だけ。`idf.py size` と `idf.py size-components` を取り、写しは消した。作業コピーの sdkconfig・defaults・build/ には触っていない (build/ は Linux の x86-64 のまま)。
- 端末は焼いていない。実行時の数字は、これまでの記録 (headless_s3/report/h1.md・h2.md、ruby_asterism/report/g2a.md 11.7、reference/internal_ram_budget.md の Retro の BLE) からの推定。

## 3. BLE を入れる構成

### 3.1 sdkconfig.defaults.atoms3r の差分案

```
-# No Bluetooth: WiFi is how this board is reached (and the S3 runs one radio
-# at a time anyway). main/drivers/ble/ble_none.c answers the BLE API.
-CONFIG_BT_ENABLED=n
+# BLE for the setup mode only (doc/ruby_asterism/report/cf3.md): the board
+# boots either BLE-only (setup) or WiFi-only (normal), decided at boot.
+CONFIG_BT_ENABLED=y
+CONFIG_BT_NIMBLE_ENABLED=y
+CONFIG_BT_NIMBLE_SVC_GAP_DEVICE_NAME="FamilyMruby"
+CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
+CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
+CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
+CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y
+CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y
+CONFIG_BT_NIMBLE_NVS_PERSIST=y
+CONFIG_BT_NIMBLE_50_FEATURE_SUPPORT=n
+CONFIG_BT_NIMBLE_PINNED_TO_CORE_1=y
+CONFIG_NIMBLE_PINNED_TO_CORE_1=y
+CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512
+# Controller code in flash instead of ROM + IRAM: IRAM shares the internal
+# RAM address space, so the IRAM copy costs about 13 KB in the normal mode,
+# where BLE never starts (案 B)
+CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y
```

- NimBLE の項目は Retro (sdkconfig.defaults.n16r8) と同じ値。Web Bluetooth のコンソールと BLE のファイルの仕組みは Retro で動いている構成なので、そのまま使う。
- `CONFIG_ESP_COEX_SW_COEXIST_ENABLE=n` は今のまま (WiFi と BLE は同じ起動で同時に動かさない)。
- NimBLE の確保 (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE`) は既定の内蔵のまま。設定のモードは内蔵 RAM に余裕がある (5 章) ので、PSRAM に移す必要は無い。NimBLE の .bss は既に `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` で PSRAM にある (libbt の DIRAM の .bss は 0)。
- 案 A は最後の `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y` を入れないもの (Retro と同じ)。

### 3.2 案 B の懸念: flash の書き込み中の BLE

IDF の説明 (`BT_CTRL_RUN_IN_FLASH_ONLY`) は「flash を消している間は BLE が正しく動かないことがある。flash の auto suspend を入れることを勧める」と書く。flash を消す・書く間はキャッシュが止まり、flash に置いたコントローラの割り込みが走れないため。設定のモードは BLE で受けたファイルを littlefs に書く (tar は数 KB〜) ので、まさにこの場面になる。

- Step 2 の最初に確かめる: BLE でつないだまま tar (数十 KB) と wifi.toml を PUT し、切れないこと、書いた中身が合っていること。この PC には Bluetooth が無いので、端末の側のログ (切断の理由) とユーザの Web Bluetooth で見る。
- 切れるときの手: (1) PUT の塊を小さく・間を空ける (コンソールの側)、(2) `CONFIG_SPI_FLASH_AUTO_SUSPEND` (S3 は対応。flash の型番が GD25QxxE 系なら使える。AtomS3R の起動のログは `detected chip: gd` までで、系列は未確認)、(3) 案 A に戻す (普段のモードで +15 KB 前後になるので、ユーザに相談する)。

### 3.3 CMake・system_conf

- CMake: 変えない。main/CMakeLists.txt は `CONFIG_BT_ENABLED` で ble_task.c / ble_none.c を選び、`bt` は常に REQUIRES にある。試しのビルドで ble_task.c が AtomS3R でもそのまま通り、リンクした (案 A・案 B とも)。ble_none.c は使う構成が無くなるが、BLE 無しで作る構成のために残す。
- system_conf_atoms3r.toml: `ble_auto_start = false`・`wifi_auto_start = true` は今のまま (普段のモードの意味)。設定のモードを許す鍵を足す (名前は案。例 `setup_mode = true`、既定 false)。AtomS3R だけが true にするので、ほかの機種の動きは変わらない。注釈の「No Bluetooth in this firmware」は直す。

### 3.4 Retro (NARYAv3) との差

| | Retro (n16r8) | AtomS3R (案 B) |
|---|---|---|
| NimBLE の項目 | 3.1 と同じ | 同じ |
| コントローラのコード | ROM + IRAM (既定) | flash (`RUN_IN_FLASH_ONLY`) |
| coexistence | 切ってある | 切ってある |
| BLE の起こし方 | `ble_auto_start` (既定 true) か、デスクトップのメニュー (BLE Setup) | 起動のときの判定だけ (設定のモード)。メニューは無い |
| 普段のモードの BLE の領域 | 返さない (メニューで後から起こすため) | 起動で返す案 (4.2) |

## 4. 内蔵 RAM の見込み

### 4.1 静的 (測った、`idf.py size` の DIRAM)

| | 今 (BLE 無し) | 案 A | 案 B |
|---|---|---|---|
| DIRAM 計 | 111,039 | 127,855 (+16,816) | 114,715 (+3,676) |
| .text (IRAM に置くコード) | 66,075 | 81,207 (+15,132) | 68,011 (+1,936) |
| .bss | 22,704 | 23,456 (+752) | 23,480 (+776) |
| .data | 22,260 | 23,192 (+932) | 23,224 (+964) |
| fmruby-core.bin | 4,164,896 | 4,369,504 (+204,608) | 4,461,632 (+296,736) |

- 案 A の増分の大半はコントローラ (libbtdm_app) の IRAM のコード 9,744 B と .bss・.data 1,153 B、それに 16 KB の IRAM の区画が埋まって押し出された IDF のコード (xtensa 1,528・bootloader_support 926・spi_flash 751 ほか)。
- 案 B の +3,676 B の内訳 (size-components の差):

| もの | B | 減らせるか |
|---|---|---|
| コントローラ (libbtdm_app_flash) の .bss 688 + .data 504 | 1,192 | 普段のモードで heap に返せる (4.2) |
| BLE が要る IDF の IRAM のコード (esp_system 352・btbb 293・freertos 272・spi_flash 260・newlib 199・esp_hw_support 194・hal 148・esp_timer 109・bt 53) | 1,880 | 返せない (コード)。IDF に手を入れない限り残る |
| NimBLE (libbt) の .data | 217 | `esp_bt_mem_release` なら返せる |
| ble_task.c の .data (GATT の characteristic の表、複合リテラル 0x60 + 0x80) | 224 | `static const` の表にすれば flash へ (Retro・P4 も減る) |
| ble_task.c の .bss (状態の小さな変数) | 25 | PSRAM へ移せる |
| fatfs .bss 38・phy .bss 8・配置の端数 | 約 140 | 原因は未確認 |

### 4.2 普段のモード (推定)

- BLE のコントローラは起こさない。それでも静的な分は払う (案 B で +3,676 B)。
- **この起動では BLE を使わないので、起動で BLE の領域を heap に返す**: IDF の `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` (または `esp_bt_mem_release`) は、コントローラの .bss・.data (1,192 B) と、**ROM の BLE が使う DRAM の領域** (ROM の配置表 `ets_rom_layout_p` の btdm の data・bss) を heap に足す。ROM の領域は BLE の有無にかかわらず heap から外してある (heap の memory_layout_utils.c が ROM の予約の範囲を丸ごと外す) ので、今の BLE 無しのファームでは使われずに眠っている。返すとその起動では BLE が使えなくなるが、AtomS3R は起動ごとにモードが決まるので害が無い。Retro はメニューで後から BLE を起こすので返せない (internal_ram_budget.md 2026-08-02 の節)。
- 見込み: +3,676 − 1,192 (コントローラ) − 217 (NimBLE の .data) − 224 (表を const に) − 25 (PSRAM へ) ≈ **+2.0 KB − ROM の BLE の領域**。ROM の領域の大きさは ROM の中の表にあり、端末で読むしかない (未測)。2 KB 以上あれば増分は 0 以下になる。
- 残る約 1.9 KB は BLE を入れると IRAM に置かれる IDF のコードで、sdkconfig では消せない。ROM の領域で相殺できなければ、増分 0 の条件に当たる (Step 2 の最初に測って、当たれば止まる)。
- 比べ方 (Step 2): 同じコミットで今の sdkconfig と新しい sdkconfig のファームを焼き、`M1|boot_start`〜`spawn:...`・`wifi_ready`・定常の `IRAM free:` を比べる。返した量は起動のログに 1 行出す。

### 4.3 設定のモード (推定)

| 項目 | 値 | 出どころ |
|---|---|---|
| WiFi なしの定常 (試しのアプリと debugd が動いている) | 213,824 B | h1.md 3.2 (測った) |
| アプリを起こさない分 (試しのアプリの spawn の差) | +16,920 B | h1.md 3.2 (241,772 → 224,852) |
| 静的の増分 (案 B) | −3,676 B | 4.1 (測った) |
| BLE (コントローラ + NimBLE + GATT + ble_fs のタスク) | −74,960 B | internal_ram_budget.md (Retro で測った) |
| **設定のモードの空き** | **約 152 KB** | 推定 |

- 普段のモード (WiFi + TLS の Asterism + asterism_demo) の空きは 117,824 B (g2a.md 11.7、測った)。設定のモードは WiFi (約 68.9 KB)・devctl (約 11.6 KB)・アプリを起こさないので、BLE の 75 KB を足しても普段より空く。
- 画面を Ruby のアプリで出すなら、その VM の分 (約 17 KB) が減って約 135 KB。カーネルの画面 (5.2) なら減らない。
- 案 A なら静的の差で約 139 KB。どちらでも足りる。

## 5. 起動の分かれ道

### 5.1 置き場所

今の起動の順 (boot.c `fmrb_os_init`): `init_hardware` (littlefs のマウントまで) → `fmrb_kernel_start` (最初に `read_system_config`、そのあとカーネルの task を spawn) → カーネルの ready を待つ → Retro の無線の節 (`ble_auto_start` なら BLE、そうでなく `wifi_auto_start` なら WiFi と devctl) → debugd。カーネルの Ruby は ready のあと別の task で `initial_sequence` を進め、`boot_mode = "app"` なら startup_app を起こす。

- **判定は `read_system_config` の 1 か所** (fmrb_kernel.c)。system_conf の設定のモードの鍵が true で、かつ (a) `/home/wifi.toml` も `/etc/wifi.toml` も無い、または (b) ボタンが押されている、なら `g_system_config.boot_mode` を新しい値 (例 `FMRB_BOOT_MODE_SETUP`) にする。カーネルの task を spawn する前なので、カーネルの Ruby と無線の節が同じ答えを見る。無線の節で決めると、カーネルの Ruby がそれより先に startup_app を起こす競争になる。
- (a) の判定は wifi_task.c・ble_task.c (CF2 の要約) と同じ順のファイルの有無。3 か所で同じ道を持つので、`fmrb_secret_path.c` の隣に「WiFi の設定のファイルを選ぶ」関数を 1 つにまとめる案 (中身が壊れているときは今どおり普段のモードで WiFi が `disabled` を出す。判定は有無だけ、指示書のとおり)。
- **無線の節** (boot.c): `boot_mode` が設定のモードなら `ble_service_start()` のあと `ble_settings_window_set(...)` を呼び、WiFi と devctl は起こさない。`ble_settings_window_set` は `ble_service_is_started()` (起こしている途中も true) を見るので、`ble_service_start` の直後に呼べる。普段のモードなら今どおり WiFi を起こし、その前に 4.2 の領域を返す。`ble_auto_start`・`wifi_auto_start` の意味は変えない (設定のモードの鍵が false の機種は今と同じ道)。
- **カーネルの Ruby** (fmrb_kernel.rb `initial_sequence`): `_boot_mode` が設定のモードなら、startup_app もサービスの host も起こさず、設定のモードの画面を出す (5.2)。mruby と Spinel のカーネルは同じ rb から作られる。
- P4・Tab5 は無線の節が別 (`modern_radio_init_task`)、Linux・wasm はこの節が無い。鍵が false の Retro も含め、動きは変わらない。

### 5.2 設定のモードの画面

- カーネルの止まった画面 (`fmrb_kernel_show_halt`: canvas 1 枚、6x8 の字、128 px で 21 桁 12 行) と同じ仕組みで、色を変えた画面を出す案。アプリを起こさないので内蔵 RAM を使わない。文は C で組む (BLE の名前は MAC から作るので)。例:

```
SETUP MODE

BLE name:
Family-mruby-aaaaaa

Open the console and
write the settings.
Then press the
button to restart.
```

- 受け付ける時間の残りや、書かれたもの (wifi.toml の有無) を出し直すなら、Ruby のアプリにするか、カーネルの tick で書き直す。最初は止まった文で足りると見る。

### 5.3 受け付ける時間

- CF2 の受け付ける時間は 300 秒。設定のモードは、ボタンを押して入ったか、WiFi の設定が無い (出荷のまま) かのどちらかなので、**起動し直すまで開けたまま**にする案。300 秒で閉じると、書き終える前に閉じたときに起動し直しが要る。
- 開けたままの間、WiFi のファイルは BLE で読める (CF2 8 章の決まり)。tar はいつでも読めない (変わらない)。
- 0x17 の答えの残り秒は、開けたままを表す値 (例 `"until_restart":true`) を足して、コンソールが大きな数を出さないようにする。

## 6. ボタンと表示

### 6.1 ボタン

- AtomS3R の使えるボタンは 1 つ: **G41** (`FMRB_PIN_BUTTON_ENTER`、回路図で 10 kΩ の引き上げ、押すと Low)。もう 1 つは ESP_EN のリセットで GPIO ではない。
- G41 は strap ではない (S3 の strap は 0・3・45・46)。JTAG の MTDI の兼用だが、JTAG は USB-Serial-JTAG に出ているので、ふつうの入力として読める。pin manager の system pin に登録済みで、アプリには渡していない。
- 読む時点: `read_system_config` は起動の最初の方 (app_main は電源から約 1.42 秒、h1.md)。ボタンを押したままリセット (か USB を挿す) して、画面が出るまで押し続ければ読める。読み方は `fmrb_hal_gpio_config` (入力、引き上げ無し) と `fmrb_hal_gpio_get_level` を 10 ms おきに数回 (チャタリング)。Linux は posix の版があるので共有のコードのまま書ける。
- **押したときに Low になることはまだ確かめていない** (h0.md 3.3: 待機時の High だけを確かめた)。Step 2 でユーザに押してもらって確かめる。
- 普段のモードでは、ファームはボタンを読まない (アプリが使う余地を残す。H4 の計画)。

### 6.2 設定のモードであることを示す手段

| 手段 | 使えるか |
|---|---|
| 画面 (128x128) | 使える。5.2 の画面 |
| LED | 使えない。LP5562 の R/G/B は配線が無く (h0.md)、緑の LED は補助の MCU (PMS150G) が点ける。背光の明るさは LP5562 の W で変えられるが、画面があるので要らない |
| シリアルのログ | 使える。`setup mode: no WiFi settings` / `setup mode: button held` と、BLE の名前を 1 行ずつ |
| BLE の名前 | 今どおり `Family-mruby-` と MAC の下 6 桁。コンソールの一覧に出る |

## 7. 起動し直しの手段 (案)

| 手段 | 作り | 向き・不向き |
|---|---|---|
| ボタン | 設定のモードの間だけ G41 を見て、押されたら `esp_restart` (カーネルの tick か小さな timer) | 端末の前にいれば分かりやすい。画面の文に書ける |
| コンソールの指示 | BLE のコマンドを 1 つ足す (例 `0x18`、受け付ける時間の間だけ受ける)、コンソールの Settings に「Restart」 | 書き終えた人はブラウザの前にいる。tool/web と試験の手直しが要る |
| リセットのボタン・電源の入れ直し | 作りは要らない | いつでも使える。ボタンを押したままにしないこと、を画面に書く |

推す案: ボタン (と、いつでも使えるリセット) を作り、コンソールの指示はユーザの判断で足す。

## 8. flash の区画

- アプリの区画 (factory) は 5 MB = 5,242,880 B。

| | fmruby-core.bin | 空き |
|---|---|---|
| 今 | 4,164,896 | 1,077,984 (20.6%) |
| 案 A | 4,369,504 | 873,376 (16.7%) |
| 案 B | 4,461,632 | 781,248 (14.9%) |

- 案 B はコントローラのコードを flash に持つので、案 A より 92 KB 大きい。Step 2 の作り (判定・画面・ボタン) は数 KB の見込み。zenoh・Asterism が育つ余地は約 760 KB 残る。storage の区画は変わらない。
- 区画の表は変えなくてよい。
- 考えて捨てた案: BLE 入りの設定用のファームを別の区画に置き、起動の区画を切り替える。8 MB は factory 5 MB と storage 3008 KB で埋まっていて、もう 1 つの像 (4 MB 前後) を置く場所が無い。

## 9. ユーザに決めてほしい点

1. **sdkconfig の差分 (3.1、案 B)** を入れてよいか。案 B の懸念 (flash の書き込み中の BLE、3.2) は Step 2 の最初に確かめ、だめなら手 (塊・auto suspend・案 A) を相談する。
2. **普段のモードで BLE の領域を返す** (4.2) か。返すとその起動では BLE を起こせない (AtomS3R は起動ごとにモードが決まるので害は無い)。
3. **増分 0 に届かないとき** (IRAM の IDF のコード約 1.9 KB が ROM の領域で相殺できないとき) は、Step 2 の最初の計測で止まって相談する、でよいか。
4. **起動し直しの手段** (7 章): ボタンだけか、コンソールの指示も足すか。
5. **受け付ける時間** (5.3): 設定のモードの間は起動し直すまで開けたままか、300 秒か。
6. **画面** (5.2): カーネルの止まった画面と同じ仕組み (止まった文) でよいか。残り時間などを出し直すなら Ruby のアプリか tick。
7. **system_conf の鍵** (3.3): 名前 (例 `setup_mode = true`) と、AtomS3R だけで有効にすること。
8. 設定のモードでは startup_app (今は asterism_demo など) を起こさない、でよいか (WiFi が無いので繋がらず、内蔵 RAM も使う)。
9. WiFi の設定はあるが繋がらない (パスワードの誤りなど) ときに、自動で設定のモードへ戻る仕組みは作らない (ボタンで入る)、でよいか。
10. 開発のビルドは `config/wifi.toml` から `/etc/wifi.toml` を焼き込むので、WiFi の設定が無い状態にはならない (ボタンで入る)。WiFi の設定を消す確認 (受け入れ条件 1) では `/etc/wifi.toml` も消す必要があるが、devctl のファイルの操作は `/home`・`/app` などに限られ `/etc` に届かない。ボタンで設定のモードに入って BLE のコンソールで消す (受け付ける時間の間は消せる) か、devctl の範囲に `/etc` を足す (開発のビルドだけ) か、を Step 2 で決める。確認のあとはバックアップから戻す。

## 10. Step 2: 最初の計測 (ここで止めた)

ユーザの決定 (2026-10-14、9 章の 10 点はすべて推し案) を受けて Step 2 に入り、案 B の sdkconfig と、普段のモードで BLE の領域を heap に返す手を入れて計測した。**普段のモードの内蔵 RAM が develop より 2,016 B 少ない (増分 0 に届かない)** ので、決定 3 のとおりここで止めた。BLE の書き込みの確認 (決定 1) は、判断の材料になるので止める前に端末の側でできる範囲を行った (10.4)。

### 10.1 作ったもの (コミット済み、未検収)

ブランチ feature/cf3-atoms3r-setup (develop をマージ済み)。止まった状態でも続きから入れるように、Step 2 の作りは一通り入れてコミットした。AtomS3R の ESP32 のビルドと host の試験 (`rake secret:test` 122 項目、`node tool/web/test/settings.mjs`) は通る。ほかの構成のビルド・sim・wasm はまだ回していない。

| 場所 | 中身 |
|---|---|
| `config/sdkconfig.defaults.atoms3r` | 案 B の差分 (3.1) と、冒頭の注釈の「no Bluetooth」の直し |
| `config/system_conf_atoms3r.toml` | `setup_mode = true`。ble_auto_start の注釈を直した |
| `main/kernel/fmrb_kernel.c/.h` | `read_system_config` で設定のモードを決める (`setup_mode` が true で、WiFi の設定のファイルが無いか、G41 が 10 ms おき 5 回とも Low)。`boot_mode` を `FMRB_BOOT_MODE_SETUP` (2) にする。`setup_mode` の既定は AtomS3R だけ true (/etc が古い端末でも効くように)。設定のモードの画面 `fmrb_kernel_show_setup` (止まった画面と同じ仕組み、紺の地) |
| `main/kernel/fmrb_secret_path.c/.h` | 判定の純粋な関数 `fmrb_setup_mode_reason` と、WiFi の設定のファイルの道の定義 (wifi_task・ble_task・カーネルが同じものを使う) |
| `main/prebuild_scripts/kernel/fmrb_kernel.rb` ほか | 設定のモードなら startup_app もサービスも起こさず `_show_setup`。mruby (lib/add の kernel.c) と Spinel (ffi・base) の両方 |
| `main/boot/boot.c` | 普段のモードは kernel の開始の直後に `ble_release_memory()`。設定のモードは `ble_setup_mode_start(G41)` だけで WiFi と devctl は起こさない |
| `main/drivers/ble/ble_task.c/.h`・`ble_none.c` | 受け付ける時間を起動し直すまで開ける。`0x17` に `"setup":true,"until_restart":true`。`0x18` (起動し直し、設定のモードのときだけ受ける)。ボタンを放してから押すと起動し直し。`esp_bt_mem_release`。characteristic の表を const の配列に (Retro・P4 の .data も減る) |
| `tool/web/` | Settings に「Reboot」(設定のモードのときだけ出る)、設定のモードの表示。版 `2026-10-14c`。試験に 9 項目。README |

### 10.2 静的な内蔵 RAM (`idf.py size`、同じコミットで比べた)

develop (sdkconfig は BLE 無し) と、このブランチを同じ作業コピーで `rake clean_all` から作り直して比べた。違いは 10.1 の変更だけ。

| | develop | このブランチ | 差 |
|---|---|---|---|
| DIRAM 計 | 111,039 | 114,491 | +3,452 |
| .text (IRAM に置くコード) | 66,075 | 68,011 | +1,936 |
| .bss | 22,704 | 23,480 | +776 |
| .data | 22,260 | 23,000 | +740 |
| fmruby-core.bin | 4,164,896 | 4,466,000 | +301,104 (区画の空き 776,880 B、14.8%) |

- Step 1 の案 B (+3,676) より 224 B 少ない。characteristic の表を const にした分。
- ブートローダは中身が同じ (違いは作った時刻の文字列とその後ろの検査の値だけ、37 B)。区画の表は同じ。アプリの区画だけの書き込みで足りる。

### 10.3 端末での内蔵 RAM (AtomS3R、普段のモード)

develop とこのブランチを同じ端末にアプリの区画だけ書き、同じ /home・同じ startup_app (試しのアプリ) で起動した。

| M1 / 周期ダンプ | develop | このブランチ | 差 |
|---|---|---|---|
| `boot_start` | 330,124 | 326,820 | -3,304 |
| `spawn:fmrb_kernel` | 241,988 | 238,692 | -3,296 |
| `ble_released` (BLE の領域を返した直後) | - | 239,876 | +1,272 を返した |
| `spawn:` 試しのアプリ | 225,068 | 223,040 | -2,028 |
| `devctl` | 144,380 | 142,364 | -2,016 |
| `debugd` | 136,912 | 134,896 | -2,016 |
| 定常の `IRAM free` | 135,736 | 133,720 | -2,016 |
| asterism_demo (VPS に TLS で接続中) の `IRAM free` | 117,800 | 115,784 | -2,016 |

- **返せたのは 1,272 B だけ**。コントローラの .bss・.data (1,192 B) とわずかな端数で、Step 1 で当てにした「ROM の BLE の領域」はほとんど無かった (多くて 80 B)。そのため IDF の IRAM のコード (約 1.9 KB) と .bss・.data の残りが相殺されず、**普段のモードで 2,016 B 減る**。
- asterism_demo は今どおり VPS に TLS で繋がった (1,258 ms。develop は 1,301 ms)。
- PSRAM は起動の時点で 36 KB 減る (NimBLE の .bss は PSRAM に置かれている)。

### 10.4 設定のモードと、BLE を起こしたままの flash の書き込み (端末の側)

一時のビルド (WiFi の設定を「無い」とみなす 1 行と、試しの書き込みを足したもの。コミットしていない) を焼いて確かめた。

- `kernel: setup mode: no WiFi settings` → カーネルは `Setup mode: no app started` で startup_app を起こさず画面を出し、BLE が起き (`BLE device name: Family-mruby-aaaaaa` の形、画面の名前と一致)、`settings window open until restart (setup mode)`。WiFi・devctl は起きない。ボタンは `button released; press it to restart` まで進んだ (押す確認はユーザ)。
- 設定のモードの内蔵 RAM: `ble_ready` 153,452 B、定常の `IRAM free` **159,860 B** (Step 1 の見込み約 152 KB より多い。debugd は動いたまま)。
- **BLE を起こしたまま littlefs に書く**: BLE のファイルのタスクの中で (PUT と同じタスク)、`/home` に 256 KB を 2 KB ずつ書いて読み戻し、消すことを 5 秒おきに 3 回。3 回とも読み戻しが一致し、書き込みは 2.2〜2.6 秒 (1 塊の最悪 194 / 34 / 44 ms)。書いたあとも `synced=1 enabled=1 adv_active=1`、NimBLE の host のリセットやコントローラの assert、再起動は無い。
- **限界**: この PC に Bluetooth が無いので、つないだまま (connection event がある状態) で書く場面は確かめていない。広告だけの状態で壊れないことまで。つないだまま切れないかは Web Bluetooth で tar を書く確認 (ユーザ) が要る。

### 10.5 判断してほしいこと

増分は 2,016 B (定常、普段のモード)。残りは BLE を入れると IRAM に置かれる IDF のコード (Step 1 の 4.1 の表: esp_system・btbb・freertos・spi_flash・newlib など約 1.9 KB) と .bss・.data の端数で、sdkconfig のこの差分の中では消せない。手は次のどれか。

1. **+2 KB を受け入れる** (設定のモードの利点と引き換え)。
2. **ほかの場所で同じだけ減らして相殺する**: このブランチの外の内蔵 RAM の削減 (internal_ram_budget.md の候補から 2 KB 以上) を先に入れる。
3. **BLE を入れたまま IRAM のコードを減らす sdkconfig を探す** (どの項目がどの関数を IRAM に引き込んでいるかを調べてから案を出す。sdkconfig の追加の承認が要る)。
4. 案 A は +16.8 KB なので悪くなる。BLE を入れない (CF3 をやめる) と 0。

### 10.6 端末と作業コピーの今

- AtomS3R: develop のファーム (アプリの区画だけ) に戻した。/home はそのまま (config.toml・tar・ほかのファイルの名前と大きさが同じ。試しのファイルは消した)。asterism_demo を起動し、VPS に TLS で繋がっている。
- 作業コピーは develop、build/ は Linux の標準構成 (x86-64)。
