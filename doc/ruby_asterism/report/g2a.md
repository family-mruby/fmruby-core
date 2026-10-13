# G2a 報告: 端末が TLS (mTLS) でクラウドのルータに直接繋ぐ

> 状態: ステップ 2 一部完了 (P4-Nano は確かめ済み、AtomS3R は PC に繋がっておらず未確認) | 更新: 2026-10-14 | instruction_g2a.md の記録。1-8 章がステップ 1 (調べと案、2026-10-13)、9 章以降がステップ 2 (実装と確かめ)。ユーザの決定 (2026-10-13) は TLS 1.3 と、mbedTLS の確保を sdkconfig で PSRAM に回すこと (4.2 の案)。P4-Nano は手元の mTLS の zenohd に家のルータなしで TLS 1.3 で繋がり、オセロを sim と指せた。同じコミットで TLS ありとなしの内蔵 RAM の差は誤差の範囲 (±0.3 KB)、静的な内蔵 RAM は develop より 240 B 減、PSRAM は接続 1 本で約 35 KB、flash は +70.8 KB。繋ぐまで 0.5-0.65 秒、ルータの起動し直しから 1.5-2.7 秒で戻る

## 1. 結果の要約

- **zenoh-pico の TLS**: 1.10.1 に TLS のリンク (`tls/...`、mbedTLS 2.x/3.x) があり、CA・クライアントの証明書 (mTLS)・名前の確かめ (`verify_name_on_connect`) が設定の項目でそろっている。証明書と鍵は「ファイルのパス」か「base64 の文字列」で渡せる。ただし (1) ソケットの型に TLS の欄があるのは Unix の platform だけ、(2) mbedTLS の頭書きが zenoh-pico の中心のヘッダから読まれる、(3) 読み・書き・握手がどれも時間の制限なしに止まる。よって**ESP-IDF の TLS の口は gem (`ports/esp32/`) に足す** (2 章)。
- **TLS 1.3 が要る**: zenohd 1.10.1 (rustls) は mTLS のとき TLS 1.3 だけを受ける (zenoh-link-tls が版を TLS 1.3 に固定)。今の ESP-IDF の設定は TLS 1.2 だけなので、そのままでは握手の最初で断られる。手元で TLS 1.2 だけの mbedTLS で試すと zenohd は `SupportedVersionsExtensionRequired` で断った。**sdkconfig を変えずに済む道は無い**。
- **メモリ**: mbedTLS の確保は、握手の最大 37,411 B・接続中 34,843 B・切った後に残る 2,904 B (PSA の共通の状態)。1 回の確保の最大は 16,765 B (受けの buffer)。今の設定 (`CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC=y`) のままだと全部が内蔵 RAM から取られる。**`mbedtls_platform_set_calloc_free()` で起動時に PSRAM へ回せる** (ESP-IDF の mbedTLS は `MBEDTLS_PLATFORM_MEMORY` 入りで組まれている。sdkconfig は変えない)。これで定常の内蔵 RAM の増分は 0 の見込み。残る心配はアプリの task の stack (内蔵 RAM) で、握手の分の深さが今の余り (P4-Nano のオセロ 4,560 B、AtomS3R の demo 約 6,300 B) に収まるかはステップ 2 で測る (3 章)。
- **sdkconfig の案**: 第一案は **`CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y` の 1 項目だけ** (NARYAv4・TAB5・ATOMS3R)。PSRAM への回しはコードで行う。代わりの案として `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` も表に挙げた (4 章)。
- **証明書の入れ方**: console の tar をそのまま端末の `/home/asterism/` に置き、端末の Ruby (asterism の portable の層) が tar を読む。picoruby の制約の中で書ける (Regexp・`pack`・`File.binread` を使わない)。遠隔の道具 (`/fs/get`) から鍵の入った tar を読めないようにする (5 章)。
- **手元の試し**: asterism-console の `deploy/local` の稽古の構成に、ルータの mTLS のポートを LAN に出す重ねの compose を 1 つ足し、ルータの証明書に PC の LAN の IP を入れる (6 章)。
- **ステップ 2 の見積もり**: gem に C で約 400-500 行 (TLS の口)、asterism に Ruby で約 100 行 (tar の読み)、fmruby-core に起動時の設定・devctl・CMake・アプリの選び方。flash は約 +50-65 KB (うち mbedTLS の TLS 1.3 が 35-50 KB)。繋ぐまでの見込みは P4 で 0.5-1 秒、AtomS3R で 1-2 秒 (7 章)。

ファーム・sdkconfig・sdkconfig.defaults は変えていない。端末は焼いていない。fmruby-core の `build/` は Linux の標準の構成 (x86-64) のまま。

## 2. zenoh-pico 1.10.1 の TLS

ピン: `ZENOH_PICO_PIN` の 1.10.1 (`e1ab223a`)。場所は fmruby-core の `vendor/zenoh-pico` (rake zenoh:setup の取り出し)。

### 2.1 あるもの

| もの | 場所 | 中身 |
|---|---|---|
| TLS のリンク | `src/link/unicast/tls.c` | `tls/<host>:<port>` の locator。リンクの関数 (open・read・read_exact・write・close)。session の設定の TLS の項目を locator の設定に重ねる (`_z_tls_merge_config`、47-83 行) |
| TLS の流れ (mbedTLS) | `src/link/transport/upper/tls_stream.c` | 証明書と鍵の読み、握手、読み書き。mbedTLS 2.x と 3.x の両方 (`MBEDTLS_VERSION_MAJOR` で分ける、130-135 行) |
| 設定の項目 | `include/zenoh-pico/config.h.in` 166-178 行、`include/zenoh-pico/link/config/tls.h` | `root_ca_certificate(_base64)`・`connect_certificate(_base64)`・`connect_private_key(_base64)`・`enable_mtls`・`verify_name_on_connect` ほか (listen の側も) |

- **mTLS**: `enable_mtls` が真のとき、クライアントの証明書と鍵を読み (`_z_tls_load_client_cert`、307-353 行)、`mbedtls_ssl_conf_own_cert` で出す (423-432 行)。
- **CA の指定**: `root_ca_certificate` (ファイル) か `_base64` (文字列)。無ければ接続しない (229-255 行)。ESP-IDF の証明書の束 (bundle) は使わない。
- **名前の確かめ**: 既定で必ず確かめる (`MBEDTLS_SSL_VERIFY_REQUIRED` と `mbedtls_ssl_set_hostname`、419-420・449 行)。`verify_name_on_connect=false` のときは名前の食い違いだけを許し、ほかの検証の失敗は断る (468-480 行)。名前は locator の host の部分。mbedTLS 3.6 は証明書の IP の SAN も照合する (`x509_crt_check_san_ip`) ので、IP で繋いでも確かめられる。
- base64 の値は「PEM の文字列を base64 にしたもの」。中で decode して PEM として読む (`_z_tls_decode_base64`、56-100 行。上限 64 KB)。CRuby の `credentials:` と同じ項目名なので、tar から作る値の形はそろう。

### 2.2 ESP-IDF の上でそのままは使えない理由

1. **ソケットの型に TLS の欄が無い**: `_z_sys_net_socket_t` の `_tls_sock` (TLS の流れへのポインタ) は `include/zenoh-pico/system/platform/unix.h` 51-53 行にしかない。ESP-IDF の `espidf.h` には無い。gem は自前の `include/zenoh_espidf_platform.h` を使っており、そこには TLS を入れると止める `#error` がある (TLS は想定外として組んでいた)。gem の header なので欄を足せる。
2. **中心のヘッダが mbedTLS の頭書きを読む**: `include/zenoh-pico/link/link.h` 34-35 行が `tls_stream.h` を読み、`tls_stream.h` が `mbedtls/ssl.h` ほかを読む (リンクの union の中に `_z_tls_socket_t` がある、136-137 行)。gem の作りでは zenoh-pico の中心は mruby のビルド (素の cross compiler、ESP-IDF の include なし) で組むので、そのままでは組めない。中心の側は `_z_tls_context_t` の中身を見ない (使うのは `_z_close_tls_socket`・`_z_tls_accept` などの関数だけ。`src/transport/manager.c`・`peer.c`・`unicast/accept.c`) ので、**gem の `include/` に同じ名前の `zenoh-pico/link/transport/tls_stream.h` を置き、context を不完全型 (中身を見せない形) にすれば中心は mbedTLS なしで組める**。gem の `include/` は zenoh-pico の `include/` より先に探される (`mrbgem.rake` の include_paths の順)。zenoh-pico の checkout は書き換えない。
3. **`tls_stream.c` は BSD ソケットと mbedTLS を直に使う** ので、ESP-IDF の component の側で組む (今の `ports/esp32/` の TCP の口と同じ扱い)。そのうえで、次の 2.3 の穴があるので、上流の `tls_stream.c` をそのまま組むのでなく、**gem の ESP-IDF 用の TLS の口 (`ports/esp32/zp_tls_esp32.c`、上流から派生、Apache-2.0)** として書き直す。esp-tls は使わない (esp-tls は自前の接続と時間の扱いを持ち、zenoh-pico のリンクの形に合わない。mbedTLS を直に使う上流の形のほうが差分が小さい)。

### 2.3 G1.5b と同じ穴が TLS の読みにあるか

gem の TCP の口 (`ports/esp32/zp_tcp_esp32.c`) が直している 6 点と、上流の TLS の流れを比べた。

| gem の TCP の口の直し | 上流の TLS の流れ | 判定 |
|---|---|---|
| 1. 平文の読みは待たない (poll の中で呼ばれる) | `_z_read_tls` は `mbedtls_ssl_read` → BIO の `recv(fd, ..., 0)` (`tls_stream.c` 166-179 行)。ソケットは blocking のままなので、**データが来るまで poll が止まる** (ルータの keep-alive が来るまで、最大で数秒) | 穴。BIO を待たない読み (`MSG_DONTWAIT`) にし、`MBEDTLS_ERR_SSL_WANT_READ` を「まだ無い」(SIZE_MAX) として返す。mbedTLS の中に復号済みの残りがあるときはソケットが読めなくても返す (`mbedtls_ssl_get_bytes_avail`) |
| 2. connect に時間の制限 | `_z_tcp_open` を呼ぶ (397 行) ので gem の口の制限がそのまま効く | 問題なし |
| 3. 握手の読み (`read_exact`) は時間の制限つき、失敗したソケットで終わる | TLS の握手 (`mbedtls_ssl_handshake` の輪、459-466 行) も zenoh の握手の読み (`_z_f_link_read_exact_tls`、`tls.c` 158-179 行) も時間の制限なし。黙った相手には**いつまでも止まる** | 穴 (TCP より悪い)。リセットされたソケットは `MBEDTLS_ERR_NET_RECV_FAILED` で終わるので G1.5b の「回り続ける」穴そのものは無いが、書き直すときに「失敗」を「まだ無い」と同じに扱うと同じ穴になる。期限つきの BIO (select で待つ) にして、握手の全体にも期限を置く |
| 4. 送りに時間の制限、切れたら shutdown | `_z_write_all_tls` は `WANT_WRITE` を 0 として回り続け、blocking の send は WiFi が消えると lwIP が諦めるまで (分の単位) 止まる (692-720 行) | 穴。gem の TCP の口と同じ期限と shutdown にする (`zp_tcp_socket_closed` がそれを見る) |
| 5. SO_LINGER なし | TCP の close を使う (665 行)。ただし先に `mbedtls_ssl_close_notify` を blocking で送る (662 行) | 小さな穴。close_notify は待たない送りで 1 回だけ試す |
| 6. 閉じたソケットに気づく (`zp_tcp_socket_closed`、MSG_PEEK) | リンクのソケットは `_socket._tls._sock` (`link.c` 254-256 行) で `_fd` を持つので、gem の今の確かめがそのまま効く | 問題なし |

- 戻り値の約束が逆: 上流の `_z_read_tls` は「まだ無い」を 0、「閉じた」を SIZE_MAX で返す (680-686 行)。gem の TCP の口は「まだ無い」が SIZE_MAX、「閉じた」が 0。client の読み (`src/transport/unicast/read.c` の `_z_unicast_client_read`) はどちらでも動くが、peer の読みは 0 を「閉じた」と取る。gem の TLS の口は TCP の口の約束にそろえる。
- **つなぎ直し** (`Z_FEATURE_AUTO_RECONNECT`) は、リンクの種類に関係なく `_z_open` を 1 秒ごとに試す。TLS では 1 回の試しに握手が入るので、ルータが応じる (TCP が通る) のに握手が失敗する間は、試すたびに握手の時間 (7 章) だけ poll が止まる。ルータが居ない (拒まれる・リセット) ときは TCP の段ですぐ返るので今と同じ。
- **TLS 1.3 で証明書が断られるとき**: クライアントの証明書をルータが断ると、端末の側では握手が終わった後の最初の読みで警告 (alert) が来る。手元の試し (3.2) では、zenoh-pico は警告を読みの失敗として受け、zenoh の握手の期限 (10 秒) まで試し直してから `-120` で諦めた。落ちはしない。gem の口では最初の読みの失敗で接続を閉じ、つなぎ直しの 1 秒の間隔に任せる。

### 2.4 TLS の版 (zenohd の側)

- zenoh-link-tls 1.10.1 (`io/zenoh-links/zenoh-link-tls/src/utils.rs`) は、`enable_mtls` のとき server・client の両方を `builder_with_protocol_versions(&[&TLS13])` で作る。設定で TLS 1.2 を許す項目は無い。
- 手元の試し: TLS 1.2 だけの mbedTLS 3.6.5 で繋ぐと、zenohd のログに `peer is incompatible: SupportedVersionsExtensionRequired` が出て断られた。TLS 1.3 を入れた mbedTLS では繋がった。
- mbedTLS 3.6 の TLS 1.3 は PSA の暗号の層を使うので、最初の握手より前に `psa_crypto_init()` を 1 回呼ぶ必要がある (ESP-IDF の esp-tls は自分で呼んでいる。zenoh-pico の上流は呼ばない)。gem の口と fmruby-core の起動の両方で呼ぶ (2 回目以降は何もしない)。

## 3. メモリ

### 3.1 今の ESP-IDF の設定 (3 つの構成で同じ)

`config/sdkconfig.defaults.{p4,naryav4,atoms3r}` には mbedTLS の項目が無く、ESP-IDF v5.5.4 の既定のまま (defaults から設定を解いて確かめた。sdkconfig は作っていない)。

| 項目 | 値 |
|---|---|
| `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC` | y (mbedTLS の確保は全部内蔵 RAM) |
| `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` / `OUT_CONTENT_LEN` | 16384 / 4096 (`ASYMMETRIC_CONTENT_LEN=y`) |
| `CONFIG_MBEDTLS_DYNAMIC_BUFFER` | n |
| `CONFIG_MBEDTLS_SSL_PROTO_TLS1_2` / `TLS1_3` | y / n |
| `CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` | y (TLS 1.3 に要る) |
| `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` | y (FULL。zenoh の TLS では使わない) |
| `CONFIG_MBEDTLS_HAVE_TIME_DATE` | n (端末は証明書の期限を見ない) |
| ハードウェアの暗号 | P4: AES・GCM・SHA・MPI・ECC (ECDSA の検証も)。S3: AES・SHA・MPI (ECC はソフト) |
| PSRAM | `CONFIG_SPIRAM_USE_CAPS_ALLOC=y` (malloc は内蔵 RAM)。S3 は `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` |

- mbedTLS はすでに 3 つの構成のファームに入っている (picoruby-socket の SSLSocket、WiFi の supplicant)。zenoh の TLS のために足すのは TLS 1.3 の部分と gem の口だけ。
- ESP-IDF の mbedTLS は `MBEDTLS_PLATFORM_MEMORY` を定義して組まれ、確保の関数は `MBEDTLS_PLATFORM_STD_CALLOC` (既定の値) として指定されている (`components/mbedtls/port/include/mbedtls/esp_config.h`)。マクロで固定していないので、**`mbedtls_platform_set_calloc_free()` で実行時に差し替えられる**。

### 3.2 手元で測った確保の量

組み方: ESP-IDF v5.5.4 の docker image の中で、同梱の mbedTLS 3.6.5 を ESP-IDF の buffer の大きさ (受け 16384・送り 4096) と TLS 1.3 入りで host 向けに組み、zenoh-pico 1.10.1 を `Z_FEATURE_LINK_TLS=1` で組んだ。mTLS の zenohd 1.10.1 (docker) に client で繋ぎ、mbedTLS の確保を全部数える確保の関数 (`mbedtls_platform_set_calloc_free`) で測った。証明書は console と同じ ECDSA P-256 (CA・ルータ・端末 `fmruby-aaaaaa`)。作業はスクラッチパッドの中だけで、手順と道具はコミットしていない。

| 場面 | mbedTLS の確保 |
|---|---|
| 握手の最大 | 37,411 B |
| 繋がっている間 (問い合わせ 1 回の往復の後も同じ) | 34,843 B |
| 切った後に残る (PSA の共通の状態、1 回だけ) | 2,904 B |
| 1 回の確保の最大 | 16,765 B (受けの buffer) |
| 握手 1 回の確保の回数 | 約 10,000 回 (ほとんどは小さな一時の確保) |

- 内訳の見当: 受け 16.8 KB + 送り 4.5 KB = 21.3 KB が buffer。残り約 13.5 KB が CA・自分の証明書と鍵・相手の証明書 (TLS 1.3 は持ち続ける)・鍵の材料・PSA の鍵の欄。
- 測ったのは x86-64。端末 (32 bit) では構造体が小さくなるので、buffer 以外は少し減る (見積もり: 握手の最大 約 36 KB、繋がっている間 約 33 KB)。
- 確保の回数が多いので、内蔵 RAM から取ると細切れを作りやすい。PSRAM に回すと内蔵 RAM の細切れも避けられる。

mbedTLS の外の確保:

| もの | 取る場所 | 大きさの見積もり |
|---|---|---|
| zenoh-pico の TLS の context (`_z_tls_context_t`: ssl・config・entropy・drbg・証明書と鍵の入れ物) | gem の `z_malloc` = PSRAM | 2-3 KB |
| 設定の base64 の値 (CA 574 B・証明書 680 B・鍵 241 B の PEM の base64) と decode の一時 | `z_malloc` = PSRAM | 約 2 KB + 一時 1 KB |
| lwIP のソケット | 内蔵 RAM | 平文の TCP と同じ 1 本 (今と増減なし) |
| ハードウェアの暗号の一時 (S3 の AES の DMA で PSRAM の buffer を扱うときの内部の中継の buffer など) | 内蔵 RAM (一時) | 1-2 KB の見込み。ステップ 2 で握手と送受の最中の最低値として測る |

### 3.3 内蔵 RAM と PSRAM の分け方 (端末ごと)

| | P4-Nano (NARYAv4) | AtomS3R |
|---|---|---|
| 今の内蔵 RAM の空き (平文で繋がった定常) | 約 189.7 KB (オセロ、g1_5b_p4) | 約 135.3 KB (demo、H3) |
| **今の設定のまま TLS** (mbedTLS は内蔵 RAM) | 繋がっている間 約 -33 KB、握手の最大 約 -36 KB (+細切れ) | 同じ量 (空きの 1/4 強) |
| **PSRAM に回したとき**: 内蔵 RAM の定常 | 0 (ソケットは今と同じ) | 0 |
| 同: 内蔵 RAM の一時 | ハードウェアの暗号の一時のみ (要測定) | 同 (S3 は AES の DMA の中継が出やすい) |
| 同: PSRAM | +約 38 KB (接続 1 本) + 2.9 KB (1 回だけ) | 同じ。PSRAM の空き 1.27 MB に対して小さい |
| アプリの task の stack (内蔵 RAM) | 今の余り 4,560 B (オセロ) | 今の余り 約 6,300 B (demo) |

- **stack が最大の心配**: 握手は poll の中 (アプリの task) で走る (`z_open` とつなぎ直しの `_z_open`)。手元の x86-64 では、mbedTLS だけの TLS 1.3 + mTLS の握手の stack の最大が 8,888 B、zenoh-pico の `z_open` 全体は TCP でも TLS でも 11,112 B (同じ値で、握手は zenoh の握手の深い所より浅かった)。32 bit では小さくなるが、端末の今の余りに収まるかは測るまで言えない。ステップ 2 で `fmrb_task:` の high-water を TLS ありとなしで比べる。足りなければ stack を増やすことになり、それは内蔵 RAM の増加なので、そこで止まって相談する (指示書の止まる条件)。
- P4 は WiFi の supplicant が副チップ (C6) の側にあるので、mbedTLS の確保の回しは P4 の側のアプリの TLS (SSLSocket と zenoh) だけに効く。S3 では WiFi の supplicant の暗号の確保も PSRAM に回る (S3 はすでに WiFi と lwIP の確保を PSRAM に回している構成なので、同じ方向)。

## 4. sdkconfig の案

**変更なしの道は無い** (2.4: zenohd が mTLS で TLS 1.3 しか受けない)。要る変更を最小にした案を第一案にする。書き換えはしていない。

### 4.1 第一案 (推奨): TLS 1.3 の 1 項目だけを変え、PSRAM への回しはコードで行う

| 項目 | 対象 | 今の値 | 案の値 | 理由 | 見積もった増減 |
|---|---|---|---|---|---|
| `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3` | NARYAv4・TAB5・ATOMS3R (`sdkconfig.defaults.naryav4` / `.p4` / `.atoms3r`) | n (既定) | **y** | zenohd 1.10.1 は mTLS のとき TLS 1.3 だけを受ける | flash +35-50 KB (TLS 1.3 の client・server・鍵の導出・HKDF。x86-64 の object で 76.5 KB、端末では 0.5-0.65 倍と見た)。静的な内蔵 RAM 0。依存の `MBEDTLS_HKDF_C` は自動で入る。下位の項目 (`TLS1_3_COMPATIBILITY_MODE`・`KEXM_*`) は既定の y のまま |

コードで行うこと (sdkconfig ではない):

- fmruby-core の起動 (ESP-IDF の platform の初期化、WiFi より前) で `mbedtls_platform_set_calloc_free()` を 1 回呼び、mbedTLS の確保を **PSRAM 優先 (取れないときだけ内蔵 RAM)** にする。解放は `heap_caps_free` (どちらの heap のものも解放できるので、差し替え前の確保も安全に解放できる)。同じ所で `psa_crypto_init()` を 1 回呼ぶ。
- ほかへの影響:
  - picoruby の SSLSocket (Net::HTTP の HTTPS、tts サービスのクラウド) の確保も PSRAM に回る。内蔵 RAM は減る方向。TLS 1.3 を話すサーバとは TLS 1.3 で繋ぐようになる。SSLSocket の esp32 の口は `psa_crypto_init()` を呼ばないので、起動での呼び出しが無いと HTTPS が握手で失敗する。起動で呼ぶのはそのため。ステップ 2 で Net Test アプリと tts で HTTPS の回帰を見る。
  - S3 の WiFi の supplicant の確保も PSRAM に回る。
  - Retro (NARYAv3、`sdkconfig.defaults.n16r8`) は sdkconfig を変えない。起動での回しは S3 の共通の処理に置くので Retro にも効くが、Retro は Asterism を入れない (H3) ので zenoh の TLS は無い。Retro に効かせたくなければ `FMRB_ASTERISM` のときだけにする (ステップ 2 で決める。案: 全 ESP の構成で同じにする方が分かりやすい)。
- 第一案の弱み: 「mbedTLS は PSRAM」という決まりが sdkconfig でなくコードにあるので、見えにくい。起動のログに 1 行出す。

### 4.2 代わりの案: sdkconfig で PSRAM に回す

| 項目 | 対象 | 今の値 | 案の値 | 理由 | 見積もった増減 |
|---|---|---|---|---|---|
| `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3` | 同上 | n | y | 同上 | 同上 |
| `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` (選択肢 `INTERNAL_MEM_ALLOC` の代わり) | 同上 | `INTERNAL_MEM_ALLOC` | `EXTERNAL_MEM_ALLOC` | mbedTLS の確保を全部 PSRAM に。ESP-IDF の正式な選択肢で、設定に見える | 内蔵 RAM: 接続中 -33 KB 相当を回避 (増分 0)。PSRAM +38 KB/接続。PSRAM が尽きたときに内蔵 RAM へ戻る道は無い |

- 効き目は第一案のコードと同じ (全体に効く)。違いは見え方と、PSRAM が尽きたときに内蔵 RAM へ戻れるかだけ。

### 4.3 検討して案に入れなかったもの

| 項目 | 入れない理由 |
|---|---|
| `SSL_IN_CONTENT_LEN` を小さく | zenohd (rustls) は最大 16 KB の record を送り、rustls は record の大きさを決める拡張 (max_fragment_length) に応じない。小さくすると大きな通信で読めなくなる。PSRAM に回せば大きさは問題でない |
| `SSL_OUT_CONTENT_LEN` | 今の 4096 で足りる (送りの record を分けるだけ) |
| `DYNAMIC_BUFFER` | 使っていない間の buffer を返す仕組み。PSRAM に回すなら要らない。TLS 1.3 の session ticket の扱いが変わるので触らない |
| 暗号の絞り込み (ECDSA P-256 と AES-GCM だけに) | flash は減るが、SSLSocket で一般の HTTPS サイトへ繋ぐ用途を壊す |
| 証明書の束 (`CERTIFICATE_BUNDLE`) を外す | zenoh の TLS では使わないが、SSLSocket の既定の HTTPS が使っている (約 69 KB) |
| `HAVE_TIME_DATE` | 入れると端末が相手の証明書の期限を見るが、時刻が合う前 (起動直後・RTC なしの AtomS3R) は全部の握手が失敗する。端末の証明書の期限切れはルータの側 (rustls) が断るので、受け入れ条件 4 は満たせる。ルータの証明書の期限切れに端末が気づかない点は残る (5.4) |
| `CONFIG_MBEDTLS_TLS_SERVER` を外して flash を減らす | picoruby の TCPServer + SSL の可能性を消す。減る量 (TLS 1.3 の server 部分 10-15 KB) に見合わない |

## 5. 端末への証明書の入れ方

### 5.1 案の比較

| 案 | 入れ方 | 良い点 | 弱い点 |
|---|---|---|---|
| **A (推奨)**: tar をそのまま置き、Ruby が読む | console の「Save all (.tar)」の tar を、端末の `/home/asterism/<名前>.tar` に置く (`tab5_fs put`・`ruby tools/fmrb_rd_fs.rb`・ブラウザ版なら web_fs) | CRuby の `credentials:` と同じ考え。ファイル 1 つ。宛先とノード名も `asterism.json` から入る | Ruby で tar を読む小さなコードが要る |
| B: tar を PC で展開して 3 つのファイルを置く | `ca.pem`・`<名前>.pem`・`<名前>.key` を `/home/asterism/` に | 読みの作りが無い | 置くファイルが増え、宛先は別に書く。取り違えやすい |
| C: tar を C (gem) で読む | A と同じ置き方 | Ruby の VM の確保を通らない | gem の C が tar とファイルの読みを持つことになり、portable の層 (asterism) と二重になる |

### 5.2 案 A の作り

- **読み手**: asterism の portable の層に `Asterism::Credentials` の mruby 版 (または同じ名前で中を分ける)。ustar は 512 B の header (名前 0-99、大きさ 124-135 の 8 進、型 156) と 512 B に詰めた中身の並びなので、`File.open(path, "r") { |f| f.read }` で全部読み (8 KB)、`byteslice`・`getbyte`・`to_i(8)` で切り出せる。Regexp・`pack`・`File.binread` は使わない (memory の既知の制約。mruby の `String#length` は UTF-8 の文字数なので、位置は `bytesize` で数える)。`asterism.json` は picoruby-json で読む。
- **渡し方**: 読み手が PEM の 3 つを取り出し、`Asterism::Zenoh::Session.open(locator, tls: { ca:, cert:, key:, verify_name: true })` に渡す。gem の C が base64 にして zenoh-pico の `*_base64` の項目に入れる (zenoh-pico の C の文字列の設定は PEM の改行をそのまま持てないため。base64 の符号化は 20 行ほどで、mbedTLS の頭書きは要らない)。一時ファイルは作らない (つなぎ直しは zenoh-pico が持つ設定を使う)。
- **宛先とノード名**: `Asterism.connect(credentials: path)` は、locator を渡さなければ `asterism.json` の `router` (例 `tls/router.example.org:7448`) を、`node:` を渡さなければ `name` を使う (CRuby と同じ)。Family mruby の端末のノード名は mDNS の名前 `fmruby-XXXXXX` なので、console で端末を登録するときの名前もそれにする。tar の名前と端末の名前が違うときは、tar の名前 (= 証明書の名前、ルータの ACL が見る名前) を使い、警告を 1 行出す。
- **TLS か平文かの選び方** (端末の設定): `/home/zenoh_echo.txt` の 1 行目を今どおり宛先とし、`tls/` で始まれば TLS、`tcp/` なら今の平文。TLS のときは `/home/asterism/` の tar を使う (1 つだけ置く。複数あれば名前の順の最初と、警告)。宛先の行が無く tar だけがあるときは tar の `router` に繋ぐ。これで「家のルータ経由の平文」と「クラウドへ直接の TLS」を、ファイルの置き換えだけで切り替えられ、今の形はそのまま動く。
- **期限**: `asterism.json` の `not_after` を、時刻が合っていれば (WiFi の後の時刻合わせの後) 起動のログで見て、30 日前から警告する (端末の mbedTLS は期限を見ないので、その代わりの目安)。

### 5.3 鍵の置き場所と遠隔の道具

- 端末のファイルにはアクセス権が無い。端末の上のどのアプリも `/home/asterism/` の tar を読める。これは受け入れる (端末で動くアプリは持ち主のもの)。
- 遠隔の道具 (`FMRB_DEV_REMOTE_CTL`、既定 ON・リリースのビルドで OFF・認証なし) の `/fs/get` は、同じ LAN の誰でも鍵を取り出せる。**devctl の `/fs/get` は `/home/asterism/` の下を断る** (`/fs/put` (入れる)・`/fs/del`・`/fs/list` (名前と大きさだけ) は許す)。小さな C の変更で、Tab5 (rd_http) と S3 (devctl_task) の両方の経路に入れる。MCP の `tab5_fs get` でも読めなくなることを tab5_fs の description に書く。
- 鍵が漏れたと思ったら、console でその行を止める (W2 の台帳から ACL を作り直す) のが戻し方。手順書 (ステップ 2 の文書) に書く。
- `rake flash` (全体の焼き直し) は `/home` を消すので tar も消える (既知)。アプリの区画だけの焼き直しでは残る。
- ブラウザ版 (wasm) は今どおり `wss://` で、この tar は使わない。

### 5.4 残る弱さ

- 端末はルータの証明書の期限を見ない (`HAVE_TIME_DATE=n`)。名前と CA で相手を確かめるので、ほかの相手になりすまされることは無いが、期限切れのルータの証明書でも繋ぐ。console の CA で作った証明書の入れ替えの運用で補う。

## 6. 手元の試しの構成

asterism-console の `deploy/local` (install.md の通し稽古) をそのまま使い、端末から届くようにするための差は 2 つだけ。console のリポジトリは変えない (この作業の範囲の外。重ねの compose はスクラッチパッドか fmruby-core の作業の中に置く)。

1. **ルータの証明書に PC の LAN の IP を入れる**: `deploy/local/rehearse prepare` の後、`deploy/local/.env` の `ASTERISM_RELAY_EXTRA_IPS=127.0.0.1` を `127.0.0.1,192.0.2.10` (PC の LAN の IP) にしてから `bootstrap` する。`bin/asterism setup` は DNS 名の変化でしか証明書を作り直さない (`Relay::LocalIdentity.stale?` に IP を渡していない) ので、すでに volume があるときは `VOLUMES=1 rehearse down` で作り直す。
2. **ルータの mTLS のポートを LAN に出す**: 稽古の compose は 127.0.0.1:17448 にしか出さないので、重ねの compose を 1 つ足す:

   ```yaml
   # g2a-lan.yml (not part of asterism-console): the router's mutual TLS on the LAN
   services:
     zenohd:
       ports: !override
         - "127.0.0.1:17448:7448"
         - "${ROUTER_LAN_BIND:-0.0.0.0}:17448:7448"
   ```

   `docker compose -p asterism-rehearsal -f deploy/vps/docker-compose.yml -f deploy/local/docker-compose.rehearsal.yml -f g2a-lan.yml --env-file deploy/local/.env up -d` (rehearse の up と同じ並びに 1 つ足す)。WSL2 の Docker Desktop では、H3・G1.5b の 7447 と同じく Windows の側のファイアウォールでポートを許す必要があることがある。

手順:

1. console で端末の行を作る: client、名前 `fmruby-aaaaaa` (端末の mDNS の名前)、役割はアプリに合わせる (P4-Nano のオセロは `match`、AtomS3R の `asterism_demo` は app `demo` を許す役割を作る)。発行して「Save all (.tar)」、Apply。
2. 端末へ: `tab5_fs put` (または `ruby tools/fmrb_rd_fs.rb <IP>` の対話の put) で `/home/asterism/fmruby-aaaaaa.tar`、`/home/zenoh_echo.txt` に `tls/192.0.2.10:17448`。
3. アプリを起動 (`tab5_app` / `/app/launch`)。シリアルは serial_start で開いたまま (Tab5 は開くとリセットされる)。
4. 確かめ: console の図に `fmruby-aaaaaa` が出る、オセロ・demo が動く、ルータの起動し直し (`docker compose ... restart zenohd`) から戻る、周期のダンプ (`IRAM free:`・`fmrb_task:`) を TLS ありとなしで比べる。
5. 断られる場合: tar を消す (証明書なし)、別の CA の tar (稽古の CA を作り直す前の tar)、期限の短い証明書 (console の発行で日数を 1 にして待つか、手で作る)。

PC の上で先に確かめられること: 3.2 と同じ形の host のビルド (mbedTLS 3.6.5 + zenoh-pico) で、gem の TLS の口の POSIX 版を組めば、端末に焼く前に握手・期限・断られ方を試せる。ステップ 2 で gem の `rake test_tls` (docker の mTLS の zenohd に繋ぐ) にするかは、POSIX の側にも mbedTLS が要る (Linux の sim は OpenSSL を使っている) ので、手間を見て決める。

### 6.1 この調べで手元で確かめたこと

zenohd 1.10.1 (mTLS、`listen tls/0.0.0.0:7448`) に、host 向けの mbedTLS 3.6.5 (TLS 1.3 入り) + zenoh-pico 1.10.1 の client で:

| 場合 | 結果 |
|---|---|
| 正しい mTLS (同じ CA の端末の証明書、IP で繋ぐ) | 繋がる。ルータの管理の空間に問い合わせて 1 件返る |
| クライアントの証明書なし (`enable_mtls` なし) | 握手の後の最初の読みで alert (-0x7780)、`z_open` は 10 秒後に -120 で諦める。落ちない |
| 別の CA のクライアントの証明書 | 握手で失敗 (-0x004e)、すぐ -102 |
| ルータを別の CA で確かめる (端末の CA が違う) | 握手で失敗 (-0x2700 = 証明書の検証失敗)、すぐ -102 |
| 名前の食い違い (証明書に無い名前で繋ぐ) | 握手で失敗 (-0x2700) |
| 名前の食い違い + `verify_name_on_connect=false` | 繋がる (名前だけを許す) |
| TLS 1.2 だけの mbedTLS | zenohd が `SupportedVersionsExtensionRequired` で断る |

期限切れの証明書は試していない (ステップ 2 で、端末の証明書の期限切れはルータが断ることを確かめる)。

## 7. ステップ 2 の作業の見積もり

| リポジトリ | 足すもの | 大きさの見当 |
|---|---|---|
| picoruby-asterism-zenoh | `include/zenoh-pico/link/transport/tls_stream.h` (context を見せない同じ名前の header)、`include/zenoh_espidf_platform.h` に `_tls_sock` の欄と `#error` の外し、`include/zenoh_generic_config.h` で ESP-IDF のとき `Z_FEATURE_LINK_TLS=1`、`ports/esp32/zp_tls_esp32.c` (上流の `tls_stream.c` から派生: 待たない BIO、期限つきの握手と読み、送りの期限と shutdown、約束を TCP の口にそろえる、`psa_crypto_init`)、`mrbgem.rake` で ESP-IDF のとき上流の `tls_stream.c` を外す、`src/zenoh.c` の `Session.open` に `tls:` (PEM を受けて base64 に)、README・CHANGELOG (版は上げない) | C 約 400-500 行 |
| asterism | mruby で動く tar の読み手と `Asterism.connect(credentials:)` の portable の層 (CRuby の `Gem::Package::TarReader` の版はそのまま)、試験 (CRuby と mruby で同じ tar を読む) | Ruby 約 100 行 + 試験 |
| fmruby-core | 承認された sdkconfig の項目、起動での `mbedtls_platform_set_calloc_free` と `psa_crypto_init`、`components/picoruby-esp32/CMakeLists.txt` に TLS の口のソース、devctl の `/fs/get` の断り (2 経路)、オセロと `asterism_demo` の宛先の選び方 (`zenoh_echo.txt` の `tls/` と tar)、ピンの上げ (push の後)、この報告の後半 | C 約 50 行、Ruby 約 40 行 |

- **flash**: TLS 1.3 の mbedTLS 35-50 KB、zenoh-pico の TLS のリンクと gem の口 6-10 KB、tar の読み手のバイトコード 3-5 KB。合わせて **約 +50-65 KB**。P4 (NARYAv4・TAB5) の 7M の区画の残り約 814 KB (11%)、AtomS3R の 5M の区画の残り約 1.17 MB (22%) に収まる。
- **繋ぐまでの時間**: 握手は 1 往復の TLS 1.3 + zenoh の握手。暗号は ECDHE の鍵作り・共有、相手の証明書と CertificateVerify の検証 2 回、自分の CertificateVerify の署名 1 回で、P-256 の点の掛け算が 7 回ほど。P4 は ECC のハードウェアで 0.2-0.4 秒、S3 はソフト (MPI のハードウェアのみ) で 0.7-1.5 秒と見る。平文の今の接続 (起動直後 383 ms、開き直し 69 ms、H3) に足して、**P4 で 0.5-1 秒、AtomS3R で 1-2 秒**。クラウドのルータ (往復 20-50 ms) なら +0.1-0.2 秒。
- **つなぎ直し**: ルータが戻るまでは TCP の段ですぐ失敗し、今と同じ 1 秒ごと。戻った最初の試しで握手の時間だけ poll が止まる (画面が 0.5-1.5 秒止まる)。
- **止まる見込みのある所**: アプリの task の stack (3.3)。握手を入れて余りが足りなければ、stack を増やす (内蔵 RAM の増加) か、握手を別の所で走らせる作り替えになるので、そこで止まって相談する。

## 8. ユーザに決めてほしいこと

1. **sdkconfig**: `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y` を `sdkconfig.defaults.naryav4`・`.p4` (TAB5)・`.atoms3r` に入れてよいか (必須)。
2. **PSRAM への回し**: 第一案 (起動のコードで `mbedtls_platform_set_calloc_free`、PSRAM 優先・尽きたら内蔵 RAM) か、代わりの案 (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` を同じ 3 つに) か。推奨は第一案 (sdkconfig の変更を 1 項目に抑え、PSRAM が尽きても止まらない)。
3. **起動での回しを Retro (NARYAv3) にも効かせるか**: 推奨は全 ESP の構成で同じ (SSLSocket の内蔵 RAM も減る)。
4. **証明書の入れ方**: 案 A (tar を `/home/asterism/` に置き、Ruby が読む) と、devctl の `/fs/get` で `/home/asterism/` を断ること。

## 9. ステップ 2 の結果の要約

ユーザの決定 (2026-10-13): 第一案ではなく **4.2 の案** (mbedTLS の確保の場所を sdkconfig で PSRAM にする)。ほかは 4 章・5 章の推奨どおり。

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| picoruby-asterism-zenoh | main | `5645b13` | ESP-IDF の TLS のリンクと流れ (`src/zp_tls_link.c`・`ports/esp32/zp_tls_esp32.c`・中身を隠した `tls_stream.h`)、`Session.open(config:)` で TLS の設定、`Asterism::Zenoh::TLS`、host 試験、README・CHANGELOG (Unreleased) |
| asterism | main | `2d9ecd4` | `Asterism::Credentials` を `mrblib/` に移し、端末の Ruby で tar を読む。`Asterism.connect(credentials:)` が端末でも使える。`rake test:mruby` に照合の試験 |
| fmruby-core | feature/asterism-g2a | `c637a8ff`・`87603142`・この報告 | sdkconfig.defaults (3 つ)、mbedTLS の静的な状態を PSRAM に (`main/mbedtls_psram.lf`)、SSLSocket の口で PSA の初期化、`FmrbAsterism.credentials_for`、オセロと `asterism_demo` の選び方、devctl の `/fs/get` の断り |

版はどれも上げていない。push していない。`lib/add/ASTERISM_PIN`・`PICORUBY_ASTERISM_ZENOH_PIN` は動かしていない (指すコミットが push されていないため)。ビルドは `ASTERISM_DIR=../asterism PICORUBY_ASTERISM_ZENOH_DIR=../picoruby-asterism-zenoh` で手元の checkout から行った。push の後でピンを上げる。

| 受け入れ条件 | 結果 |
|---|---|
| 1. P4-Nano と AtomS3R が家のルータなしで繋がり、オセロ・`asterism_demo` が動く。console の図に出る | P4-Nano は合格 (11.1)。AtomS3R は未確認 (PC に繋がっておらず焼けない。ビルドと大きさだけ、11.3)。console の図は見ていない (11.1 の代わりの確かめ) |
| 2. 内蔵 RAM の増分 0、PSRAM・flash の増え | P4-Nano 合格 (10 章)。AtomS3R は静的な分だけ |
| 3. 繋ぐまでの時間、つなぎ直しの時間 | 0.50-0.65 秒、1.5-2.7 秒 (11.2) |
| 4. 証明書が無い・期限切れ・別の CA で断られて落ちない | 合格 (11.4) |
| 5. 家のルータ経由 (平文) がそのまま動く | 合格 (同じファームで `tcp/` を交互に、10 章) |

## 10. 作ったもの

### 10.1 sdkconfig (承認された 3 項目、`config/sdkconfig.defaults.naryav4`・`.p4`・`.atoms3r` の末尾)

```
CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y
# CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC is not set
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
```

- 選択肢の組 (INTERNAL / EXTERNAL / DEFAULT / CUSTOM) の 1 つを選ぶので、INTERNAL を外す行と EXTERNAL を選ぶ行の 2 行で書いた。解いた設定 (`sdkconfig`) で `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`・`CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y` を確かめた。下位の TLS 1.3 の項目は既定 (y) のまま。
- Retro (`n16r8`・`n8r8`) と Linux は変えていない。

### 10.2 mbedTLS の静的な状態を PSRAM に (sdkconfig ではない)

TLS 1.3 を入れると PSA の中心 (`psa_crypto.c` の `global_data` 508 B、`psa_crypto_slot_management.c` の 184 B) が初めてリンクされ、暗号の組の表 (`ssl_ciphersuites.c` の `supported_ciphersuites`) が 16 B 伸びる。どれも内蔵 RAM の .bss で、最初のビルドでは静的な内蔵 RAM が develop より **708 B 増えた**。`main/mbedtls_psram.lf` (リンカの割り付け、`CONFIG_MBEDTLS_SSL_PROTO_TLS1_3` と .bss を PSRAM に置ける設定のときだけ main が渡す) で、この 3 つの object の .bss を PSRAM に置いた。esp_wifi の `extram_bss` の割り付けは WiFi が副チップの P4 (esp_hosted) には無いので、自前の割り付けの名前にした。割り込みからは触らない (task だけ)。結果、静的な内蔵 RAM は develop より 240 B **減った** (前からあった暗号の組の表 236 B も PSRAM に移ったため)。

### 10.3 PSA の初期化は最初の TLS で

mbedTLS 3.x の TLS 1.3 は PSA を使い、`psa_crypto_init()` を誰かが先に呼ぶ必要がある (esp-tls は自分で呼ぶ。picoruby の SSLSocket の口と zenoh-pico は呼ばない)。最初は起動 (WiFi の開始) で呼んだが、何もしていない状態の内蔵 RAM が約 400 B 減った (PSA が作る mutex)。そこで起動では呼ばず、**SSLSocket の口の `SSLContext_create` (`lib/patch/picoruby-socket/ports/esp32/ssl_socket.c`) と gem の TLS の流れの握手の前**で呼ぶ形にした。TLS を使わない端末は何も払わない。

### 10.4 TLS の口 (picoruby-asterism-zenoh)

- 2.2 の作り方のとおり。zenoh-pico の `link/unicast/tls.c` と `upper/tls_stream.c` は ESP-IDF では組まず、gem の `src/zp_tls_link.c` (mruby のビルドで組む、mbedTLS の頭書き不要) と `ports/esp32/zp_tls_esp32.c` (ESP-IDF の component で組む) に置き換えた。
- 2.3 の穴の直し: mbedTLS の下のソケットの読み書きは `MSG_DONTWAIT` で待たない。待つ所は select で期限つき (握手 10 秒、握手の読み 1 回の socket timeout と 5 秒、送り 3 秒)。mbedTLS の「読みたい / 書きたい」以外の失敗 (リセット、警告、壊れた record) は、読みを 0 (閉じた) で終え、ソケットを shutdown する。`zp_tcp_socket_closed` がそれを見て zenoh-pico の失敗の道 (つなぎ直し) に渡す。「まだ無い」と同じに扱って回り続ける形 (g1_5b_p4 のリセットの穴) にはしていない。
- 証明書と鍵は base64 だけ (ファイルのパスは断る)。TLS で待ち受ける peer は断る。
- 握手のたびに 1 行: `zenoh: TLS handshake with <host> (TLSv1.3) took N ms; internal RAM <前> -> low <握手の段の間の最低> -> <後>`。
- Ruby: `Session.open(locator, config: {...})` は TLS の 5 項目だけを CRuby の binding と同じ zenoh の名前で受ける。ほかの項目は `ArgumentError`、TLS の無い構成 (POSIX) では `Asterism::Zenoh::Error`。
- host 試験 (`rake test`、POSIX): 80 項目すべて通る。`Asterism::Zenoh::TLS` が偽、TLS の設定を渡すと Error、TLS 以外の config は ArgumentError を足した。

### 10.5 証明書の読み (asterism) と端末の選び方 (fmruby-core)

- `Asterism::Credentials` を `mrblib/credentials.rb` に移した (CRuby も同じファイルを読む)。tar は `byteslice`・`getbyte`・`to_i(8)` で分け、base64 は `pack` なしで作り、`asterism.json` は JSON があれば JSON、無ければ文字列の項目だけを読む小さな読み手。CRuby の読み (標準の tar の読み手、ファイルの権限の警告、鍵の組の確かめ) は class に prepend した module に残した (メソッドの定義し直しの警告が出ない)。
- `Asterism.connect(locator = nil, node: nil, ..., credentials:)`: 端末でも `credentials:` が使え、宛先と `node:` は tar から入る。
- 試験: `rake test:mruby` に `host_test/test_credentials.rb` (CRuby で作った tar を mruby で読み、名前・宛先・base64 の 3 つが CRuby と一致、壊れた tar・無いファイル・String の config を断る) を足した。15 項目通る。CRuby の既定の試験 (`rake`) も全部通る。
- fmruby-core: `FmrbAsterism.credentials_for(locator)` は `tls/` の宛先なら `/home/asterism/` の最初の `.tar` を読む (無ければ `Asterism::Error`)。オセロと `asterism_demo` は、tar があれば証明書の名前をノード ID にし (ログに 1 行)、`credentials:` で繋ぐ。`tcp/` は今までどおり。
- devctl: `/fs/get` は `/home/asterism` の下を 403 で断る (`/home//./asterism/...` のような書き方も同じ)。put・list・del は使える。実機で確かめた (`curl` で 403、list は名前と大きさが出る)。MCP の tab5_fs の description への追記は親のリポジトリなのでしていない。

## 11. 確かめ (P4-Nano、NARYAv4、アプリの区画だけを焼いた)

構成: 親の compose の `docker-compose.relay.yml` を、スクラッチパッドの CA と証明書 (console と同じ形の tar を Ruby の OpenSSL で作った) で起動した。zenohd-cloud (mTLS、ACL なし) を PC の LAN の 7447 に出し (PC のファイアウォールが通すのが 7447 だけだったため。17448 は端末から届かなかった)、家のルータ (zenohd、平文) は zenohd-cloud に mTLS で繋ぐ。平文を比べるときは同じ compose で LAN の 7447 を家のルータに切り替えた。

### 11.1 繋がる、オセロ

- 端末の `/home/zenoh_echo.txt` に `tls/192.0.2.10:7447`、`/home/asterism/fmruby-p4tls.tar` を置いてオセロを起動 → `the certificate's name fmruby-p4tls is the ID`、`TLS handshake ... (TLSv1.3) took 238-277 ms`、`up (tls/...)`。
- PC の CRuby (`Asterism.connect(credentials: "cruby.tar")`) から `nodes` に端末が出て、`fmruby-p4tls/game/othello.info` が返る (10 回で 19-201 ms)。家のルータ経由 (平文の CRuby) からも同じく見えて呼べた。
- sim (家のルータに平文で繋ぐ) と P4-Nano (クラウドのルータに TLS で直接) でオセロを対局: sim が誘い、端末が受け、4 手 (d3 e3 f3 c5) を指した。手の答えは 104-205 ms。
- console の図は見ていない (console を手元で立てる作業をしていない)。図が使うのと同じ liveliness を CRuby の `nodes` で確かめた。console での確かめは VPS の段 (ユーザが端末を登録した後) に回す。

### 11.2 時間

| | 値 |
|---|---|
| TLS の握手 (TCP の接続から証明書の確かめまで) | 238-277 ms (6 回) |
| 繋がるまで (オセロの `connect` 全体) | 502-647 ms (TLS)、42-205 ms (平文、同じファーム) |
| つなぎ直し (`docker restart` から `connected again`) | 1.5 / 2.5 / 2.7 秒、別の回に 1.5 / 2.5 / 1.5 秒 |

つなぎ直しの握手は 184-276 ms。G1.5b の平文 (1.2-2.4 s) とほぼ同じで、握手の分が足されるだけ。

### 11.3 大きさ (同じ develop `fa0dfa4b` からの比べ)

| | develop | このブランチ | 差 |
|---|---|---|---|
| P4 (NARYAv4) fmruby-core.bin | 6,525,648 | 6,596,496 | **+70,848** (7M の区画の残り 743,536 B、10%) |
| P4 静的な内蔵 RAM (DIRAM .data+.bss) | 40,312 | 40,072 | **-240** |
| P4 PSRAM の .bss | 18,146,348 | 18,147,292 | +944 |
| AtomS3R (ATOMS3R) fmruby-core.bin | 4,095,184 | 4,156,768 | **+61,584** (5M の区画の残り 1,086,112 B、21%) |
| AtomS3R 静的な内蔵 RAM (DIRAM 合計) | 111,279 | 111,039 | **-240** |

flash の増えのほとんどは mbedTLS の TLS 1.3 (見積もり 35-50 KB) と gem の TLS (約 10 KB)。

### 11.4 内蔵 RAM と PSRAM (P4-Nano、最終のファーム、周期のダンプ)

同じ起動の中で、オセロを平文と TLS で交互に起動し直した (各 50 秒、ダンプ 5 回の最後の値)。

| 状態 | IRAM free | PSRAM free | オセロの stack の Free |
|---|---|---|---|
| 起動後、アプリなし (develop) | 208,232 | 11,228,252 | |
| 起動後、アプリなし (このブランチ) | 208,500 | 11,228,216 | |
| 平文 1 回目 | 185,888 | 10,796,580 | 4,572 |
| TLS 1 回目 | 185,580 | 10,758,868 | 4,572 |
| 平文 2 回目 | 185,260 | 10,793,856 | 4,572 |
| TLS 2 回目 | 185,584 | 10,758,500 | 4,572 |
| (参考) develop の平文 2 回 | 184,620 / 184,636 | 10,796,636 | 4,572 |

- **内蔵 RAM の定常**: TLS と平文の差は -308 B / +324 B で、起動し直しの揺れの範囲。増えていない。develop の平文より約 1 KB 多い。
- **握手の最中の最低値** (握手の段の間の見本): 握手の前より 5.1-8.4 KB 低い (例 185,832 -> 177,384、185,680 -> 180,532)。握手の後 1.6 KB 戻り、定常に戻る。mbedTLS の確保は PSRAM なので、これは lwIP の受けの buffer (証明書の束の受け取り) とハードウェアの暗号の一時の分と見ている。平文の接続の最中の最低値は測っていない (比べる値が無い)。
- **stack**: オセロの task の Free は TLS でも 4,572 B で変わらない (握手の深さは今の最も深い所より浅い)。止まる条件には当たらない。
- **PSRAM**: TLS の接続 1 本で約 35.0-35.4 KB (3.2 の見積もり約 38 KB に近い)。つなぎ直しを 3 回しても減っていかない (漏れていない)。
- SSLSocket の HTTPS も、TLS の session の分の内蔵 RAM が `costs 3268-4688 bytes internal` に (前の測りは 5,864-6,200 B)。

### 11.5 断られる場合 (落ちない)

| 場合 | 端末のログ | 時間 |
|---|---|---|
| tar が無い | `tls/... needs a credentials .tar in /home/asterism` (Asterism::Error、オセロは down) | すぐ |
| 期限切れの端末の証明書 | 握手の後の最初の読みで `-0x7780` (ルータの警告)、`cannot open a session (-99)`。ルータのログに `certificate expired` | 387 ms |
| 別の CA の tar (端末がルータを信用しない) | `handshake ... failed: -0x2700 (verify 0x00000008)` | 345 ms |

どれも Guru・abort は 0 件、オセロは down の表示のまま次の試しを待つ。

### 11.6 既存の HTTPS (SSLSocket)

試しのアプリ (スクラッチパッド、コミットしていない) で、`https://api.open-meteo.com/...` に GET → 200 (1,976-2,057 ms)、`https://api.openai.com/v1/models` (tts のクラウドと同じ相手、鍵なし) → 401 (690-717 ms)。TLS 1.3 を入れ、PSA の初期化を `SSLContext_create` に置いた後も HTTPS は通る。

## 12. 残った課題

- **AtomS3R の確かめ**: PC の USB に繋がっていなかった (usbipd の一覧に無い) ので、焼いても動かしてもいない。ビルドと静的な大きさだけ (11.3)。繋いでもらえば、アプリの区画だけを焼き、`asterism_demo` で同じ確かめ (握手の時間はソフトの ECC なので P4 より長い見込み、stack の余り約 6.3 KB) をする。
- **console の図での確かめ**: していない (11.1)。
- 握手の最中の内蔵 RAM の最低値の、平文との比べ。
- MCP の tab5_fs の description に「/home/asterism は get できない」と書く (親のリポジトリ)。
- ピンの上げ (push の後)。
- 2.3 の「TLS 1.3 でクライアントの証明書が断られると握手の後の読みで分かる」は、gem の口では最初の読みで閉じるので 0.4 秒で諦める (ステップ 1 の手元の試しの上流の作りでは 10 秒かかった)。

## 13. ユーザがすること

- AtomS3R を PC に繋ぐ (USB)。親が続きの確かめをする。
- console で端末を client として登録し (名前は端末の mDNS の名前 `fmruby-XXXXXX`、役割はアプリに合わせる)、「Save all (.tar)」の tar を端末の `/home/asterism/` に置く (`tab5_fs put`)。`/home/zenoh_echo.txt` をクラウドのルータの `tls/<名前>:<port>` にする。VPS での確かめは親と一緒に行う。
