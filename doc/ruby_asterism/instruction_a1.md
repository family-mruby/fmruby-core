# Asterism A1 指示書: 遠くのオブジェクトの代理

> 状態: 指示 | 更新: 2026-10-07 | Zenoh の gem を Asterism::Zenoh に改名し、peer の上限を 3 にし、その上に純 Ruby の picoruby-asterism (代理・公開・一覧) を作る。sim と Tab5 の間で APU と画面を代理で呼ぶ

前提として次を読むこと:
- plan.md の A1 の節 (「決定事項 (2026-10-07)」を含む)
- design.md の 2-4 章
- report/z1.md-z3.md
- report/metaprog_check.md
- gem の README (lib/add/picoruby-zenoh/README.md)

## 1. やること

### 1.1 改名 (A1 の最初、単独のコミット)

- Zenoh の gem のモジュールを `Asterism::Zenoh` にする。最上位の `Zenoh` は残さない。
- gem の名前は `picoruby-asterism-zenoh` (ディレクトリ `lib/add/picoruby-asterism-zenoh/`) にそろえる。
  - Rakefile・rakelib・`family_mruby_linux.rb` / `family_mruby_esp32p4.rb`・`components/picoruby-esp32/CMakeLists.txt` の
    参照 (P4 の分岐の zenoh の部分) も追従させる。
  - `lib/add/ZENOH_PICO_PIN` はそのままでよい。
- 例外のクラスは `Asterism::Zenoh::Error`。
- 試しのアプリ (`flash/app/test/zenoh_*.app.rb`) と README・report の中の例を追従させる。試しのアプリのキーは
  Zenoh の層の試験なので今のままでよい。
- 改名だけのコミットにする。sim (標準構成) で zenoh_echo の往復が通ることを確かめてから次に進む。

### 1.2 peer の上限を 3 に

- zenoh-pico の `Z_LISTEN_MAX_CONNECTION_NB` (Z3 で 10 だった) を gem の設定で 3 にする。vendor/ は書き換えない。
- README に書く。4 つ目の peer が断られることを sim で確かめる (Z3 と同じ方法でよい)。

### 1.3 picoruby-asterism (純 Ruby、`lib/add/picoruby-asterism/`、mrblib だけ)

- Family mruby に依存しない。使うのは `Asterism::Zenoh` と `MessagePack.pack` / `unpack` だけ。
- Linux と P4 のビルドに入れる (S3・wasm には入れない)。
- **API の目安** (plan.md の A1 のゴール):
  ```ruby
  Asterism.connect(locator, node: id, app: "demo")    # mode: / listen: は Asterism::Zenoh に渡す
  Asterism.expose("apu", obj, methods: [:play, :stop])
  Asterism.unexpose("apu")
  Asterism.poll                                       # アプリの更新ごとに呼ぶ
  p = Asterism["<ID>/<アプリ>/<オブジェクト>"]           # 代理
  p.play("cde")                                       # 答えを待つ (既定 2 秒)。戻り値が返る
  f = p.async.play("cde"); f.done?; f.value           # 待たない形
  p.respond_to?(:play); p.methods                     # meta から
  Asterism.each("*/*/apu") { |proxy| ... }            # 生きているものを回る
  Asterism.nodes                                      # 生きているノードの ID の一覧
  Asterism.close
  ```
- **キー**: design.md 4 章。
  - `asterism/<ID>/<アプリ>/<オブジェクト>/call` と `.../meta`。
  - 生存のトークンは `asterism/<ID>` と `asterism/<ID>/<アプリ>/<オブジェクト>`。
- **符号**:
  - call の payload は `[メソッド名, 引数の配列, キーワードの Hash]`。
  - 答えは `["ok", 戻り値]` か `["error", 例外のクラス名, メッセージ]`。
  - meta の答えは `{"methods" => [[名前, arity], ...]}`。
- **値**: MessagePack で表せるもの (nil・真偽・整数・浮動小数・文字列・配列・Hash) だけ。
  - Symbol は文字列になる。
  - それ以外を引数や戻り値に渡すと、送る前に `Asterism::EncodeError`。
- **例外**:
  - 相手で起きた例外は `Asterism::RemoteError` (クラス名とメッセージを持つ)。
  - 公開していないメソッドは `Asterism::RemoteError` (NoMethodError 相当)。
  - 答えが来ないと `Asterism::Timeout`。
  - 接続が切れていると `Asterism::Zenoh::Error` をそのまま、または `Asterism::Disconnected` に包む (どちらかに決めて README へ)。
- **待ちの間**: 答えを待つ間も poll を回し、自分あての call に答え続ける。
  - 2 台が互いに呼び合っても詰まらないこと。
  - 入れ子 (待ちの中で答えた call が、さらに呼ぶ) の深さには上限を付ける。
- **公開する側**:
  - 公開したメソッドだけを `public_send` する。
  - 引数の数が合わないときは `["error", "ArgumentError", ...]` を返す。

### 1.4 試しのアプリ (`flash/app/test/asterism_demo.app.rb` など)

- **ID**: 機体は基板ごとの mDNS 名 (Family mruby の API で取れるもの)。sim は `linux` か設定ファイル。上書きは
  `/home/asterism_node.txt`。接続先は `/home/zenoh_echo.txt` と同じものを読んでよい。
- **公開するもの**:
  - `apu`: MML を鳴らす `play(mml)` と `stop`。
  - `screen`: 窓に文字を出す `say(text)`。
  - `info`: 名前・空きメモリを返す `status`。
- **画面**: 生きているノードの一覧を出す。キーを押すと、相手の `screen.say` と `apu.play` を呼び、戻り値・例外・時間を表示する。
  - 自動で試す用に、一定間隔で相手の `info.status` を呼ぶ。
  - async の形も 1 か所で使う。
- 試験用に、公開していないメソッドを呼ぶ操作、互いに呼び合う操作も入れる。

### 1.5 PC 側

- `tools/fmrb_zenoh.rb` に `call <ID>/<アプリ>/<オブジェクト> <メソッド> [引数 JSON]` と `meta <...>` を足す。
- REST で get を出し、MessagePack で包んで送り、答えをほどいて表示する。MessagePack の符号化は Ruby の標準ライブラリに
  無いので、要る分 (小さい部分集合) を tools/ に純 Ruby で書く。外部の gem には頼らない。

## 2. 受け入れ条件

1. 改名の後、sim の標準構成・互換構成、P4 (TAB5 と NARYAv4) のビルドが通る。zenoh_echo と zenoh_nodes が動く。
   最上位の `Zenoh` がどこにも残っていない (grep)。
2. peer の 4 つ目が断られる。
3. sim と Tab5 の間で、片方が公開した `apu.play` (音。Tab5 はミュート中なので、鳴ったことはログか APU の
   状態で確かめる) と `screen.say` (画面。tab5_screenshot / sim_screenshot) を、もう片方から呼べる。戻り値と例外
   (`RemoteError`) が届く。async も動く。
4. 公開していないメソッドは呼べない。`respond_to?` と `methods` が公開の一覧と合う。
5. 相手のアプリを閉じると、呼び出しは時間切れで `Asterism::Timeout` (アプリは止まらない)。`Asterism.each` から消える
   (生存の監視)。開き直すと戻る。
6. 互いに呼び合っても詰まらない (両方が同時に相手を呼ぶ操作を 10 回)。
7. PC から `fmrb_zenoh.rb call` / `meta` で機体のオブジェクトを呼べる。
8. 内蔵 RAM: P4 (Tab5) の起動時の増分 0。動かしている間と閉じた後の値を書く。
9. 標準構成でエディタを起動して 1 打鍵。`rake test` が通る。S3・wasm の構成に変更が無い。

## 3. 触ってよい範囲

- fmruby-core:
  - `lib/add/picoruby-zenoh/` を `lib/add/picoruby-asterism-zenoh/` へ移す。
  - `lib/add/picoruby-asterism/`。
  - Rakefile / rakelib の zenoh の部分、`lib/add/family_mruby_linux.rb` / `family_mruby_esp32p4.rb`。
  - `components/picoruby-esp32/CMakeLists.txt` の zenoh の部分。
  - `flash/app/test/zenoh_*`・`asterism_*`。
  - `doc/ruby_asterism/report/a1.md`、gem の README、既存の report の中の例 (名前の追従だけ)。
- 親リポジトリ: `tools/fmrb_zenoh.rb` と、それが使う純 Ruby の MessagePack の部分。
- 触らないもの:
  - `.env` (TAB5 のまま。NARYAv4 は環境変数 `FMRB_HW_TARGET=NARYAv4` で)
  - sdkconfig / sdkconfig.defaults*、パーティションの表
  - submodule の中、vendor/ の中、fmruby-graphics-audio
  - S3 / wasm の構成、`docker-compose*.yml` のポート
  - picoruby-fmrb-msgpack (使うだけ。足りなければ止まる)

## 4. 止まる条件

- 起動時の内蔵 RAM の増分が 0 にできないとき、P4 の区画に収まらないとき。
- MessagePack の gem (picoruby-fmrb-msgpack) が A1 の値を正しく往復できない (直す必要がある) とき。
- アプリの VM で method_missing などが期待どおりに動かず、API の形を変える必要があるとき。
- 範囲の外の変更が要るとき、上の API の目安から大きく外れるとき、利用者から見た動き (切断の扱いなど) の選択が要るとき。
- 実機のボタン操作、PC の設定の変更が要るとき。

## 5. report (`report/a1.md`) に書くこと

- API の一覧 (メソッド、引数、戻り値、例外、時間切れ)。キーと符号の実際。
- 受け入れ条件 1-9 の結果と証拠、flash / 内蔵 RAM / PSRAM の増分、呼び出しの往復の時間 (ルータ経由、sim と Tab5 の間)。
- 見立てと違った点、撤回した仮説、踏んだ罠。A2 (参照の受け渡し・イベント) と ROS の最小の疎通への申し送り。

## 6. 作業の決まり

- ブランチ `feature/asterism-a1` を fmruby-core と親リポジトリの develop から切ってコミットする。
  - 英文、`<領域>: <要約>`、Co-Authored-By を付ける。
  - 改名は単独のコミットにする。
  - develop へのマージと push はしない。
- **機体**: **Tab5 (192.0.2.20)**。今 USB で繋がっていて、親の serial の capture もこれを向いている。
  - `.env` が TAB5 なので、普通の `rake build:esp32` がそのまま Tab5 用になる。
  - P4-Nano は今は外されているので使わない。間違った機体に焼かないこと: MCP の `flash` は capture の port に焼く。
  - 作業の前にミュートを確かめる (`GET /audio/mute`)。
  - シリアルは親の capture が動いている (`serial_start` を呼ばない。`serial_log` で読む)。
  - 焼くのは MCP の `flash` (`app_only`)。mDNS が引けないので tab5_* には ip を渡す。
- **zenohd**: LAN に開けるのは作業の間だけ。
- **終わったら**: sim は sim_down、`build/` は Linux の標準構成 (x86-64) に戻す。
