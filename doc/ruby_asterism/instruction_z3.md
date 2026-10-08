# Asterism Z3 指示書: 問い合わせと応答、生存の監視、機体どうしの直接の接続

> 状態: 指示 | 更新: 2026-10-06 | picoruby-zenoh に get / queryable と liveliness を足し、sim と P4 実機の 2 つの機体の間で問い合わせる。後半で、ルータを通さない直接の接続 (peer) を確かめる

前提として plan.md と report/z1.md・report/z2.md を読むこと。Z1・Z2 の作り (ポーリング、受信のリング、
gem の独立、PSRAM だけから確保、切断したら閉じる) はそのまま守る。

## 1. 前半 (Z3a): ルータ経由で get / queryable / liveliness

2 つの機体は **Linux の sim** と **P4-Nano (NARYAv4、192.0.2.15)**。どちらも PC の zenohd に client で
つなぐ (P4 は Z2 と同じく LAN 用の compose を重ねる)。Tab5 は繋がっていないので使わない。

### 1.1 API の目安

細部の名前はサブが決めてよい (report に一覧を書く)。ただし受信は Z1 と同じく**ポーリングで取り出す形**に
そろえ、zenoh-pico のコールバックの中で VM を触らない。

```ruby
# 問い合わせる側: 答えを待たずに戻り、poll の後で答えを取り出す
g = s.get("fmrb/node/p4/info", 2000)        # 時間制限 (ms)
s.poll
g.each_reply { |key, payload| ... }         # 届いた答え
g.done?                                     # 全部届いたか、時間切れ

# 答える側: 問い合わせを溜め、取り出して答える
qa = s.queryable("fmrb/node/p4/**")
qa.each_pending { |q| q.reply(q.key, "...") }   # q.key / q.params / q.payload
# 答えずに捨てた問い合わせは、取り出しの後で片付ける (問い合わせ側は時間切れで終わる)

# 生存の監視
tok = s.liveliness("fmrb/alive/p4")         # 持っている間「生きている」
w = s.liveliness_watch("fmrb/alive/**")     # 現れた / 消えた を溜める
w.each_pending { |key, alive| ... }         # alive は true / false
```

- 問い合わせを後で答えるには、コールバックの中で zenoh-pico の query を複製 (`z_query_clone`) して
  リングに置く。リングの上限と、溢れたときの扱い (捨てて数える) は購読と同じ考え方。
- 1 つの問い合わせに複数の答えが来ることがある (複数の機体が同じキーに答える)。`each_reply` は全部返す。
- 既に購読している人がいるかの扱い (liveliness の「今いる人」の取り出し) も確かめる。zenoh-pico に
  liveliness の get があれば `liveliness_watch` を始めたときに今いる人を `alive=true` で出す。

### 1.2 試しのアプリ

- `flash/app/test/zenoh_nodes.app.rb` (仮の名前): 起動すると
  - 自分の名前で liveliness を出す (`fmrb/alive/<名前>`)。名前は `/home/zenoh_node.txt` があればその 1 行、
    無ければ機種の名前 (`FmrbConst::HW_FAMILY` など。試しのアプリなので fmrb の API を使ってよい)。
  - `fmrb/node/<名前>/info` に queryable で答える (名前、起動からの秒数、空きメモリなど短い文字列)。
  - `fmrb/alive/**` を見て、生きている機体の一覧を画面に出し、それぞれに数秒おきに get して答えを出す。
- 接続先は Z2 と同じく `/home/zenoh_echo.txt` を読む (共通にしてよい)。

### 1.3 PC 側

- `tools/fmrb_zenoh.rb` に問い合わせ (`query <key>`、REST の GET は問い合わせとして届く) と、生存の一覧
  (REST で取れるなら) を足す。取れなければ report に書く。

### 1.4 受け入れ条件 (前半)

1. sim (標準構成と互換構成) と NARYAv4 のビルドが通る。P4 の区画の残りを書く (Z2 で 13%)。
2. sim と NARYAv4 で試しのアプリを同時に動かすと、どちらの画面にも相手と自分が「生きている」と出て、
   相手への get の答えが出る (`sim_screenshot` と `tab5_screenshot`)。
3. PC から `query fmrb/node/<名前>/info` で各機体の答えが読める。
4. 片方のアプリを閉じると、もう片方の画面から数秒以内に消える (liveliness の消滅)。開き直すと戻る。
   5 回くり返して異常が無い。
5. 相手が答えない (時間切れ) とき、get は時間制限で `done?` になり、アプリは止まらない。
6. 内蔵 RAM: P4 の起動時の増分 0 (Z2 と同じ比べ方)。動かしている間の内蔵 RAM と PSRAM を書く。
7. Z1・Z2 の試しのアプリ (zenoh_echo) が今も動く (sim の往復 1 回)。標準構成でエディタを起動して 1 打鍵。
   `rake test` が通る。S3 / wasm の構成に変更が無い。

## 2. 後半 (Z3b): ルータを通さない直接の接続 (peer)

前半が通ってから行う。

- zenoh-pico の unicast の peer (`Z_FEATURE_UNICAST_PEER`) が、**シングルスレッド (読み取りのタスクなし)
  で、ポーリングだけで**待ち受け (accept) と送受信ができるかを確かめる。
- 確かめ方: NARYAv4 のアプリが peer で `tcp/0.0.0.0:7447` を待ち受け、sim のアプリが peer で
  `tcp/192.0.2.15:7447` に接続する (sim のコンテナからの外向きの接続は届く)。zenohd は止めておく。
  put / subscribe と get / queryable が通るか。
- API は `Session.open` に mode を渡す形の目安: `Zenoh::Session.open("tcp/...", mode: :peer, listen: "tcp/0.0.0.0:7447")`
  (mruby のキーワード引数が使えなければ位置引数でよい)。
- 待ち受けで内蔵 RAM がどれだけ増えるか (lwIP の待ち受けのソケット)、接続できる相手の数の上限を測る。
- **シングルスレッドでは peer が成り立たない** (読み取りのタスクやスレッドが要る) と分かったら、作らずに
  止まる。分かったことと案を report に書く。

### 2.1 受け入れ条件 (後半)

8. zenohd なしで、sim と NARYAv4 の間で put / subscribe と get / queryable が通る。
9. 待ち受けている側のアプリを閉じて開き直しても、相手がつなぎ直せば通る (自動の再接続はしない方針のまま。
   つなぎ直すのは接続する側のアプリの開き直し)。
10. 待ち受けの内蔵 RAM の増分と、相手の数の上限を書く。

## 3. 触ってよい範囲

- fmruby-core: `lib/add/picoruby-zenoh/` (zenoh-pico の機能の有効化を含む)、`flash/app/test/zenoh_*`、
  `doc/ruby_asterism/report/z3.md`。gem の README。
- 親リポジトリ: `tools/fmrb_zenoh.rb`。
- 触らないもの: `.env` (TAB5 のまま。NARYAv4 のビルドは環境変数 `FMRB_HW_TARGET=NARYAv4` で渡す。.env は
  書き換えない)、sdkconfig / sdkconfig.defaults*、パーティションの表、submodule の中、fmruby-graphics-audio、
  S3 / wasm の構成、`docker-compose*.yml` のポートの設定 (LAN に開けるのは Z2 の重ね合わせのファイルで)。

## 4. 止まる条件

- 内蔵 RAM の起動時の増分が 0 にできないとき。
- P4 の区画に収まらないとき。
- sdkconfig の変更が要るとき (変えずに案を書く)。
- 範囲の外の変更が要るとき、上の API の目安から大きく外れる必要があるとき、利用者から見た動き (切断の
  扱い、再接続など) の選択が要るとき。
- 後半で、peer にシングルスレッドの外のもの (タスク・スレッド) が要ると分かったとき。
- 実機のボタン操作や、PC の設定の変更 (管理者の権限) が要るとき。

## 5. report (`report/z3.md`) に書くこと

- 有効にした zenoh-pico の機能と、flash / 内蔵 RAM / PSRAM の増分。
- API の一覧 (メソッド、引数、戻り値、例外、時間切れの扱い)。
- 受け入れ条件 1-10 の結果と証拠。
- peer の可否と、待ち受けのコスト。
- 見立てと違った点、撤回した仮説、踏んだ罠、A1 (遠くのオブジェクトの代理) と Z4 (S3) への申し送り。

## 6. 作業の決まり

- ブランチ `feature/asterism-z3` を fmruby-core と親リポジトリの develop から切ってコミットする (英文、
  `<領域>: <要約>`、Co-Authored-By を付ける)。develop へのマージと push はしない。
- 実機の作業の前にミュートを確かめる (`GET http://192.0.2.15/audio/mute`。Z2 の終わりではミュート中)。
  P4-Nano で動いているアプリは終了させてよい。シリアルは開きっぱなし (私が開いた capture が動いている。
  開き直さない)。焼くのは MCP の `flash` (`app_only` で足りるときはそれで)。
- zenohd を LAN に開けるのは作業の間だけ。終わったら止める。sim は sim_down、`build/` は Linux の標準構成
  (x86-64) に戻す。
