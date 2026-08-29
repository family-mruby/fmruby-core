# P1 実装指示: NARYAv4 ビルドターゲットの実体化

> 状態: 計画済 | 更新: 2026-08-29 | 別セッション実装用のフェーズ指示。ビルドが通ることまでが受け (実機表示は P2)

## 最初に読むもの

- doc/naryav4/plan.md (全体計画。P1 の位置づけと方針)
- doc/naryav4/report/p0.md (**ピン・rev・採用解像度の一次情報はここ**)
- doc/reference/support_esp32p4.md の「HW ターゲットの分離」節
- fmruby-core/CLAUDE.md (コミットは求められたときだけ、コメントは英語、等)

## ゴールと受け入れ条件

NARYAv4 ターゲットを「Tab5 のプレースホルダ」から実体にする。
**このフェーズはビルドレベルまで**。実機で画面が出るのは P2 の仕事
(P1 のバイナリは display ドライバが Tab5 のままなので、P4-Nano で
ブートしても表示初期化は失敗してよい)。

受け入れ条件:

1. `.env` を `FMRB_HW_TARGET=NARYAv4` にして `rake clean_all` →
   `rake build:esp32` が成功し、**"building with the Tab5 pin assignment
   as a placeholder" の CMake 警告が出ない**。
2. `.env` を `FMRB_HW_TARGET=TAB5` に戻して `rake clean_all` →
   `rake build:esp32` が従来どおり成功する (退行ゼロ)。
3. `rake test` (host テスト) が通る。

## 環境の注意 (先に読む)

- **同一チェックアウトを Tab5 / wasm の作業と取り合う**。着手前に他の
  作業が走っていないことを確認し、ターゲット切替は必ず `rake clean_all`
  を挟む (config/sdkconfig.defaults.* を編集した時も同様)。
- ターゲット指定は `.env` の `FMRB_HW_TARGET` を編集する (シェル環境変数
  との優先関係に過去の混乱があるため、.env 編集に統一する)。終わったら
  TAB5 に戻す。
- P4-Nano のシリアルは `rake attach` で取り込める (CH343 対応済み、
  /dev/ttyACM1 に cdc_acm で生える)。**`rake check-port` は使わない**
  (全ポートをプローブするため、attach 中の Tab5 をリセットする)。
- sdkconfig.defaults 系の編集は config/ 配下の新ファイル追加として行う
  (プロジェクト直下の sdkconfig / sdkconfig.defaults は触らない)。

## 作業項目

### 1. config/sdkconfig.defaults.naryav4 (新規)

`config/sdkconfig.defaults.p4` をコピーして以下だけ変える。

- **chip revision** (P4-Nano は v3.1 実測。Tab5 v1.0 とは相互排他):

```
# 削除する (Tab5 用):
CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y
CONFIG_ESP32P4_REV_MIN_1=y
# 代わりに置く:
CONFIG_ESP32P4_REV_MIN_301=y
```

  (シンボル名は tmp/naryav4_hw/display/sdkconfig (v3.1 実機で動作確認済みの
  生成物) から採ったもの。疑わしければそちらを grep して確認する)

- **esp_hosted の SDIO ピン** (P4-Nano 回路図実測。report/p0.md 参照):

```
CONFIG_ESP_HOSTED_SDIO_PIN_CMD=19
CONFIG_ESP_HOSTED_SDIO_PIN_CLK=18
CONFIG_ESP_HOSTED_SDIO_PIN_D0=14
CONFIG_ESP_HOSTED_SDIO_PRIV_PIN_D1_4BIT_BUS=15
CONFIG_ESP_HOSTED_SDIO_PIN_D2=16
CONFIG_ESP_HOSTED_SDIO_PIN_D3=17
CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54
```

  (RESET_ACTIVE_LOW=y は p4 と同じまま。54 は C6 の EN)

- それ以外 (PSRAM HEX 200MHz / flash 16MB / USB hub 等) は p4 と同値の
  まま変えない。

### 2. config/system_conf_naryav4.toml (新規)

`config/system_conf_p4.toml` をコピーし、`display_mode = "naryav4_hdmi"`
に変える。解像度 426x240・margin 0 などは据え置き。

display_mode の新値はカーネル側にも足す:

- main/kernel/fmrb_kernel.h の display mode 列挙に
  `FMRB_DISPLAY_MODE_NARYAV4_HDMI` を追加。
- main/kernel/fmrb_kernel.c の `parse_display_mode()` (65 行付近) に
  `"naryav4_hdmi"` の分岐を追加。
- 実際のドライバ選択は CMake のソースリスト分岐で行われており、この値は
  ほぼ情報表示用。P1 では追加だけして消費側は触らない。

### 3. ビルド配線 (2 箇所を必ずセットで)

- rakelib/build.rake の `hw_config` テーブル (87-100 行付近):
  NARYAv4 の行を Tab5 共用から
  `config/sdkconfig.defaults.naryav4` / `config/system_conf_naryav4.toml`
  に向ける。パーティションは `config/partitions_p4.csv` を共用のまま。
- cmake/check_storage_inputs.cmake (30-38 行付近): 上のテーブルの
  ミラーになっているので、同じ対応を追加する。**片方だけ直すと storage
  検査がビルドを止める**。

### 4. components/fmrb_common/include/fmrb_pin_assign.h

現在 `#if defined(FMRB_HW_TAB5) || defined(FMRB_HW_NARYAV4)` (15 行付近)
で共用している分岐を分割し、NARYAv4 専用の `#elif defined(FMRB_HW_NARYAV4)`
ブロックを新設する (冒頭コメントもこの分割を指示している。作業後は
そのコメントを現状に合わせて書き換える)。

**Tab5 ブロックが定義している全マクロを同名で定義する** (欠けると
pin manager がコンパイルエラーになる。ATOM 分岐が実際にこの壊れ方を
している)。値は report/p0.md の「P4-Nano の実ピン」節が一次情報:

- I2C (display/codec 共有バス): SDA=7, SCL=8
- SD (SDMMC): CLK=43, CMD=44, D0=39, D1=40, D2=41, D3=42 (Tab5 と同一)
- audio I2S: MCLK=13, SCLK=12, LRCK=10, DOUT(P4→DAC)=9, DIN(ADC→P4)=11、
  アンプ EN=53 (NS4150B。Tab5 の PI4IO 経由と違い直 GPIO)
- C6 EN=54 (SDIO データ線は sdkconfig 側なので pin_assign には EN のみ)
- BOOT ストラップ/ボタン = GPIO35
- Tab5 固有 (タッチ INT/バックライト、内蔵キーボード I2C、PI4IO、
  Grove) は NC にする。Grove の 53/54 は NARYAv4 では audio EN と
  C6 EN に使われている点に注意。
- Ethernet PHY が GPIO28-31, 34, 35, 49-52 を基板上で占有している。
  ソフトからは使わないが、コメントで予約として明記する。
- USB は HS OTG PHY 専用パッド (GPIO ではない)、デバッグ UART0=37/38。
  Tab5 ブロックの RESTRICTED 系マクロの流儀に合わせて埋める。

### 5. components/fmrb_common/fmrb_hw_defines.cmake

NARYAv4 の `message(WARNING ... placeholder ...)` を削除する。

## 検証手順

1. `.env` を NARYAv4 にし `rake clean_all` → `rake build:esp32`。
   成功 + 警告なしを確認。
2. (任意の追加インテリジェンス。ゲートではない) できたバイナリを
   P4-Nano に焼いてシリアルでブートを観測する。ポートは /dev/ttyACM1 を
   **直指定** (rake check-port 禁止。.serial_port キャッシュが Tab5 を
   指していないか確認)。display 初期化 (Tab5 の PI4IO プローブ) がどう
   失敗するか・どこまでブートするかを report に記録すると P2 が楽になる。
   ブートしなくても P1 の失敗ではない。
3. `.env` を TAB5 に戻し `rake clean_all` → `rake build:esp32` で退行確認。
4. `rake test`。

## 書き残し (フェーズ完了時)

- doc/naryav4/report/p1.md に経過と気づき (踏んだ罠・実測値) を書く。
- plan.md の状態行を更新し、未確定事項から確定分を消す。
- `rake docs:index` を実行する。
- コミットは求められたときだけ。行う場合の件名例:
  `core: add NARYAv4 build target (P4-Nano pins, rev v3.1 sdkconfig)`

## スコープ外 (やらない)

- display_p4 / audio_p4 のボード分岐実装 (P2/P3。ローカルの Tab5 ピン
  定数はまだ触らない)
- boot.c の電源順序変更 (P3)
- 800x600 HDMI の実表示 (P2。採用モードと実績値は report/p0.md の
  「結論」「環境メモ」)
