# S3 の続き: NARYAv3 の Asterism と、StickS3・AtomS3R の区画表

> 状態: 1 は実装済・検収待ち (実機は未確認)、2 は案 (ユーザが選ぶ) | 更新: 2026-11-07 | NARYAv3 の既定を `FMRB_ASTERISM=1` にした。DIRAM は 125,699 → 126,395 (+696、全部 mbedTLS の PSA の状態)、ファーム +258,976 B。+696 B は linker の fragment の条件を 1 つ変えれば PSRAM へ移せ、そのときは Asterism なしより −240 B になる (測った、入れていない)。StickS3・AtomS3R の区画表は factory 5.5M・storage 2496K を推奨 (app の空き 4% → 13%、/home に使える量 約 1.29 MB → 約 0.78 MB)。推奨案で組み立てと storage のイメージが通ることを確認した

前の段は `report/f6_s3.md`、決定は `plan.md` の「S3 の決定 (2026-11-07 …)」。

## 1. NARYAv3 は既定で Asterism を入れる

### 1.1 変えたもの (2e13ff74)

| 所 | 中身 |
|---|---|
| `Rakefile` | `BOARD_BUILD_DEFAULTS` に `"NARYAv3" => { "FMRB_ASTERISM" => "1" }`。ほかの端末の既定は変えていない。板を名指さない S3 の構成と ATOM_DISPLAY は 0 のまま |
| `tools/regress/targets.rb` | NARYAv3 の `build` に `FMRB_ASTERISM=1` (`regress:size` が Rakefile の既定と同じかを確認する) |
| `.env`・`lib/add/family_mruby_esp32.rb`・`components/picoruby-esp32/CMakeLists.txt` | 説明の文言だけ |

基準の更新は別のコミット (82f24d3c、NARYAv3 の項だけ)。

### 1.2 大きさ

`rake regress:size ONLY=naryav3,sticks3,atoms3r` (`idf.py size-components` と同じ esp_idf_size)。前は f6_s3.md の 3 章 (同じ木で `FMRB_ASTERISM=0`)。

| 端末 | DIRAM | IRAM | ほかの内蔵 RAM | ファーム | app の領域の空き |
|---|---|---|---|---|---|
| NARYAv3 | 125,699 → 126,395 (**+696**) | 16,384 (+0) | 104 (+0) | 4,954,896 → 5,213,872 (+258,976) | 1,077,584 (17%、区画 6M) |
| StickS3 | 114,527 (+0) | 16,384 (+0) | 104 (+0) | 5,023,408 (+0) | 219,472 (4%) |
| AtomS3R | 114,163 (+0) | 16,384 (+0) | 104 (+0) | 5,008,640 (+0) | 234,240 (4%) |

- 内蔵 RAM で増えたのは DIRAM の 696 B だけ。部品ごとでは `libmbedcrypto.a` が +692 (`psa_crypto.c` の `.bss` 508 B と `psa_crypto_slot_management.c` の 184 B) で、残りの 4 B は詰め物。zenoh-pico と Asterism の gem 自身の静的な領域は内蔵 RAM に無い (f6_s3.md の 6 章で「zenoh-pico と Asterism の gem の分」と書いたのは誤りで、Zenoh の TLS の経路が mbedTLS の PSA の核を連れてくる分)。
- 基準の名前は 3,403 → 4,163 (f6_s3.md の 4 章の「NARYAv3 + Asterism」の行)。VM の基準の常駐は StickS3 と同じ程度 (32 bit の見積もりで約 71〜74 KB、VM を開くときに約 90 KB) になる見込み。

### 1.3 +696 B は PSRAM へ移せる (測った、入れていない)

AtomS3R・StickS3 では同じ 2 つの `.bss` が既に PSRAM にある。`main/mbedtls_psram.lf` (PSA の核と `ssl_ciphersuites.c` の `.bss` を PSRAM へ) を、`main/CMakeLists.txt` が「TLS 1.3 があり、かつ `.bss` を PSRAM に置ける」ときだけ足しているため。NARYAv3 の sdkconfig (`sdkconfig.defaults.n16r8`) は TLS 1.3 を入れていないので、PSA の核は linker で入るのに fragment が付かない。

条件を `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` だけにして NARYAv3 を測った (作業の木で一時的に。コミットしていない):

| NARYAv3 | DIRAM |
|---|---|
| Asterism なし (前) | 125,699 |
| Asterism あり (この段) | 126,395 (+696) |
| Asterism あり + fragment の条件を変える | **125,459 (−240)** |

−240 は `ssl_ciphersuites.c` (Asterism と関係なく SSLSocket の HTTPS で前から内蔵 RAM にあった) も PSRAM へ移る分。fragment の理由 (割り込みからは触らない、ほかの mbedTLS の状態は既に PSRAM) は NARYAv3 でもそのまま当てはまる。ただし NARYAv3 は `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` が無く、mbedTLS が実行時に取るもの (record の buffer など) は内蔵の heap からのまま (1.4 の 4)。入れるかは親・ユーザが決める。

### 1.4 BLE・起動への影響 (コードを読んだ)

- **起動時の C の処理は増えない**。Asterism の C の部分は zenoh-pico と ESP-IDF の port (`components/picoruby-esp32/CMakeLists.txt`) だけで、起動の経路 (`boot.c`) に Asterism の分岐は無い。BLE と WiFi の選び方 (`ble_auto_start` を先に、両方なら BLE だけ) は変わらない。NARYAv3 の既定 (`system_conf_n16r8.toml`) は `ble_auto_start = true`・`wifi_auto_start = false` なので、普段の起動は今までどおり BLE (debugd) で、WiFi も zenoh も動かない。`setup_mode` も無いので BLE の領域を heap に返す処理も動かない。
- **mruby の VM を作るたびに gem 3 つ分の初期化が増える** (`picoruby-asterism-zenoh`・`picoruby-asterism`・`picoruby-fmrb-asterism`。最後のものは VM の始めに `Asterism.profile` を呼ぶ)。クラスとメソッドの表はアプリのプール (PSRAM) に乗り、内蔵 RAM ではない。デスクトップ・エディタ・各アプリの起動の時間と、プールの使用量が少し増える。カーネルは Spinel なので変わらない。
- **転送のアプリ**: カーネルは起動時に `/home/asterism/receive.toml` があるときだけ転送のアプリを起こす。無い端末 (焼いたばかりの NARYAv3) は起動が何も変わらない。ある端末で BLE の起動をすると WiFi が無いので、転送のアプリは WiFi を待ち続け (`FmrbAsterism::Link` の `:waiting_wifi`、3 秒ごと)、VM を 1 つ持ったままになる。
- **TLS**: `sdkconfig.defaults.n16r8` は TLS 1.3 も `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` も無い (AtomS3R・StickS3 にはある、g2a.md)。zenohd 1.10.1 はクライアントの証明書を求めると TLS 1.3 しか受けないので、**NARYAv3 は tls/ のルータ (クラウド、相互 TLS) につながらない見込み**。家の中の tcp/ のルータは使える。つないだとしても mbedTLS の record の buffer (1 接続あたり約 21 KB) が内蔵の heap から取られる。sdkconfig を変えるのはユーザの承認が要るので、この段では変えていない。

### 1.5 NARYAv3 を移す相手にする前提

- WiFi の側で起動する: `/etc/system_conf.toml` で `ble_auto_start = false`・`wifi_auto_start = true`、`/home/wifi.toml` に ssid・password。BLE とは同時に動かない (S3 は無線が 1 つ)。開発の遠隔操作 (`FMRB_DEV_REMOTE_CTL`) の HTTP も WiFi の側でだけ起きる。
- ルータの場所 (tcp/ の家のルータ。1.4 の TLS)。
- 受け取るなら `/home/asterism/receive.toml` (`from = [...]`)。無ければ何も受け取らない (F5 の決まり)。

### 1.6 実機で確認すべき点

1. `rake regress:boot BOARD=narya_v3`: crash の印 0 件、BLE の起動 (既定) で debugd がつながる。`M1|` の内蔵 RAM と `IRAM free:` が前のファームより約 700 B 少ないだけ (1.3 を入れたら約 240 B 多い)。
2. `M1|spawn:` の行とアプリのプールの使用量、デスクトップとエディタの起動の時間を、Asterism なしのファームと比べる (gem 3 つの初期化の分)。
3. WiFi の側の起動 (1.5) で、WiFi・lwIP・zenoh のセッションがつながった状態の内蔵 heap の空き。StickS3 の H3 では zenoh の分は 1 KB 未満だった。
4. movable のアプリを開いたときの `VM set up for checkpoints` の行 (4,163 名前。見積もりは常駐約 71〜74 KB、作る間の一時的な量約 19 KB)。
5. `rake regress:checkpoint BOARD=narya_v3` の移す場面 (相手は StickS3 か P4-Nano、tcp/ のルータ)。
6. `receive.toml` を置いたまま BLE で起動したときに、待ち続ける転送のアプリが他を邪魔しないか (1.4)。
7. tls/ のルータへはつながらない見込み (1.4)。確かめるなら TLS 1.3 を入れたファームで。

## 2. StickS3・AtomS3R の区画表

### 2.1 storage の中身

ビルドと同じ手順 (`flash/` を写し、`flash_local/` を重ね、Retro の除外と `config/storage_exclude_<板>.txt` を外す) で木を作り、イメージを作る道具と同じ littlefs-python 0.13.3 (block 4 KB、name_max 64) で使う block を数えた。StickS3 と AtomS3R の除外の一覧は同じ中身なので、木もほぼ同じ (`system_conf` の差だけ)。

| | ファイル | 中身の合計 | littlefs で使う量 |
|---|---:|---:|---:|
| git の木だけ | 301 (ディレクトリ 51) | 753,316 B | 430 block = 1,761,280 B |
| `flash_local/` (共有の checkout の、`home/music` の NSF 11 個 344,848 B) を重ねる | 312 | 1,098,164 B | 528 block = 2,162,688 B |

- ファイルは小さくても 1 block (4 KB) を取り、ディレクトリは 2 block 取るので、中身の 2.3 倍になる。
- 大きいもの (block): `app/test` 177 (127 ファイル、そのうち `ckpt_*` が 20)、`home/music` 94 (`flash_local/` のときだけ)、`usr/share/asterism` 93 (ROS 2 の型の定義 `msgs`)、`app/tool` 24、`usr/share/sprites` 21、`usr/share/basic` 10。
- `flash_local/` は利用者ごとの持ち物で配る木には入らない。配る木の数は git の木だけの行。

### 2.2 案

8 MB の flash は今の表で末尾まで使っているので、factory を広げた分だけ storage が減る。factory は 0x10000 から、storage は factory のすぐ後から flash の末尾 (0x800000) まで。app の空きはこの段のファーム (StickS3 5,023,408 B、AtomS3R 5,008,640 B)、/home に使える量は storage の空き (littlefs の空き block。git の木だけ / `flash_local/` を重ねたとき)。

| 案 | factory | storage | app の空き StickS3 / AtomS3R | /home に使える量 (git の木) | 同 (`flash_local/` あり) |
|---|---|---|---|---|---|
| 今 | 5M (0x500000) | 0x510000、3008K | 219,472 (4%) / 234,240 (4%) | 1,318,912 | 917,504 |
| A | 5.25M (0x540000) | 0x550000、2752K | 481,616 (9%) / 496,384 (9%) | 1,056,768 | 655,360 |
| **B (推奨)** | **5.5M (0x580000)** | **0x590000、2496K** | **743,760 (13%) / 758,528 (13%)** | **794,624** | **393,216** |
| C | 6M (0x600000) | 0x610000、1984K | 1,268,048 (20%) / 1,282,816 (20%) | 270,336 | **入らない** (528 block > 496) |

参考に、16 MB の板:

| 端末 | 区画 | app の空き | storage と /home に使える量 | 使っていない flash |
|---|---|---|---|---|
| NARYAv3 (`partitions_n16r8.csv`) | factory 6M、storage 5M | 1,077,584 (17%、Asterism を入れた 1 の後) | 使う量 882 block、空き 1,630,208 (`flash_local/` ありで 1,228,800) | 0xB10000 から末尾まで 5,177,344 B |
| P4 (`partitions_p4.csv`、参考) | factory 0x7F0000、storage 8M | 13〜14% (f6_s3.md の 10 章) | — | 0 |

NARYAv3 は区画表を変えなくてよい。広げるときも末尾に 4.9 MB 余りがある。

### 2.3 既存の端末を新しい表にする手順 (どの案も同じ)

1. `/home` を退避する (WiFi の側で起動して devctl の `/fs/get`、`ruby tools/fmrb_rd_fs.rb <IP>` の `pull`、MCP の `tab5_fs`)。中身は `/home/wifi.toml`、`/home/asterism/` (端末の証明書と `receive.toml`)、利用者のファイル。
2. フル flash (`rake flash`: bootloader・区画表・app・storage)。`/home` が消える。nvs は同じ場所のままなので消えない。**`rake flash:app` を古い表の端末に使わない** (5M を超えたイメージが古い storage の頭に重なる)。
3. 起動すると `/home/wifi.toml` が無いので設定のモード (BLE だけ、CF3) に入る。BLE で WiFi の設定を書いて起動し直す。
4. WiFi の側で上がったら、退避した `/home` を `/fs/put` で戻す。

### 2.4 推奨: B (factory 5.5M・storage 2496K)

- app の空きが 13% (約 740 KB) になり、P4 (13〜14%) と同じ程度。次に入る大きなもの (f6_s3.md の表の rodata は約 300 KB、Asterism は約 259 KB) を 2 つ分ほど受けられる。A (9%、約 480 KB) は 1 つ分で、また区画表を変える (フル flash と /home の退避) ことになりやすい。
- storage は配る木で 794 KB、利用者が NSF などを `flash_local/` で入れても 393 KB 残る。C は `flash_local/` の木が入らず、配る木でも 264 KB しか残らない。
- `/home` に置くもの (WiFi の設定、証明書、`receive.toml`、移って来たアプリが書くファイル) は数 KB〜数十 KB で、ヘッドレスの板では 394〜795 KB で足りる見込み。
- storage を増やしたくなったときは、`app/test` のうち `ckpt_*` 以外を除外の一覧に足すと約 600 KB 空く (板の上で試験のアプリを動かすことが無ければ。これは別に決める)。

### 2.5 推奨案で組み立てを確認した

作業の木で `config/partitions_atoms3r.csv` を B に変え (factory `0x580000`、storage `0x590000`・`2496K`)、共有の checkout の `flash_local/` を一時的に写して、`FMRB_HW_TARGET=STICKS3 rake clean_all build:esp32` (gem は mruby-checkpoint の S3 の版)。

- 通った: `fmruby-core.bin binary size 0x4ca6b0 bytes. Smallest app partition is 0x580000 bytes. 0xb5950 bytes (13%) free.`
- storage のイメージ: `littlefs-python create ... --fs-size=0x270000` が通り、`build/storage.bin` 2,555,904 B。中身の使う量 528 block (2,162,688 B)、空き 393,216 B (2.2 の表の B の `flash_local/` ありの値と同じ)。
- flash の書き込みの行は `0x10000 build/fmruby-core.bin 0x590000 build/storage.bin` になった。
- 確認の後、区画表を元に戻し、写した `flash_local/` を消した。AtomS3R はファームが StickS3 より小さく、storage の木も同じなので、同じ結果になる (組み立てていない)。

## 3. 確認

| 何を | 結果 |
|---|---|
| `rake regress` | 通る (ホスト 21 suites、描画の記録 205) |
| `rake regress:size ONLY=naryav3,sticks3,atoms3r` | 前の基準 (6f7b5848) に対して NARYAv3 の DIRAM +360 で落ちる (意図した変化。f6_s3.md の後の値 125,699 からは +696)。StickS3・AtomS3R は前と同じ。NARYAv3 の基準を更新した後は通る |
| 1.3 の fragment の条件 | NARYAv3 だけ測った (DIRAM 125,459)。元に戻した |
| 2.5 の区画表 | StickS3 で組み立てとイメージが通った。元に戻した |
| 実機 | 行っていない |

## 4. サブの仮決め (ユーザが見直す)

1. 板を名指さない S3 の構成と ATOM_DISPLAY は `FMRB_ASTERISM=0` のまま (1.1)。
2. +696 B を PSRAM へ移す fragment の条件の変更は入れていない (1.3)。入れるなら `main/CMakeLists.txt` の 1 行で、NARYAv3 の DIRAM は 125,459。
3. NARYAv3 の sdkconfig (TLS 1.3、mbedTLS の PSRAM) は変えていない (1.4)。
4. 区画表は変えていない。推奨は B (2.4)。
