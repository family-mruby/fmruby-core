# IPCの通信プロトコルの仕様

## 基本的な設計

Family mruby では、2つのESP32コアが連携して動作する。

- fmruby-core : ESP32-S3
  Family mruby OSを実行。USBホスト機能で、キーボード、マウスの入力を受け付ける。
- fmruby-graphics-audio : ESP32-WROVER
  LovyanGFXのNTSC出力機能で映像を出力。
  音声はnofrendoのAPU実装を移植したものでWAVを生成して、I2Sで出力。

コア間はSPIで通信を行う。core側がマスターになる。

開発用にLinuxでもfmruby-coreを実行できるようにしている。
その場合は、fmruby-graphics-audio の機能を別のLinuxプロセス（host/）でSDL2を用いて実現して、coreからのSPI通信をソケット通信を用いて模擬して、coreのHALより上のレイヤーではコード変更不要で動くようにする。

## IPC

fmrb_ipcが対応。

本番ではSPIを利用。
開発用では、socket通信で模擬する。

### 送信側(S3, SPI master)

msgpackで構造体をシリアライズして送信する。
送信する際は、CRC32を付与したあと、COBSエンコード（区切りは0x00）して送信。

基本構造体ヘッダ
```
typedef struct {
    uint8_t type;   // msg type
    uint8_t  seq;   // sequence number
    uint16_t len;   // payload bytes
} frame_hdr_t;
```

- type
  - 1: control
    WROVER制御(設定更新、ファイル転送、設定読み出し、状態読み出し、トランザクションリセット、ソフトリセットなど)
  - 2: graphics
  - 4: audio
  - 8: 
  - 16: 
  - 32: ack required
  - 64: chunked
  - 128: input(linux only)

タイプごとにペイロードの構造体が変わる

```
// Chunkedヘッダ
typedef struct {
    uint8_t flags;   // Chunkフラグ
    uint8_t chunk_id;   // chunk識別子
    uint16_t chunk_len;
    uint32_t offset;
    uint32_t total_len;
} chunk_info_t;

// Chunkフラグ
enum {
  FL_START   = 1 << 0,   // チャンク開始
  FL_END     = 1 << 1,   // チャンク終了
  FL_ERR     = 1 << 7,   // エラー通知
};

```


### 受信側(WROVER, SPI slave)

0x00まで集める→COBSデコード→CRC32検証

応答構造体
```
typedef struct {
    uint8_t type;   // msg type
    uint8_t  seq;   // rolling counter
    uint16_t response;   // 0: OK, その他: Fail
} frame_response_hdr_t;


typedef struct {
    uint8_t  chunk_id;    // 対象レーン
    uint8_t  gen;
    uint16_t credit;      // 0..windowサイズ（次の同時要求許容量）
    uint32_t next_offset; // 次に送ってほしいオフセット
} frame_chunk_ack_t;
```

## type=graphics の定義

fmrb_gfxが対応。fmrb_ipcを利用して実現。

リモートからの実行に適さないもの以外は、LovyanGFXのAPIを透過させるイメージ。

### オブジェクト管理

window_handle
- 0: 背景（Create不要で最初から存在）
- 1～: Windowハンドル

image_handle
- 0～1000: プリセットイメージ(システム予約)
- 1001～: 動的に生成したイメージ

### API

いくつかはまとめてバッチ送信できるようにする。

- create_window(x,y,width,height,bg_color,title)
  - response
    - OK/Error
    - window_handle(1～)

- set_window_order(window_handle, no)
  Windowの表示順番を指定。window_handle=0は対象外
  - response
    - OK/Error

- set_window_pref(window_handle, x,y,width,height,color,title)
  Windowの設定を更新。window_handle=0は対象外
  - response
    - OK/Error

- refresh_all_windows()
  背景と全Windowを描画しなおす。
  - response
    - OK/Error

- draw_xxx(window_handle, coordinate, color)
  描画系API。LovyanGFXのAPIに準拠する
  - response
    - OK/Error

- update_window(window_handle)
  対象windowのバッファを映像出力する
  - response
    - OK/Error

- create_image_handle_from_mem(location(pointer to bitmap))
  - response
    - OK/Error
    - image_handle(1001～)

- create_image_handle_from_file(location(path/to/file=Wrover FROMから読み出し))
  - response
    - OK/Error
    - image_handle(1001～)

- delete_image_handle( image_handle(1001～) )
  - response
    - OK/Error
    - image_handle(1001～)

## type=control の定義

fmrb_halが対応。fmrb_ipcを利用して実現。
設定更新、ファイル転送、設定読み出し、状態読み出し、トランザクションリセット、ソフトリセットなど

### システム系
- read_version
  firmware_ver
  protocol_ver

- read_config
- write_config

- reset_transaction
- reset_esp32

### ファイル系
- create_file
- delete_file
- read_dir
- read_status

TBD

## type=audio の定義

fmrb_audioが対応。fmrb_ipcを利用して実現。
TBD

## COBS, CRC32のサンプルコード

```
// cobs_crc_socket_example.c
// Build: gcc cobs_crc_socket_example.c -o demo
// Note: サーバ側は echo サーバでOK（受け取ったバイト列を返すだけ）

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define COBS_ENC_MAX(n) ((n) + ((n)/254) + 2)   // コードバイト + 終端0x00
#define FRAME_TERM 0x00

// --- CRC32 (poly=0xEDB88320, 反転あり) ---
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t len) {
    crc ^= 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int)(crc & 1)));
    }
    return crc ^ 0xFFFFFFFFu;
}

// --- COBS encode/decode ---
static size_t cobs_encode(const uint8_t *in, size_t len, uint8_t *out) {
    size_t read = 0, write = 1, code_pos = 0;
    uint8_t code = 1;

    while (read < len) {
        if (in[read] == 0) {
            out[code_pos] = code;
            code_pos = write++;
            code = 1;
        } else {
            out[write++] = in[read];
            if (++code == 0xFF) {
                out[code_pos] = code;
                code_pos = write++;
                code = 1;
            }
        }
        read++;
    }
    out[code_pos] = code;
    out[write++] = FRAME_TERM;  // フレーム終端
    return write;
}

static ssize_t cobs_decode(const uint8_t *in, size_t len, uint8_t *out) {
    size_t read = 0, write = 0;
    while (read < len) {
        uint8_t code = in[read++];
        if (code == 0 || read + code - 1 > len) return -1; // 不正
        for (uint8_t i = 1; i < code; i++) out[write++] = in[read++];
        if (code != 0xFF && read < len) out[write++] = 0;
    }
    return (ssize_t)write;
}

// --- 送信: payload に CRC32 を付けて COBS で包み、0x00 終端で送る ---
static int send_frame_cobs_crc(int sock, const uint8_t *payload, size_t plen) {
    uint8_t tmp[4 + plen]; // [payload | CRC32]
    memcpy(tmp, payload, plen);
    uint32_t c = crc32_update(0, payload, plen);
    memcpy(tmp + plen, &c, 4);

    uint8_t enc[COBS_ENC_MAX(sizeof(tmp))];
    size_t enc_len = cobs_encode(tmp, sizeof(tmp), enc);

    // TCP送信（短いので単発send。大きい場合はループで送る）
    ssize_t n = send(sock, enc, enc_len, 0);
    return (n == (ssize_t)enc_len) ? 0 : -1;
}

// --- 受信: 0x00 までを1フレームとして取り出し→COBSデコード→CRC検証 ---
static int recv_frame_cobs_crc(int sock, uint8_t *out, size_t out_cap, size_t *out_len) {
    uint8_t buf[4096];
    size_t used = 0;

    // 0x00 が来るまで読み続ける（適宜タイムアウト設定推奨）
    while (used < sizeof(buf)) {
        ssize_t n = recv(sock, buf + used, sizeof(buf) - used, 0);
        if (n <= 0) return -1;
        used += (size_t)n;
        // 終端を探す
        for (size_t i = 0; i < used; i++) {
            if (buf[i] == FRAME_TERM) {
                // フレーム [0..i] をデコード
                ssize_t dec_len = cobs_decode(buf, i + 1 /*含む*/, out);
                if (dec_len < 4) return -2; // CRC領域不足
                // 末尾4BがCRC
                uint32_t recv_crc;
                memcpy(&recv_crc, out + dec_len - 4, 4);
                uint32_t calc_crc = crc32_update(0, out, dec_len - 4);
                if (recv_crc != calc_crc) return -3; // CRC不一致
                if ((size_t)(dec_len - 4) > out_cap) return -4; // バッファ不足
                *out_len = (size_t)dec_len - 4;

                // 余剰データ（次フレーム先頭以降）があれば左詰め
                size_t remain = used - (i + 1);
                memmove(buf, buf + i + 1, remain);
                used = remain;
                return 0;
            }
        }
    }
    return -5; // 長すぎ
}

// --- デモ（clientとして接続→送信→受信） ---
int main(void) {
    const char *server_ip = "127.0.0.1";
    uint16_t    server_port = 12345;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return 1;
    }

    // 送るデータ（0x00を含む）
    const uint8_t payload[] = {0x11, 0x00, 0x22, 0x33, 0x44};
    if (send_frame_cobs_crc(sock, payload, sizeof(payload)) != 0) {
        fprintf(stderr, "send_frame failed\n");
        close(sock); return 1;
    }

    // サーバがそのままエコーしてくる想定で受信
    uint8_t rx[1024]; size_t rx_len = 0;
    int r = recv_frame_cobs_crc(sock, rx, sizeof(rx), &rx_len);
    if (r != 0) {
        fprintf(stderr, "recv_frame failed (%d)\n", r);
        close(sock); return 1;
    }

    printf("OK: rx_len=%zu\n", rx_len);
    for (size_t i = 0; i < rx_len; i++) printf("%02X ", rx[i]);
    printf("\n");

    close(sock);
    return 0;
}

```
