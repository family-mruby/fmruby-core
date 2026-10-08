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
Asterism.connect("tcp/192.168.10.2:7447", node: "fmruby-90bce8", app: "demo")
Asterism.expose("apu", apu, methods: [:play, :stop])

# 呼ぶ側 (sim のアプリ)
apu = Asterism["fmruby-90bce8/demo/apu"]  # <ID>/<アプリ>/<オブジェクト>
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

## C2: ruby-asterism への分割 (2026-10-08 ユーザ決定、進行中)

- GitHub のオーガナイゼーション `ruby-asterism` を作成済み。リポジトリは非公開で、役割ごとに分ける
  (asterism / asterism-zenoh / picoruby-asterism-zenoh。instruction_c2.md)。
- Ruby だけで書いた層の正は asterism 側に移す。fmruby-core は PIN で取り込む側になる。
- CRuby らしいブロックの API は、いずれ足す。mruby 版との小さな違い (版の定数の名前、peer の上限、自分のトークン) はそろえない
  (CRuby 版はリッチな環境で動くため)。
- 配布は後で決める。まずは入れるときに C をコンパイルする形 (zenoh-c は入れるときに取る) が楽、という見立て。

## 決定事項 (Z1 の前、2026-10-06 ユーザ決定)

1. zenoh-pico は PIN ファイル + rake で取得する (submodule にしない)。
2. 下の層の gem は `Zenoh` (picoruby-zenoh、汎用)。Asterism は上に別の層として作る。(2026-10-07 に `Asterism::Zenoh` へ改名)
3. zenohd は親のリポジトリの docker compose に足し、sim と一緒に上げる。

## 進め方

- これまでどおり、段階ごとに指示書 (instruction_z1.md など) を書き、サブエージェントが実装して report を書き、
  親が検収する。仕様 (使う人から見た動き、API の名前) が変わる選択は、サブに選ばせずユーザが決める。
