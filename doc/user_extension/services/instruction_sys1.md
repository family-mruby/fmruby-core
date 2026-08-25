# 実装指示書 SYS1: システムサービスの実肉 — net と timesync

対象: 実装担当セッション。作業リポジトリ: fmruby-core のみ。
先に読むもの: plan.md (候補表と契約)、report/s2.md (ホストの現状)。
進め方の約束は instruction_s1.md のとおり。報告は report/sys1.md。
サービスは Modern 専用 (S2 T0) なので、検収は Modern 向け sim + Tab5。

## 下調べ済みの事実 (C の新規工事は不要)

- `FmrbApp.wifi_info` → `{connected:, ip:, ssid:, hostname:}` か nil。
  **Linux 開発ビルドではホストの網の状態を返す**ので sim で検収できる。
  確保なしの `FmrbApp.wifi_connected?` もある (app.c 1353 行付近)。
- `UDPSocket` は picoruby-socket にある (C 実装 + mrblib)。ホスト名解決が
  通るかは最初に 1 行のテストで確かめる (通らなければ IP 直書きも
  config で許す)。
- 時刻設定は `Machine.set_hwclock(UTC epoch)`、RTC への書き戻しは
  clock_setting.rb の RX8130 経路 (I2C は hw_proxy 仲介。サービスが動く
  頃には display は確実に ready)。
- picoruby に `Array#pack` は無い。48 バイトは `"\0" * 48` + setbyte で
  組み、応答は getbyte で読む (既知の流儀)。

## net サービス (/usr/share/services/net.rb)

- 5 秒周期 (interval_ms = 5000) で `FmrbApp.wifi_connected?` を見る
  (確保なし)。**変化したときだけ** `wifi_info` を読み、`net/state` に
  publish: `{"connected"=>bool, "ip"=>..., "ssid"=>...}`。接続時は
  `ctx.log` に IP を 1 行 (「IP は毎回引く」痛みへの直接の答え)。
- 起動時も現状を 1 回 publish する (**遅れて購読した者のために、
  `svc/ctl` の list と同様、要求 topic `net/get` に応えて現状を返す**
  でもよい。どちらにしたか plan の契約に書く)。
- sim では docker の網がホスト扱いで「常時接続」になる — 変化の検収は
  wifi_info が nil を返す Retro… は無いので、**一時的にサービス内の判定を
  騙すテスト** (config で強制切断フラグ) を入れず、変化検出のロジックを
  services テスト (host テスト) に切り出して検証する。

## timesync サービス (/usr/share/services/timesync.rb)

- `net/state` を購読。connected を受けたら SNTP を 1 回 (再試行は
  `wake_in`、3 回失敗で次の接続イベントまで待つ)。成功後は 24 時間ごと
  (interval_ms) に再同期。
- SNTP は UDP 48 バイト: 先頭バイト 0x1B (LI=0,VN=3,Mode=3)、残り 0。
  応答の 40-43 バイト目 (ビッグエンディアン 32bit) が秒。**1900 年紀元**
  なので epoch へは 2,208,988,800 を引く。往復遅延の補正はしない (精度
  ±秒で足りる)。
- 妥当性検査: 得た時刻が 2020..2099 年の範囲外なら捨てて再試行 (RTC
  ドライバと同じ考え方)。
- 適用: `Machine.set_hwclock(epoch)`。**実機 (PLATFORM == "esp32") では
  RX8130 にも書き戻す** (clock_setting.rb の UTC 経路を関数として共用
  できるなら共用、できなければ同じ手順を書く。どちらにしたか report へ)。
  sim では set_hwclock だけ (ホストの時計は触らない — sim の
  set_hwclock が何をするかを先に確認し、危険なら sim では表示だけ)。
- 設定 `[timesync.config]`: `server` (既定 "pool.ntp.org")、
  `interval_hours` (既定 24)。
- **ブロックで待たない**: UDP の受信待ちはタイムアウト付き (無ければ
  ノンブロッキング + `wake_in` で刻む)。50ms 警告が出ない形にする。
  DNS 解決が長く塞ぐようなら report に実測を書いて相談 (own_vm 送りの
  判断材料)。

## /etc/services.toml (config/services.toml) への追加

```toml
[net]
file = "net.rb"
class = "NetWatch"
enable = true
interval_ms = 5000

[timesync]
file = "timesync.rb"
class = "TimeSync"
enable = true
interval_ms = 86400000
```

## 検収

### sim (Modern 向け)

- 起動で net が現状を publish (一時アプリで購読、IP がログに出る)。
- timesync が SNTP に成功し、時刻が実時間に合う (docker から外へ UDP 123
  が出られない環境なら、その旨と代替 (ローカル NTP か検収の実機送り) を
  report に書く)。
- 変化検出・SNTP パケットの組み立てと解析・紀元変換・妥当性検査を
  **services の host テストに追加** (ネットワークに触らない純粋部分を
  切り出す。S1 の流儀)。
- 失敗系: server 不達 → 3 回で静かに待機、ログ各 1 行。
- `svc disable timesync` が効く (S2 の機構の実使用)。

### Tab5

- ブート → WiFi 接続 → **Set Clock を触らずに**時計が合い、シリアルに
  IP と時刻同期のログが出る。
- リセット後、`fmrb_rtc: RTC read` が実時間と一致 (RTC 書き戻しの証明)。
- `clock/hour` (clock サービス) が時刻ジャンプ後も正しく動く (同期直後に
  分の境界を 1 回またいで確認)。

## 受け入れ条件

- 上記検収が report/sys1.md に揃う。net/state の契約 (payload と取得の
  作法) を plan.md に記載。
- コミット 2 本 (net / timesync。本書と plan 更新は 2 本目に)。英語、
  ユーザ確認のうえ。2 構成ビルド + doctor 新規指摘 0 + rake test。
  `.env` 復元。

## やらないこと

- NTP の精密補正 (往復遅延・うるう秒)、IPv6、複数サーバのフェイルオーバ
- ウィジェット表示 (IP の表示先は将来の案 5)
- Retro 対応 (サービス自体が Modern 専用)
