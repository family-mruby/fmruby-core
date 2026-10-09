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
- セキュリティ: v1 は「信頼できる LAN の中」が前提 (ESP32 の zenoh-pico は TLS がまだ使えない)。
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

## W: ルータどうしの中継と、Rails の管理画面 (計画、2026-10-08 ユーザ決定で C6 の後)

機体は LAN の中のルータにだけつなぎ (zenoh-pico は ESP32 で TLS が使えない)、インターネットを越える部分はルータどうし
(zenohd) を TLS と認証でつなぐ (design.md 8 章)。その設定を Rails の画面で管理する。

| 段階 | 内容 |
|---|---|
| W1 (完了 2026-10-09、report/w1.md) | ルータどうしの中継と守り: PC の中の docker でクラウド役と家役の zenohd を立て、mTLS (自前の CA で署名した証明書を持つルータだけがつながる) と ACL (証明書の名前ごとに書けるキーを絞る、管理用の空間を外から見せない) を付ける。sim と P4-Nano をそれぞれ別のルータにつなぎ、A1 の呼び出し・R1 のトピックが中継を通ることを確かめる。設定の項目名は使う版 (1.10.1) で確かめる |
| W2 (完了 2026-10-09、report/w2.md) | Rails の管理画面の最小の形: ルータの台帳 (モデル)、証明書の発行 (Ruby の OpenSSL)、ACL の編集、クラウドの zenohd の設定ファイル (JSON5) を作って起動し直す。正は Rails の DB に置き、設定ファイルは毎回そこから作る |
| W3 | 網の様子と操作: Rails 自身が asterism の gem でノードになり、管理用の空間 (`@/**`) と生存の監視からルータ・機体の一覧を読み、Action Cable でブラウザに出す。画面から機体のオブジェクトを呼ぶ (討議まとめ 17 節の `robot.remote` の形) |

- 守りの要点: CA の秘密鍵は Rails (Web の画面) と同じ所に置かない (署名だけを受け持つ別の仕組みか、クラウドの鍵の管理
  サービス)。管理画面には認証 (できれば多要素) を付ける。
- 反映: まずは設定ファイルを作り直して zenohd を起動し直す形 (つながっているルータは一度切れてつなぎ直す)。動かしたまま
  管理用の空間から書き換えられる項目 (特に ACL) が使う版で分かったら、そちらへ移る。
- 段が増えると往復が延びるので、A1 の呼び出しの時間制限を遠くの相手に合わせて延ばせるようにする。

## 決定事項 (Z1 の前、2026-10-06 ユーザ決定)

1. zenoh-pico は PIN ファイル + rake で取得する (submodule にしない)。
2. 下の層の gem は `Zenoh` (picoruby-zenoh、汎用)。Asterism は上に別の層として作る。(2026-10-07 に `Asterism::Zenoh` へ改名)
3. zenohd は親のリポジトリの docker compose に足し、sim と一緒に上げる。

## 進め方

- これまでどおり、段階ごとに指示書 (instruction_z1.md など) を書き、サブエージェントが実装して report を書き、
  親が検収する。仕様 (使う人から見た動き、API の名前) が変わる選択は、サブに選ばせずユーザが決める。
