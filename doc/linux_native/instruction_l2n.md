# L2n 指示書: native のネットワーク (ソケット・HTTP・WebSocket・Zenoh)

> 計画: doc/linux_native/plan.md。前段の結果: report/l1.md、report/l2.md

## 目的

native に、Linux sim と同じネットワークの機能を入れる: picoruby-socket・
picoruby-net-http (TLS を含む)・picoruby-net-websocket・Asterism の Zenoh
(picoruby-asterism-zenoh / picoruby-asterism / picoruby-fmrb-asterism)。
L1 で外したのは、**協調 port では、待ち続けるシステムコール (recv・connect・
accept・名前解決・TLS の握手) を呼んだタスクが、全部のタスクを止める**ため
(協調 port は同時に 1 つのタスクしか走らせない)。

## 設計の要点: 待つ呼び出しの扱い

次の 2 つの方式を比べ、report に理由を書いて選ぶ。

- **A. 代わりに待つスレッド**: 待つ呼び出しを、FreeRTOS の外の補助の
  pthread で実行し、呼んだタスクは FreeRTOS の通知 (またはセマフォ) で
  正しく眠る。終わったら補助のスレッドが通知する。名前解決
  (`getaddrinfo`、待たない版が無い) や、OpenSSL の握手・読み書きもそのまま
  包める。協調 port の受け口として 1 か所に置けば、ソケット以外 (将来の
  pty の読み取り、L10) にも使える。
- **B. 待たない呼び出しを順に確かめる**: ソケットを O_NONBLOCK にし、
  EAGAIN の間は短く眠って (`vTaskDelay`) 確かめ直す。L1 の debugd の直し
  (`select()` の中で待たない) と同じ考え。名前解決と TLS は別の手当てが要る。

親の見立ては A (1 か所で済み、L10 にも使える) だが、picoruby-socket の
posix port と OpenSSL の呼び方を読んで判断してよい。いずれにしても:

- **submodule の中は直接直さない**。posix port に手を入れる必要があれば、
  lib/add・lib/patch・lib/replace と Rakefile の仕組みで行う。
- sim (SIGALRM の本来の port) と端末の動きは変えない。協調 port の
  ビルド (native) にだけ効く形にする。ブラウザ版は今の WebSocket の経路の
  ままにする。
- 補助のスレッドから FreeRTOS の API を呼ばない (通知を除く。ISR 版か、
  協調 port で安全な形を使う。port の作りを読んで確かめる)。
- タスクの強制終了 (kill) のとき、補助のスレッドで待っている呼び出しを
  どう片付けるか (ソケットを閉じて起こす、など) を決め、取り残しが無いことを
  確かめる。

## Zenoh

- zenoh-pico は 1 スレッドで組んであり (sim の build_config を参照)、読み取りは
  呼ぶ側が回す形。native では sim と同じ unix の platform 層を使えるか確かめ、
  待つ所は上の方式に乗せる。
- ルータは手元の docker の zenohd (`127.0.0.1:7447`、`docker compose up -d zenohd`
  で単独でも起動できる)。sim のアプリは `tcp/zenohd:7447` を使っているので、
  native の設定 (system_conf_native か機種の定義) でルータの場所を
  `tcp/127.0.0.1:7447` にする。置き場所は既存の設定の読み方に合わせる。

## サービス

- `config/services_native.toml` を用意する (無ければ)。
- **timesync は native では無効**にする。時計は Linux の時計をそのまま使う
  (Family mruby から Linux の時計は変えない)。
- net のサービス (NetWatch) は、sim と同じ答え方 (sim の WiFi の HAL が
  何を返しているかを読む) に揃える。OS に問い合わせる本実装は L6。

## 触ってよい範囲

- fmruby-core: `native/`、`hosted/` (協調 port の受け口)、`lib/add/`
  (native の build_config、必要なら port の差し替え。**lib/ を触ったら
  rake clean** の決まりに注意。native の libmruby の作り直しで足りるなら
  それでよい)、`lib/patch/`・`lib/replace/` と Rakefile (submodule の中を
  変える必要があるときだけ)、`config/`、`rakelib/native.rake`、`test/`、
  `doc/linux_native/report/`。
- family-mruby (ルート): `tools/` (確認の道具に要るもの)。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中の直接の編集、
  `flash/` の中身、`doc/linux_native/plan.md`、端末・sim・ブラウザ版の動き。

## 作業場所と git

- 本体の checkout、両リポジトリとも `feature/linux-native` に続けてコミットする
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
- develop へのマージと push はしない。
- `build/` は ESP32-P4 のビルド。自分で `rake clean_all` はしない。`rake clean`
  (lib/ を触ったとき) が sim・端末のビルドに及ぶなら、回帰の道具が作り直す
  範囲か確かめ、及ぶなら report に書いて止まる。

## 受け入れ条件

1. native (標準の構成と全部 mruby の構成) に、ソケット系の 3 つの gem と
   Asterism の Zenoh が入る。
2. TCP: 手元で待ち受けた相手 (例: `ruby -rsocket` の echo) と TCPSocket で
   やり取りできる。**相手が返事をしない間も、デスクトップの時計とポインタが
   動き続ける** (画面を 2 回取って時計が進むこと、入力の注入で窓が動くこと)。
3. HTTP: 手元の HTTP サーバ (例: `ruby -run -e httpd`) への Net::HTTP の GET、
   と HTTPS の GET (届くなら外の https のサイト、届かなければ手元の自己署名
   で、証明書の検証の失敗が正しく例外になること) が通る。名前解決 (localhost
   以外の名前) の間も全体が止まらない。
4. Zenoh: 手元の zenohd に native のアプリから put し、PC 側
   (`tools/fmrb_zenoh.rb` か REST の 8000) で読める。逆向き (PC から put →
   native のアプリが受け取る) も通る。Asterism の既存のデモか試験の
   アプリで確かめる (どれを使ったかを report に書く)。
5. 待っているアプリを kill (`fmrb_native.rb app kill`) しても、残りが固まらず、
   補助のスレッドやソケットが残らない。
6. timesync は動かず、net のサービスは sim と同じ答えを返す。
7. 既存を壊さない: `rake regress` と `rake regress:screens` が通る。
   `rake regress:live` (卓のゲームホスト・歩数・MIDI・音) も流し、sim の
   ネットワークに変化が無いことを確かめる。

## 止まる条件

- `sudo` やパッケージの導入が要るとき (OpenSSL の開発用ヘッダが無い、など)。
- submodule の中を変えないと済まず、lib/ の仕組みでも差し替えられないとき。
- 協調 port の作りの上で、A も B も成り立たない事実が出たとき。
- 端末・sim・ブラウザ版の動き (仕様) が変わる直し方を選ぶ必要が出たとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l2n.md) に書くこと

- A と B のどちらを選んだか、その理由と、受け口の形 (どこに置いたか、
  何を包んだか)
- kill のときの片付け方
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (待ちの間の時計の進み、
  HTTP・HTTPS・Zenoh の結果)
- サービスの設定 (timesync・net) と、ルータの場所の設定
- 見立てと違った点、撤回した仮説、踏んだ罠、残件 (L6・L10 に渡すもの)
