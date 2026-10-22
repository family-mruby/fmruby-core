# 端末の証明書の束から信頼できないルートを外す

> 状態: 進行中 | 更新: 2026-10-22 | ESP32 の端末の HTTPS が使う ESP-IDF の証明書の束 (全部入り) に、eMudhra の件 (2026-10、中間認証局 8 つの秘密鍵が誰でも計算できた) の大本 emSign ECC Root CA - G3 が入っている。IDF の束から外す一覧を除いた自前の束を作って使う

## 目的

- 端末は失効を確かめない (mbedTLS)。G3 の下の中間認証局の鍵は誰でも求められるので、同じ網にいる人が端末の HTTPS をなりすませる (TTS が OpenAI の API キーを送る通信など)。G3 を端末の信頼から外す。
- つながるサイトは今と変えない (全部入りから外す一覧だけを除く)。

## 決まったこと (2026-10-22 ユーザ)

1. **全部入りから G3 だけを外す** (使うルートだけの小さな束にはしない: 利用者のアプリがつなぐ先を狭めないため)。
2. 外す一覧はリポジトリに持つ (`config/cacrt_exclude.txt`、理由つき)。束は rake で IDF の束から作り直せる形にする。
3. sdkconfig.defaults に自前の束を使う 4 行を足す (sdkconfig.defaults は編集しない決まりだが、この 4 行はユーザが許可した)。

## 今の端末 (2026-10-22 に確認)

- HTTPS は `lib/patch/picoruby-socket/ports/esp32/ssl_socket.c` が `esp_crt_bundle_attach` で IDF の束を付けて相手を確かめる。束は IDF の既定 (全部入り: `cacrt_all.pem` 150 枚、Mozilla 2025-02-25 の分。emSign は G1・G3・C1・C3)。
- Zenoh の TLS (Asterism の mTLS) は console の自前の認証局だけを信じるので関係しない。
- つなぐ先とその大本のルート: api.openai.com (TTS) → GlobalSign Root CA、api.open-meteo.com・raw.githubusercontent.com・*.github.io → ISRG Root X1、github.com → USERTrust ECC。どれも G3 の系統ではない。

## 段階

| 段 | 中身 |
|---|---|
| B1 | `rake certs:bundle` と外す一覧と自前の束、host の試験、4 つの機種の sdkconfig.defaults、Tab5 向けのビルドで束の中身を確認 (サブ、instruction_b1.md) |
| B2 | Tab5 に焼き (先に /home を退避)、天気のデモと TTS が HTTPS でつながることを確認 (親) |

## 未確定事項

- emSign のほかの 3 つ (G1・C1・C3) も外すか (今回の件の系統ではない。様子を見る)。
