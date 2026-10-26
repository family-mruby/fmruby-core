# L2n 報告: native のネットワーク (ソケット・HTTP・WebSocket・Zenoh)

> 状態: 完了 (受け入れ 7 点) | 更新: 2026-10-26 | 指示書は instruction_l2n.md。ブランチは両リポジトリとも feature/linux-native (develop へは未マージ・未 push)

## 選んだ方式: A (代わりに待つスレッド)

### 理由

posix port と OpenSSL の呼び方、Zenoh の TCP リンクを読んで、A に決めた。

- 待つ呼び出しは 3 か所に散っている。picoruby-socket の posix port
  (`getaddrinfo`、待たない `connect` のあとの `select`、`SO_RCVTIMEO` つきの
  `recv`、`send`)、OpenSSL (`SSL_connect` / `SSL_read` / `SSL_write`。
  中で待つのは libssl の中の `read`/`write` で、こちらからは触れない)、
  Zenoh の gem の TCP リンク (`getaddrinfo`、`poll` での connect 待ちと
  握手の読み取り)。
- B (待たない呼び出しを `vTaskDelay` で確かめ直す) は、この 3 か所の
  呼び出しを 1 つずつ書き換えることになる。`readpartial` の
  `SO_RCVTIMEO` の意味、`send` が全部送るまで戻らない意味、TLS の
  WANT_READ/WANT_WRITE の回し方を、それぞれの場所で作り直す。しかも
  posix port は lib/patch で直せるが、Zenoh の gem は固定した外の
  checkout から写していて (`rake setup`)、lib/patch の仕組みに乗っていない。
  `getaddrinfo` には待たない版が無い (glibc の `getaddrinfo_a` は中で自分の
  スレッドを使うので、結局 A と同じ形になる)。
- A は gem を 1 行も変えずに済む。待つ呼び出しをリンクの時点で差し替える
  (`-Wl,--wrap=...`) ので、gem の posix port・OpenSSL を呼ぶ所・Zenoh の
  リンクのどれにも同じ受け口が効く。差し替えは native のリンクにだけ
  あり、sim・端末・ブラウザ版はこのファイルをリンクしない。
- 受け口は 1 か所 (`hosted/gate/`) で、ソケットを知らない。L10 の pty の
  読み取りも同じ受け口に載せられる。

### 受け口の形

| 場所 | 中身 |
|---|---|
| `hosted/port/port.c` / `port_hosted.h` | 協調 port に足した口 3 つ: **poll hook** (tick を食わせる所 = 切り替えのたびと idle のたび、critical section の外で呼ぶ)、**delete hook** (`vPortCleanUpTCB`、スレッドを刈る前)、`xPortHostedTaskMayBlock()` (FreeRTOS のタスクのスレッドで、scheduler が動いていて critical section の外か)。診断に `ulPortGetMaxTickBurst` / `vPortResetMaxTickBurst` を公開した。どれも設定しなければ何もしない (ブラウザ版は設定しない) |
| `hosted/gate/fmrb_gate.{h,c}` | 待つ呼び出しの受け口。`fmrb_gate_call(job)` は job を補助のスレッドに渡し、呼んだタスクは job ごとの FreeRTOS の 2 値セマフォで眠る。補助のスレッドは呼び出しを実行して job を「済み」の列に積むだけ (pthread の mutex。FreeRTOS も fmrb の allocator もログも使わない)。poll hook が済みの列を空け、`xSemaphoreGiveFromISR` で待つタスクを起こす (FromISR はその場で切り替えない)。slot は 64、補助のスレッドは要るだけ作り (上限 64)、5 秒仕事が無ければ終わる。名前は `fmrb-gate` |
| `native/net/net_wrap.{h,c}` | 差し替えた呼び出し: `getaddrinfo connect accept recv recvfrom send sendto select poll SSL_connect SSL_read SSL_write SSL_shutdown SSL_get_error socket close` (`native/CMakeLists.txt` の `NATIVE_NET_WRAPPED`) |
| `native/backend/main_native.c` | scheduler を起こす前に `fmrb_gate_init()` と `native_net_init()`。`FMRB_NATIVE_GATE=off` で受け口を使わない (比べるための診断) |
| `native/backend/native_ctl.c` | 制御のソケットに `stats` / `stats reset` (受け口の数と burst_ms) |

差し替えた呼び出しは、まず待つかどうかを見る。

- FreeRTOS のタスクでない (主スレッド、ALSA のスレッド、補助のスレッド自身)、
  受け口が使えない (critical section の中、idle、slot が無い) → そのまま呼ぶ。
- 待たない形 (`O_NONBLOCK` の fd、`MSG_DONTWAIT`、時間 0 の `poll`/`select`、
  数字の `getaddrinfo`) → そのまま呼ぶ。
- 待たずに一度試す (`recv` は `MSG_DONTWAIT`、`poll`/`select` は時間 0、
  `SSL_read` は `SSL_pending`、`accept` は時間 0 の `poll`)。本当に待つ
  ときだけ補助のスレッドに渡す。`send` は送れなかった残りだけを渡す
  (待つ `send` は全部送るまで戻らない、という意味を保つ)。
- `errno` は補助のスレッドの値を写して返す。OpenSSL の `SSL_get_error` は
  呼んだスレッドのエラーの列を見るので、補助のスレッドで聞いた答えを
  タスクの側に覚えておき、同じ SSL・同じ戻り値の `SSL_get_error` に返す
  (理由はエラーの列ごと補助のスレッドに残るため。理由の文は補助のスレッドが
  stderr に出す)。

job の記録の置き場所は 2 通り。ソケットの呼び出しと TLS は呼んだ側の
メモリ (受け取りの buffer、SSL) を直接使う。`getaddrinfo`・`poll`・`select`
は job の中の写し (1280 バイトの scratch) だけを使うので、待つのを途中で
やめても (下の kill) 補助のスレッドが呼んだ側のメモリに触れることがない。

## kill のときの片付け

1. **頼んで終わらせる (ふつうの kill)**: `fmrb_app_kill` は最初に
   `ctx->should_exit` を立てる。受け口で眠っているタスクは 50 ms ごとに
   自分のアプリのこの旗を見ていて、立っていたら job が名指しした fd を
   `shutdown(SHUT_RDWR)` する。これで `recv`・`accept`・`connect`・
   `poll`/`select` は即座に戻り (`close` では戻らない)、呼び出しは
   EOF かエラーを返し、Ruby は例外になり、アプリは自分の終わり方
   (メッセージ → VM の後始末 → ソケットの finalizer で `close`) で終わる。
   起こせない `getaddrinfo` は 200 ms 待って手放す (`EAI_SYSTEM`/`EINTR`
   を返す)。手放した job は受け口の中の写しだけを使っていて、補助の
   スレッドが終わったときに poll hook が結果を `freeaddrinfo` する。
2. **消して終わらせる (答えないアプリ)**: 1 s 待っても終わらないと
   `fmrb_app_kill` はタスクを消す。delete hook が、そのタスクの job の fd を
   `shutdown` し、呼んだ側のメモリに書く job は終わるまで待つ (最大 5 s。
   shutdown 済みなので実際はすぐ。VM のメモリが解放されるのはこの後)。
   その後、`net_wrap.c` がそのタスクが開いたまま残したソケットを閉じる。
   `socket`・`accept` で開いた fd を開いたタスクごとに覚えておき (`close` で
   忘れる)、消されたタスクの分を、fd がまだ同じ inode のソケットであることを
   確かめてから閉じる (C ライブラリが裏で閉じた番号が別の物に使われて
   いても閉じない)。

## 受け入れ条件

確認の道具は `test/native/net/l2n_check.rb` (ルートの `tools/fmrb_native.rb` で
自分用の run dir・debug の port 5585・空の利用者の場所に native を起こし、
PC 側の相手を自分で立てる)。試験のアプリは `test/native/net/net_l2n.app.rb`
(flash/ に入れず、利用者の `/home/l2n/` に写して起動する)。

```
# fmruby-core で (native:core が済んでいること)
ruby test/native/net/l2n_check.rb                      # 標準構成、全部
ruby test/native/net/l2n_check.rb --engine mruby       # 全 mruby
ruby test/native/net/l2n_check.rb --only wait --no-gate   # 受け口なしと比べる
```

`burst_ms` は、どれか 1 つのタスクがほかの全部を待たせた最長の時間
(port が tick をまとめて食わせた最大の数。`stats reset` から数える)。

| 条件 | 結果 |
|---|---|
| 1. gem が入る | `lib/add/family_mruby_native.rb` に sim と同じ 6 つ (picoruby-socket・picoruby-net-http・picoruby-net-websocket・picoruby-asterism-zenoh・picoruby-asterism・picoruby-fmrb-asterism)。`rake native:core` と `NATIVE_ENGINE=mruby rake native:core` の両方でリンクが通り、下の確認はどちらの構成でも通った (l2n_check 24/24 ずつ) |
| 2. TCP と、待つ間の画面 | 6 s 後に答える相手 (127.0.0.1:7601) と `TCPSocket` で往復: `tcp ok after 6003 ms: pong ping from native`。待っている間に 2.2 s 空けて撮った 2 枚でメニューバーの時計が進み、窓の題の帯 (色 [222,36,82]) を注入のドラッグで y=35 から y=105 へ動かせた。待ちの始めから答えまでの **burst_ms = 2** (受け口の呼び出し 2 回)。比べに `--no-gate` (受け口なし) では時計が止まり、窓は描かれもせず、**burst_ms = 6002** |
| 3. HTTP・HTTPS・名前解決 | 手元の WEBrick への `Net::HTTP` GET: `200 15B` 4 ms。`https://example.com/` (名前解決 + システムの CA で検証する TLS): `200 577B` 83-92 ms。手元の自己署名の HTTPS (localhost:7603): 例外 `IOError: Failed to connect to localhost:7603 - SSL connection failed` (stderr に OpenSSL の `certificate verify failed`)。`example.com:80` への接続 (localhost 以外の名前解決): 11-14 ms。WebSocket の往復 (手元の echo): `"hello l2n"` 147-152 ms。この間の **burst_ms = 2** (受け口の呼び出し 18 回)。受け口なしでは 80 ms (HTTPS 1 回の長さがそのまま全体の止まった時間になる) |
| 4. Zenoh | `/app/test/zenoh_echo.app.rb` (既存の試験のアプリ) を `tcp/127.0.0.1:7447` の zenohd (`docker compose up -d zenohd`) につないだ: 開くのに 3-8 ms。native → PC: `ruby tools/fmrb_zenoh.rb get fmrb/test/out` が `fmrb/test/out = 3`、2.2 s 後に `= 5`。PC → native: `ruby tools/fmrb_zenoh.rb put fmrb/test/in l2n-12531` のあと、アプリの窓に `in: l2n-...`、`received: 1  dropped: 0`。burst_ms = 2-4 |
| 5. kill | 答えない相手 (127.0.0.1:7605) を待つアプリを `fmrb_native.rb app kill`: 0.27-0.30 s で「Exited on request」(頼んだ終わり方。受け口の cancel 1 回)、相手はつながりが閉じたのを受け取り、job 0・相手へのソケット 0・ソケットの総数は起動前と同じ 3、5 s 後に補助のスレッド 0 (`/proc/<pid>/task/*/comm` に `fmrb-gate` 無し)。止めに応じないアプリ (待ちを繰り返し、イベントの輪に戻らない) では 1.2 s で消され (「forcing termination」)、delete hook が 1 件を片付け、`closed 1 socket(s) a deleted task left open`、同じくソケット 3・補助のスレッド 0。どちらの後も `app ps` が答える |
| 6. サービス | `svc[net] up: 10.255.255.254` (sim と同じ関数 `getifaddrs` の最初のループバックでない IPv4。WSL2 の上ではこの値)、`services: timesync disabled`、timesync は起動しない |
| 7. 既存 | `rake regress`: host 19/19、描画の記録 205/205。`rake regress:screens`: 26 passed / 0 failed (retro 8/8、modern 10/10、web 6/6、native 2/2、9.8 分)。`rake regress:live`: 16 passed / 0 failed (卓のゲームホスト・歩数・MIDI・音、sim の Zenoh を含む)。基準は更新していない。MCP の `ruby tools/mcp/selftest.rb` も全部通る |

## サービスとルータの場所

- `config/services_native.toml` を足し、`rake native:etc` がそれを最初の
  `/etc/services.toml` にする (今までは services_wasm.toml を写していた)。
  中身は sim の config/services.toml と同じ並びで、**timesync は
  `enable = false`**。仮に Services で有効にしても、timesync.rb は
  `FmrbConst::PLATFORM == "esp32"` のときしか時刻を設定しないので、
  native では「would set the clock」とログに出すだけ。Linux の時計は
  Family mruby からは変えない。
- **net** は有効。`FmrbApp.wifi_connected?` / `wifi_info` は native も sim と
  同じ `CONFIG_IDF_TARGET_LINUX` の枝 (`getifaddrs` の最初のループバックで
  ない IPv4、SSID とホスト名は空) を通るので、答え方は sim と同じ。値は
  走っている所で変わる (sim のコンテナでは 172.x、WSL2 の native では
  10.255.255.254)。OS に聞く本実装は L6。
- ルータの場所: `config/system_conf_native.toml` に
  `zenoh_router = "tcp/127.0.0.1:7447"`。`FmrbAsterism.settings` は
  `/home/asterism/config.toml` の router → この設定 → 資格情報 → 既定
  (`tcp/zenohd:7447`) の順に見るので、ブラウザ版と同じ置き場所になる。
- **すでに使っている利用者の場所には効かない**: `/etc` は最初の起動で
  写すだけなので、L1・L2 で作った ~/.local/share/family-mruby-dev/etc は
  古い services.toml (net・timesync なし) と zenoh_router の無い
  system_conf.toml のまま。新しい場所で起動するか、その etc を消して
  作り直す。更新で増えた項目を足す処理は L3。

## 回帰

- sim・端末・ブラウザ版に効く変更は `hosted/port/port.c` だけ (ブラウザ版も
  コンパイルする)。足した hook は設定しなければ何もせず、ブラウザ版は設定
  しない。`lib/add/family_mruby_native.rb` は native の libmruby にしか
  効かず、`rake native:mruby` の作り直しで足りた (`rake clean` は要らな
  かった)。lib/patch・submodule の中は変えていない。
- 結果は上の表の 7。regress:screens はブラウザ版を作り直して 6/6、
  regress:live は sim の Zenoh を使う卓のゲームホストを含めて 16/16。

## 見立てと違った点・撤回した仮説・踏んだ罠

- **見立ては A で合っていた**が、受け口の入口は「posix port に手を入れる」
  ではなく「リンクで差し替える」になった。gem が 3 つの経路で同じ系統の
  呼び出しをしていて、そのうち Zenoh の gem は lib/patch の外にあったため。
- **補助のスレッドは FreeRTOS に触れられないだけでなく、poll hook でも
  fmrb の allocator を使えない**。`fmrb_sys_free` は FreeRTOS の mutex を取る。
  poll hook は `vPortYield` の中 (切り替えの途中) で走るので、そこで待つ
  呼び出しはできない。手放した job の記録は最初 `fmrb_sys_malloc` で取って
  いたが、受け口の slot の中の scratch に移した (手放した job の後片付けが
  allocator を使わずに済む)。
- **OpenSSL のエラーの列はスレッドごと**。補助のスレッドで `SSL_connect` を
  呼ぶと、呼んだ側の `SSL_get_error` は空の列を見て、証明書の失敗を
  `SSL_ERROR_SYSCALL` と答える。結果としては失敗のままだが、意味がずれる
  ので `SSL_get_error` も差し替えた。
- **`close` では待っている `recv` は起きない** (Linux)。cancel は `shutdown`。
  名前解決だけは起こす手が無いので、手放す。
- **`burst_ms` は待たせた時間が終わったときに数える**。待っている最中に
  読むと小さいまま。比べの数字は答えが来た後に読んでいる。
- 確認の道具の罠: 制御のソケットの `stats` の接続は主スレッドが答えた直後に
  閉じるので、その直後に `/proc/<pid>/fd` を数えると 1 つ多く見える
  ことがあった (ソケットの片付けの失敗に見えた)。数える前に少し待つ。
  また `--keep` で残した native が debug の port を握ったまま次の確認が
  走り、別の native にアプリを起動していた。道具は最初に自分の run dir の
  ものを止める。
- picoruby-socket の `TCPSocket.gethostbyname` は名前をそのまま返す仮の
  実装で、名前解決はしない。名前解決の確認は `TCPSocket.new("example.com", 80)`
  で行った。
- flash/app/test/net_test.app.rb (クリックで HTTP・HTTPS・BADSSL・WS・
  TIMEOUT を走らせる) は native でもそのまま動く形だが、WS の相手が
  `host.docker.internal` 固定なので、確認には自前の試験のアプリを使った。

## 残件 (L6・L10 ほか)

- **L10 (pty)**: `fmrb_gate_call` に読み取りの job を渡す形で載る。ただし
  pty は `shutdown` で起きないので、待ちは時間つきの `poll` を繰り返すか、
  `poll` の job (scratch の写しで動き、手放せる) にする。
- **L6**: net の答えを NetworkManager に聞く。今は sim と同じ getifaddrs。
- 消して終わらせたアプリでは、ソケットは閉じるが、OpenSSL の SSL・
  zenoh-pico の session の malloc したメモリは残る (VM を finalizer なしで
  捨てるのと同じ理由。sim・端末も同じ)。
- 手放した名前解決は、resolver が答えるかあきらめるまで補助のスレッドを
  1 本使い続ける (その間に終わらないだけで、取り残しにはならない)。
  名前解決が遅い・答えない場合の時計の進みは、手元に遅い DNS を作れず
  確かめていない (速い名前解決では burst_ms 2)。
- slot (64) が埋まったときは受け口を使わずにそのまま呼ぶ (全体が止まる
  元の動き)。
- `stats` は CLI (`fmrb_native.rb stats`) だけで、MCP の native_* には
  出していない。`l2n_check.rb` は docker の zenohd と外のネットワークを
  使うので、回帰の道具 (`rake regress:*`) には入れていない。
- aarch64 (L3) では OpenSSL の開発用の物 (libssl-dev:arm64) がクロスの
  環境に要る。

## 最後に残っているビルド

- `build/`: regress:screens・regress:live が退避して戻した ESP32-P4 (TAB5)。
- `native/build/fmruby-native` (標準構成、regress:screens が作り直したもの) と
  `native/build-mruby/fmruby-native` (全 mruby): x86_64、ネットワークの gem 入り。
- docker の zenohd は確認の道具が起こし、終わりに止めた (起動前も止まっていた)。
