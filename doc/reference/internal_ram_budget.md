# 内蔵 RAM 削減計画

Spinel 化とは独立した、コアファームウェア全体の課題。
**内蔵 RAM (MALLOC_CAP_INTERNAL) が枯渇しており、同時に動かせるアプリ数と
機能追加の両方を縛っている**。PSRAM は 3 MB 以上余っているので、
不足しているのは総量ではなく内蔵 RAM に置く必然性のない配置と、
使われないまま確保されている余剰である。

Spinel 側の残課題は `doc/spinel_aot/phase7.md` を参照。
例外スタック 84,608 B の回収 (T7-1) と S3 のメモリ余裕測定 (T7-6) は
本計画と重なるので、実施はどちらか一方で行い他方は参照にとどめる。

## 現状 (2026-10-01)

全体の経過は「実施済み」と「残り」の表、個々の数字と根拠は「計測記録」に書く。
施策を足したら、この 3 か所をそろえて直す。

静的な D/IRAM (map の `.dram0.*` と IRAM の合計、`esp_idf_size`) は、3 機種とも
develop の同じコミットを `rake clean_all` からビルドして測った値。

| | S3 (NARYAv3) | TAB5 | NARYAv4 |
|---|---:|---:|---:|
| 静的な D/IRAM (R1 の前、2026-09-28) | 146,995 | 183,008 | 178,410 |
| 静的な D/IRAM (今、RMT を IRAM から外した後) | **126,251** | **130,186** | **125,524** |
| 差 | -20,744 | -52,822 | -52,886 |

待機時の内蔵 RAM の空き (`IRAM free:`、ユーザアプリ 0 個) は、機種と構成ごとに
測った最新の値。**測った日とコミットが違うので、横に並べて比べない**。

| 機種 | 待機時の空き | 測った時点 |
|---|---:|---|
| S3 (BLE を起動) | 142,708 | 2026-08-02 (メッセージキューの PSRAM 化の後)。R1-R3 の分 (静的 -18 KB) は未計測 |
| S3 (BLE を遅延起動、未起動) | 216,956 | 2026-08-02 |
| Tab5 | 152,612 | 2026-08-15 (Spinel の TU ごとの表を PSRAM へ移した後)。R1-R3 の分は未計測 |
| NARYAv4 (P4-Nano) | 203,520 | 2026-10-01 (R3 と RMT の後) |

最初の記録 (2026-07-31) では S3 の待機時の空きは 61,328 B、Tab5 は 76,108 B で、
Tab5 はユーザアプリ 2 つで 31,316 B まで落ちていた。

## 実施済み

| 日付 | 軸 | 施策 | 効果 | 記録 |
|---|---|---|---|---|
| 2026-08-02 | - | ブートステップごとの計装 (M1 の行) | 誰が食っているかの表 (BLE 75 KB、host task 58 KB) | 計測記録 M-1 |
| 2026-08-02 | E | Spinel の例外 / catch スタックの深さ 64 → 16 (T7-1) | S3 待機時 +8,452 | 計測記録 |
| 2026-08-02 | E | `g_fs_ctx` と debugd の `linebuf` を PSRAM へ | S3 待機時 +6,432 | 計測記録 |
| 2026-08-02 | A | スタックの適正化 (host 32→24 KB、hw_proxy 8→6 KB、hid_host 4→5 KB) | 約 +9.2 KB | 計測記録 M-2、`fmrb_task_config.h` |
| 2026-08-02 | E | メッセージキューの格納域を PSRAM へ | S3 待機時 +36.6 KB | 計測記録 |
| 2026-08-02 | D | BLE の遅延起動 (`ble_auto_start`、S3 のみ) | 未起動のあいだ +74.5 KB | 計測記録 |
| 2026-08-15 | E | Spinel の生成 TU ごとの表 (例外 / catch スタックほか) を `SP_TU_BSS` で PSRAM へ | Tab5 待機時 +56.7 KB | spinel_aot/per_tu_psram_plan.md、spinel_aot/report/per_tu_internal_ram.md |
| 2026-09-07 | - | 起動可否の判定を連続ブロックはスタックだけで数える、余裕分を設定化 | 断片化した状態でもエディタが起動できる | 計測記録 |
| 2026-09-26 | E | Spinel の上流取り込みで増える +54 KB を 0 以下に (凍結リテラルと表を flash、nil で始まる定数を PSRAM) | 静的 S3 -6,736 / P4 -6,812 (取り込み前より減) | spinel_upstream_ext/report/p2c.md |
| 2026-09-28 | E | R1: 描画・tmpfs・editor・MIDI などの表を PSRAM、mrblib の irep 表を const | 静的 S3 -16,384 / P4 約 -39,590、P4-Nano 待機時 +39,564 | 計測記録、iram_reduction/report/r1.md |
| 2026-09-29 | E | R2: P4 のファイル書き込みの跳ね返しを外し、devctl の `s_fs_buf` を PSRAM | 静的 P4 -8,192、P4-Nano 待機時 +8,172 | 計測記録、iram_reduction/report/r2.md |
| 2026-09-29 | E | R3: host / ble / 表示 / 音声の小物と、Spinel の生成 C の定数 313 個を PSRAM | 静的 S3 -1,832 / P4 約 -2,350 | 計測記録、iram_reduction/report/r3.md |
| 2026-10-01 | sdkconfig | RMT の割り込みと符号化の関数を IRAM から外す (3 機種の defaults) | 静的 S3 -2,528 / P4 -2,726、P4-Nano 待機時 +4,492 | 計測記録 |

取り下げたもの: Spinel の GC のマークスタック 32 KB の PSRAM 化 (もともと PSRAM から
取られていた。2026-07-31 の節の訂正を参照)、BLE ファイルサービスのスタック 8→6 KB
(転送の実測でピークが 5.3 KB)、prebuilt_gems の const 化 (220 B に対して直す場所が 2 つ)。

## 残り

| 候補 | 見込み | 条件・次の一手 | 扱い |
|---|---:|---|---|
| sdkconfig: ringbuf (タスク用・割り込み用)、FreeRTOS、heap、S3 の SPI の割り込みを flash へ | P4 約 -21 KB / S3 約 -25 KB | 得られる量に対して速さと動作の危険が大きい。S3 は WROVER との UART 通信 (1 フレームごとに ACK を待つ) が ringbuf と FreeRTOS を毎回通り、その速さが重要 | やらない (2026-10-01 ユーザ決定、P4 も含めて) |
| S3 のファイル書き込みの跳ね返しを外す (R2 と同じ変更) | S3 -8,192 | Retro の実機で、書き込み数 MB 以内の最小の試験 | R4 |
| P4 の lwIP の .bss を PSRAM へ | P4 約 -4 KB | sdkconfig では動かない。自前の linker fragment で scheme を足す | R4 候補 |
| PPA の `scaled` / `block` | P4 5,120 | LovyanGFX の pushImage が DMA を使うかを確かめる | R4 |
| I2S の `s_stereo_buf` / `s_mic_buf` | P4 4,192 | i2s_channel_read / write が写すことと、音の途切れ | R4 |
| apu_emu の `apu` と表 | P4 約 1,000 | 1 サンプルごとに触るので速さを測る | R4 |
| prism の `pm_binding_powers` | 1,980 | libmruby は ldgen の対象外。objcopy で節の名前を変える後処理 (新しい仕組み) | 後回し |
| link_local のメッセージバッファ (実行時の確保) | P4 約 10 KB / チャネル | 描画の命令が毎回通る経路。速さへの影響を避けて移さない (2026-09-28 ユーザ決定) | やらない |
| C: status_led のタスクを畳む、debugd と ble_fs の統合 | スタック 1 本 (4-8 KB) | 未着手 | 未定 |
| D: USB Host・debugd の遅延起動 | 10.9 KB / 13.6 KB (S3) | 「挿されたら起動する」検出手段が要る | 未定 |
| B: タスクスタックを PSRAM へ | 最大 (16-24 KB x タスク数) | 過去に撤退。安全境界と再挑戦の手順は軸 B | 長期 |

## アプリ起動の可否判定 (計算方式)

アプリを起動してよいかは、起動前に `app_internal_ram_available()`
(`main/app/fmrb_app.c`) が内蔵 RAM を見て決める。**問いは 2 つあり、
必要量が違う。同じ数字で両方を判定してはいけない**。

| 検査 | 見る値 | 必要量 | 拒否時のログ |
|---|---|---|---|
| 連続ブロック | `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` | タスクスタックだけ | `refused: largest internal block ...` |
| 総量 | `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` | スタック + 付帯 + 余裕分 | `refused: internal free ...` |

```
largest >= stack                          # 連続ブロック
free    >= stack + overhead + margin      # 総量
```

- `stack` = `attr->stack_words`。名前は words だが、ESP-IDF では
  `StackType_t` が 1 バイトなので**実体はバイト**である
  (本家 FreeRTOS の Linux ポートでは語。この検査自体が Linux ビルドでは
  無効なので実害はないが、値を他所で使うときは単位を確認する)。
  既定はユーザアプリ 16 KB、エディタ 24 KB、`task_stack_kb` で最大 64 KB。
- `overhead` = `FMRB_APP_SPAWN_OVERHEAD` (8 KB)。TCB・二値セマフォ・
  メッセージキューの分で、Tab5 実測 2-6 KB に丸めた値。
- `margin` = `system_conf.toml` の `app_spawn_margin_kb` (既定 30 KB)。
  0 で無効、200 KB でクランプ。

### 連続ブロックにスタックだけを数える理由

**1 回の確保で連続領域を要求するのはタスクスタックだけ**である。
`fmrb_task_create_ex` はスタックを 1 ブロックとして `xTaskCreate` に渡すが、
TCB・セマフォ・メッセージキューはそれぞれ別の小さな確保になる。したがって
最大空きブロックが持てばよいのはスタック分だけで、ここに付帯分を足すと
**起動できる状況を拒否する**。

実例 (Tab5, 2026-09-07): アプリを数本起動した後の最大ブロックは
31,744 B。エディタのスタック 24,576 B は収まるのに、スタック + 8 KB =
32,768 B で判定していたため 1 KB 足りず拒否された。総量は 70 KB 以上
空いており、`app_spawn_margin_kb` をいくつにしても (0 にしても) 変わらない。
利用者からは「メモリはあるのにエディタだけ開かない」と見える。

### 余裕分 (margin) の意味と限界

余裕分は**アプリを起動した後も機械が動き続けるための床**であり、保証では
ない。WiFi と遠隔操作だけでアプリ稼働中の内蔵 RAM は数十 KB 動く。

**大きくしすぎると全アプリが起動できなくなる**。エディタもシェルも起動
できないので、端末上で設定を書き戻す手段が無くなり、復旧はファームウェアの
再書き込みになる
(Tab5 で 200 を設定して実測。空き約 127 KB に対し必要 229 KB となり、
何一つ起動しなかった)。上げるときは小刻みに。

### ログの読み分け

- `refused: largest internal block N < M (task stack, one block)`
  → 断片化。総量ではなく連続領域が足りない。余裕分を下げても解決しない。
  他のアプリを終了して大きな穴を作り直すか、そのアプリのスタックを見直す。
- `refused: internal free N < M (need X + margin Y)`
  → 総量不足。余裕分の設定が効くのはこちら。
- `internal RAM ok: free=.. largest=.. need=.. (stack ..)`
  → 通過。need は総量側の必要量、括弧内が連続で要る分。

## 原則: 計測してから決める

過去に推論でサイズを決めて失敗している。同じ轍を踏まないこと。

- host task を 16 KB のまま運用していたとき、GFX 洪水で残り約 1 KB になり、
  スタックオーバーフローが NimBLE の BSS (`ble_hs_state_ctx`) を破壊して
  「Host not enabled. Dropping the packet!」として現れた。
  症状はスタックの話に一切見えない。
- mruby desktop は 12 KB のうち残り 1,020 B で動いていた。
- Spinel の例外スタックを「タスクスタックが 24 KB だからネストは数段」と
  推論して 32 に下げ、GC ルート管理配列を破壊して偽 OOM を起こした。

`configCHECK_FOR_STACK_OVERFLOW = 2` は有効なので、境界を越えれば検知される。
だが**上記 3 件はいずれも「越える前に隣を壊す」型ではなく、越えた結果が
別の症状として現れる**ものだった。マージンは必ず残す。

## 計測手順

### M-1: 内蔵 RAM を誰が食っているかの表を作る (最初にやる)

`boot.c` の初期化シーケンスの各ステップ前後で
`heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` を出し、
差分を表にする。対象は少なくとも:

LittleFS マウント / USB Host 初期化 / BLE (BT コントローラ + NimBLE) 初期化 /
HAL (UART link) / host task 生成 / graphics 初期化 / kernel task 生成 /
system_desktop 生成 / debugd 生成。

これが無いまま個別の削減に着手すると、効かないところを削ることになる。
**削減の優先順位はこの表が決める**。

### M-2: タスクスタックの最悪値を採る

`fmrb_app_dump_vm_pools()` と同じ周期ダンプに出ている Free 列が
`uxTaskGetStackHighWaterMark` 由来なので計装は既にある。
アイドルではなく、以下を一通り通した後の値を読む。

- desktop の config ダイアログ開閉、launcher のスクロールとアプリ起動
- ユーザアプリを 2〜3 個起動して終了
- ウィンドウのドラッグとリサイズ、フルスクリーン切り替え
- BLE ファイル転送 (ble_fs)、デバッガ接続 (debugd)
- USB の抜き差し (usb_host_lib, hid_host)
- GFX 洪水 (fmrb_host の最悪ケース)

`-fstack-usage` による静的なフレームサイズ確認も併用する
(`fmrb_task_config.h` の既存コメントはこの方法で得た値を根拠にしている)。

### M-3: 静的確保 (.bss/.data) の内訳

map ファイルから内蔵 RAM に載っているシンボルを大きい順に並べる。
Phase 5 では `fmrb_spx_app_config` の返信バッファ 41,745 B が
これで見つかり、`EXT_RAM_BSS_ATTR` で PSRAM へ退避して回収した。

**実施済み (2026-08-15)**: `doc/spinel_aot/report/per_tu_internal_ram.md`。
**生成 Spinel TU 1 本につき約 11.4KB**、5 本で 60,496 B = 内蔵 .bss/.data の
**37%**。85% は例外/catch ハンドラスタック (`SP_EXC_STACK_MAX` = 16)。
カーネルの実測 `ExcHW` は 3/0 なので **8 段に下げれば約 24KB 回収できる**見込み
だが、ライブラリ呼び出しの gem 側 ExcHW が未計測なので、そこを採ってから。
計測手順の落とし穴 (`.ext_ram.*` を除外する / `.sdata`・`.sbss` も拾う) も同報告に記載。

## 削減の軸

### A: タスクスタックの適正化

M-2 の実測 + マージンで `components/fmrb_common/include/fmrb_task_config.h` を
更新する。**このヘッダが唯一の定義箇所**で、各サイズには既に決定根拠が
コメントされている。変更する場合は根拠も同時に更新すること。

見込みの大きい順:

- `FMRB_HOST_TASK_STACK_SIZE` 32 KB: 現状ピーク 15.2 KB。ただし 16 KB は
  過去に失敗した値なので、GFX 洪水下の最悪値を採ってから判断する。
- `FMRB_HW_PROXY_TASK_STACK_SIZE` 8 KB / `FMRB_BLE_FS_TASK_STACK_SIZE` 8 KB:
  いずれもピーク 2 KB 前後。ただし両者とも flash DMA 経路を持つので、
  LittleFS の内部フレームが乗る最悪経路を通してから決める。
- `FMRB_DEBUGD_TASK_STACK_SIZE` 6 KB: アタッチ中の経路が未計測 (既知)。
- `FMRB_STATUS_LED_TASK_STACK_SIZE` 4 KB: ピーク 2.1 KB。C の軸と併せて検討。
- `FMRB_USB_HID_TASK_STACK_SIZE` 4 KB: **削減対象ではなく増量候補**。

### B: スタックの配置 (回収量は最大だが、過去に失敗している)

回収量だけ見れば最大である。kernel 16 KB + system_desktop 24 KB +
ユーザアプリ 16 KB x N が内蔵 RAM から消える。
**同時起動アプリ数の上限を外す本命の手段**であり、長期的には再挑戦したい。
だが**この道は一度試して撤退している**ので、順序としては最後に置く。
「いつかやる」ことと「今すぐ戻す」ことは別である。

#### 経緯

`fmrb_task_config.h` には `FMRB_TASK_FLAG_PSRAM` があり、PSRAM スタックの
タスクが flash DMA に触れないよう `hw_proxy` がファイル I/O を代行する
仕組みまで作った。しかし **KERNEL / SYSTEM_APP / SHELL_APP / USER_APP は
PSRAM スタックを禁止して内蔵 RAM に戻した** (2026-05-09)。
理由は、ファイルアクセスだけでなく**メッセージ通信など、少しでも DMA が
関わりそうな処理でクラッシュした**ため。ヘッダのコメントには
`_bt_bss_start` 付近の BSS ガード破壊を切り分けるための「TEMPORARY」と
書かれているが、実態としてはこの一連の不安定さで撤退している。

**この撤退は ESP32-S3 環境での経験である**。P4 は世代が違い、
メモリサブシステムと DMA の構成も異なるので、同じ結論になるとは限らない。
再挑戦するなら P4 から試す方が見込みがある。

#### なぜそうなるか (機序)

**タスクスタックを PSRAM に置くことは、そのタスクのすべてのスタックローカル
変数が PSRAM 上に載るということ**である。ファイル I/O を hw_proxy へ
逃がしても、それ以外の経路が残る。ESP-IDF 側の制約は次のとおり。

1. **フラッシュ操作中はキャッシュが無効化され、PSRAM も同時にアクセス
   不能になる**。読み書きすると illegal cache access 例外になる。
   IDF はこれを検出するために、フラッシュ操作の入口
   (`spi_flash_disable_interrupts_caches_and_other_cpu`) で
   **現在のスタックポインタが DRAM 内にあることを assert している**
   (`esp_task_stack_is_sane_cache_disabled`)。PSRAM スタックのタスクから
   NVS や LittleFS を直接/間接に呼ぶと、ここで確実に落ちる。
   間接呼び出しも同じなので、ログ出力やコンフィグ読み込みが内部で
   ファイルに触れるだけで再現する。
2. **DMA に渡すバッファがスタックローカルだと、それは PSRAM バッファになる**。
   IDF のドキュメントは「スタックが PSRAM にあり得る場合、DRAM バッファを
   スタックに置くことは推奨されない」と明記している。多くの周辺 DMA
   (SPI, UART, SDMMC 等) は送受信バッファが DRAM かつワード整列であることを
   要求し、**DMA ディスクリプタは PSRAM に置けない**。
   これが「メッセージ通信でクラッシュした」の正体である可能性が高い。
   転送関数に一時バッファをスタックで渡している箇所が 1 つでもあれば踏む。
3. **ROM 内のコードを直接/間接に呼ぶタスクでは使えない**、というのが
   この機能の元々の但し書きである。何が ROM を呼ぶかを網羅的に
   監査するのは現実的でない。
4. `xTaskCreate` は PSRAM スタックを割り当てない。静的生成 +
   専用の Kconfig (IDF 5.x の `CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY`、
   IDF 6 では `CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM` に改称) か、
   `xTaskCreateWithCaps` が要る。**IDF 自身がこれを既定で禁止し、
   ドキュメントで「推奨しない」と書いている**という事実は重い。

#### S3 と P4 の差

撤退したのは S3 である。S3 の GDMA は整列とキャッシュ同期の条件付きで
PSRAM にアクセスできる (DMA が PSRAM に一切触れない ESP32 無印とは違う)
ので、当時のクラッシュは「DMA が PSRAM を読めない」ではなく、
**1. のキャッシュ無効、あるいは 2. の整列・同期・ディスクリプタ配置の
条件違反**だった可能性が高い。つまり原因は特定可能な範囲にある。

P4 について IDF のドキュメントを確認した結果、**1. のキャッシュ無効時に
外部 RAM がアクセス不能になる点と、既定でタスクスタックに外部 RAM を
使わない点は S3 と同一**である。差が出るとすれば DMA 周辺の条件で、
P4 は世代が新しくメモリサブシステムも異なる。**再挑戦は P4 から**、
というのが現時点の判断。ただしディスクリプタが内部 RAM 必須である点は
どちらも変わらない。

#### 安全境界 (この条件を全部満たすタスクだけが候補)

- フラッシュに一切触れない (LittleFS / NVS / esp_partition / OTA を、
  間接呼び出しも含めて呼ばない)
- スタックローカルのバッファを DMA に渡さない (転送系 API に渡す
  バッファはすべて `MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL` から取る)
- ROM コードに依存しない
- PSRAM のアクセス速度低下を許容できる

mruby / Spinel のアプリタスクは、Ruby から `File` も描画転送も呼ぶので
**素のままではこの条件を満たさない**。満たすには全経路の proxy 化が要る。

#### Spinel 固有のトレードオフ

Spinel は Ruby のフレームをタスクスタック上のネイティブ C フレームとして
積む。スタックが PSRAM にあると**実行そのものが遅くなる**。
mruby VM は Ruby フレームを VM プール内の mrb スタックに置くので影響が
小さい。Spinel の速度優位 (起動 3 倍、描画の最悪値 14 ms → 3 ms) を
消してまで内蔵 RAM を回収するのは本末転倒になりかねない。

#### 再挑戦の手順 (長期目標)

ここが通れば同時起動アプリ数の制約が根本から外れるので、
**いつかは再挑戦する**。ただし順序を守ること。いきなりアプリタスクを
移さない。

1. **残りの調査を完了させる**。P4 の DMA が PSRAM を扱える条件
   (整列幅、キャッシュ同期 API、ディスクリプタの配置制約) を
   ドキュメントと IDF ソースで確認し、本書に追記する。
   S3 での過去のクラッシュが 1. と 2. のどちらだったかを、
   当時の症状から特定できるならしておく。
2. **検出手段を先に用意する**。`hw_proxy.c` には既に「現在のスタックが
   PSRAM アドレス範囲か」を判定するコードがある。これを一般化して、
   PSRAM スタックのタスクがフラッシュ経路や DMA 転送 API に入ったら
   即座に落ちる (fail-loud) チェックを入れる。**黙って壊れるのを、
   その場で落ちるに変える**のが再挑戦の前提条件。
3. **P4 から始める**。撤退したのは S3 であり、P4 は未検証。
4. **最も条件の緩いタスク 1 本で試す**。アプリタスクではなく、
   フラッシュにも DMA にも触れないタスクから。
5. soak で確認してから次の 1 本。一度に複数を移さない。
6. アプリタスクに到達したら、Ruby から届く経路
   (File, 描画転送, ネットワーク) がすべて proxy 化されているかを
   経路単位で確認する。1 つでも残っていれば同じ失敗になる。

**この調査と検出手段が揃うまで、PSRAM スタックには手を出さないこと。**
過去の撤退は、原因を特定しないまま「なんとなく DMA 絡みで落ちる」状態で
行われている。同じ状態に戻すだけなら着手しない方がよい。

### C: タスクの統合と削減

- `status_led` (4 KB): 周期的な GPIO パターン出力だけなので、
  FreeRTOS ソフトウェアタイマか `hw_proxy` に畳める見込み。
  タスク 1 本を消すとスタックに加えて TCB とガード領域も消える。
- `debugd` と `ble_fs`: どちらも低優先度・低頻度・BLE 経由という点で近い。
  統合できればスタック 1 本ぶん。ただし debugd は TCP (Linux) 経路も持つので
  条件を確認する。
- `usb_host_lib` / `hid_host`: IDF のドライバ構造に従っているので触らない。

### D: 遅延起動 (M-1 の結果次第で最大の一手)

初期化した時点で内蔵 RAM を確保する重量級のサブシステムを、
必要になるまで起動しない。候補:

- **BLE** (BT コントローラ + NimBLE ホスト + ble_fs)。ファイル同期と
  リモートデバッグにしか使わないなら、常時起動している必要は薄い。
  内蔵 RAM の消費量は M-1 で確定させる。
- **USB Host**。デバイスが挿さっていない構成では丸ごと不要。
  ただし「挿されたら起動する」検出手段が要る。
- **debugd**、リモートデスクトップ (`rd_*`)。

いずれも「止める」ではなく「必要になったら起動する」設計にする。
起動済み前提のコードがどこにあるかの棚卸しが伴うので、
M-1 で回収量を確認してから着手する。

### E: 静的バッファの PSRAM 退避

M-3 で見つかったものを `EXT_RAM_BSS_ATTR` で PSRAM へ移す。
DMA に渡すバッファと ISR から触るものは移せない。

Spinel の例外/catch スタックは `phase7.md` の T7-1 で扱う (fail-loud 化 →
high-water 実測 → サイズ決定の順)。ここでは重複させない。
なお phase5 で挙がっていた 84,608 B は P4 / 2 インスタンス構成の値で、
S3 の Spinel カーネル単独構成では 12,800 B (2026-07-31 の map 実測、
後述の計測記録を参照)。回収見込みは構成ごとに読み替えること。

## 進め方

**回収量ではなく「安全に取れる順」に並べる**。B は回収量が最大だが
過去に失敗しているので最後に置く。

1. **M-1**。これが無いと優先順位が決まらない。
2. **E (静的バッファの PSRAM 退避)**。実績のある手法で副作用が小さい。
   M-3 で見つかった順に処理する。
3. M-1 の表を見て **D (遅延起動)**。BLE / USB が数十 KB 規模なら、
   スタック調整より回収量が大きい。設計変更を伴うが、動作中の
   メモリ配置を変えないので B より安全。
4. **M-2 → A**。実測に基づくスタック調整。
5. **C**。統合はリスクの割に回収量が小さい。
6. **B**。1〜5 で足りない場合にのみ、調査と fail-loud 検出を揃えた上で。
   ここに到達する前に必要量が満たせているのが理想。
7. 各段階で **P4 / S3 の両方** と **mruby / Spinel の両構成**で回帰を確認する。
   dual build に差を作らないこと。

## 落とし穴

- **推論でサイズを決めない**。上の「原則」の 3 件を読み返すこと。
- **アイドルの high-water で判断しない**。最悪経路を通してから採る。
- **スタック不足は別の症状として現れる**。NimBLE の BSS 破壊、偽 OOM、
  vsnprintf でのプロテクションフォールト。「メモリを削った後に出た
  無関係に見える不具合」は、まずスタックを疑う。
- **PSRAM は遅い**。特に Spinel インスタンスのスタック。回収量と
  実行速度はトレードオフで、どちらも計測してから決める。
- **PSRAM スタックは「ファイル I/O を避ければ安全」ではない**。
  そのタスクのスタックローカル変数がすべて PSRAM になるので、
  DMA に渡す一時バッファ、キャッシュ無効中に触れるデータ、ROM 呼び出しが
  すべて対象になる。過去にこれで撤退している (軸 B を参照)。
  ファイル I/O の hw_proxy 化だけでは足りなかった、というのが履歴の要点。
- **「DMA 絡みで落ちる」を症状のまま放置しない**。フラッシュ操作中の
  キャッシュ無効と、DMA バッファ / ディスクリプタの配置は別の制約で、
  現れ方が似ているだけである。どちらなのかを特定してから対策する。
- **sdkconfig / sdkconfig.defaults は編集禁止**。PSRAM 関連の設定変更が
  必要になったら提案のみ。
- **比較は同一コミットのビルド同士で**。別日の記録と突き合わせない。
- ターゲットを切り替えるときは `rake clean_all`。`.env` の
  `FMRB_HW_TARGET` は環境変数より優先されるので、ブートログで
  実際のチップを確認する。

## 記録

M-1 / M-2 / M-3 の計測結果と、各軸で採った決定は**本ドキュメントに
追記して育てる** (計測 → 決定 → 効果、の順に節を足す)。併せて
`fmrb_task_config.h` の各マクロのコメントに**採用値の根拠
(実測値 + マージン)** を書く。
このヘッダのコメントが、次に触る人が推論で決めるのを防ぐ唯一の防壁になる。

## 計測記録

計測 → 決定 → 効果の順に、日付つきで追記していく。

### 2026-07 (最初の記録): タスクスタックの実測 (S3, アイドル)

最初の記録の S3 は待機時の空き 61,328 B (起動時の内蔵 RAM は約 292 KB)。

| タスク | 確保 | ピーク使用 | 余剰 |
|---|---:|---:|---:|
| fmrb_host | 32,768 | 15,240 | 17,528 |
| system_desktop | 24,576 | 10,436 | 14,140 |
| fmrb_kernel | 16,384 | 6,548 | 9,836 |
| hw_proxy | 8,192 | 1,940 | 6,252 |
| ble_fs | 8,192 | 1,980 | 6,212 |
| debugd | 6,144 | 2,856 | 3,288 |
| usb_host_lib | 4,096 | 1,968 | 2,128 |
| status_led | 4,096 | 2,108 | 1,988 |
| hid_host | 4,096 | 2,980 | 1,116 |
| **合計** | **108,544** | **46,056** | **62,488** |

タスクスタックだけで 62 KB が未使用のまま内蔵 RAM を占めている。
アイドル時 IRAM free とほぼ同額で、理屈の上では倍にできる余地がある。

**ただしこの表で切ってはいけない**。アイドル時の high-water であり、
最悪経路を通した値ではない。`hid_host` は既に残り 1,116 B で、
削るどころか増やす判断もあり得る。

### 2026-07-31: S3 混成ビルド (Spinel カーネル + mruby desktop) の実機ログ

構成: NARYA (S3 + WROVER)、`FMRB_KERNEL_ENGINE=spinel` +
desktop は mruby。ユーザアプリ 0 個。操作は desktop overlay の開閉と
マウス移動程度で、約 6 分間の周期ダンプから各タスクの最小 Free を採った。
**M-2 の最悪経路 (GFX 洪水、BLE 転送、USB 抜き差し、ユーザアプリ起動) は
通していない**ので、この表を根拠にスタックを切ってはいけない。

| | |
|---|---:|
| アイドル時 IRAM free | 82,448 B (観測期間中一定) |
| PSRAM free | 3,227,920 B |

| タスク | 確保 | 最小 Free | ピーク使用 |
|---|---:|---:|---:|
| fmrb_host | 32,768 | 17,320 | 15,448 |
| system_desktop (mrb) | 16,384 | 5,556 | 10,828 |
| fmrb_kernel (spx) | 16,384 | 8,032 | 8,352 |
| hid_host | 4,096 | 1,164 | 2,932 |
| debugd | 6,144 | 3,232 | 2,912 |
| status_led | 4,096 | 1,860 | 2,236 |
| hw_proxy | 8,192 | 6,156 | 2,036 |
| ble_fs | 8,192 | 6,212 | 1,980 |
| usb_host_lib | 4,096 | 2,128 | 1,968 |
| **合計** | **100,352** | **51,660** | **48,692** |

読み取り:

- タスクスタックの未使用分は 51.7 KB。「現状」節の S3 実測 (62.5 KB) と
  傾向は同じだが、**別コミット・別構成なので数値の直接比較はしない**。
- `fmrb_host` のピーク 15,448 B は過去実測 15,240 B と整合。GFX 洪水下の
  値を採ってから 32 KB → 24 KB を判断する、という A 軸の方針は変えない。
- `fmrb_kernel` (Spinel) のピーク 8,352 B は `fmrb_task_config.h` の
  コメントにある P4 実測 8.8 KB と整合。重量メソッドのフレームが
  4.4 KB あるため、16 KB から下げる余地は小さい。
- `hid_host` は残 1,164 B。引き続き削減禁止・増量候補。
- VM プールの frag 列はこのログでも最大 113% を示した。計算側を直すまで
  この列は信用しない (phase7 T7-8)。

### 2026-07-31: map による静的確保の実数 (M-3 の部分実施)

working tree の S3 ビルド (`build/fmruby-core.map`) から。上記ログと
同一コミットの保証はない点に注意。

- 静的な内蔵 DRAM は `.dram0.data` 21,196 B + `.dram0.bss` 33,208 B =
  **54,404 B しかない。静的確保は主犯ではなく、内蔵 RAM を食っているのは
  実行時のヒープ確保 (タスクスタック、BLE、USB、キュー類) である**。
  M-1 (初期化ステップごとのヒープ差分表) の優先度がさらに上がった。
- `.bss`/`.data` の大物 (4 KB 級以下は省略):

  | シンボル | サイズ | 出所 |
  |---|---:|---|
  | g_fs_ctx | 4,384 | ble_task.c (BLE ファイルサービス) |
  | sp_catch_stack | 4,352 | Spinel カーネル生成 TU |
  | sp_exc_stack | 4,352 | Spinel カーネル生成 TU |
  | s_file_write_bounce | 4,096 | fmrb_hal_file_esp32.c |
  | linebuf | 2,048 | fmrb_debugd.c |
  | pm_binding_powers | 1,980 | mruby prism (.data) |

- Spinel カーネルインスタンスの例外/catch 系
  (`sp_exc_stack` / `sp_catch_stack` / `sp_catch_val` / `sp_dyn_syms` +
  深さ 64 の管理配列 8 本) の合計は **12,800 B**。
  大半が深さ (`SPINEL_RT_EXC_STACK_MAX = 64`) に比例するので、
  T7-1 の実測後に深さ 16 へ下げられれば**約 9 KB / インスタンス**戻る。
  desktop も Spinel 化するとこの塊がもう 1 セット増える。
- `.dram0.dummy` は 83,456 B。**静的 IRAM (コード) が DRAM アドレス空間を
  専有している分**で、IRAM_ATTR コードを減らせば 1:1 で DRAM が返る。
  命令キャッシュは既に最小の 16 KB なので sdkconfig 側の余地はない。
  IRAM_ATTR の棚卸しは効果はあるが、ISR / flash 無効中に走るコードの
  判別が要るため優先度は低い。

### 2026-07-31: 新規判明 — Spinel GC マークスタック 32 KB が内蔵 RAM に載る

**[2026-08-02 訂正: この節の結論は誤り。sp_mem_override.h の差し替えを
見落としていた。マークスタックは est pool (PSRAM) から取られており
対応不要。2026-08-02 節を参照。]**

`components/fmrb_spinel_rt/spinel_rt/sp_gc.c` の `sp_gc_mark_all()` が
初回 GC で `malloc(sizeof(void*) * SP_GC_MARK_STACK_MAX)` を行う。
現在 `SPINEL_RT_GC_MARK_STACK_MAX = 8192` なので ILP32 で **32,768 B**。
本ビルドは `CONFIG_SPIRAM_USE_CAPS_ALLOC` (PSRAM は heap_caps 経由のみ)
なので、**素の malloc は必ず内蔵 RAM から取られる**。map に出ない
実行時確保のため、これまでの M-3 視点では見えていなかった。

対策は 2 案:

1. **PSRAM 退避 (推奨)**。マークスタックは CPU しか触らない
   (DMA / ISR / flash 無効区間と無関係) ので、B 軸の PSRAM スタックの
   ような危険性はない。代償は GC マーク中のアクセスが PSRAM 速度に
   落ちることで、GC 停止時間への影響を T7-4 の GC 計測で確認する。
   fork 側の変更になるので、確保関数を差し替えられる口
   (マクロか weak 関数) を fork に設けて core 側から注入する。
2. サイズ削減。溢れても即時の再帰 scan に退化するだけでクラッシュは
   しないが、**その再帰はタスクスタック上で起きる**ので、深いオブジェクト
   グラフで A 軸のスタックサイズ決定と結合してしまう。単独では採らない。

### 2026-08-02: M-1 実施 — ブートステップごとの内蔵 RAM 差分表 (S3 実機)

計装を常設した: `fmrb_mem_log_boot_snapshot()` (fmrb_mem) が
`M1|ラベル|internal=..|largest=..|psram=..` の 1 行を出す。呼び出し点は
boot.c / fmrb_kernel_start / host task / fmrb_app_spawn 成功時。
ブートログを `grep "M1|"` して隣接行を差分すると下表になる。
アプリ起動ごとにも `spawn:<name>` 行が出るので、1 アプリの内蔵 RAM
コストも同じ仕組みで採れる。

構成: NARYAv3 (S3)、Spinel kernel + mruby desktop、develop 3f41f4e +
本計装。ユーザアプリ 0 個。

| ステップ | 直後の internal free | 消費 |
|---|---:|---:|
| boot_start | 303,908 | — |
| mem_init | 303,644 | 264 |
| gpio_led_proxy (pin mgr + hw_proxy + status_led) | 290,460 | 13,184 |
| littlefs_mount | 288,240 | 2,220 |
| fs_bench | 288,240 | 0 |
| usb_host | 277,376 | 10,864 |
| **ble (BT コントローラ + NimBLE + ble_fs)** | 202,416 | **74,960** |
| system_config (TOML 読込) | 197,684 | 4,732 |
| hal (UART link) | 196,584 | 1,100 |
| file_sync / app_init / mp_init | 196,232 | 352 |
| **host task 生成 (host_task_entry 時点)** | 138,196 | **58,036** |
| gfx_audio_init | 138,196 | 0 |
| spawn:fmrb_kernel (16 KB スタック) | 121,364 | 16,832 |
| debugd | 107,748 | 13,616 |
| spawn:system_desktop | 90,916 | 16,832 |
| (VM ブート完了後の定常) | 82,040 | 8,876 |

読み取り:

- **単独最大は BLE の 74,960 B**。優先順位表 3 の「数十 KB 級の見込み」が
  確定した。遅延起動 (D 軸) が成立すれば、GC マークスタック (32.8 KB) を
  大きく上回る回収になる。
- **host task 生成の 58,036 B の内訳は算術で閉じた**: スタック 32,768 +
  host メッセージキュー 24,064 (FMRB_HOST_MSG_QUEUE_LEN 128 ×
  sizeof(fmrb_msg_t) ≈ 188 B) + TCB・セマフォ・キュー管理 ≈ 1.2 KB。
  **キュー 24 KB は新顔の削減候補**: 長さ 128 の根拠確認 (flow 制御は
  セマフォ側にあるので長さは詰められる可能性) と、キュー格納域の
  PSRAM 化 (`xQueueCreateWithCaps`、ISR から触らないことの確認が前提)
  の 2 方向がある。
- gfx_audio_init の内蔵 RAM 消費は 0 (転送バッファは PSRAM / UART ドライバは
  hal 時点で確保済み)。
- タスク生成 1 本の固定費はスタック + 約 450 B (TCB 等)。kernel/desktop の
  16,832 B = 16,384 + 448 が丁度それ。
- debugd の 13,616 B はスタック 6 KB + linebuf 2 KB を 5.6 KB 上回る。
  BLE GATT 側の確保が乗っている可能性。D 軸 (遅延起動) の候補のまま。
- **Spinel GC マークスタック 32 KB はこの表に出ていない**。初回 GC で
  malloc されるが、アイドルではまだ走っていない。使用中に突然 32 KB
  消えるので、定常値を読むときは注意。
- 注意: spawn 以降は kernel VM のブートが並行して走るため、
  debugd / spawn:system_desktop 行の差分には並行確保が混ざる。
  ステップ単位の厳密な帰属は直列区間 (ble まで) に比べて粗い。

優先順位への反映 (7/31 の表に対して):

- **1 (GC マークスタックの PSRAM 退避) は誤分析だったので取り下げ**。
  7/31 の「素の malloc で内蔵 RAM に載る」は誤り。SP_MULTI_CTX ビルドは
  `-include sp_mem_override.h` で runtime 内の malloc を `sp_mem_malloc` に
  差し替えており (components/fmrb_spinel_rt/CMakeLists.txt の
  SPINEL_MC_FLAGS、sp_gc.c にも PRIVATE で効いている)、マークスタックは
  現インスタンスの est pool = PSRAM 上の mempool から取られる。
  実測でも裏が取れた: kernel の GC はブート中から何度も走っている
  (spx pool 使用量が周期ダンプで増減) のに、M-1 の差分表のどこにも
  32 KB の内蔵 RAM 低下が無い。**この項目は対応不要**。
- 3 (BLE 遅延起動) の回収量が **74,960 B で確定**。単独最大。
  安全順の原則は変えないが、D 軸の設計検討を前倒しする価値がある。
- **2 (T7-1) は 2026-08-02 に完了** (詳細は phase7.md の T7-1 実施記録)。
  begin/catch push の fail-loud 化 + high-water 計測 (ps の exc_hw /
  catch_hw、VM Pools の ExcHW 列) を実装し、観測最大 4 に対して
  `SPINEL_RT_EXC_STACK_MAX` 64 → 16 に決定。効果は S3 実機で
  **定常 IRAM free 82,040 → 90,492 B (+8,452 B/インスタンス)**。
- 新規候補: **host メッセージキュー 24 KB** (上記)。長さ適正化なら
  A 軸並みに安全、PSRAM 化なら ISR 経路の確認が要る。
- usb_host は 10.9 KB、debugd は 13.6 KB。D 軸 (遅延起動) の回収量として記録。

### 2026-08-02: T7-1 と E (一部) の効果 — 定常 IRAM free 82.0 → 96.9 KB

同日の S3 実機ビルド (Spinel kernel + mruby desktop、アイドル定常) の系列:

| 施策 | 定常 IRAM free | 差分 |
|---|---:|---:|
| ベースライン (M-1 計装のみ) | 82,040 | — |
| T7-1: 例外/catch スタック深さ 64 → 16 | 90,492 | +8,452 |
| E: g_fs_ctx (4,384) + debugd linebuf (2,048) を PSRAM 退避 | 96,924 | +6,432 |

- 合計 **+14,884 B (+18%)**。最大連続ブロックも 49,152 → 81,920 B。
- g_fs_ctx / linebuf はどちらも CPU コピーのみ (GATT コールバックの
  os_mbuf_copydata、ログリングからの memcpy)。ファイル書込は HAL の
  内蔵 RAM bounce バッファ (s_file_write_bounce) 経由なので PSRAM 源で
  問題ない。 [2026-09-29 追記: bounce バッファの前提は誤りで、どの経路も PSRAM の
  書き込み元で問題ない。R2 の節を参照。]**BLE ファイル同期の実機疎通確認は未** (機能は placement
  非依存のはずだが、ユーザの通常運用で一度確認する)。
- E の残り: pm_binding_powers 1,980 B (.data、mruby prism submodule。
  const 化で .rodata へ落とせる見込みだが lib/patch 経由が要る)。

### 2026-08-02: M-2 実施 — 最悪経路を通したタスクスタック実測と A 軸の決定

S3 実機で、リセット直後からユーザ操作で最悪経路を一通り通した
(config/set_clock 開閉、launcher、アプリ 6 種以上の起動と終了
〈ゲーム = GFX 洪水含む〉、ドラッグ・リサイズ・fullscreen、
**BLE ファイル転送**、**デバッガ attach + Web コンソールからの spawn**、
USB 抜き差し)。high-water は単調悪化なので、セッション終端の周期
ダンプ最小 Free = 最悪値。

| タスク | 確保 | 最小 Free | ピーク使用 | 決定 |
|---|---:|---:|---:|---|
| fmrb_host | 32,768 | 17,400 | 15,368 | **24 KB に削減** (3 回の計測でピークが 15.2〜15.4K と安定 = 固定チェーン) |
| fmrb_kernel (spx) | 16,384 | 7,984 | 8,400 | 維持 |
| system_desktop (mrb) | 16,384 | 5,472 | 10,912 | 維持 |
| ble_fs | 8,192 | 2,876 | **5,316** | **維持** (転送実測でアイドル値の 2.6 倍。6K 案は撤回) |
| hw_proxy | 8,192 | 6,156 | 2,036 | **6 KB に削減** |
| debugd | 6,144 | 3,172* | ≥3,564 | **維持** (*attach 中の spawn で瞬間残 2,580 B を別途観測) |
| hid_host | 4,096 | 1,164 | 2,932 | **5 KB に増量** (系内最薄。列挙経路) |
| usb_host_lib | 4,096 | 2,128 | 1,968 | 維持 |
| status_led | 4,096 | 1,828 | 2,268 | 維持 (C 軸で再検討) |
| user app (PicoRabbit) | 16,384 | **1,112** | 15,272 | 16 KB は下限と判明。削減禁止 |
| FM-Shell | 12,288 | 2,876 | 9,412 | 維持 |
| FM-Editor | 12,288 | 1,964 | 10,324 | 維持 (余裕薄、要観察) |

- 差し引き回収: host −8K + hw_proxy −2K + hid_host +1K = **約 9.2 KB**。
- kernel の exc_hw はこのセッションで 4 に更新 (深さ 16 に対して余裕 4 倍)。
- 副産物: USB を運転中に抜き差しすると入力が復帰しない (切断イベントが
  ログに出ない = ホスト側が切断を検知していない)。**運転中の抜き差しは
  サポート外とする (2026-08-02 決定)**。USB HID は電源投入時に接続して
  おくこと。抜けた場合の復帰はリセットで行う。修正課題としては扱わない。

### 2026-08-02: メッセージキューの PSRAM 化 — 定常 IRAM free 142.7 KB

fmrb_msg の全キュー (host 24 KB + kernel/desktop/app 各 6 KB) の格納域を
`xQueueCreateStatic` + PSRAM heap に移した (6ea5911)。成立根拠は
**コードベース全体に \*FromISR 送信が存在しない**こと (2026-08-02 検証)。
管理ブロックは内蔵 RAM のまま。PSRAM 不在時と Linux は従来の動的キュー。

| | |
|---|---:|
| 定常 IRAM free (アプリ 0) | 106,152 → **142,708 B (+36.6 KB)** |
| 本日の累計 (ベースライン 82,040 から) | **+60.7 KB (+74%)** |

動作検証: ゲーム + 入力の実機プレイで `hid_event slow` 警告 0 件
(変更前の同種セッションは 101 ms 級 2 件)、GFX レート正常。
1 アプリあたりの内蔵 RAM コストも約 25 KB → 約 17 KB に下がる
(キュー 6 KB が PSRAM へ)。

**副産物 (soak で発見・修正済み)**: `ctx->est` がアプリ終了・スロット
再利用で残留し、mruby アプリの後に BASIC アプリが同スロットに入ると
周期ダンプが TLSF が書き換え中の領域を estalloc として読んで
InstructionFetchError (9bce545 で修正。ダンプの無ロック走査も同時に修正)。
ダンプ実装当初からの潜在バグで、読むゴミの内容次第で発火する型。

### 2026-08-02: D 軸実装 — BLE 遅延起動 (config + メニュー手動起動)

`ble_auto_start` (system_conf.toml、既定 true = 従来動作) を追加し、
false のときは boot で BLE を立てず、desktop メニューの「Start BLE /
BLE起動」から `ble_service_start()` (冪等・ワンショットタスク) で
起動する。Retro (内蔵 BLE) のみ。P4 は C6/SDIO の初期化順序が WiFi と
絡むため対象外のまま。`esp_bt_controller_mem_release` は手動起動と
両立しないため使わない (呼ぶとリブートまで再起動不可になる)。

S3 実機での 3 段階検証:

| 状態 | 定常 IRAM free |
|---|---:|
| auto=true (既定) | 142,460 B (従来同等) |
| auto=false、BLE 未起動 | **216,956 B (+74,496 B)** |
| メニューから手動起動後 | 142,240 B (Web コンソール接続・subscribe・切断まで動作確認) |

M-1 の内訳計測点を ble_task_init 内に常設した: controller + NimBLE host
(`ble_nimble_port`) ≈ 60 KB、GATT + ble_fs タスク (`ble_ready` まで)
≈ 14.7 KB。

### 2026-08-03: S3 WiFi 有効化のコスト (62b7456)

WiFi をビルドに含めた常時コスト (起動しなくても払う分) は、
`ESP_WIFI_*_IRAM_OPT` を切った状態で **約 10.5 KB** (静的バッファ)。
IRAM opt を有効のままだと +17.6 KB (WiFi コードが IRAM = DRAM アドレス
空間を専有) なので切ってある。定常 IRAM free は BLE 稼働で 131,852 B、
WiFi 稼働 (BLE off) で 140,168 B。WiFi のバッファ類は
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` で PSRAM に逃げている (−65 KB)。

**新たな制約は flash**: app パーティション残が 22% → **6% (約 200 KB)**。
(2026-08-03 追記: この 6% は **app パーティションが 3M だった時点**の値。
この後 networking gem を入れる際に 4M へ拡大したので、**現在は約 24%
(約 1MB)**。`config/partitions_n16r8.csv` を参照。)
Spinel インスタンス追加や大きな機能はここが先に詰まる (T7-6 の懸念が現実化)。

### 2026-09-07: 起動可否の計算方式の修正と、余裕分の設定化 (Tab5)

**計測**。Tab5 (P4) を起動し、アプリを何本か起動・終了しながら
`internal RAM ok` / `refused` 行を追った。

| 時点 | 内蔵 free | 最大ブロック |
|---|---:|---:|
| ブート直後 (wifi_rd) | 183,032 | 143,360 |
| アプリ 1 本目の起動時 | 127,952 | 83,968 |
| 数本を起動・終了した後 | 70,816 | 31,744 |
| 稼働アプリを減らした後 | 117,808 | 60,416 |

総量はアプリを終了すればある程度戻るが、**最大ブロックは戻り方が鈍い**。
アプリのスタックが 16-24 KB の連続領域を内蔵 RAM から取るため、
起動・終了を繰り返すと穴が細切れになる。この状態でエディタ (スタック
24 KB) が拒否された:

```
W (16:09:20.810) fmrb_app: [FM-Editor] refused: largest internal block 31744 < 32768 needed
```

**決定**。連続ブロックの検査はスタックだけを数える (付帯 8 KB を足さない)。
根拠は「アプリ起動の可否判定」の節に書いた。31,744 B は 24,576 B のスタックを
収められるので、この判定でエディタは起動できる。総量側の判定は従来どおり
スタック + 付帯 + 余裕分で、そちらは 70,816 >= 63,488 で通っていた。

あわせて余裕分を `system_conf.toml` の `app_spawn_margin_kb` にした
(既定 30 KB、0 で無効、200 KB でクランプ)。**200 を設定して再起動すると
エディタもシェルも起動できず、端末上で設定を戻せなくなることを実測した**
(`[Kamon] refused: internal free 127040 < 229376 (need 24576 + margin 204800)`)。
設定が端から端まで効いていることの確認でもあるが、この値は小刻みに上げること。

**効果** (Tab5 実機、同日確認)。アプリを起動・終了して最大ブロックを削った
状態で、エディタが開いて打鍵まで通った。

```
I [FM-Editor] internal RAM ok: free=84516 largest=31744 need=32768 (stack 24576)
```

拒否されていたのと同じ 31,744 B である。総量側の歯止めも効いたままで、
空きが減った状態では従来どおり断られる。

```
I [BlockGame]    internal RAM ok: free=58532 largest=16384 need=24576 (stack 16384)
W [Raycaster] refused: internal free 41276 < 55296 (need 24576 + margin 30720)
```

BlockGame は最大ブロックがスタックとちょうど同値 (16,384 B) で通っている。
`heap_caps_get_largest_free_block` は**確保可能な最大サイズ**を返す
(管理領域を含まない) ので、この境界は等号で正しい。

### 2026-09-28: 静的確保の洗い直しと移設 (iram_reduction R1)

**計測**。develop `63f2e7cf` を 3 機種 clean からビルドし、map の内蔵 RAM の
入力節をシンボル単位で並べた (全表と分類は doc/iram_reduction/report/r1.md 3 章)。
静的な D/IRAM は S3 146,995 / TAB5 183,008 / NARYAv4 178,410 B。IRAM (コード) は
3 機種とも IDF のものだけで、自前のコードは 0。

**決定**。タスクからしか触らず、DMA にも割り込みにも flash 書き込みの元にも
ならないものを PSRAM へ、書き込まない表を flash へ移した。

| 移したもの | 移し先 | S3 | P4 |
|---|---|---:|---:|
| display_p4 の表 (`g_progs` `g_recv_buf` `g_canvases` `s_cursor_save` `g_sorted` `g_masks`) | PSRAM | - | 15,360 |
| mrblib の子 irep 表 `*_reps_N` (mrbc の cdump.c を lib/patch で const 化) | flash | 5,480 | 5,616 |
| tmpfs の `s_files` `s_fds` | PSRAM | 2,144 | 5,504 |
| link_local の `g_recv_internal_buf` | PSRAM | - | 4,096 |
| editor の表 (`g_wrap` `g_docs` `widths` `g_hover` `g_call` `g_oom`) | PSRAM | 3,280 | 3,516 |
| MIDI scheduler の `s_ring` | PSRAM | 2,048 | 2,048 |
| spx の FFI 返却用 `buf` / `payload` | PSRAM | 1,434 | 1,434 |
| usb の `g_hid_devices` と raw report の `msg` | PSRAM | 1,212 | 1,212 |
| transport の `g_tranport_context` | PSRAM | 776 | 776 |

**効果**。静的な D/IRAM は S3 -16,384 / TAB5 -39,592 / NARYAv4 -39,584 B。
P4-Nano (NARYAv4) 実機の待機時 IRAM free は 150,524 → **190,088 B (+39,564)**、
アプリ起動時の最大ブロックも +40 KB (BlockGame 起動時 63,488 → 104,448)。
render の時間は変わらず (待機 avg 27 / max 30 ms)、Guru 0。

**残したもの** (R2 で確かめる): `s_fs_buf` と `s_file_write_bounce`
(flash 書き込みの元)、PPA の経路の `scaled` / `block`、I2S の
`s_stereo_buf` / `s_mic_buf`、prism の `pm_binding_powers` (libmruby は ldgen の
対象外なので配置の断片が効かない)、Spinel の `cst_*` / `pool_count` (フォークの変更)、
link_local のメッセージバッファ (実行時の確保、チャネルごとに 10 KB)。

### 2026-09-29: ファイル書き込みのバッファと g_recv_buf の速さ (iram_reduction R2)

**確認**。`s_file_write_bounce` の前提「PSRAM の元から SPI flash に書くと黙って失敗する」は、
IDF のコードの上で成り立たない (詳細は doc/iram_reduction/report/r2.md 2 章)。

- LittleFS は呼び出し元のデータを flash に触る前に自分のキャッシュ (内蔵) へ memcpy する (`lfs_bd_prog`)。
- `esp_flash_write()` は元が内部 DRAM でなければ、キャッシュを止める前にスタックの一時領域へ写す。
- SD は `sdmmc_write_sectors()` が PSRAM の元を DMA 可能なバッファへ自分で跳ね返す。FAT の作業領域はもともと PSRAM。
- 跳ね返すかどうかの番地の判定は S3 の外部データの窓 (`0x3C000000`-`0x3E000000`) で、**P4 では一度も動いていなかった**。
  この跳ね返しは、本当の原因 (picoruby-machine が IO#write を UART に差し替えていた) と同じコミットで推測で足されたもの。

「flash の書き込み中は PSRAM が読めない」は正しいが、どの経路も書き込み元を読むのはキャッシュを止める前である。
flash 書き込みの元になるバッファでも、LittleFS / esp_flash / sdmmc を通るなら PSRAM でよい
(直接 `esp_flash_write` に渡す場合も IDF が写す)。

**決定と効果**。P4 では跳ね返しを外し (S3 だけに残す)、devctl の `s_fs_buf` を PSRAM へ。静的な D/IRAM は
TAB5 143,416 → 135,224、NARYAv4 138,826 → 130,634 (**-8,192**)、S3 は 130,611 のまま (Retro の実機で確かめてから)。
P4-Nano の待機時の IRAM free は 190,088 → **198,260 (+8,172)**。File#write (Ruby の文字列が元、1 B-2 MB)、
`/fs/put`、エディタの保存を、BlockGame を動かしながら書いて読み戻し、すべて一致。

**g_recv_buf (R1 で PSRAM へ移した描画の受信バッファ) の速さ**。同じコミットで内蔵に戻した版と比べた
(NARYAv4、各 2 回、受け取りと解釈の時間を一時的に計装)。受け取り 2.3-2.6 us、解釈と実行 0.37-0.70 ms、
edit_lat 平均 11.3 ms (PSRAM) 対 11.7 ms (内蔵)、cmds/s と render も同じで、**差は無い**。
1 メッセージが約 22 B と小さく、memcpy よりメッセージバッファの手続きが大半を占める。PSRAM のままにした。

### 2026-09-29: sdkconfig の見積もり・小物・Spinel の定数 (iram_reduction R3)

**計測 (sdkconfig、変更はしていない)**。1 設定ずつ別の build ディレクトリで試しのビルドをして、静的な D/IRAM の
差を 3 機種で測った (表・条件・推奨の全体は doc/iram_reduction/report/r3.md 3 章)。減るのは全部 IRAM のコード。

| 設定 | TAB5 | NARYAv4 | S3 |
|---|---:|---:|---:|
| RMT の ISR / 符号化を flash へ | -2,726 | -2,726 | -2,528 |
| SPI master / slave の ISR を flash へ | 0 | 0 | -8,996 |
| ringbuf を flash へ (ISR 用まで) | -2,806 | -2,806 | -2,496 |
| heap を flash へ (S3 は SPI master の ISR が強制で外れる分を含む) | -6,192 | -6,196 | -13,440 |
| FreeRTOS (タスク用の関数) を flash へ | -11,884 | -11,844 | -8,476 |
| 組: RMT + ringbuf + SPI | -5,532 | -5,532 | -14,036 |
| 組: 上 + heap + FreeRTOS | -23,668 | -23,632 | -28,088 |

lwIP の PSRAM 配置 (P4 で静的には +100)、mDNS の PSRAM 配置 (静的には 0、実行時に約 4 KB)、P4 の WiFi の IRAM 最適化 (0) は
静的には効かない。**P4 の lwIP の .bss (約 4 KB) は sdkconfig では動かせない**: lwip/linker.lf の `extram_bss` の scheme は
esp_wifi の linker.lf が定義していて、WiFi をローカルに持たない P4 のビルドでは登録されない。
試しのビルドは並べて走らせられない (どの build ディレクトリからも picoruby の rake が走り、共有のビルド物を取り合う)。

**決定と効果 (コード)**。タスクだけが触る小物を PSRAM へ (host_task の `g_file_transfer` `s_sync_cmd_buf`、ble の
`g_put_cache`、display_p4 の `g_images_store`、動画の `s_p`、audio_p4 の `s_tracks`)。Spinel の生成 C の、初期値がゼロの
ファイルスコープの領域 (`cst_` 57 個、`civ_` 105 個) と pool の数え (`pool_count` / `pool_max` ほか) を fork `654c9fd5` で
`SP_TU_BSS` に置いた (313 個 1,249 B、P4 では .sbss / .sdata から外れたことを ELF の番地で確認)。静的な D/IRAM は
TAB5 135,224 → **132,912**、NARYAv4 130,634 → **128,250**、S3 130,611 → **128,779**。SPINEL_PIN は同日 `654c9fd5` に更新した。

**GC の停止時間 (T3)**。sim で同じコードの前後を各 5 回 (editor に 25 行打鍵 + マウス 150 回): kernel の 1 回の平均
352 → 297 us、editor 2,613 → 2,572 us で、回ごとの揺れ (kernel 217-383 us、editor 1,997-2,908 us) の中。sim では
`SP_TU_BSS` が空なので PSRAM の影響は見えない。実機での計測は R4。

### 2026-10-01: RMT を IRAM から外す (sdkconfig)

**決定 (ユーザ)**。iram_reduction R3 の見積もりのうち、RMT だけを採る。ringbuf・FreeRTOS・heap・S3 の SPI は、
得られる量に対して危険が大きいので P4 も含めてやらない。S3 では WROVER との UART 通信
(`fmrb_hal_link_uart_esp32.c`) が、送信で ringbuf に積み、受信で ringbuf から取り出し、1 フレームごとに mutex を取って
ACK を待つ。ringbuf と FreeRTOS を flash に置くと、この往復が毎回 flash の処理を通る。

**変えたもの**。`config/sdkconfig.defaults.{n16r8,p4,naryav4}` に `CONFIG_RMT_TX_ISR_HANDLER_IN_IRAM=n` /
`CONFIG_RMT_RX_ISR_HANDLER_IN_IRAM=n` / `CONFIG_RMT_ENCODER_FUNC_IN_IRAM=n`。RMT を使うのは RMT gem (LED マトリクスの
デモ) だけで、状態 LED は GPIO。チャネルは割り込みをキャッシュ安全にせずに作っているので、flash 書き込み中に止まるのは
もとから同じ。

**効果**。静的な D/IRAM は S3 128,779 → 126,251 (-2,528)、TAB5 132,912 → 130,186 (-2,726)、NARYAv4 128,250 → 125,524
(-2,726)。どれも見積もりどおり。P4-Nano の待機時の IRAM free は同じコミットの前後で 199,028 → **203,520 (+4,492)**。
静的な減り (2,726) より多く増えた理由は追っていない。Guru 0。

**気づいたこと (既存の問題、今回の変更と関係ない)**。NARYAv4 で LED マトリクスのデモを起動すると、約 12 秒後に
esp_hosted の SDIO が失敗して再起動する。デモは NARYAv4 でもデータ線を GPIO54 にしているが、P4-Nano の GPIO54 は
C6 の EN (`CONFIG_ESP_HOSTED_SDIO_GPIO_RESET_SLAVE=54`) で、駆動すると無線のチップがリセットされる。RMT の初期化と
送信そのものは、再起動までエラー無く動いた。同日、デモの NARYAv4 のデータ線をピンヘッダの GPIO48 に変えて直した
(30 秒以上動かして SDIO の失敗も再起動も無し)。LED が正しく光るかは、LED マトリクスをつないで確かめる。

## 参考資料

軸 B (PSRAM スタック) の判断根拠。IDF は v5.5 系を見ている。

- Support for External RAM (ESP32-S3, v5.5) — タスクスタックが既定で
  内部 RAM であること、キャッシュ無効時に外部 RAM がアクセス不能になること、
  DMA ディスクリプタを PSRAM に置けないこと
  https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/external-ram.html
- Support for External RAM (ESP32-P4, v5.5) — P4 でも同じ制約であることの確認
  https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32p4/api-guides/external-ram.html
- Memory Types (ESP32-S3, v5.5) — DMA バッファは DRAM かつワード整列、
  「スタックが PSRAM にあり得る場合、DRAM バッファをスタックに置くのは
  推奨しない」
  https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-guides/memory-types.html
- Support for External RAM (ESP32, latest) — ROM コードを直接/間接に
  呼ぶタスクでは使えない、という但し書き
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/external-ram.html
- esp-idf `components/spi_flash/cache_utils.c` —
  `spi_flash_disable_interrupts_caches_and_other_cpu()` の先頭にある
  `assert(esp_task_stack_is_sane_cache_disabled())` と、その実体
  (`esp_ptr_in_dram(sp)`)
  https://github.com/espressif/esp-idf/blob/master/components/spi_flash/cache_utils.c
- esp-iot-solution issue #708 — PSRAM スタックのタスクから NVS を触って
  上記 assert で落ちる実例
  https://github.com/espressif/esp-iot-solution/issues/708
- ESP32 Forum: Crash when using stack on external PSRAM — 同種の事例
  https://esp32.com/viewtopic.php?t=25931
