# Asterism: 計画

> 状態: 進行中 | 更新: 2026-10-06 | 異なる Ruby・機体・Web を一つのオブジェクトの網として扱う構想の実装計画。Z1 (sim)・Z2 (P4 実機、WiFi 越し)・Z3 (問い合わせ・生存の監視・ルータなしの直接の接続) 完了。A1 (遠くのオブジェクトの代理)・R1 (ROS 2 の最小の疎通)・R2 (ROS 2 のサービス)・R3 (メッセージ型の変換器)・C1 (CRuby 版) 完了

## 目的

CRuby、Family mruby (mruby / Spinel)、マイコン、Web、外部のサービスを、Ruby から一つの網 (分散した
オブジェクトの空間) として扱えるようにする。下の通信には Zenoh を使う。構想と使い方は同じディレクトリの
ruby_unified_discussion_summary.txt、usecases.md、pet_design.md、node_variants.md、remote_window.md、
名前は naming.md、Zenoh の調査は zenoh_idea.md。

## 決まっていること (これまでの議論)

- 名前は Asterism (2026-08-27)。下の通信層の名前として Silk を予約。gem 名 `asterism` は rubygems.org で
  取得済み (2026-10-06、0.0.0 は名前の確保だけ)。
- **使う人が触る層は mruby のアプリの VM** で動かし、ブロックやメタプログラミングを自由に使う
  (method_missing ほか 13 項目が sim で使えることを確認済み: report/metaprog_check.md)。**下回り
  (zenoh-pico、データの符号化、受信の取り出し) は C の gem** で速くする。Spinel にはしない。
- 受信は**ポーリング** (アプリの `_spin` から取り出す)。コールバックのスレッドを立てない (mruby の VM を
  スレッドをまたいで触らないため)。遅延の下限は `_spin` の周期 (約 50 ms) で決まり、用途上は十分。
- 高頻度のメッセージ (毎フレームの制御など) は対象外。
- zenoh-pico は **1.10.0 以上** (ESP-IDF のシングルコアでのビジーループの修正が入っている版)。1.x 系の中は
  ワイヤの後方互換が保証されるが、組み合わせの表は無いので、使う版を固定して記録する。
- **gem は Family mruby に依存しない作り**にする (素の PicoRuby / R2P2 でもビルドできる)。機体の差は
  zenoh-pico のポート層に任せる。
- セキュリティ: v1 は「信頼できる LAN の中」が前提 (ESP32 の端末は今は TLS を使っていない。ESP-IDF には mbedTLS があり、gem に TLS のリンクを足せば使える)。
- 内蔵 RAM を増やさない方針は Asterism にも適用する (専用のタスクを立てない、バッファは PSRAM に置けるか測る)。

## 全体の段階 (案)

| 段階 | 内容 | 目に見える成果 |
|---|---|---|
| **Z1** (完了) | sim で最小の疎通: picoruby-zenoh gem (zenoh-pico をリンク)、mruby アプリから put / subscribe、PC の zenohd と往復 | sim のアプリが送った値を PC で読める、PC から送った値がアプリの画面に出る |
| Z2 (完了) | 実機 (P4: NARYAv4 / Tab5) で WiFi 越しに同じ往復。P4 (RISC-V) で zenoh-pico が動くことを確定、flash / RAM の実測 | P4 の機体と PC が話す |
| Z3 (完了) | get / queryable (問い合わせと応答) と liveliness (生存の監視)。前半は sim と P4-Nano がルータ経由で、後半はルータなしで直接 (peer、シングルスレッドで成り立つか確かめる) | 機体どうしが問い合わせる |
| Z4 | S3。**AtomS3R (ヘッドレス、doc/headless_s3/) から行い、Retro は優先度を下げる** (2026-10-07 ユーザ決定)。flash の区画の見直しとセット | S3 の小型機が網に入る |
| A1 (完了) | Asterism の本体の最初: 遠くのオブジェクトの代理 (method_missing で呼び出しを get / queryable に載せる) と、キー空間の命名規則 | `home.lamp.on` のような呼び出しが別の機体で動く |
| A2 以降 | usecases.md の最小で成立する案 (家を each する、部品を借りる) → ペット (pet_design.md) など | デモ |

ブラウザ版 (wasm) から網に入る方法 (node_variants.md 2.7、zenoh-ts と remote-api) は、Z1 と並行して小さく
調べる (段階は Z3 の頃に決める)。

## Z1: sim で最小の疎通 (完了 2026-10-06)

結果は report/z1.md。zenoh-pico 1.10.1 / zenohd 1.10.1。受信はアプリの側からのポーリングだけで成立した
(読み取りのタスクは不要)。zenoh-pico の POSIX の TCP 層は gem 側の実装に差し替えている (データの無い poll が
止まらないこと、sim の EINTR、接続の時間制限)。Z2 でも ESP32 の TCP 層に同じ問題がある見込み。

Z1 の後の決定 (2026-10-06 ユーザ決定):

- ルータが消えたことに気づくまでの時間 (今は約 20 秒、put の失敗で分かる) は Z1 のまま。すぐ気づく形にするかは Z2 で決める。
- 自動の再接続は入れない (切れたらアプリが開き直す)。
- zenohd のポートは PC の中 (127.0.0.1) だけに出す。LAN に開くのは実機がつなぐ Z2 で。

### ゴール

Linux の sim の中で動く mruby のアプリと、PC で動く zenoh のルータ (zenohd) が、Zenoh で値をやり取りできる。

1. アプリから `put` した値を、PC 側で読める。
2. PC 側から `put` した値を、アプリが購読して受け取り、画面に出せる。
3. アプリを閉じて開き直しても、くり返し使える (セッションの後始末が漏れない)。

### 作るもの

- **zenoh-pico の取り込み**: PIN ファイル (`lib/add/ZENOH_PICO_PIN`) に版を固定し、rake で vendor/ (gitignore)
  に取得する。既存の `lib/add/PICORUBY_TI_PIN`、Spinel の `SPINEL_PIN` と同じ流儀。Linux の sim では
  zenoh-pico の Unix のポートを使う。
- **picoruby-zenoh gem** (`lib/add/picoruby-zenoh/`): 最小の API。
  - セッション: 開く (mode: client、接続先: `tcp/<PC>:7447`) と閉じる。
  - 送る: `put(key, payload)` (payload は String。Ruby の値の符号化は後の段階で決める)。
  - 受け取る: `subscribe(key)` が購読を返し、購読から**溜まった値を取り出す** (ポーリング。例: `each_pending`
    か `poll` で `[key, payload]` を返す)。zenoh-pico の受信は `zp_read` 相当をアプリの側から回す
    (読み取りのタスクを立てない形が作れるかを Z1 で確かめる。作れなければ案を返す)。
  - Ruby の名前は `Zenoh` (zenoh-pico の薄い皮、汎用)。Asterism はその上に別の層 (別の gem) として A1 で作る。
    (A1 で `Asterism::Zenoh`・`lib/add/picoruby-asterism-zenoh/` に改名した)
- **PC 側**: zenohd を親のリポジトリの docker compose に足し、sim と一緒に上げる (公式のイメージ、版を固定)。sim のコンテナから届くネットワークの設定を
  決める。確かめる道具は Ruby (REST プラグイン経由の HTTP の GET / PUT、または zenoh の CLI)。手順を
  tools/ に残す。
- **試しのアプリ**: `flash/app/test/` に置く (ランチャーには出さない)。送受信した値を画面に出す。

### 範囲の外 (Z1 ではやらない)

- 実機 (Z2)、get / queryable / liveliness (Z3)、Ruby の値の符号化 (msgpack など)、内部の pub/sub との
  ミラー (常駐の仕組み)、Asterism のオブジェクトの層 (A1)、ブラウザ版。

### 受け入れ条件

- ゴールの 1-3 が sim の標準構成と互換構成で通る。
- gem を Family mruby に依存しない形で書いている (Family mruby の API を呼ぶのは試しのアプリだけ)。
- 実機のファームのビルド (TAB5 / NARYAv4 / S3) に影響しない (Z1 では Linux だけでリンクする。ESP32 の
  ビルドに入れるのは Z2)。
- PC 側の手順 (zenohd の起動、確かめ方) が文書と tools/ にある。

## Z2: P4 実機で WiFi 越しの往復 (完了 2026-10-06)

結果は report/z2.md。NARYAv4 (P4-Nano) で確認した (Tab5 は後日の実機確認に回す)。

- gem は P4 のビルドだけに入る (S3 は Z4、wasm は対象外)。ファームは +58,944 B、区画の残り 13%。
- 内蔵 RAM は起動時に増えない。zenoh-pico の確保は PSRAM だけ。接続中は lwIP の側で約 2.4 KB 使い、
  閉じてから約 2 分で戻る (lwIP が閉じた接続を 120 秒持つため)。
- ESP-IDF の TCP 層も gem 側の実装に差し替えた (受信の時間制限が無く poll が止まる、接続の時間制限が無い)。
- PC 側は `docker-compose.zenoh-lan.yml` を重ねたときだけ 7447 を LAN に開く (REST の 8000 は PC の中だけ)。
  WSL2 (ミラーモード) では Windows の Hyper-V のファイアウォールに 7447 の受信の規則が要る。
- 切断 (ユーザ決定で案 1・2 を採用、リース切れもそろえた): 接続が切れる・送信が 3 秒で終わらない・リースが
  切れる、のいずれでもセッションを閉じ、`poll` が false、`closed?` が true、`put` が `Asterism::Zenoh::Error` (A1 の改名の後の名前)。自動の
  再接続はしない (アプリが開き直す)。zenohd の停止に気づくまで実機で約 0.5 秒、固まった場合は約 12 秒。
  WiFi が切れた場合は実機では測らない (リース切れと同じ経路)。

## Z3: 問い合わせ・生存の監視・peer (完了 2026-10-06)

結果は report/z3.md。sim と P4-Nano で確認した。

- `get` / `queryable` と `liveliness` (トークン・監視・今いる人の取り出し) を足した。受け取りは全部ポーリングで
  取り出す形 (コールバックの中で VM を触らない)。
- ルータなしの直接の接続 (unicast の peer) はシングルスレッドのまま成り立った (待ち受けは zenoh-pico が約 1 秒ごとに
  回すので、つながるまで最大 1 秒)。peer の上限は zenoh-pico の既定の 10 台 (絞るかは未決)。
- P4 のファームは Z2 から +30,896 B (区画の残り 12%)。起動時の内蔵 RAM の増分は 0。peer 1 台あたり内蔵 RAM を
  1.5-3 KB 使い、閉じれば戻る。
- 試験の後に内蔵 RAM が約 38 KB 戻らなかったのは zenoh ではなく、ESP-Hosted (WiFi のチップとの通信) が転送用の
  バッファを解放せずに溜める作りのためだった。P4 の WiFi の通信すべてで起きる。対応は doc/hosted_mempool/。

## A1: 遠くのオブジェクトの代理 (完了 2026-10-07)

結果は report/a1.md。sim と Tab5 で確認した。gem は `lib/add/picoruby-asterism/` (純 Ruby) と
`lib/add/picoruby-asterism-zenoh/` (改名した Zenoh のバインディング)。

- 受け入れ条件 1-9 を満たした。呼び出しの往復はルータ経由で約 35-240 ms。Tab5 のファームは +18,688 B (区画の残り 12%)、
  起動時の内蔵 RAM の増分 0。
- 制約: 答えを待つ呼び出しは、アプリの `on_update` から呼ぶか `async` を使う。C から Ruby へ戻る呼び出しごとに C の
  スタックが一段深くなり、`on_event` から待つと 16 KB のうち 12.4 KB を使う (gem の README に書いた)。
- 残り: 同じ機体の複数の Asterism アプリが同じノードのトークン (`asterism/<ID>`) を出すので、1 つ閉じると
  ノードが消えたように見える。入れ子の上限は 4 だが、P4 で測ったのは深さ 2 まで。


### ゴール

別の機体の Ruby のオブジェクトを、手元のオブジェクトと同じ書き方で呼べる。

```ruby
# 公開する側 (P4-Nano のアプリ)
Asterism.connect("tcp/192.0.2.2:7447", node: "fmruby-bbbbbb", app: "demo")
Asterism.expose("apu", apu, methods: [:play, :stop])

# 呼ぶ側 (sim のアプリ)
apu = Asterism["fmruby-bbbbbb/demo/apu"]  # <ID>/<アプリ>/<オブジェクト>
apu.play("t120 o4 cdefg")             # 音は P4-Nano から鳴る
apu.respond_to?(:play)                # => true (公開された一覧から)
Asterism.each("*/*/apu") { |a| a.stop }  # 生きている機体を回る
```

### 作り

- **層**: Zenoh gem (Z1-Z3) の上に、純 Ruby の gem `lib/add/picoruby-asterism/` (mrblib だけ) を置く。Family mruby に
  依存しない。値の包みは `MessagePack.pack` / `unpack` (CRuby の msgpack gem と同じ名前の API) だけを使う。
- **キー空間** (design.md 4 章): `asterism/<ID>/<アプリ>/<オブジェクト>` を根にする (Zenoh の `@` は管理用に予約
  されているので使わない)。
  - 呼び出し: `asterism/<ID>/<アプリ>/<オブジェクト>/call` に get。payload は `[メソッド名, 引数の配列, キーワードの Hash]`。
    答えは `["ok", 戻り値]` か `["error", 例外のクラス名, メッセージ]`。
  - 一覧 (メタ情報): `.../meta` に get。公開したメソッドの名前と引数の数。`respond_to?`・
    `methods`・エディタの補完の元になる。
  - 生存: `asterism/<ID>` (ノード) と `asterism/<ID>/<アプリ>/<オブジェクト>` を liveliness のトークンにする。`Asterism.each` / 一覧はこれで作る。
- **呼ぶ側の代理**: `method_missing` で呼び出しを get に変える (アプリの VM で method_missing・respond_to_missing?・
  キーワード引数が使えることは report/metaprog_check.md で確認済み)。
- **公開する側**: `queryable` で受け、公開したメソッドだけを `public_send` する。それ以外は "error" で返す。
- **ポーリング**: アプリの `on_update` から `Asterism.poll` を呼ぶ (Zenoh の poll と、届いた呼び出しへの応答)。
- **待ち**: 呼び出しが答えを待つ間は、その中で poll を回し、自分あての呼び出しにも答え続ける (互いに呼び合っても
  詰まらないため)。待つのはそのアプリだけで、ほかのアプリや画面は止まらない。

### 範囲の外 (A1 ではやらない)

- オブジェクトを参照のまま渡す (戻り値が別の代理になる)、ブロックを渡す、イベントの購読 (A2 以降)。
- CRuby (PC) の側の Asterism (zenoh の Ruby 版が要る。下の未確定事項 5)。
- Spinel のアプリ・カーネルからの利用 (method_missing を使うので mruby のアプリ VM に限る)。
- 認証・権限 (信頼できる LAN の前提のまま)。

### 受け入れ条件

1. sim と P4-Nano の間で、片方が公開した APU (音) と画面 (文字) を、もう片方から代理で呼べる。戻り値と例外が届く。
2. 公開していないメソッドは呼べない (呼ぶと Asterism::RemoteError)。`respond_to?` が公開の一覧と合う。
3. 相手が消えると、呼び出しは時間切れで `Asterism::Timeout` (アプリは止まらない)。`Asterism.each` から消える。
4. 互いに呼び合っても詰まらない。
5. gem は Family mruby に依存しない。sim の標準構成・互換構成、P4 のビルドが通り、起動時の内蔵 RAM は増えない。

### 決定事項 (2026-10-07 ユーザ決定)

- 全体の設計 (gem の分け方、環境ごとの接続層、キー空間、ROS 2、ブラウザ版) は design.md。
- **A1 を先にやる**。その後に ROS 2 の最小の疎通 (機体から `std_msgs/String`)、CRuby 版はその後。
- **モジュールの名前**: Zenoh のバインディングは `Asterism::Zenoh` (gem の名前は asterism-zenoh)。mruby 側にも最上位の
  `Zenoh` は残さない (まだ公開していないので互換は要らない)。A1 の最初に今の gem を改名する。
- **キー空間**: 最上位は `asterism/` (Family mruby とは別のプロジェクト)。`asterism/<ID>/<アプリ>/<オブジェクト>/call`・
  `.../meta`、生存は `asterism/<ID>` と `asterism/<ID>/<アプリ>/<オブジェクト>`。ID は機体なら基板ごとの mDNS 名
  (`fmruby-XXXXXX`)、gem は `node:` で受け取る。
- **peer の上限は 3** (zenoh-pico の既定の 10 から絞る。内蔵 RAM を優先)。
- 未確定だった 5 点は推奨案で決定: 呼び出しは答えを待つ形 (既定 2 秒) を基本に待たない形 (`async`) も用意する、
  公開するメソッドは明示する、渡せる値は MessagePack で表せるものだけ (Symbol は文字列になる、ほかは送る前に例外)、
  CRuby の側は A1 では作らず PC からは tools/fmrb_zenoh.rb で call / meta を試せるようにする。

## R1: ROS 2 (rmw_zenoh) との最小の疎通 (完了 2026-10-07)

結果は report/r1.md。P4-Nano と sim で確認した。

- P4-Nano のアプリが出した std_msgs/String を PC の `ros2 topic echo /chatter` で読め、`ros2 topic pub /chatter_back` が
  機体の画面に出る。`ros2 node list` / `topic list` に機体のノードとトピックが出て、アプリを閉じると消える。
- ROS 2 Jazzy + rmw_zenoh 0.2.11 (Zenoh 1.8.0)。ルータは既存の zenohd 1.10.1 を使う (rmw_zenohd は使わない)。
  PC 側は親リポジトリの `docker-compose.ros2.yml` を重ねて上げる (手順はそのファイルのコメントと report/r1.md 7 章)。
- gem: put / subscribe の attachment と `Session#zid` を足した。CDR と ROS のノード (`Asterism::CDR`、`Asterism::ROS`) は
  `picoruby-asterism` の中の純 Ruby。attachment はトピックでも必須だった (design.md 5 章を直した)。
- P4 のファームは +12,272 B (区画の残り 12%)、起動時の内蔵 RAM の増分 0。
- 残り: サービス (get / queryable の attachment)、transient local の QoS (zenoh-pico の高度な pub/sub、内蔵 RAM を測ってから)、
  ドメイン 0・名前空間 "/"・std_msgs/String 以外は未確認。

## ROS 2 の続き (順番、2026-10-07 ユーザ決定)

| 段階 | 内容 |
|---|---|
| R2 (完了 2026-10-07) | サービス (example_interfaces/srv/AddTwoInts)。機体が提供して PC の `ros2 service call` から呼べる、機体から PC のサービスを呼べる (P4-Nano・sim)。get / queryable の attachment と get の設定 (target ALL_COMPLETE、queryable は complete)。往復は機体から PC が中央値 49 ms、PC から機体が約 100 ms (50 ms のポーリング待ちを含む)。ファーム +8,864 B、起動時の内蔵 RAM の増分 0。型は手書き。結果は report/r2.md |
| R3 (完了 2026-10-08) | メッセージ型の変換器。`lib/add/picoruby-asterism/tools/asterism_msggen.rb` (CRuby、標準ライブラリだけ) が `.msg` / `.srv` から型を作り、型のハッシュを計算する (同梱 66 型と Jazzy の 6 パッケージ 335 個が Jazzy の JSON と一致)。値は普通のクラス (アプリの VM に Data も Struct も無い)。同梱の 62 ファイルは storage の `/usr/share/asterism/msgs` に置き、`Asterism::ROS.require_type` で使う型だけ読む (eval。picoruby の require は 1 ファイル 14 KB かかるため)。byte / uint8 / char の配列はバイナリの String。ファーム +8,384 B、起動時の内蔵 RAM の増分 0。結果は report/r3.md |

### R3: メッセージ型の変換器 (計画)

- 今 (R1・R2) は型ごとに Ruby のクラスを手で書いている: 型名 (`std_msgs::msg::dds_::String_` の形)、型のハッシュ
  (RIHS01、PC で調べた定数)、CDR への変換と CDR からの変換 (`Asterism::CDR`)。
- R3 では **PC (CRuby) のスクリプト**が `.msg` / `.srv` を読み、Ruby のクラス・型名・型のハッシュを作る。型のハッシュは
  PC で計算する (SHA-256 を機体で計算しない)。出来たものは純 Ruby で、機体 (mruby)・sim・将来の CRuby 版で同じものを使う。
- よく使う型は作って同梱する (std_msgs、geometry_msgs の Twist・Pose など、sensor_msgs の一部、example_interfaces)。
  ほかの型はユーザが自分の `.msg` から作って機体に置く。アプリが使う型だけを読み込み、全部は入れない (flash と VM の容量)。
- 値の形: `Data.define` の値オブジェクトを案にする (mruby に `Data` があるか確かめる。無ければ Struct か普通のクラス)。
  どれでも Hash から作れるようにする (`pub << { data: "hi" }`)。
- 型のハッシュの作り方は ROS 2 の版で変わりうるので、同梱の型は対象の版 (今は Jazzy) を明記して作る。
- 後回し: 知らない型を受け取る (ROS 2 Jazzy の型の定義の問い合わせを使えば、機体に型が無くても中身を読める可能性がある。
  流れている型を見るデバッグ用の表示に便利)。

## C1: CRuby 版 (完了 2026-10-08)

結果は report/c1.md。ユーザの「CRuby 版まで親の推奨で実装」の指示で、選択は親の推奨 (instruction_c1.md 0 章)。

- 新しい独立のリポジトリ `family-mruby/asterism/` (親リポジトリには無視させる。remote なし)。gem は `asterism-zenoh`
  (zenoh-c 1.10.1 のビルド済みを C 拡張で包む、mruby 版と同じ API) と `asterism` (純 Ruby の層の写し + CRuby の小さな追加)。
- 純 Ruby の層の正は fmruby-core の `lib/add/picoruby-asterism/`。`rake sync` で写し、一致を試験で確かめる。
- CRuby ⇔ P4-Nano / sim のオブジェクトの呼び合い、CRuby ⇔ ROS 2 (Twist、AddTwoInts)、CRuby ⇔ zenoh_echo、切断の扱いが通る。
  CRuby から P4-Nano の呼び出しは中央値 134 ms (機体が 50 ms ごとに答えるため)。
- zenoh-c のビルド済みのものに、不安定な API (高度な pub/sub、共有メモリ、querier、つながった相手の通知) も入っている
  (親が libzenohc.so の公開の名前で確認)。
- あとでユーザが決めること: 配布の形 (機種ごとのビルド済みの gem / 入れるときに zenoh-c を取る / Magnus 版)、純 Ruby の層の正の
  置き場所、CRuby らしいブロックの API、mruby 版との小さな違い (版の定数の名前、MAX_PEERS、自分のトークン)、gem の版と公開。

## C2・C3: ruby-asterism への分割と公開 (完了 2026-10-08)

- 3 つのリポジトリ (asterism / asterism-zenoh / picoruby-asterism-zenoh) を作り、ライセンスの表示を整えて (report/c3.md)
  2026-10-08 に**公開**した。zenoh-pico・zenoh-c は Apache-2.0 の側を選んで使う (fmruby-core の GPL-3.0 と組み合わせるため)。
  fmruby-core は `lib/add/ASTERISM_PIN`・`PICORUBY_ASTERISM_ZENOH_PIN` で https から取り込む (CI も取れる)。
- 以下は分割のときの決定:

- GitHub のオーガナイゼーション `ruby-asterism` を作成済み。リポジトリは非公開で、役割ごとに分ける
  (asterism / asterism-zenoh / picoruby-asterism-zenoh。instruction_c2.md)。
- Ruby だけで書いた層の正は asterism 側に移す。fmruby-core は PIN で取り込む側になる。
- CRuby らしいブロックの API は、いずれ足す。mruby 版との小さな違い (版の定数の名前、peer の上限、自分のトークン) はそろえない
  (CRuby 版はリッチな環境で動くため)。
- 配布は後で決める。まずは入れるときに C をコンパイルする形 (zenoh-c は入れるときに取る) が楽、という見立て。

## C4: `gem install asterism` (完了 2026-10-08、公開はユーザ)

- 配布は「入れるときに C をコンパイルする」(ユーザ決定)。asterism-zenoh の extconf が、ZENOH_C_PIN の版のビルド済みの zenoh-c を
  機種に合わせて取り、sha256 を確かめる (Linux x86_64/aarch64 の glibc・musl、macOS x86_64/arm64。macOS は未確認)。
  標準ライブラリだけで取得・展開する。取れないときは ZENOH_C_DIR か ASTERISM_ZENOH_C_MIRROR を案内して止まる。
- 版は 0.1.0。asterism は asterism-zenoh `~> 0.1.0` に依存 (asterism-zenoh だけ直した版を出せるように)。
- docker の素の Ruby 3.2 / 3.3 / arm64 / alpine で gem のファイルから入れて動くことを確認。結果は report/c4.md。
- 公開 (`gem push`) はユーザが行う。asterism-zenoh が先、asterism が後。rubygems.org の MFA が要る。

## C5: CRuby らしい API (完了 2026-10-08、未公開)

- 今のポーリングの API (機体と同じ) はそのまま。CRuby だけの層 (asterism の `lib/asterism/cruby/`) に、ブロック・受け取りの
  スレッド・Enumerator・パターンマッチ (`Data` の値、メッセージの `deconstruct_keys`) を足した。
  `Asterism::Zenoh.open { |s| }`・`Asterism.connect { |net| }`・`Asterism::ROS.connect { |ros| }`、`subscribe { }`・`every`・
  `on_join` / `on_leave`・`start` / `stop` / `run` / `spin`。結果は report/c5.md。
- 受け取りのスレッドは使う人が始めたときだけ。答えを待つ呼び出しは、受け取りのスレッドが動いていれば届くのを待つ。
- asterism-zenoh の C を 1 か所直した (閉じるときに別のスレッドが使う隙)。版は 0.2.0 (公開はユーザ)。
- 機体側に持っていける候補 (未実施): `node.every`・`poll` の中で呼ぶ `subscribe { }`、`on_join` / `on_leave`、`call_async` の
  シーケンス番号の読み方。

## C6: 機体側への持ち込み (完了 2026-10-08)

結果は report/c6.md。共有の層 (mrblib) に `Asterism.on_join` / `on_leave`、ROS の `node.every` とブロックつきの
`node.subscribe` (どちらも poll の中で呼ぶ。答えを待つ呼び出しの中では呼ばない)、メッセージの `deconstruct_keys`
(アプリの VM でも `case/in` が使える。ただしハッシュの型のパターンとブロックの中の束縛は picoruby のコンパイラで動かない) を入れた。
CRuby の層はこれを使う形にそろえた。キー m (呼び合い) は届いていて、CRuby の例が表示していなかっただけ。別に、zenoh-c で
答えより先に get の終わりが見える競合を見つけて直した (`done?` を先に読む)。起動時の内蔵 RAM の増分 0、スタックの減りは
Twist の型の読み込みの 624 B だけ。

## C7: zenoh-c の機能を CRuby から (完了 2026-10-08)

3 つのリポジトリに GitHub Actions の CI を足した (asterism-zenoh は Linux x86_64 / arm64・macOS arm64 / x86_64 × Ruby 3.2-3.4、gem を作って素の環境に入れる確認を含む。macOS の確認はここで初めて済んだ)。

結果は report/c7.md、対応表は asterism-zenoh の `docs/feature_coverage.md` (README の Feature coverage から辿れる)。
設定の受け渡し (TLS・QUIC・WebSocket・認証)、スカウティング、put の設定と delete、宣言した publisher / querier と matching、
値の付加情報、reply_err / reply_del、エンコーディング、時刻 (HLC)、キーの演算、高度な pub/sub、接続・リンクの通知、ログを入れた。
受け取りは今までどおり溜めて取り出す形。0.2.0 の呼び方はそのまま。入れなかったのは共有メモリ・裏で動く宣言・ze_serialize・
古い形の cache・get の取り消し。ROS 2 の transient local は高度な publisher で届くことを確認 (Asterism::ROS がまだ自動では使わない)。
機体 (zenoh-pico) へ持っていける候補は report の最後。

## gem の課題 (ベータ・1.0 に向けて、2026-10-09 の評価)

0.3.0 の時点の評価は「動く試作 (アルファ)」。実験・デモ・発表には十分、ほかの人が業務で頼れる段階ではない。
1.0 を名乗るのは、少なくとも 1-3 が済んでから。

1. **API の見直し** (0.x のうちに): 時間の単位がポーリングの層 (ミリ秒) と Ruby らしい層 (秒) で混ざっている。
   書き方が 2 通り (ポーリングとブロック) あって使い分けの案内が弱い。名前の揺れ (`peers` は数、`peer_zids` は一覧 など)。
2. **文書**: 英語の入門の手引き、API の説明書 (YARD など)、設計の考え方の英語の文書 (今は fmruby-core の日本語の文書だけ)。
   最初の 5 分で動かせる例。
3. **守りの既定**: 公開したメソッドは網の中の誰からでも呼べる。既定は平文・認証なし。README に「信頼できる LAN の外では
   TLS と認証を」と目立つように書き、設定の例を置く。
4. **ROS 2 の層**: QoS は既定だけ (transient local は高度な publisher で届くことは確認済みだが自動では使わない)。アクション
   なし。名前空間・ドメイン 0 以外は未確認。
5. **オブジェクトの網**: 値は MessagePack で表せるものだけ (参照のまま渡せない)。時間制限の既定 2 秒。同じ機体の 2 つの
   アプリが同じノードのトークンを出す件。
6. **入れるときの重さ**: zenoh-c (17 MB) をネットから取る、C コンパイラが要る、入れた後は RubyGems の決まりで 2 か所に置かれ
   34 MB。glibc 2.34 以上。Windows は未対応。
7. **性能を測っていない**: 受け取りのスレッドは何も来ないと 2 ms 眠る形。送受信の速さ・遅延・ノードの数を測ってから作りを決める。
8. **長時間の試験**: 実機と CRuby を何時間も動かし、切断とつなぎ直しをくり返す試験が無い。
9. **残っている不具合**: zenoh-pico どうしの peer で `liveliness_get` が終わらない。
10. **Ruby 4.0**: CI の行列が 3.2-3.4 だけ (指示書で親が 4.0 を入れ忘れた)。4.0.7 で手元で試したところ、入れる・試験 (asterism-zenoh 33 件)・
    ROS の型・パターンマッチは通るが、共有の層 (`mrblib/ros.rb` の `type_constant`) で「文字列リテラルを書き換えている
    (将来は凍結される)」警告が出る。直し方は mruby でも動く形 (`String.new` など) で。CI に 4.0 を足す。
    → 2026-10-09 対応済み: 文字列リテラルを凍結した状態で試験を回して書き換えの 3 か所を見つけ、`"".dup` に直した
    (asterism 92afc11)。CI に Ruby 4.0 と、凍結した文字列リテラルで試験を回す段を足した。
11. **同梱の型の不足**: `rcl_interfaces/msg/Log` (`/rosout`)・ParameterEvent・nav_msgs (Odometry など) が asterism の同梱に無い
    (console は Log を自分で生成している)。同梱を増やす。
12. **型にフィールドの型の情報が無い**: 生成した型は `FIELDS` の名前と既定値だけ。列の要素の型 (文字列の列か数の列か) が分からない。
    変換器が `FIELD_TYPES` を出すようにする。
→ 11-13 は 2026-10-09 対応済み (asterism 0.4.1: 同梱 84 型、`FIELD_TYPES` と `Asterism::ROS.field_types`、名前の検査)。tf2_msgs は BSD-3-Clause。
13. **`require_type` の名前の検査が甘い**: パッケージ名に `..` を入れると TYPE_PATH の 1 つ上を指せる。型の名前は網から来るので、
    パッケージ名と型名の文字を検査する。
14. **大きな型の読み解きが機体で遅い** (S1): P4 で Odometry を生成した型で読むと、更新の 1 周が 91-122 ms になった。要るフィールドだけを
    読む、配列を読み飛ばす仕組みを gem に入れる (Odometry、CompressedImage など)。

## V: Rails の画面 (asterism-console) と網のグラフ (2026-10-09 ユーザ決定、W より先に)

- 最初は「見る・触る」形 (V1、instruction_v1.md): Zenoh の網をグラフでリアルタイムに見せる。層は 4 つ。
  - ルータ・セッション (zenohd の管理用の空間)
  - Asterism のノード・アプリ・オブジェクト
  - ROS 2 のノード・トピック・サービス
  点をクリックすると詳細が出て、公開されたメソッドを画面から呼べる。キーに流れる値を眺められる。
- Rails は網に直接つながず、網とつながる専用のプロセス (bridge) 1 つが集めて Action Cable で配る。
- 認証は付けない (手元の網だけ)。外に出す前に足す。
- その後: W1 (ルータの中継) でルータどうしの図が本物になり、W2 (管理) で台帳・証明書・ACL を画面に重ねる
  (登録済みでつながっている / 来ていない / 未登録の相手を色で分ける)。
- リポジトリは Asterism の側 (asterism-console)。

### V2.5: ノードの自己紹介とアイコン (計画 2026-10-12 ユーザ)

V2.2 では、端末か CRuby かを名前の決まり (`fmruby-XXXXXX` / `linux` / `wasm-XXXXXX`) で判じていた。網が種類を運ばないためで、名前を変えた端末は ◈ になる。これを、ノードが自分で名乗る形にし、あわせてアイコンを出す。

- **gem (asterism 0.5.0)**: ノードが「自己紹介」の口を持つ。キーは `asterism/<ID>/@<口の名前>` (`@` で始まるので、アプリの名前とぶつからない)。中身は MessagePack の Hash:
  - `kind`: 動いている Ruby の処理系 (2026-10-12 ユーザ決定): `family_mruby` / `picoruby` / `cruby` / `console`。sim とブラウザ版も `family_mruby`。`board` という呼び方はやめる
  - `host`: 任意。どこで動いているか (`atoms3r` / `tab5` / `p4nano` / `browser` / `sim` など)。Family mruby のファームは自動で埋める。console はノードの詳細に出す
  - `name`: 表示名 (任意)
  - `icon`: PNG のバイト列 (任意。32x32 程度、8 KB まで)。PNG だけ受ける (SVG はスクリプトを含められるので受けない)
  - `icon_hash`: アイコンの要約 (console が覚えておき、同じものは取り直さない)
  - 機体側 (picoruby) と CRuby の両方に入れる。Family mruby の端末はファームが既定の自己紹介を出し (種類 `family_mruby`、アイコンは `/usr/share/asterism/icon.png` など)、アプリが上書きできる。CRuby は既定で `cruby`。ブラウザ版は `family_mruby` で `host` が `browser` (アイコンはブラウザ版用)。
  - アプリごとのアイコン (Family mruby のランチャーのアイコン) も出せるようにするかは、gem の設計のときに決める。
- **console (V2.5)**: 新しいノードが入ったら bridge が自己紹介を問い合わせる。答えがあれば種類で印を決め、アイコンを枠の名前の前に出す。答えが無いノード (古い版・ROS 2) は今の名前の決まりと印のまま。
- **公開**: gem の新しい版の公開はユーザが行う。
- **結果 (2026-10-12、report/v2_5.md)**: asterism 0.5.0 (ブランチ feature/profile、未 push) に `Asterism.profile` / `profile_of` / `Asterism::Profile`。端末の既定は fmruby-core の新しい gem `picoruby-fmrb-asterism` が VM の始めに `Asterism.profile(kind: :family_mruby, host: ...)` を呼んで出し、アイコンは gem の既定の場所 `/usr/share/asterism/icon.png` (ブラウザ版の束はそこへブラウザ版のアイコンを置く)。CRuby は `cruby` と gem 同梱の `data/icon.png`。console は自分を `console` と名乗る。アプリ単位のプロフィールは見送り、キー `asterism/<ID>/<app>/@profile` を後のために空けた。「Ruby プロファイル」の表は「Ruby の対応表」(英語 Ruby feature table、`docs/ruby_features.md`) に言い換えた (`profile/` の名前は変えていない)。
- **API の名前は `profile` に決定 (2026-10-12 ユーザ)**。`Asterism.profile(kind:, name:, icon:)`、キーは `asterism/<ID>/@profile`。asterism の既存の「Ruby プロファイル」の表は、紛れないよう「Ruby の対応表」に言い換えた (0.5.0)。検討した候補 (記録):

| 案 | 書き方の例 | キー | 感じ |
|---|---|---|---|
| (採用) profile | `Asterism.profile(kind: :family_mruby, name: "Rover", icon: png)` | `@profile` | SNS のプロフィール (名前・アイコン・紹介) と同じで、中身にいちばん合う |
| card (名刺) | `Asterism.card(kind: :board, name: "Rover", icon: png)` | `@card` | 名刺を差し出す。短く、中身 (種類・名前・アイコン) に合う |
| introduce (自己紹介) | `Asterism.introduce(kind: :board, name: "Rover", icon: png)` | `@intro` | 動詞で読みやすい。呼ぶと網に名乗る、という動きが分かる |
| identity | `Asterism.identity(kind: :board, icon: png)` | `@identity` | 「何者か」。認証の意味に読まれやすい (W2 の証明書と紛れる) |
| face | `Asterism.face(kind: :board, icon: png)` | `@face` | 見た目 (アイコン) に寄った名前。種類を入れると少しずれる |
| badge | `Asterism.badge(kind: :board, icon: png)` | `@badge` | console の「数の印 (badge)」とぶつかる |

  - ほかに見送ったもの: `info` (汎用すぎる)、`presence` (生存の監視と紛れる)、`avatar` (アイコンだけに寄る)。
  - console の凡例・印の説明も `board` から「Family mruby」などの処理系の名前に直す (V2.2 の英語の表記も含めて)。

### V の続き: ROS 2 の道具 (rosbag・rqt) の良いところを取り込む (2026-10-09 ユーザ決定、この順)

元の道具の役割を、ブラウザで共有でき、ROS 2 だけでなく Asterism のオブジェクト・機体・ルータの網も同じ画面で扱える形で取り込む。

| 段階 | 内容 | 元の道具 |
|---|---|---|
| V2 (完了 2026-10-09、console の docs/v2.md) | 見やすさ: ROS 2 のサービスはノードの属性 (数の印と詳細の一覧、パラメータ用は畳む)。トピックは送り手のノードから受け手のノードへの矢印 (線の名前がトピック名、相手のいないトピックはノードの属性)、点として出す形にも切り替えられる。Asterism のアプリとオブジェクトはノードの枠の中の入れ子 (cytoscape.js の入れ子の点)。トピックの頻度 (Hz) と最後の値を線と詳細に出す | rqt_graph、rqt_topic |
| V2.1 (完了 2026-10-11、report/v2_1.md、console の docs/v2_1.md) | 接続の描き方 (bridge の Bridge::Connections で組み立て、記録の巻き戻しにも効く。1 本の接続に複数のノードが乗るときは各ノードから線を引く。箱でまとめる案は図を壊したので採らない): 既定では Session (Zenoh の 1 本の接続、`client 9c2b4503` のような点) を描かず、ノードをルータへ直接線でつなぎ、線に役 (`client` / `peer`) を小さく書く。peer どうしの直接の接続はノードとノードの線にする。Zenoh の ID・役・アドレス・通信の種類はノードの詳細に出す。どのノードにも結び付かない接続は「その他の接続 N 本」に 1 つにまとめる。今の描き方 (Session を点で出す) は「回線を点で表示」の切り替えで残す (つながらない原因を探すとき用) | (rqt_graph は回線を描かない) |
| V2.2 (完了 2026-10-11、report/v2_2.md、console の docs) | 色と台帳の見え方 (種類の印は ▣ 機体・◈ CRuby・◎ console。網は種類を運ばないので名前の決まりで判じる。台帳から分かるつながり先はクラウドのルータだけ): Asterism の色は 1 色にし (青緑で作り、2026-10-11 ユーザの指示で Ruby の赤 `#cc342d` に変えた。ROS 2 のサービスの数の印は赤と紛れないよう灰青に)、段の違いは形で出す (ノード=その色の実線の枠で中は白・名前の前に種類の印、アプリ=灰色の破線の枠、オブジェクト=その色の小さな丸)。台帳 (Relay registry) の表示は既定でオフ。表示するときは、つながっていないものを「未接続 (台帳)」の枠にまとめ、つながるはずのルータが台帳で分かるものはそのルータへ破線を引く。今つながっているものの台帳の状態は、色の輪ではなく枠の角の小さな印で出す | |
| V2.3 (完了 2026-10-11、report/v2_3.md、console の docs/v2_3.md) | ROS 2 のノードを機械ごとに畳む (名前は管理者だけが付けられる。照合は名前の組が大きい方の半分以上一致。残る穴: docker の公開ポート越しに来る LAN の機器はみな docker の出入口の IP に見えるので、ROS 2 のノードを持つ機体が 2 台あると 1 つに畳まれる): ROS 2 は親の情報を運ばないので、接続元の IP が同じ ROS 2 のノード (2 つ以上) を 1 つの点に畳む (既定で畳む、クリックで開く)。名前は既定で IP。ユーザが名前を付けられ、名前はメンバーのノード名の組で覚える (IP は変わりうるため)。顔ぶれが大きく変われば IP の表示に戻る | rqt_graph の名前空間の畳み |
| V2.4 (完了 2026-10-11、report/v2_4.md、console の docs/v2_4.md) | 開いた機械の枠と、出入口の IP (出入口は bridge 自身の接続の手元の IP とルータから見えた IP の食い違いで見つける。設定 ASTERISM_GATEWAY_IPS でも与えられる): 開いた機械のメンバーを囲む四角を、飾りの絵 (入れ子の箱ではない) として描き、メンバーが動くたびに描き直す。メンバーは自由に動かせる。docker の公開ポートの出入口の IP から来た ROS 2 のノードは、機械が分からないので畳まない (S4 で端末 2 台が 1 つに畳まれるのを防ぐ) | |
| V2.5 (完了 2026-10-12、report/v2_5.md、console の docs/v2_5.md) | ノードの自己紹介とアイコン: 各ノードが種類・表示名・アイコン (PNG) を自分で出し (asterism 0.5.0 の `Asterism.profile`、キー `asterism/<ID>/@profile`)、console がノードの現れたときに 1 回取りに行って図に出す (印は処理系の名前、アイコンは名前の前、host と表示名は詳細)。アイコンは要約 (icon_hash) で覚え、同じアイコンは取り直さない。プロフィールを出さないノードは名前の決まりの印のまま。下の「V2.5: ノードの自己紹介」 | |
| V3 (完了 2026-10-09、console の docs/v3.md) | 見る道具: 数値のフィールド (例: `/cmd_vel` の `linear.x`) の時間の線グラフ。ログの一覧 (`/rosout` と機体のログ、重要度・送り手で絞る) | rqt_plot、rqt_console |
| V4 (完了 2026-10-09、console の docs/v4.md) | 記録と再生: 選んだキーを MCAP で記録し、時間の帯で見て再生する。ROS 2 は CDR のまま、Asterism は MessagePack のまま 1 つのファイルに入れる (ros2 bag と Foxglove でも開ける)。網の構造の変化も時刻付きで記録し、グラフを巻き戻せる。再生は本物の網に流すか、画面の中だけで見るかを選べる。MCAP の読み書きは Ruby で書く (公式の Ruby の部品は無い) | rosbag (ros2 bag)、rqt_bag |
| V5 | 網のための irb: 画面の中で Ruby を書き、網のオブジェクトや記録を触る。強い権限なので管理者だけ、記録付き | (無い。Ruby ならでは) |

## C8: コンパイラの case/in の直しと gem 0.4.0 (完了 2026-10-09)

結果は report/c8.md。上流 mruby の 2 コミットを `lib/patch/compiler/mruby-compiler-codegen.c` で当てた (submodule が
mruby-compiler2 `a2c72afb` 以降に進んだら外す)。3 つの gem を 0.4.0 にした (API の見直しの推奨案の第 1 段: 秒の `timeout:`、
`connection_count`、`Asterism::Error` の下のエラーの系統、非推奨の警告、文書の「Which API?」)。CI は全部緑、P4-Nano で確認、
起動時の内蔵 RAM の増分 0。1.0 に残すものは report の最後。

## S: シミュレータのロボットとつなぐ (計画 2026-10-09、V4 と gem 0.4.1 の後)

物理シミュレータ MuJoCo (オープンソース、Apache-2.0) の中のロボットを、Asterism の網に入れる。

- **動作の確認 (2026-10-09)**:
  - docker の中で `pip install mujoco` (3.15.0) し、画面なしで物理の計算が回ることを確かめた (落とした球が床で止まる)。
  - この PC の WSL には WSLg と GPU (RTX 4070) があるので、viewer の窓も出せる見込み。目で見る確かめはユーザ。
  - カメラの画像は EGL (GPU) か OSMesa (CPU) で窓なしに描く。EGL が WSL で動くかは試す。

| 段階 | 内容 |
|---|---|
| S1 (完了 2026-10-10、report/s1.md) | ROS 2 経由: MuJoCo を ros2_control とつなぐ部品 (mujoco_ros2_control など、Jazzy で使えるものを調べる) で 2 輪の台車を動かす。ROS 2 の docker の像に足す。Asterism の側は R1-R3 の ROS 2 の層のまま、Tab5 / CRuby から `/cmd_vel` を送り、`/joint_states`・`/odom` を受ける。asterism-console のグラフ・線グラフ・記録 (V4) で見る |
| S2 (完了 2026-10-10、report/s2.md) | Asterism で直接: CRuby が MuJoCo 3.15.0 の C の API を標準の Fiddle で直接呼んで物理を回し (構造体は固定した版のヘッダから求めた位置で読む、版が違えば止まる)、S1 の台車をノード `mujoco`・アプリ `rover` のオブジェクト (`drive.cmd/stop`、`state.pose/odom/imu/speed/all`、`world.reset/objects/info`) として公開する。CRuby・sim・P4-Nano・asterism-console から操縦した。実時間 500 歩/秒で 1 コアの 2-5 %、オドメトリの誤差 0.2 %・0.3 度。置き場所は asterism の `examples/mujoco/` (gem には入れない)。複数の操縦者は**最後の命令が勝つ** (止まっている側は送らない)。握っている側だけ・送り手ごとの優先の案は report/s2.md にあり、要れば決める。カメラは無い (S3 は S1 の ROS 2 の台車を使う) |
| S3 (完了 2026-10-10、report/s3.md) | 見せ場: P4-Nano から S1 の ROS 2 の台車を操縦し (`flash/app/test/rover_cam.app.rb`)、台車のカメラの JPEG を機体の画面に出す。JPEG は今ある `create_image` で出せた (P4 の表示側が先頭のバイトで JPEG と見て SoC の回路で解く。`/tmp` の RAM の上に 1 枚書いて読む)。1 枚の描画 9-12 ms、約 4.95 Hz、送り手から描画まで約 100 ms、内蔵 RAM は S1 と同じ。sim は PNG しか読まないので画像の代わりに大きさと頻度を出す。asterism-console でグラフ・画像・odom の線グラフが同時に動き、記録と巻き戻しも合う。手順書は親の `docker/mujoco/README.md` の S3 の節。Tab5 での確かめはユーザ |
| S4 (完了 2026-10-12、report/s4.md、手順は親の docker/mujoco/README.md の S4 の節) | 端末 2 台: P4-Nano が台車の頭脳 (`rover_brain`、`brain/rover` を公開、`/cmd_vel` を出すのは頭脳だけ、`go`/`turn` は `/odom` で止める)、ブラウザ版が操縦機 (`rover_pilot`、頭脳を生存の監視で見つける)。go(1.0) の誤差は数 mm、turn(90) は 2 度以内。呼び出しの往復はブラウザ → P4 で約 0.1 s。P4 の内蔵 RAM は S3 と同じ。**未決**: 呼んだ相手を知らせる `from:` を頭脳のメソッドに足した (gem は送り手を知らせないため)。残すか、gem で送り手を渡すかはユーザが決める |

- ほかのシミュレータ (参考): Gazebo (ROS 2 の標準、重い)、Isaac Sim (NVIDIA の GPU 前提、とても重い)、Webots (軽め)。最初の 1 台は、軽くて速く、Python からも C からも扱いやすい MuJoCo にする。
- 決定 (2026-10-09 ユーザ):
  - つなぐ部品は **mujoco_ros2_control** (ros-controls の公式、Jazzy に apt `ros-jazzy-mujoco-ros2-control` で入る、2026-09 に Jazzy 向けに出た)。ros2_control の標準の制御器 (diff_drive_controller など) と、RGB-D カメラ・LiDAR の部品が使える。
  - 台車のモデルは**簡単な 2 輪の台車を自分で書く** (箱の車体、2 輪、補助輪、カメラ、IMU。URDF / MJCF)。小さく、ライセンスの心配が無く、diff_drive_controller とそのまま合う。
  - カメラの画像は **160x120 の JPEG を 5 Hz** (sensor_msgs/CompressedImage、1 枚約 5 KB、毎秒約 25 KB)。Tab5 は MJPEG の再生の回路を持つので画面に出せる。中継と記録も軽い。

### S4: 端末 2 台 (計画 2026-10-11、2026-10-12 改訂)

S3 は「マイコンが ROS 2 のロボットを操縦する」だった。S4 はその一段上で、「マイコンが別のマイコンのオブジェクトを呼び、そのマイコンがロボットを動かす」を見せる。RubyKaigi の本命の切り口 (オブジェクトモデルを網に広げる、rubykaigi_talk.md) にそのまま重なり、実際のロボットの形 (ロボットに載った計算機 + 手元の操縦機) とも同じになる。

| 役 | 端末 | すること |
|---|---|---|
| 頭脳 | P4-Nano (今の構成のまま。HDMI は後で) | 頭脳のオブジェクトを公開する。ROS 2 の `/cmd_vel` を出すのは頭脳だけ。`/odom` を見て「1 m 進む」「90 度回る」の制御の輪を自分で回す。HDMI の大きな画面にカメラ・位置・今している仕事を出す |
| 操縦機 | ブラウザ版の Family mruby (B1) | 頭脳のオブジェクトを呼ぶ (キー)。カメラと頭脳の状態を画面に出す |
| 見る | asterism-console | 網の図 (V2.1 の描き方)、呼び出しの記録、線グラフ、記録と巻き戻し |

- 頭脳が `/cmd_vel` を出す唯一の者になるので、S2 で残った「複数の操縦者」の問題は形で解ける (操縦機は何台でも頭脳を呼べ、頭脳が順番を決める)。
- 頭脳のオブジェクトは下の叩き台で進める (2026-10-12 ユーザ「次の段を進めて」)。アプリ `brain`、オブジェクト `rover`:
  - `rover.drive(v, w)`: 直接の速さ。0.5 s 呼ばれなければ止まる (S2 の `drive.cmd` と同じ考え)。
  - `rover.go(m)` / `rover.turn(deg)`: 仕事を頼んですぐ戻る (仕事の番号を返す)。終わりは `rover.state` で見る。頭脳が odom で止める。
  - `rover.stop`: 仕事も速さも止める。
  - `rover.state`: 位置・向き・速さ・今の仕事と進み具合。
  - 仕事の途中で別の仕事や `drive` が来たときは、新しいものが勝つ (今の仕事は取り消し)。
- 段の分け方:
  - S4a: 頭脳 (P4-Nano のアプリ)。オブジェクトの公開、制御の輪、画面に状態を出す。sim でも動かす (ROS 2 の台車は sim からも呼べる)。
  - S4b: 操縦機 (ブラウザ版で動くアプリ。同じアプリが sim・P4 でも動く)。キーで頭脳を呼ぶ。カメラは ROS 2 のトピックを直接受ける (頭脳を経由しない)。
  - S4c (見送り、2026-10-12): Tab5 を傾けての操縦。操縦機をブラウザ版にしたため。Tab5 を使うときに戻す。
  - S4d: 見せ場の手順書。S3 の手順書に足す。
- 要るもの・確かめはユーザ:
  - 全部 Claude が自分で確かめられる (P4-Nano は遠隔の画面、ブラウザ版は web_* ツール)。最後に見せ場をユーザが見る。
- 見送った案: 台車を 2 台にする (同じ世界に 2 台、端末 1 台につき 1 台)。見た目は楽しいが、伝わる中身は S3 と同じで、ROS 2 の制御器を 2 台分に分ける作業が大きい。後で余裕があればやる。

## B: ブラウザ版 (wasm) を網に入れる (計画 2026-10-11 ユーザ決定、V2.4 の後)

ブラウザで動く Family mruby (wasm 版) を、ほかの端末と同じ Asterism のノードとして網に入れる。やり方は design.md 9 章 (zenoh-pico を Emscripten でビルドし、WebSocket でルータに直接つなぐ。Asterism の層は純 Ruby のまま共有する)。

| 段階 | 内容 |
|---|---|
| B1 (完了 2026-10-11、report/b1.md、手順は browser.md) | 手元の http の版で動かすところまで。zenoh-pico を Emscripten で入れ、WebSocket はページの主スレッドの JS が持ち、タスクの側とは共有メモリの輪 2 本で受け渡す (上流の ASYNCIFY 前提の層は使わない。見回り 1 回 0.014 ms)。ルータは `/home/zenoh_echo.txt` → 設定 `zenoh_router` (既定 `ws/localhost:7449`) → URL の `?zenoh=`。ノード名は既定で `wasm` (sim の `linux` と分けた)。`asterism_demo` が console の図に ▣ で出て呼べる、`rover_cam` で台車を操縦しカメラも出る (JPEG はブラウザ版の表示がそのまま読む)。wasm +168 KB・data +147 KB。zenohd の `ws/` は親の docker-compose.zenoh-ws.yml (127.0.0.1:7449) |
| B2 (完了 2026-10-12、report/b2.md、手順は browser.md) | https の版から網に入る: https のページは、手元 (ループバック) のルータへは `ws://` のまま、それ以外は `wss://` を自動で選ぶ (Chrome・Chromium・Firefox で確かめた)。手元以外のルータは TLS を終える中継 (親の docker-compose.zenoh-wss.yml、Caddy) 越し。ページの「Network」の欄でルータを設定し保存できる。ノードの ID はブラウザごとに `wasm-<16 進 6 桁>` を作って保存 (2 つ目のタブはその回だけの ID)。公開の版への反映はまだ: Chrome の手元のネットワークへの接続の許可 (Local Network Access) を実物のブラウザで確かめること、外のルータには本物の証明書が要ること、ルータに認証が無いこと |

- 確かめること (B1 の最初): zenoh-pico 1.10.1 の Emscripten のシステム層と WebSocket のリンクが動くか。受け取りの見回り (poll) が wasm 版のタスクの動き方の上で画面を止めないか。ブラウザの WebSocket は非同期なので、zenoh-pico の同期の読み書きとどう合わせているか。
- 止まる条件の目安: zenoh-pico の Emscripten の層が今の版で使えない (直すのに上流の大きな作業が要る) と分かったら、zenoh-ts を JS 連携で呼ぶ案と比べて返す。

## G: 対戦ゲーム (計画 2026-10-12 ユーザ)

遠隔地の Family mruby (と、ブラウザ版・CRuby) どうしで対戦する。Asterism の生存の監視で相手を見つけ、相手のゲームのオブジェクトを呼ぶ (`opponent.move(x, y)`) 形にする。発表の本命の切り口 (オブジェクトモデルを網に広げる) の見せ場にもなる。

| 段階 | 内容 |
|---|---|
| G0 (手元の通し稽古まで完了 2026-10-12、report/g0_w4.md、一式は asterism-console の deploy/vps/) | インターネットのルータと守り (下の「G0: インターネットのルータと守り」)。VPS への配置は手順書でユーザが行う。ブラウザ版に合言葉を渡す直しは G1 で済んだ (合言葉はフラグメント・1 回きり・入口で外す・接続は 4 時間まで、2026-10-12 ユーザ決定) |
| G1 (完了 2026-10-12、report/g1.md、手順は game.md) | 家の中 (LAN) で、交互に指す遊び (オセロなど) を端末 2 台かブラウザ版 2 つで遊ぶ。相手の一覧と待ち合わせ (on_join / on_leave)、手を呼び出しで送る、切れたときの扱い。asterism-console で観戦し、記録して巻き戻す。実時間の測り (G-RT の家の中の分) も済んだ。残り: 1 回きりの合言葉と、2 つの接続を開くアプリの食い違い (report/g1.md 8 章) |
| G1b (完了 2026-10-12、report/g1b.md。asterism 0.6.0 は feature/session-poll、公開はユーザ) | 結果: `Asterism.session` (CRuby は `net.session`) でアプリの接続が 1 本になり、オセロ・実時間の測り・操縦機が通し稽古の 1 回きりの合言葉で入れた。`Asterism.poll` はノード 5・オブジェクト 5 で P4-Nano 14.3 → 0.26 ms、AtomS3R 21.7 → 0.50 ms (同じファームで比べた)。台車の頭脳の ROS 2 のノードは console の図で端末の枠の中 (console の直し)。名前は `/home/asterism_profile.toml`、オセロの F2 で変えられ、console の図に `名前 (ID)`。計画: asterism 0.6.0 とアプリの直し: (1) **接続を 1 本にする**: `Asterism.session` で Asterism の Zenoh の接続を公開し、アプリが状態の配信 (`put`・`subscribe`) と ROS 2 のノードに同じ接続を使う。オセロ・実時間の測り・台車の頭脳・操縦機を 1 本に直す (1 回で使い切る合言葉と両立させる。短い時間だけ使い回す案などの付け焼き刃は入れない)。(2) **`Asterism.poll` を軽くする**: ノードの一覧は生存の印が変わったときだけ作り直し、確保を減らす。P4-Nano と AtomS3R で、ノード 5・オブジェクト 5 のときの 1 回の見回りの時間を直す前と後で比べる (目標 1 ms 前後)。(3) **自分の名前**: 名前はプロフィールの `name` に置き、全アプリで共通の保存先 (例: `/home/asterism_profile.toml`、ブラウザ版はブラウザの保存領域) から読む。オセロの待ち合わせの画面で変えられる。未設定ならノードの ID。gem の公開はユーザ |
| X0 (2026-10-12 ユーザ決定、G1b の次) | 網の決まりを言語によらない仕様にする (Py の節の段 0 を前倒し): 決まりの文書、Ruby に寄った形の洗い出しと決め直し、適合の試験、決まりの版。下の「X0: 言語によらない網の決まり」 |
| G1.5 (2026-10-12 ユーザ決定) | ACL を対戦向けに: (1) **役割で書く**: 台帳に「役割」(例: プレイヤー) を置き、許すキーをひな形で書く (`asterism/${名前}/game/**` を読み書き、`asterism/*/game/**` を読むだけ、など)。台帳の各行は役割を選ぶだけにし、console が `${名前}` を行の名前 (証明書の名前) に置き換えて ACL を作る。(2) **止めずに入れ替える**: 反映でルータを起動し直さず、動いているルータの ACL だけを新しくする (使う版 1.10.1 で、管理用の空間からの設定の書き換えが ACL に効くかを最初に確かめる。効かない・一部しか効かないなら、その範囲と代わりの案 (切れる人数の警告、空いている時間に反映) を書いて止まる)。対戦用のキーの決め方は G1 の作りに合わせる。詳しくは下の「G1.5: 役割と部屋」 |
| G2 | 同じ遊びを、クラウドのルータの中継 (W1) 越しに遊ぶ。家の端末は家のルータ経由 (今の端末は TLS を使っていない。gem に TLS のリンクを足せば直接つなぐ道もある)、ブラウザ版は `wss://` でクラウドのルータに直接 (B2)。遅れを測る |
| G3 | 多少の遅れを許す遊び (陣取り・落ちものの対戦で攻撃を送り合う、ターン制に近い速さのもの)。毎秒数回、自分の状態を `put` で配り、相手の最新だけを使う。相手の画面が少し遅れて見えても遊びが成り立つものに限る |

### G1.5: 役割と部屋 (計画 2026-10-12 ユーザ)

例: 3 人 (A・B・C) が 1 つのゲーム (アプリ `othello`) で遊ぶ。各プレイヤーのゲームは自分のノードの下にオブジェクトを出し、相手のオブジェクトを呼ぶ・状態を受け取る。ACL は「自分の `othello` に書ける・答えられる」「他人の `othello` を読める・呼べる」「相手を見つけられる (生存の印)」があればよい。

- **役割**: 台帳に役割を置き、許すキーをひな形で書く。
  ```
  役割「othello のプレイヤー」
    読み書き: asterism/${ノード}/othello/**
    読むだけ: asterism/*/othello/**
    見つける: asterism/*   (生存の印)
  台帳: A・B・C の行で役割を選ぶだけ
  ```
- **行ごとのノードの一覧**: ACL が相手を見分けるのは証明書の名前。家のルータ経由では証明書の名前 (例: `home-a`) と、ゲームを動かす端末のノードの ID (例: `fmruby-aaaaaa`) が違う。そこで台帳の各行に「この行が代わりに話してよいノードの ID の一覧」を持たせ、ひな形の `${ノード}` はその一覧で広げる (家 A の行: `fmruby-aaaaaa`、`wasm-aaaaaa` など)。端末が直接つなぐ場合 (G2) は、証明書の名前とノードの ID を同じにすれば一覧は 1 つ。
- **部屋 (後で、G2 で知らない人どうしが同じルータに入るようになってから)**: 単純な役割では、プレイヤー全員が全員の `othello` を読める (A・B・C の対戦を D も見られる)。閉じたいときは部屋を足す: 「部屋 othello-1: 参加者 A・B・C」から、console が各参加者に「自分の場所の読み書き + ほかの参加者のノードの読むだけ」を広げて作る。Zenoh のキーには「a か b か c」の書き方が無いので、相手ごとに 1 行ずつ作る。部屋を作る・人を足すたびに ACL が変わるので、止めずに入れ替える (G1.5 の 2) が効く。
- 進め方: G1.5 では役割と行ごとのノードの一覧と、止めずに入れ替えるところまで。部屋は G2 で。

- **範囲は「多少の遅れを許す遊び」まで (2026-10-12 ユーザ決定)**。速い実時間の遊び (格闘・シューティング・アクション) は、相手の動きの予測や入力の同期 (遅れを隠す工夫) が要り、Zenoh でゲームを作った例も見当たらず手探りになるので、扱わない。遊びを作るときは、往復 0.1-0.3 秒の遅れが見えても成り立つかを先に確かめる。
- **ただし、実時間でどうなるかは検証として見ておく (2026-10-12 ユーザ)**。遊びとして仕上げるのではなく、測るための試し (G-RT):
  - 2 台 (家の中と、VPS の中継越し) で、自分の位置を毎秒 20-30 回 `put` で配り、相手の位置をそのまま描く最小の試し (例: 画面の中を動く点)。端末どうし、端末とブラウザ版、ブラウザ版どうしの組み合わせ。
  - 測るもの: 片道と往復の遅れの分布 (中央値・最大・ばらつき)、途中で落ちる・順番が入れ替わる割合、毎秒の送る量、端末の更新の 1 周の時間と内蔵 RAM。console の記録 (MCAP) にも残す。
  - 見た目の確かめ (相手の点がどれだけ遅れて・跳んで見えるか) はユーザが行う。
  - 結果は report に数字で残し、「どこまでの速さの遊びなら成り立つか」の目安にする。遅れを隠す工夫 (予測・補間) は、試すとしても 1 つ (例: 相手の点の補間) まで。
- 外に出すには、クラウドのルータの本物の証明書と、だれがつなげるかの管理 (W2 の台帳と ACL) を本番の形にする。
- Retro (S3) で遊ぶには、AtomS3R の H3 で S3 に Asterism が載った後、Retro の flash と RAM の余裕を見る。

### G0: インターネットのルータと守り (計画 2026-10-12 ユーザ)

対戦はインターネットのサーバを経由する (2026-10-12 ユーザ)。最初はユーザの運用中の VPS に置く。守りは最初からちゃんとする。ポート単位の IP の絞り込みは「二重目の鍵」で、外しても中身が守られる作りにする。

- **置き場所**: VPS の docker のサービスの置き場に `asterism/` を 1 つ足す (既存のサービスと同じ流儀)。VPS の入口は VPS 本体の nginx (Certbot の証明書)。各サービスは `127.0.0.1` にだけ出し、nginx が転送する。今動いているサービスのポートと nginx の既存の設定は変えず、足すだけ。**公開の文書には VPS のドメイン・IP を書かない** (`router.example.org` などの仮名)。

| 用途 | 出し方 | 証明書 | IP の絞り込み |
|---|---|---|---|
| ブラウザ版 (`wss://`) | zenohd の `ws/` を `127.0.0.1` にだけ出し、nginx が TLS を終えて転送する | VPS の今の証明書 (Let's Encrypt) | 遊ぶ人の IP (最初) |
| ルータどうし・端末 (mTLS) | zenohd が自分で TLS を受けるポートを 1 つ外に出す | 自前の CA (W2 の `bin/signer`) で署名したもの | 遊ぶ人の家の IP |
| SSH | 今のまま | — | 管理者の IP |

- **決めたこと (2026-10-12 ユーザ、おすすめ)**:
  1. 端末のつなぎ方: 最初は家のルータ経由 (W1 の形)。G2 の中で端末に TLS (ESP-IDF の mbedTLS の上の TLS のリンク) を足し、VPS に直接つなげるようにする。
  2. ブラウザ版の認証: console でログインした人に、短い時間だけ有効な合言葉 (トークン) を出し、`wss://` の入口 (nginx) で確かめる。IP の絞り込みと二重。
  3. 最初に遊ぶ相手: IP が分かる相手だけ。相手の IP が変わったら、許す IP の一覧を直す。
- **守り**:
  - 全部 TLS。つなげる相手は証明書 (mTLS) で限る。
  - ACL: 対戦用のキーの範囲だけを許し、プレイヤーごとに自分の場所にだけ書ける。管理用の空間 (`@/**`) は外から見えない (W1 で確かめた形)。
  - 接続数の上限。
  - CA の秘密鍵は VPS の signer (Rails と別のコンテナ。Rails からは読めない) に置く (2026-10-12 ユーザ決定で改めた。下の W4 の「console は 1 つ」)。鍵の写しは VPS の外に保存する。
  - IP の絞り込みは、さくらのパケットフィルタと VPS の中のファイアウォール (ufw) の二重。設定は W2 の「台帳から設定を作る」形に乗せる (台帳 → ACL と許す IP の一覧)。
- **ブラウザ用の入口の形 (2026-10-12 ユーザ決定)**: 最初は **A: 今のドメインの専用の TLS のポート** (VPS のほかのアプリと同じ流儀。例: `wss://<ドメイン>:<専用ポート>/`、ポートは 32767 番以下)。今の証明書のまま使え、さくらのパケットフィルタでこのポートだけを遊ぶ人の IP に絞れる。**パケットフィルタを外すとき (知らない人も入れるとき) に、B: サブドメインの 443 番** (console と同じアドレス。console の画面の WebSocket `/cable` と振り分ける) に移す。B では IP の絞り込みは nginx の中で行う (443 番は Web サイトと共通でパケットフィルタでは絞れない)。console 自体は W4 の決定どおりサブドメインの 443 番 (管理の画面は nginx で IP を絞る)。
- **合言葉の守りの直し (2026-10-12 ユーザ、G1 の中で行う)**: (1) 合言葉は URL の `#` の後ろ (フラグメント) で渡す (ページを配るサーバのログに残らない)。ページはすぐに消す。(2) 1 回使ったら無効 (最初に成功した接続で使い切る)。(3) 入口でつないでいられる最長の時間を決める (長く遊ぶときは新しい合言葉でつなぎ直す)。(4) 入口からルータへ転送するとき、URL から合言葉を外す。
- **残る弱い点 (知らない人を入れる前に考える)**: ブラウザ版どうしはルータから見分けられず、合言葉を持つ人はどの `wasm-*` の場所にも書ける (なりすませる)。zenoh-pico にユーザ名とパスワードの認証が無いため、ルータでは直せない。当面はゲームの側で相手の手を確かめることと、知り合いの範囲に留めることで受け入れる。IP の絞り込みを外す前に対策を決める。
- **ルータなしで直接つなぐ (2026-10-12 ユーザ)**: ブラウザ版はすでに `wss://` で直接 (B2・G0)。Family mruby の端末も、拠点のルータなしで直接つなげると使いやすい。G2 の最初に **G2a** として置く: 端末に TLS (ESP-IDF の mbedTLS) の部分を足し、端末ごとの証明書 (mTLS) でクラウドのルータへ直接つなぐ (端末 1 台が 1 つの拠点)。確かめるもの: 内蔵 RAM (mbedTLS の確保を PSRAM に回す設定は sdkconfig の変更なのでユーザの承認を取る)、つなぐまでの時間 (1-2 秒の見込み)、PSRAM、証明書の入れ方 (後で「端末を console に登録する」流れを考える)。AtomS3R・P4-Nano・Tab5 で。
- **進め方**: まず手元の docker で同じ構成 (nginx の転送を含む) を作って確かめ、そのあと VPS に足す手順書を作る。VPS への操作 (ufw・nginx の設定・docker の起動) は管理者の権限が要るので、手順書をユーザが実行する (Claude が行うときは毎回確認を取る)。さくらのパケットフィルタはコントロールパネルでユーザが行う。nginx の再読み込みの前に `nginx -t`。

## P: Family mruby 以外の PicoRuby とつなぐ (構想 2026-10-12)

asterism の層は純 Ruby で、picoruby-asterism-zenoh は PicoRuby の mrbgem なので、Family mruby 以外の PicoRuby も網に入れられる見込み。

- **PicoRuby.wasm (ブラウザ)**: 2 つの道がある。
  - B1 の作り (zenoh-pico を Emscripten で入れ、WebSocket はページの JS が持つ) を、picoruby-asterism-zenoh の `ports/emscripten` として gem に移し、PicoRuby.wasm のビルドに入れる。ただし B1 の受け渡しは、タスクごとに Worker を持つ Family mruby の作り (共有メモリ) に合わせてある。PicoRuby.wasm の動き方 (主スレッドで動くか) に合わせて、受け渡しを単純にする必要がありそう。
  - Zenoh の公式の TypeScript の部品 (zenoh-ts) を、PicoRuby.wasm の JS 連携から呼び、その上に asterism の層を載せる。C を足さずに済むが、ルータに remote-api のプラグインが要る。
- **ほかの PicoRuby のマイコン** (R2P2 など): zenoh-pico が対応する系 (ESP-IDF、FreeRTOS + lwIP、Raspberry Pi Pico W など) なら、picoruby-asterism-zenoh の port を足す形で入れられる見込み。
  - **最初の 1 台は Raspberry Pi Pico 2 W に決定 (2026-10-12 ユーザ、着手は後で)。R2P2 から使えるようにする** (R2P2 のビルドに gem を入れ、R2P2 のシェルや R2P2 で動かすアプリから Asterism を使う。R2P2 の側に要る直しは、R2P2 の流儀 (上流の PicoRuby の作り) に合わせる)。RP2350 (内蔵 RAM 520 KB)、無線は CYW43439。picoruby-asterism-zenoh に `ports/rp2350/` (仮) を足し、時刻・メモリ確保・TCP (lwIP + cyw43) を書く。
  - **R2P2 はユーザの管理下に無い (2026-10-12 ユーザ)**。進め方: (A) R2P2 のビルドが外のビルドの設定 (使う gem の一覧) を指定して読めるなら、その設定ファイルだけを自分の側に置き、フォークしない。(B) できなければ R2P2 をフォークし、差分はビルドの設定と Asterism 用の小さな初期化 (WiFi のあとにルータへつなぐなど) だけに保ち、上流の更新を取り込みやすくする。置き場所は `ruby-asterism` の組織の下が候補。(D) 上流でも役に立つ変更 (外の gem を足しやすくする口など) は、PR の準備までにして、出すかはユーザが決める。まず A ができるかを確かめる。
  - **R2P2 (Raspberry Pi Pico) は FreeRTOS を使わず、Pico SDK の上で動く (2026-10-12 ユーザ)**。よって zenoh-pico の Pico 向けの層 (FreeRTOS + lwIP のソケット) は使えず、Pico SDK に合わせた層を自分で書く:
    - 無線と lwIP は Pico SDK の `pico_cyw43_arch` の、OS なしの形 (`_poll` か `_threadsafe_background`)。lwIP は NO_SYS なので BSD のソケットは無い。
    - **PicoRuby には rp2040 向けのソケットの層がすでにある** (picoruby-socket の `ports/rp2040/`: `TCPSocket_create/connect/send/recv(nonblock)/closed`、lwIP の altcp の上で、受け取りは前もって確保したバッファにため、待ちの間は `cyw43_arch_poll` を回す。`SSLSocket` (altcp_tls) もある)。gem の TCP のリンク (`picoruby_zenoh_link.h` の約束) を、この C の API の上に書くのが本筋。lwIP の raw API を自分で書かずに済み、R2P2 の作りにもそのまま合う。
    - SSLSocket があるので、Pico から TLS でルータへ直接つなぐ道もありうる (ESP32 の端末も、ESP-IDF の mbedTLS で gem に TLS のリンクを足せば同じことができる。今は使っていない)。後で確かめる。
    - gem はもともと単一スレッドで poll する作り (zenoh-pico のスレッドを使わない) なので、OS なしとは相性が良い。待ちの間に `cyw43_arch_poll` を回す。
    - 時刻は Pico SDK の時刻、メモリ確保は PicoRuby の確保 (または Pico SDK の malloc) に合わせる。
    - zenoh-pico・asterism の層・受け取りの列が RAM (PSRAM なしの 520 KB) に収まるか。
  - 最初の段は「WiFi につながり、ルータへ put / subscribe ができる」最小の試し。次に asterism の層を載せ、console の図に出す (V2.5 の profile では `kind: :picoruby`、`host: "pico2w"`)。
- どれも、まず小さな試し (つながって put / subscribe ができる) から。着手は未定。

## Py: Python と MicroPython を網に入れる (構想 2026-10-12 ユーザ、着手は未定)

Asterism の網は Zenoh の上の決まり (キーの形・中身の形) でできているので、Ruby 以外の言語も同じ網に入れられる。将来、Family mruby の MicroPython のコードを Asterism に入れ、素の MicroPython と Python (CPython) 用のライブラリも作る。「Ruby と Python のオブジェクトが同じ網で呼び合う」形。

- **段 0: 網の決まりを文書にする** (どの言語を足すにも先に要る。**2026-10-12 ユーザ: 手戻りを減らすため早めに行う。G1b の次、G1.5 の前に置く (下の X0)**): 今は決まりが Ruby の実装の中にしかない。言語によらない形で書き出す: キー (`asterism/<ID>/<app>/<obj>/call|meta`、`@profile`)、呼び出しと答えの中身 (MessagePack の形、引数・キーワード引数・戻り値)、例外の形 (クラス名とメッセージ)、meta (公開しているメソッドの一覧)、生存の印、プロフィール。CRuby の側で、その決まりに沿っているかの試験 (適合の試験) を作り、ほかの言語の実装もそれで確かめる。
- **入れ方の候補** (やさしい順):
  1. **Python (CPython)**: Zenoh の公式の Python の部品 (zenoh-python) の上に、純 Python のオブジェクトの層を書く。配り方は pip (名前は後で決める)。
  2. **Family mruby の MicroPython** (doc/micropython のゲストの VM): ファームにある zenoh-pico を MicroPython から使える C の口を足し、その上に MicroPython のオブジェクトの層。Ruby のアプリと同じ端末で、Python のアプリも網に出られる。
  3. **素の MicroPython** (ESP32 など): zenoh-pico を組み込んだ MicroPython の C のモジュールと、同じオブジェクトの層。
- プロフィールの `kind` に `cpython` / `micropython` を足す (V2.5 の決まりの拡張)。console の印とアイコンも。
- 置き場所 (リポジトリと組織) は後で決める (組織の名前が `ruby-asterism` なので、Python のライブラリをそこに置くかは要相談)。
- 気をつけること: オブジェクトの層を言語ごとに書き直すので、決まりの文書と適合の試験が無いと、言語ごとにずれる。段 0 を先にする。

## X0: 言語によらない網の決まり (計画 2026-10-12 ユーザ、G1b の次)

Asterism の網の決まりは、今は Ruby の実装の中にしかない。ほかの言語 (Python・MicroPython、Zenoh の部品がある Rust・C/C++・Kotlin/Java・TypeScript など) を足すほど決まりを変えにくくなるので、**仕様だけでも早めに言語によらない形にする** (手戻りを減らす)。Ruby の実装は手本 (参照実装) で、ほかの言語は決まりと適合の試験で追いかける。

- **書き出すもの**: キー (`asterism/<ID>/<app>/<obj>/call|meta`、`@profile`、生存の印)、呼び出しと答えの中身 (MessagePack の形)、例外の形、meta (公開しているメソッドの一覧)、プロフィール、使う Zenoh の機能の最低限 (query / queryable、liveliness、put / subscribe)、Zenoh の版 (1.x)。
- **Ruby に寄った形の洗い出しと決め直し** (決め直しはユーザが決める):
  - 引数: Ruby の位置の引数とキーワード引数を、「位置の引数の並び」と「名前つきの引数の Hash」のように、どの言語でも表せる形に。
  - シンボルと文字列の区別、Hash のキーの型。
  - 数: 整数の範囲、小数と整数の区別、時刻の表し方。
  - 例外: 「クラス名とメッセージ」を、言語によらない「種類とメッセージ」(と任意の詳細) に。
  - nil / 真偽 / バイト列 (MessagePack の bin) の扱い。
- **決まりの版**: meta かプロフィールに決まりの版 (例: `protocol: 1`) を入れ、違う版どうしが分かるようにする。今の 0.x の網との互換 (しばらく両方を受けるか) も決める。
- **適合の試験**: 決まりの文書に沿っているかを、CRuby と mruby の両方で確かめる試験。ほかの言語の実装も同じ試験 (例: 呼ぶ側・答える側の決まった一連のやりとり) で確かめられる形にする。
- 置き場所: 決まりの文書は asterism のリポジトリ (`docs/protocol.md` など、英語)。
- 1.0 (report/c8.md の残り) の前提にする。

## W: ルータどうしの中継と、Rails の管理画面 (計画、2026-10-08 ユーザ決定で C6 の後)

端末は LAN の中のルータにだけつなぎ (今の端末は TLS を使っていない。ESP-IDF の mbedTLS で TLS のリンクを足すことはできる)、インターネットを越える部分はルータどうし
(zenohd) を TLS と認証でつなぐ (design.md 8 章)。その設定を Rails の画面で管理する。

| 段階 | 内容 |
|---|---|
| W1 (完了 2026-10-09、report/w1.md) | ルータどうしの中継と守り: PC の中の docker でクラウド役と家役の zenohd を立て、mTLS (自前の CA で署名した証明書を持つルータだけがつながる) と ACL (証明書の名前ごとに書けるキーを絞る、管理用の空間を外から見せない) を付ける。sim と P4-Nano をそれぞれ別のルータにつなぎ、A1 の呼び出し・R1 のトピックが中継を通ることを確かめる。設定の項目名は使う版 (1.10.1) で確かめる |
| W2 (完了 2026-10-09、report/w2.md) | Rails の管理画面の最小の形: ルータの台帳 (モデル)、証明書の発行 (Ruby の OpenSSL)、ACL の編集、クラウドの zenohd の設定ファイル (JSON5) を作って起動し直す。正は Rails の DB に置き、設定ファイルは毎回そこから作る |
| W3 | 網の様子と操作: Rails 自身が asterism の gem でノードになり、管理用の空間 (`@/**`) と生存の監視からルータ・機体の一覧を読み、Action Cable でブラウザに出す。画面から機体のオブジェクトを呼ぶ (討議まとめ 17 節の `robot.remote` の形) |
| W4 (手元の通し稽古まで完了 2026-10-12、report/g0_w4.md) | asterism-console を Web に出す: VPS の G0 のルータの隣に console と bridge を置き、インターネットから使えるようにする。下の「W4: console を Web に出す」 |
| W5 (計画 2026-10-12 ユーザ、G2 の後) | ROS 2 のロボットをインターネット越しにつなぐ: どのトピックを家の外に出すかを、家のルータとクラウドのルータで選ぶ。下の「W5: ROS 2 のトピックを選んで外に出す」 |

- 守りの要点: CA の秘密鍵は Rails (Web の画面) と同じ所に置かない (署名だけを受け持つ別の仕組みか、クラウドの鍵の管理
  サービス)。管理画面には認証 (できれば多要素) を付ける。
- 反映: まずは設定ファイルを作り直して zenohd を起動し直す形 (つながっているルータは一度切れてつなぎ直す)。動かしたまま
  管理用の空間から書き換えられる項目 (特に ACL) が使う版で分かったら、そちらへ移る。
- 段が増えると往復が延びるので、A1 の呼び出しの時間制限を遠くの相手に合わせて延ばせるようにする。

### W4: console を Web に出す (計画 2026-10-12 ユーザ)

IP の絞り込みを外せば誰でも入れる状態にできるよう、絞り込みに頼らなくても守れる作りにする。

- **決めたこと (2026-10-12 ユーザ、おすすめ)**:
  1. 入口はサブドメイン (例: `asterism.<ドメイン>`。Certbot で証明書を足す)。ポート番号なしで案内できる。
  2. 未ログインの人には何も見せない (ログインの画面だけ)。
  3. 記録 (MCAP) は、最初は管理者だけが見られる。
- **役割**: 未ログイン (ログインの画面だけ) / 利用者 (招待制。網の図・線グラフ、許された呼び出し) / 管理者 (ルータの台帳・ACL・利用者の管理)。
- **守り**:
  - 利用者は招待制 (勝手に登録できない)。ログインの総当たりに制限 (回数の上限と待ち)。管理者は TOTP 必須。
  - **管理の画面 (台帳・ACL の反映・利用者の管理) は、全体の IP の絞り込みを外した後も、nginx で IP を絞るか SSH の転送でしか使えないようにする**。
  - **console は最終的にサーバの 1 つを正にする** (2026-10-12 ユーザ)。台帳・ACL・利用者・記録はサーバの console に置く。手元の console は開発の道具として残す (量の多い試しや、ネットがないとき)。
  - **証明書の発行もサーバの console で行う (案 A、2026-10-12 ユーザ)**。`bin/signer` を VPS に置き、CA の鍵は Rails と別のコンテナに置く (Rails から読めない)。この CA はこの網専用なので、VPS の管理者の権限を取られたときに増える被害は小さいと判断した。
  - **相手の秘密鍵はブラウザの中で作る** (2026-10-12 ユーザの「画面に出してコピペしたら消す」を受けて): Web Crypto で鍵の組を作り、サーバには公開鍵だけを送って signer が署名する。秘密鍵と証明書は画面に 1 回だけ出し (コピーとファイルに保存)、画面を離れたら残らない (失くしたら取り消して発行し直す)。秘密鍵は一度もサーバに届かない。表示の画面は保存させない (no-store)、ログに出さない、https か localhost でだけ開ける。
  - 将来、端末が増えたり外の人の端末を入れたりするときは、端末が自分で鍵を作って署名の依頼 (CSR) を出す形に進める。
  - 画面からの呼び出しは利用者ごとの許可を必須にし、記録を残す (今の作りでほぼ足りる)。
  - 本番の設定: 秘密の値、HTTPS だけの cookie、画面の安全の設定 (CSP)、画面の自動更新 (Action Cable) の WebSocket も同じ入口を通す。
  - カメラの画像などの重いトピックは、見る頻度に上限を付ける。
- **置き場所**: G0 と同じ `asterism/` の compose に console (Rails、`127.0.0.1` にだけ出す) と bridge (同じ VPS の zenohd に中でつなぐ) を足す。nginx にサブドメインのサーバを足して転送する。
- **進め方**: 手元の docker で本番と同じ構成 (nginx の転送・本番の設定) で確かめてから、VPS の手順書を作る。G0 と同じく、VPS への操作はユーザが手順書で行う。

### W5: ROS 2 のトピックを選んで外に出す (計画 2026-10-12 ユーザ)

ROS 2 (rmw_zenoh) のノードは、ルータで相手を見つけたあとはノードどうしで直接やりとりする。ロボットの中の通信は家の中で閉じ、インターネットを越えるのは、家のルータからクラウドのルータへ中継されるものだけ (W1 の形)。何を越えさせるかを、次の 3 つで選ぶ。

1. **欲しい人がいるものだけが流れる** (Zenoh の性質): ルータは向こう側に購読者がいるキーだけを転送する。ただし誰かが購読すれば流れるので、守りには使わない。
2. **ACL で出してよいトピックを決める (主の手段)**: ROS 2 のトピックの Zenoh のキーは `<ドメインの番号>/<トピック名>/<型>/<型の要約>` (R1)。**家のルータの「クラウドへ出ていく」向きの ACL** に、出してよいトピックだけを書く (例: `0/odom/**`、`0/cmd_vel/**`、`0/battery/**` を許し、ほかは拒む)。クラウドのルータにも家ごと (証明書の名前ごと) の ACL を書いて二重にする。相手を見つけるための生存の印 (`@ros2_lv/...`) を通すかも選ぶ (通せばクラウドの側から家の ROS 2 のノードの一覧が見える。通さなければデータだけ)。ROS 2 のサービスは、問い合わせと答えの両方の向きを許す。
3. **頻度を落とす**: zenohd のキーごとの転送の頻度の上限 (downsampling) で、重いトピックを絞る (例: カメラは家の中では 30 Hz、外へは 2 Hz)。使う版 1.10.1 の設定の名前を確かめる。

- 選び方の目安: 操縦の命令・状態 (`/cmd_vel`・`/odom`・電池) は許す。カメラの画像は許すとしても頻度を落とす (1-5 Hz)。ログ (`/rosout`)・パラメータ・内部の処理のトピックは拒む。サービスは要るものだけ。
- **console の台帳**: 行ごとに「インターネットに出す ROS 2 のトピック」の一覧 (トピック名で書き、console がキーの形に直す) と、トピックごとの頻度の上限を持たせる。家のルータの設定 (出ていく向きの ACL と頻度の上限) と、クラウドのルータの ACL の両方を、台帳から作る。G1.5 の「役割」の考え方に合わせる (例: 役割「台車を見せる家」)。
- **拠点と出入口のルータ (2026-10-12 ユーザ)**: 「家」と書いてきたものは、正しくは**拠点** (家・研究室・会場など、1 つの LAN のまとまり) のこと。1 つの拠点に複数のルータがあり得る (ROS 2 のロボットが自分の `rmw_zenohd` を持つ、PC と小さな機械の両方でルータを動かす、網が分かれている、ロボットが複数ある)。Zenoh のルータどうしはつなぎ合わせられ、全体で 1 つの網になる。そこで、**拠点ごとに出入口のルータを 1 つ決め、インターネット (クラウドのルータ) へつなぐのは出入口だけ**にする。証明書は拠点ごとに 1 つ。W5 の「外に出すトピック」の ACL と頻度の上限は出入口にまとめて書けば、拠点の中のどのルータから来たトピックにも効く。拠点の中のほかのルータ (ロボットの中のルータなど) は出入口にだけつなぎ、ロボットの中の ROS 2 の通信はロボットの中で閉じる。
  ```
  [ロボット A の rmw_zenohd] ─┐
  [ロボット B の rmw_zenohd] ─┼─ [拠点の出入口のルータ] ──mTLS── [クラウドのルータ]
  [端末・ブラウザ版・CRuby] ──┘
  ```
  - 拠点の中の複数のルータが勝手にクラウドへつながないよう、クラウドの側の ACL・台帳は「拠点ごとに 1 つの証明書」に限る。
  - 拠点の中のルータどうしは今は認証なし (LAN を信頼)。網が分かれている拠点では、拠点の中も TLS にするかを考える。
  - console の図に、出入口のルータの印 (どのルータが外へつながっているか) を足す。
  - 台帳の言葉も「家」ではなく「拠点」にそろえる (W2 の `zenohd-home` などの名前は例として残す)。
- 確かめ方: S1 の MuJoCo の台車を家のルータの側に置き、G0 の手元の通し稽古の構成 (クラウド役) から、許したトピックだけが見え、拒んだトピック (カメラを拒んだ場合など) が届かず、頻度の上限が効くことを確かめる。
- 着手は G2 の後。

## 決定事項 (Z1 の前、2026-10-06 ユーザ決定)

1. zenoh-pico は PIN ファイル + rake で取得する (submodule にしない)。
2. 下の層の gem は `Zenoh` (picoruby-zenoh、汎用)。Asterism は上に別の層として作る。(2026-10-07 に `Asterism::Zenoh` へ改名)
3. zenohd は親のリポジトリの docker compose に足し、sim と一緒に上げる。

## 進め方

- これまでどおり、段階ごとに指示書 (instruction_z1.md など) を書き、サブエージェントが実装して report を書き、
  親が検収する。仕様 (使う人から見た動き、API の名前) が変わる選択は、サブに選ばせずユーザが決める。
