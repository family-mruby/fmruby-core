# SYS1 報告: net と timesync

instruction_sys1.md の 2 サービス。検証は Modern 向け sim + Tab5 実機。
エンジンは標準 (Spinel カーネル) と全 mruby の 2 構成。`.env` は書き換えて
いない。

**読む順**: 先に「先に潰した 2 つの穴」を読む。どちらもサービスの実装より
先にあった問題で、片方は S1 から埋まっていた欠陥、もう片方は sim の既知
不具合の穴だった。

## 下調べ (指示書が「先に確かめよ」と言っていたもの)

一時アプリ 1 本で 3 つまとめて測った (コミットしない)。

| 調べたこと | 結果 |
|---|---|
| `UDPSocket` はホスト名を解決するか | **する**。sim で `connect("pool.ntp.org", 123)` が 10ms、返信 21ms。実機は connect 1ms / 返信 37ms。IP 直書きの逃げ道は要らなかった |
| docker から UDP 123 が出られるか | **出られる**。sim でも実物の SNTP で検収できた |
| sim の `Machine.set_hwclock` は何をするか | **何もしない。`NotImplementedError` を投げる** (posix ポートに実装が無い)。ホストの時計は安全 |

3 番目が効いた。`NotImplementedError` は **`ScriptError` の子で
`StandardError` ではない**ので、`rescue => e` では捕まらない。sim でうっかり
呼ぶと、ホストの `call_service` の rescue も `services.app.rb` 末尾の rescue も
素通りして**ホストごと落ちる** (S2 T1 でホストをわざと殺すのに使ったのと同じ
経路)。したがって timesync の適用は **rescue ではなく `PLATFORM` の分岐**で
守ってある。**ホスト側の穴は別途塞いだ** (下記「`NotImplementedError` の件」)
が、sim でホストの時計を触らないという判断自体は変わらないので分岐は残る。

## 先に潰した 2 つの穴

### 1. 同じホストの中ではサービス同士に配送されていなかった (S1 からの欠陥)

net を書いて timesync を繋いだら、**net/state が timesync に届かなかった**。

原因は kernel の publish 経路: **publish 元の pid には配り返さない**
(`next if sub_pid == pid`)。ホスト内のサービスは全員がホストの pid を共有する
ので、**サービスが流した topic は、機械中の全アプリに届いて、隣のサービスにだけ
届かない**。

これは SYS1 で入った不具合ではなく、**S1 のサンプルの組 (clock -> hourly_chime)
が最初から動いていなかった**ということでもある。S1 の検収では
「一時アプリから clock/hour を流す」形で配送を確かめており、**clock サービス
自身が流した場合を試していなかった**。検収の作り方の抜けである。

直し: ホストの `ctx.publish` を 2 方向に出す (kernel へ + 自分の中の購読者へ)。
中への配りは**待ち行列に積んで次の `on_update` で配る**。`on_event` の中から
publish されたときに同期配送すると入れ子呼び出しになり、「サービスは順番に
走る」という S1 の約束が崩れるため。1 周あたりの上限を設け、超えたら警告 1 行
で捨てる (自分の topic を自分で購読するループの歯止め)。

確認: 一時サービスに `clock/minute` を購読させ、**clock サービスが分の
変わり目に流したものが届く**ことを sim で確認した。

```
I Services: svc[tmp_minute] clock/minute -> {"year"=>2026, ..., "hour"=>9, "minute"=>27}
```

### 2. sim のログデッドロック — 既知対策の穴 (doc/sim_log_deadlock.md)

net と timesync を足したらブート中に**機械全体が固まる**ようになった。gdb で
捕まえたところ、`doc/sim_log_deadlock.md` にある優先度逆転そのもの:

```
Thread 4  Services  write() の中で _IO_stdfile_1_lock を保持したまま
                    tick シグナルで suspend
                    ("...timesync started..." を書いている最中)
Thread 5  desktop   同じ FILE ロック待ち (futex)
Thread 8  host_task 同じ FILE ロック待ち (futex)
```

**同文書は「stdio の FILE ロックは esp_log がラップ済み区間から呼ばれるので
実質カバーされている」と書いているが、これは誤り**。IDF の `esp_log_va` は
タグ判定ロックを取って**放してから**出力関数を呼ぶので、**stdout をロックする
vprintf はどのラップ区間の外**にある。既存の `--wrap` 4 本 (タグロック 3 +
タイムスタンプ) はここに届いていない。

直し: `esp_log_set_vprintf` で出力そのものを port の critical section で包む
(`main/boot/sim_log_guard.c`)。リンカラップでなく IDF の API を使ったのは、
**必要なのはログ経路だけ**で、機械中の全 `printf` を critical section に
入れるのは約束が大きすぎるため。文書の見立ての訂正も同ファイルのコメントに
残した。

以後、同じ手順で固まらない (以降の全検収が通っている)。

## net サービス

- 5 秒周期で `FmrbApp.wifi_connected?` (確保なし) を見て、**変わったときだけ**
  `wifi_info` を読んで `net/state` を publish。接続時は IP をログに 1 行。
- **`net/get`** を購読し、要求されたら現状を `net/state` として流し直す。
  遅れて起動した購読者のための口 (契約は plan.md に記載)。timesync が実際に
  これを使う (一覧で net の後に読み込まれるため)。

sim / 実機とも、起動時に IP がログに出る:

```
I Services: svc[net] up: 172.19.0.4                              (sim)
I Services: svc[net] up: 192.168.10.21 (Buffalo-G-F750)          (Tab5)
```

## timesync サービス

`net/state` を購読し、接続を知ったら SNTP を 1 回。48 バイトの組み立てと
40-43 バイト目の読み出し、1900 年紀元の変換、2020..2099 の妥当性検査。
**塞がない**: 受信はノンブロッキングで、待ちは `wake_in(100)` の連鎖
(最大 2 秒)。50ms 警告は出ていない (下記の初回コストを除く)。

### RTC 書き戻しのバグ — 実機でだけ出た

最初の実装は `FmrbApp.wallclock` を読んで RTC に書いていた。リセットして
読み戻すと:

```
host UTC          2026-08-25 00:14:08
fmrb_rtc: RTC read 2026-08-25 09:14:08 UTC     <- 9 時間ずれている
```

`FmrbApp.wallclock` が返すのは**ローカル時刻**で、RTC が保持すべきなのは
**UTC**。JST の機械で 9 時間進んだ値を UTC として書き込んでいた。次回ブートは
9 時間未来から始まり、同期が済むまでそのまま。**sim は RTC を書かないので
絶対に出ない**種類の間違いで、実機検収の項目があったから見つかった。

直し: 読み戻しをやめ、**NTP から得た epoch から UTC の年月日時分秒を自分で
計算する** (`TimeSync.utc_fields`、civil-from-days)。ローカル時刻がどこにも
入らない。閏日の両側と年末を host テストに固定した。

直したあとの証明 (リセット後の 1 行目):

```
host UTC          2026-08-25 00:23:48
fmrb_rtc: RTC read 2026-08-25 00:23:51 UTC     <- 一致 (差はブートの 3 秒)
```

**Set Clock を一度も触らずに時計が合っている。**

### 諦めの規則を変えた (指示書からの逸脱・理由つき)

指示書は「3 回失敗で**次の接続イベントまで待つ**」。実機で試すと、
**書き込み直後の初回ブートは必ず失敗する**:

```
I svc[net] up: 192.168.10.21
I svc[timesync] pool.ntp.org: no reply; retrying     (+2.3s)
I svc[timesync] pool.ntp.org: no reply; retrying     (+5.5s)
I svc[timesync] pool.ntp.org: no reply; giving up    (+8.7s)
```

3 回の試行が**すべて接続直後の 9 秒以内**に入り、その時点ではまだ名前解決が
返らない (7 分後に同じ機械で試すと connect 1ms・返信 37ms)。しかも
**net は「変化したとき」しか publish しない**ので、そのまま繋がり続ける機械
には**次のイベントが永遠に来ない** — つまり指示書どおりだと、**その電源
投入の間ずっと時計が合わないまま**になる。温かい再起動では成功するので、
実機で初回ブートを見なければ気付かない。

そこで: **まだ一度も同期できていない間だけ、5 分後に仕切り直す**。同期が
済んだあとは従来どおり (日次の tick に任せ、待つ)。書き直して再確認したところ、
初回ブートでも接続の **1.1 秒後**に成功するようになった:

```
I svc[net] up: 192.168.10.21 (Buffalo-G-F750)
I svc[timesync] clock set from pool.ntp.org (epoch 1787617388)
I svc[timesync] RTC updated
```

### 失敗系 (sim)

到達しないサーバ (`192.0.2.1` = TEST-NET-1) を `[timesync.config]` の上書きで
指定:

```
I svc[timesync] 192.0.2.1: no reply; retrying
I svc[timesync] 192.0.2.1: no reply; retrying
I svc[timesync] 192.0.2.1: no reply; trying again in 5 minutes
```

各 1 行で、以後 5 分間は静か (30 秒放置でログ 3 行のまま)。**50ms 警告は 0**。

### `svc disable timesync`

S2 の機構がそのまま効く。`svc list` で `sys disabled` と出る。

## 計測 (実機 Tab5)

| 見るもの | 値 |
|---|---|
| `connect()` (DNS 込み、pool.ntp.org) | **1 ms** (`ntp.nict.jp` 90ms、IP 直指定 0ms) |
| SNTP 往復 | **37 ms** (nict 13ms、IP 直 3ms) |
| timesync の `on_start` | **148-181 ms** (下記) |
| net の `net/get` 応答 | 82 ms (1 回だけ) |

`on_start` の 150ms 前後は 50ms 警告に引っかかる。中身は
`ctx.publish("net/get")` 1 回だけなので、**メッセージ経路の初回コスト**
(msgpack の初回・確保) と見ている。2 回目以降は警告が出ない (警告は 1 回目と
10 回ごと)。塞ぐ処理ではないので実害はないが、**ブートで必ず 1 行出る**ので
記録しておく。DNS が長く塞ぐ兆候は無く、`own_vm` 送りを検討する材料は今のところ
無い。

## 2 構成 / lint / テスト

| 項目 | 結果 |
|---|---|
| 標準 (Spinel カーネル + Spinel エディタ) | ビルド通過、上記全検収 |
| 全 mruby | ビルド通過、sim で net の IP と timesync の epoch を確認 |
| `rake spinel:doctor` | 新規指摘 0 (指摘 3 件は `system_desktop` の既存のもの) |
| `rake test` | 通過。services テストに SNTP の組み立て・解析・紀元変換・妥当性検査・**UTC 変換**、net の変化検出と `net/get` を追加 |

host テストは**実物のサービスファイルを load** して class メソッドを叩く
(S1 の流儀)。net の側は `FmrbApp` だけを偽物にして、変化したときしか publish
しないことと `net/get` が同じ内容を返すことを見ている。

## `NotImplementedError` の件 (決着済み)

**`NotImplementedError` がホストの rescue を素通りする**問題は、ユーザ判断で
**サービスに向けた rescue だけを `rescue StandardError, ScriptError => e` に
広げる**ことで決着した (別コミット)。

- `ScriptError` は `NotImplementedError` / `LoadError` / `SyntaxError` の親で、
  出来の悪いサービスが現実に投げる例外はここで尽きる。S1 の隔離の約束が守れる。
- **素の `Exception` は素通りのまま**。S2 T1 の異常死の作り方
  (`raise ::Exception`) と kernel の再 spawn 検証はそのまま生きる。
  `SystemStackError` も素通り (巻き戻し中に受け止めても意味が無い)。
- 広げたのは**サービスに向いた 4 か所だけ** (`require` / インスタンス化 /
  `SUBSCRIBE` の読み取り / 4 つのコールバック)。`LoadError` と `SyntaxError`
  は `require` の側でしか出ないので、`call_service` だけでは足りない。
  **ホスト自身の仕事の rescue は従来のまま** — ホスト自身の `ScriptError` は
  ホストのバグなので、reaper に再 spawn される方が正しい。

検収 (sim):

```
E svc[tmp_notimpl] NotImplementedError: set_hwclock() ... unimplemented
I svc[heartbeat] beat 2, up 4s                    <- 止まらない
E svc[tmp_notimpl] NotImplementedError: ...
E svc[tmp_notimpl] disabled after 3 errors        <- そのサービスだけ failed
I svc[heartbeat] beat 13, up 26s                  <- 以後もずっと動く
```

同じ一時サービスを `raise ::Exception` に差し替えると、従来どおりホストが
異常死して kernel が 3 回まで再 spawn する (S2 T1 の経路が生きている確認)。

この決着により、timesync の `PLATFORM` 分岐は**保険ではなく設計**として残る
(sim でホストの時計を触らないため)。

## やっていないこと

- 精密補正 (往復遅延・うるう秒)、IPv6、複数サーバのフェイルオーバ (指示どおり)
- IP のウィジェット表示 (案 5 待ち)
