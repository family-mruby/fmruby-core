# フラッシュへの書き込みで画面が青くちらつく件 (P4)

> 状態: 完了 (NARYAv4) | 更新: 2026-10-02 | 原因はアンダーランではなく、4 KB の消去の間 (33-49 ms) キャッシュと割り込みが止まり、DPI の送り出しを 1 フレームごとに再開する割り込みが動けないこと。CONFIG_SPI_FLASH_AUTO_SUSPEND で抜けたフレーム 0、静的な D/IRAM +76 B。ユーザの目視で解消を確認し採用。Tab5 はチップの確認待ち

## 目的

ファイルを保存しても画面がちらつかないようにする (P4: NARYAv4 の HDMI、Tab5 の DSI)。

## 分かっていること (2026-10-01)

- ミュートの切り替え・音量の変更 (設定の保存) と、エディタの保存の両方で、NARYAv4 に青いちらつきが出る (ユーザの目視)。
  音声チップへの I2C は原因ではない (エディタの保存は I2C を使わない)。
- フラッシュに書く間、IDF は CPU のキャッシュを止め、もう片方の CPU も止める
  (`spi_flash_disable_interrupts_caches_and_other_cpu`)。LittleFS の 1 回の保存は消去と書き込みを数回含む。
- 以前の青ちらつき (doc/naryav4/report/p6.md) は DSI の読み出しのアンダーランで、フレームバッファの 4 KB 揃えで解消
  した。そのときはシリアルに `can't fetch data from external memory fast enough, underrun happens` が出たが、
  今回の保存のときには出ていない (割り込みのログ自体が、キャッシュが止まっている間は出せない可能性)。
- P4 は SPI フラッシュの自動中断 (`CONFIG_SOC_SPI_MEM_SUPPORT_AUTO_SUSPEND=y`) に対応している。
  `CONFIG_SPI_FLASH_AUTO_SUSPEND` は今は無効。

## 方針

- 推論で直さない。まず、保存の間に何が止まり、なぜ青くなるか (アンダーランか、ブリッジの同期か、別のものか)
  を計測で確かめる (アンダーランを割り込みの中で数える、保存の時間を測るなど)。
- 根本の対策の第一候補は `CONFIG_SPI_FLASH_AUTO_SUSPEND`。**ユーザの許可 (2026-10-01): 試験用のビルドで試してよい**。
  sdkconfig.defaults の変更を採用するかは、計測の結果をユーザが見て決める (それまでコミットしない)。
- ほかの候補 (書き込みを小分けにする、表示の側で保存の間の取りこぼしに強くする など) は、F1 の結果で比べる。
- 速さ (保存の時間、描画、入力) を悪くしないこと。内蔵 RAM を増やさないこと (自動中断が IRAM を増やすなら量を書く)。

## 段階

| 段階 | 内容 | 状態 |
|---|---|---|
| F1 | 計測で仕組みを確かめる → 自動中断を試す → 前後の比較 (instruction_f1.md) | **完了** (report/f1.md、2026-10-02 検収)。NARYAv4 の sdkconfig.defaults に採用 (ユーザ承認) |

## 受け入れ条件

- エディタの保存・設定の保存で、青いちらつきが出ない (ユーザの目視、NARYAv4。Tab5 はつないだとき)。
- 保存の時間と、描画・入力の速さが悪くならない (数値で前後)。
- 内蔵 RAM の増え方を把握している。

## 採用と残り (2026-10-02)

- `config/sdkconfig.defaults.naryav4` に `CONFIG_SPI_FLASH_AUTO_SUSPEND=y` (ユーザ承認)。
- **フラッシュのチップの制約**: 自動中断は IDF が対応を確かめたチップでしか使えず、対応外だと起動時に止まる
  (`esp_flash_init_default_chip` が失敗)。IDF v5.5.4 の一覧 (spi_flash_chip_*.c の get_caps):
  GigaDevice 0xC84016 / 0xC84017 / 0xC84018 / 0xC84319、Winbond 0xEF4017、XMC の D 系 (ID の上位 0x46、
  または 0x20 で SFDP の印があるもの)、FM (上位 0xA1)。P4-Nano は GD25Q128 (0xC84018)。
  **将来の NARYAv4 の専用基板では、部品表でこのどれかを指定する** (指定できなければ予備の
  `CONFIG_LCD_DSI_ISR_CACHE_SAFE=y` に切り替える。ちらつきは消えるが CPU は止まる)。
- Tab5: 起動ログでフラッシュのチップの ID を確かめ、一覧にあれば同じ設定を p4 の defaults に入れる (未)。
- Retro (S3) は対象外 (表示は WROVER)。WROVER 側の書き込みが NTSC を乱すかは未確認。
