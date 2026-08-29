# 内蔵 RAM 削減計画

Spinel 化とは独立した、コアファームウェア全体の課題。
**内蔵 RAM (MALLOC_CAP_INTERNAL) が枯渇しており、同時に動かせるアプリ数と
機能追加の両方を縛っている**。PSRAM は 3 MB 以上余っているので、
不足しているのは総量ではなく内蔵 RAM に置く必然性のない配置と、
使われないまま確保されている余剰である。

Spinel 側の残課題は `doc/spinel_aot/phase7.md` を参照。
例外スタック 84,608 B の回収 (T7-1) と S3 のメモリ余裕測定 (T7-6) は
本計画と重なるので、実施はどちらか一方で行い他方は参照にとどめる。

## 現状

ESP32-S3 (NARYA v3, 16MB flash / 8MB PSRAM) のブート時点:

| | |
|---|---:|
| 起動時の内蔵 RAM (heap_init 合計) | 約 292 KB |
| アイドル時 IRAM free | 61,328 B |
| ユーザアプリ 0 個 (desktop のみ) の状態での値 | 同上 |

ESP32-P4 (Tab5) の実測では、アイドル 76,108 B からユーザアプリ 1 つで
50,548 B、2 つで 31,316 B まで落ちる。`FMRB_MAX_USER_APPS = 3` に対して
実質 2 つが上限で、S3 はさらに 15 KB 少ない状態から始まる。

### タスクスタックの実測 (S3, アイドル)

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
  問題ない。**BLE ファイル同期の実機疎通確認は未** (機能は placement
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

### 改善案の優先順位 (2026-07-31 版)

「進め方」の原則 (安全に取れる順) は維持したまま、今回の実測で
具体化できたものを回収見込みつきで並べる。

| 順 | 施策 | 回収見込み | 前提 |
|---|---|---:|---|
| 1 | GC マークスタックの PSRAM 退避 | 32.8 KB | fork に確保口を追加。GC 停止時間を前後で計測 |
| 2 | T7-1: 例外/catch スタックの fail-loud 化 + 実測 + 深さ決定 | 約 9 KB/インスタンス | 生成 C の再生成。診断基盤としても最優先 |
| 3 | M-1 → D: BLE の遅延起動 (コントローラ + NimBLE + ble_fs) | M-1 で確定 (数十 KB 級の見込み) | M-1 の差分表が先。`esp_bt_controller_mem_release` の適用可否も含めて |
| 4 | M-2 → A: スタック適正化 (host 32→24, hw_proxy 8→6, ble_fs 8→6, debugd 6→4) | 合計 12〜16 KB | 各タスクの最悪経路を通した high-water が先。hid_host は触らない |
| 5 | E 残り: g_fs_ctx / debugd linebuf / pm_binding_powers の PSRAM 退避 | 約 8 KB | DMA / ISR から触れていないことの確認。prism は submodule なので lib/patch 経由 |
| 6 | IRAM_ATTR の棚卸し (.dram0.dummy 83 KB の圧縮) | 不明 | ISR / flash 無効区間の判別が必要。急がない |

1 + 2 + 4 だけで約 50 KB、アイドル 82 KB に対して 6 割増しになる。
ユーザアプリ 1 個あたりの実測コスト (P4 で約 25 KB) を踏まえると、
これで S3 でもアプリ 2 個同時が安定圏に入る見込み。3 (BLE) が
数十 KB 級なら 3 個目も視野に入る。数値はいずれも T7-6 の
「S3 でユーザアプリを増やしながら IRAM free を実測」で裏を取る。

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
