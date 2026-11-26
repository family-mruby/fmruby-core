# Flash の割り付け (機種ごと)

> 更新: 2026-11-26 | P4-Nano・Tab5・S3 の機種の Flash の区画と、ファーム・storage・フォントの使用量。
> 区画の表の正は `config/partitions_*.csv`。経緯 (storage を減らしフォントを専用の区画へ) は doc/flash_layout/plan.md。

## 1. 前提 (2026-11-26 ユーザ決定)

- **漢字の辞書は SD カードに置く前提で考える**。storage (内蔵の Flash) の見積もりに辞書は数えない。
  辞書の既定の置き場は `/sd/dict` (`components/fmrb_kanji/fmrb_kanji.h` の `FMRB_KANJI_DEFAULT_DIR`)。
- 区画の大きさは焼き直しでしか変わらず、変えると storage (/home) が消える。変えるときは /home を退避する
  (`rake flash` の keep_home、`ruby tools/fmrb_rd_fs.rb <IP> backup`)。

## 2. 区画の表

| 機種 | Flash | 表のファイル | 区画 (開始・大きさ) |
|---|---|---|---|
| P4-Nano (NARYA v4)・Tab5 | 16 MB | `partitions_p4_common.csv` | 下の 2.1 |
| NARYA v3 (S3) | 16 MB | `partitions_naryav3.csv` | 下の 2.2 |
| StickS3・AtomS3R (S3) | 8 MB | `partitions_atoms3r.csv` (StickS3 も同じ) | 下の 2.3 |

どの機種も先頭 0x0〜0x9000 はブートローダと区画の表 (ESP32-P4 のブートローダは 0x2000 から)。

### 2.1 P4-Nano・Tab5 (16 MB、すき間なし)

| 区画 | 開始 | 大きさ | 中身 |
|---|---|---|---|
| nvs | 0x9000 | 24 KB | 設定 (WiFi の遠隔操作の ON/OFF・登録した PC のトークンほか) |
| phy_init | 0xF000 | 4 KB | 無線の較正 |
| factory (ファーム) | 0x10000 | 0x7F0000 (7.94 MB) | ファーム |
| storage (littlefs) | 0x800000 | 0x6B0000 (6.69 MB) | /app・/usr・/help・/etc・/home ほか |
| font_ja | 0xEB0000 | 0x150000 (1.31 MB) | 日本語のフォント (doc/flash_layout F4) |

### 2.2 NARYA v3 (16 MB、後ろの約 4.9 MB が空き)

| 区画 | 開始 | 大きさ | 中身 |
|---|---|---|---|
| nvs | 0x9000 | 24 KB | |
| phy_init | 0xF000 | 4 KB | |
| factory | 0x10000 | 6 MB | ファーム (日本語のフォントもファームの中) |
| storage | 0x610000 | 5 MB | |
| (使っていない) | 0xB10000 | 約 4.94 MB | どの区画にも割り付けていない |

### 2.3 StickS3・AtomS3R (8 MB、すき間なし)

| 区画 | 開始 | 大きさ | 中身 |
|---|---|---|---|
| nvs | 0x9000 | 24 KB | |
| phy_init | 0xF000 | 4 KB | |
| factory | 0x10000 | 0x580000 (5.5 MB) | ファーム |
| storage | 0x590000 | 2,496 KB | |

## 3. 使っている量 (2026-11-26、develop。リモートシェルまで、UART の前)

### 3.1 ファーム (factory の区画)

`rake regress:size ALL=1` の記録 (`tmp/regress/size/<board>.json` の `app_bin` / `app_partition`)。

| 機種 | ファーム | 区画 | 空き |
|---|---|---|---|
| P4-Nano | 6,960,672 B | 8,323,072 B | 1,362,400 B (16%) |
| Tab5 | 6,988,880 B | 8,323,072 B | 1,334,192 B (16%) |
| NARYA v3 | 6,033,712 B | 6,291,456 B | 257,744 B (**4%**) |
| StickS3 | 5,246,128 B | 5,767,168 B | 521,040 B (9%) |
| AtomS3R | 5,231,360 B | 5,767,168 B | 535,808 B (9%) |

### 3.2 storage (クリーンインストールの直後、辞書なし)

P4-Nano は、ビルドが作った像 (`build/storage.bin`) の使っているブロックを数えた実測。ほかは、storage に入るファイル
(`config/storage_exclude_*.txt` を除いた `flash/` と、生成した `/help`・`/cache`) から、littlefs のブロック (4 KB、
512 B 以下は目録の中) とフォルダの目録 (1 つ 2 ブロック) を数え、P4-Nano の実測で 1.10 倍に合わせた見積もり。

| 機種 | 区画 | 使う量 | 空き | ファイル数 |
|---|---|---|---|---|
| P4-Nano | 6,848 KB | 5,172 KB (実測) | 約 1.6 MB | 748 |
| Tab5 | 6,848 KB | 約 5,170 KB (見積もり) | 約 1.6 MB | 739 |
| NARYA v3 | 5,120 KB | 約 4,650 KB (見積もり) | 約 0.45 MB | 703 |
| StickS3・AtomS3R | 2,496 KB | 約 2,030 KB (見積もり) | 約 0.45 MB | 245 |

- 大きい物 (P4-Nano): F1 の長文ヘルプ `/help` (268 ファイル、中身 836 KB、ブロックで約 1.5 MB)、同梱のアプリ `/app`、
  本体の部品 `/usr/share` (転送のアプリ約 0.2 MB を含む)。小さいファイルが多いほど、4 KB のブロックとフォルダの目録の
  分だけ中身より大きくなる。StickS3・AtomS3R はヘルプ・ゲーム・デモを入れない (`storage_exclude_atoms3r.txt`)。
- 実機は、ここに利用者のファイル (/home) と、app_sync が消さない古い本体のファイル・実行時のキャッシュが足される。
  2026-11-26 の P4-Nano は、辞書 (/home に 1 MB) を含めて空き 8 KB だった。

### 3.3 日本語のフォント (P4 の font_ja の区画)

像 (`build/font_ja.bin`) は 1,083,882 B、区画は 1,376,256 B (空き約 0.28 MB)。

## 4. 決まっている直し (doc/release_3_1)

- F1 のヘルプを 1 つのファイルにまとめる: storage が約 0.7 MB 空く (P4・NARYA v3。小さい端末はヘルプを入れていない)。
- 転送のアプリを本体に焼き込む: storage が約 0.2 MB 空き、ファームが増える。NARYA v3 のファームの空き (4%) で足りるかを測る。
- app_sync が、像に無い本体のファイルを一覧に出し、`PRUNE=1` のときだけ消す。

## 5. 知っておくこと

- NARYA v3 は Flash の後ろ約 4.94 MB を使っていない。ファームか storage が足りなくなったときの余地になる
  (区画の表を変えると焼き直しで /home が消える)。
- storage の見積もりは、像を作らずに数えた物。区画を変える判断の前には、その機種の像を作って実測する
  (`build/storage.bin` の、全部が 0xFF でないブロックを数える)。
