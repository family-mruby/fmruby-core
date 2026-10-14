# G2 ステップ 1 報告: 部屋 (パスワードで入る期限つきの名前空間) の調べと案

> 状態: ステップ 1 完了 (ユーザの判断待ち) | 更新: 2026-10-14 | plan.md「G2: 部屋」の最初の調べ。(1) Zenoh の `namespace` は zenoh 1.10.1 (zenohd・zenoh-c) では繋ぐ側のセッションの中だけで働く設定で、線の上のキーには前置きが付いたまま流れ、ルータの ACL と管理用の空間は前置きつきのキーを見る。zenoh-pico 1.10.1 (固定している版) にも上流の最新にも無い。代わりに picoruby-asterism-zenoh の側で同じ働きを足す (線の上で zenoh-c と同じ形になる)。(2) door が部屋の枠 (slot) の証明書で zenohd に mTLS で繋ぐ試作を手元で動かし、部屋 A の中だけで読み書きでき、部屋 B・登録済みの端末のキーには届かないことを確認した。door を通る遅れは手元で中央値 +0.25 ms、接続 1 本あたりのメモリは約 0.5 MB (通信中 約 1.3 MB)。(3) 部屋に入る鍵は console が署名する鍵 (期限・部屋・上限を中に持つ) を勧める。役割は鍵に入れず、部屋の中の物 (卓) が決める。(4) 作りの案と手間 (1-1.5 週間)、決めてほしいこと

## 1. 結果の要約

- **`namespace` (zenoh 1.10.1)**: セッションを作るとき、API と router の間に前置きを付け外しする層が入る (`zenoh/src/net/runtime/mod.rs:446-460` の `new_primitives`、本体は `zenoh/src/net/routing/namespace.rs`)。出ていく put・delete・subscriber/queryable/liveliness の宣言・問い合わせ (interest を含む)・返事のキーに前置きが付き、入ってくるものは前置きが外れる。前置きの外のキーは捨てる。**付け外しは繋ぐ側のプロセスの中だけ**で、線の上・ルータ・ACL では前置きつきのキーになる。よって部屋の境は ACL で守り、`namespace` はアプリを変えずに済ませるための道具になる (2 章)。
- **zenoh-pico**: 1.10.1 (固定の版 `e1ab223`) にも、上流の main (2026-10-09 の `8e204c4`) にも `namespace` は無い。zenoh-pico 本体に足すのは大きい (vendor を変えない決まりもある)。**picoruby-asterism-zenoh の C の層で、キーを渡す 7 か所と受ける 4 か所に前置きの付け外しを足す**のが合う (C で 150-250 行の見込み)。線の上では zenoh-c の `namespace` と同じ形になる (3 章)。
- **door の試作**: ブラウザ役 (zenoh-c の ws) → door (Ruby、ws を終えて 2 バイトの長さの枠に詰め替え、最初の InitAck の batch_size を 2 減らす) → zenohd 1.10.1 の `tls/` (mTLS、部屋の枠の証明書)。ACL は console の今の生成器 (`Relay::CloudConfig`) で「`room-01` の証明書は `room/01/**` だけ」と書いた。部屋 A の 2 人は put (200 KB・300 KB の分割あり)・呼び出し・生存の印で互いに届き、部屋 B と登録済みの端末 (`home-a`、`asterism/**`) とは両向きに何も届かない。Asterism (CRuby) の 2 ノードは部屋の中で `nodes` に互いだけが見え、呼び出しが 6.2 ms で返った (4 章)。
- **見つかった注意点**: (a) ACL の `room/01/**` は `@adv` を含むキーに当たらないので、advanced publisher/subscriber (遅れて来た人への履歴) が部屋の中で動かない。`room/01/**/@adv/**` も並べると動いた。(b) 前置きを自分で書く場合 (zenoh-pico の今)、`**` を購読すると ACL がその宣言ごと断る。前置きを付ける層があればこの心配は無い。(c) 名前空間を付けたセッションからは管理用の空間 (`@/**`) が見えない (0 件)。console の bridge には付けない (2.3、4.4)。
- **測った値** (手元の WSL2、localhost): 往復の中央値は zenohd の ws/ に直接 1.04 ms、今の `bin/ws_door` 経由 1.06 ms、部屋の door 経由 1.30 ms。door の RSS は Ruby の thread の stack を 128 KB にして、何もしていない接続 30 本で +14.6 MB (1 本 0.49 MB)、通信中 30 本で +38 MB (1 本 1.3 MB)。既定の stack 1 MB では 1 本 2.3 MB (4.5)。
- **部屋の鍵** (ユーザの方針): console が Ed25519 で署名した鍵 (中身は部屋・枠・期限・接続の上限・乱数の ID)。door は console の公開鍵だけで確かめ、毎回 console に聞かない。取り消しは console が書く短い一覧 (部屋の ID) を door が読み、部屋を消すと入っている接続も切る。鍵の長さは 253 バイト、リンク全体で約 290 バイト。試作で、期限切れ・書き換えた鍵は断られ、取り消すと 0.3 秒で 3 本とも切れた (5 章)。
- **勧める作り**: 部屋の枠 (例: 8 個) を最初に決めて、枠ごとの証明書と ACL を一度だけ作る (部屋を作るたびに zenohd を起動し直さない)。部屋を作ると空いた枠が割り当てられ、console が鍵を署名してリンクに入れる。ブラウザ版はリンクのフラグメントから鍵を取り、鍵の中の枠から名前空間 `room/<枠>` を付ける。役割 (打つ人・見る人) は部屋の中の物が決める (6 章)。

## 2. Zenoh 1.10.1 の `namespace`

### 2.1 ソースで分かること

ソースは zenoh 1.10.1 のタグ (`eclipse-zenoh/zenoh`) を読んだ。

- 設定: `namespace` (`commons/zenoh-config/src/lib.rs:897-905`、型は `OwnedNonWildKeyExpr` で `*` を含めない)。`DEFAULT_CONFIG.json5:354-362` の説明は「出ていくキーには前置きを付け、入ってくるキーからは外す。セッションに対して働く」。機能の切り替え (feature) の条件は無い。zenoh-c では `zc_config_insert_json5(cfg, "namespace", "\"room/01\"")` で入る (asterism-zenoh の `config: {"namespace" => ...}` がそのまま通る)。
- 働く場所: `Runtime::new_primitives` (`zenoh/src/net/runtime/mod.rs:446-460`)。namespace があると、セッションの face の前に `Namespace` (出ていく向き) と `ENamespace` (入ってくる向き) を挟む。**ルータの側の他の face には挟まらない**。zenohd に `namespace` を書いても、つないでくる相手のキーは変わらない (zenohd 自身のセッションに付くだけ)。
- 出ていく向き (`namespace.rs` の `Namespace`): Push (put/delete)、Request (get)、Response (返事)、Interest、Declare の KeyExpr・Subscriber・Queryable・Token (生存の印) と Undeclare Queryable のキーに `<ns>/` を付ける。ResponseFinal と Close はキーが無いのでそのまま。
- 入ってくる向き (`ENamespace`): 同じ種類のキーから `<ns>` を外す (`strip_nonwild_prefix`)。外せないものは捨てる (宣言は id を覚えておき、対になる取り消しも捨てる)。

### 2.2 手元で確かめたこと

zenohd 1.10.1 (ACL なし) に、namespace `room/a` のセッション 2 本 (n, m)、`room/b` のセッション 1 本 (b)、namespace なしのセッション 1 本 (plain) で繋いだ (`ns1.rb`)。

| 確かめたこと | 結果 |
|---|---|
| n が `x` に put | plain の `**` には `room/a/x`、m の `**` には `x`、b には届かない |
| plain が `room/a/y` に put | m・n に `y` として届く (前置きなしのセッションからは部屋の中に書ける。境は ACL の役目) |
| plain が `z` に put | 名前空間のセッションには届かない |
| n の queryable `q` | plain の `room/a/q` への get に返事 (`room/a/q`)、m の `q` への get にも返事 |
| plain の queryable `**` | b の `q` への get は `room/b/q` として plain に届き、返事は b に `q` として返る。前置きの外のキー (`elsewhere`) で返した返事は捨てられる |
| n の生存の印 `alive/n` | plain の `**` では `room/a/alive/n`、m の `**` では `alive/n`、b には無い |
| n が `@/**` に get | 0 件 (`room/a/@/**` を聞くことになる) |
| ルータの管理用の空間 | `@/<zid>/router/subscriber/room/a/**`・`.../room/b/**`、`@/<zid>/router/token/room/a/alive/n`。**前置きつきで見える** |

### 2.3 ACL での見え方

ACL は線の上のキーを見るので、**前置きつきのキー** (`room/01/...`) に対して書く。4 章の試作で、ACL を `room/01/**` に限った証明書で namespace `room/01` のセッションの put・購読・get・生存の印が通ることを確認した。

`namespace` は安全の仕組みではない。付けないで繋いだ (あるいは書き換えた) ブラウザは前置きの外のキーも送れるので、部屋の境を守るのは ACL だけになる。

## 3. zenoh-pico と、代わりの作り

### 3.1 有無

- 固定の版 (`picoruby-asterism-zenoh/ZENOH_PICO_PIN`: tag 1.10.1、commit `e1ab223`) の `include/` と `src/` に `namespace` は無い (docs と CMake の別の意味の語だけ)。
- 上流の main (2026-10-09、`8e204c4`、版 1.10.1 のまま) にも無い。

### 3.2 足す場所の比べ

| 案 | 中身 | 手間 | アプリ |
|---|---|---|---|
| (A) zenoh-pico 本体に足す | セッションの宣言・送受の全部の道 (`src/session/*`、keyexpr の宣言の最適化) にかかわる | 大きい。vendor を変えない決まりに反するので、上流への提案になる | 変えない |
| **(B) picoruby-asterism-zenoh の C の層に足す** | `Session.open(..., namespace:)` (と、ブラウザの起動時に決める既定の値)。キーを渡す口は `zrb_view_key` (`src/zenoh.c:199`) を通る 7 か所 (q.reply の 720 を含む)、受ける口は ring に入れるところ (`zrb_ring_push` 255 を呼ぶ 359・361・770、購読・生存の印・get の返事) と問い合わせのキー (645) の 4 か所。出ていくキーに `<ns>/` を付け、入ってくるキーから外し、外せないものは数えて捨てる | C で 150-250 行と host の試験。zenoh-c の `namespace` と線の上で同じ形 | 変えない |
| (C) Asterism の `ROOT` を部屋つきにする (`room/<枠>/asterism`) | asterism の mrblib は `ROOT` から組むので数行 (`mrblib/asterism.rb:25`, 277-280, 699)。ただし fmruby-core のアプリが自分で `"asterism/..."` を組む (othello.app.rb:382, 471、rt_probe・rover_brain)。ROS の層 (ros.rb) も別のキーを使う | 小さいが、アプリの書き換えが要る | 変える |

**(B) を勧める**。理由: 前置きを足すのが 1 か所で、アプリ・asterism の mrblib を変えない。CRuby は zenoh-c の `namespace` を使えば同じ線の上の形になる (asterism-zenoh の `Session.open` に `namespace:` を足すのは、`config` に入れるだけの数行)。キーの形は `room/<枠>/asterism/<node>/<app>/<object>/...` になる (Asterism の中から見ると今と同じ `asterism/<node>/...`)。

fmruby-core のアプリは自分で `Asterism.connect` を呼ぶ (othello.app.rb:465 など) ので、名前空間をアプリに渡さずに済むよう、ブラウザ版の起動時にリンクから読んだ部屋を binding の「既定の名前空間」に入れておく形にする (今の合言葉 `Module['zenohPass']` の渡し方と同じ道、`wasm/zenoh/zp_ws_emscripten.c:160-170`)。

## 4. door の試作 (部屋の枠の証明書で mTLS)

試作はコミットしていない。ユーザが見られるように `C:\Users\Public\asterism_deploy\g2_step1\` に置いた (鍵と証明書は置いていない)。

### 4.1 構成

- zenohd 1.10.1 (docker、`tls/` 7448 を mTLS、比べ用に `ws/` 7449)。設定は asterism-console の `Relay::CloudConfig` で作った (`mkcfg.rb`)。相手: `room-01` → `room/01/**`、`room-02` → `room/02/**`、`home-a` (登録済みの端末の代わり) → `asterism/**`。既定は deny。証明書は console の `Relay::CertAuthority` で作った。
- door (`room_door.rb`、Ruby、約 300 行): ws の head を読み、部屋の鍵を確かめ、101 を自分で返し、部屋の枠の証明書で zenohd の `tls/` に繋ぐ。向きごとに 1 thread。ws の Binary 1 通 (continuation を連結) を「2 バイトのリトルエンディアンの長さ + 中身」に、逆向きはその逆に詰め替える。Ping には自分で Pong、Text は異常として切る。zenohd からの最初のまとまりが InitAck なら batch_size を 2 減らす (S の旗が無いときは既定の値を足して書く)。試作では `65160 -> 65158` だった (G1.5 の 7 章と同じ)。
- ブラウザ役は zenoh-c の ws (asterism-zenoh 0.4.1)。zenoh-c の ws は URL にパスもクエリも付けられないので、試作では「この口に来たらこの鍵を持っていたことにする」ポートを部屋ごとに開いた。本物のブラウザは URL のクエリで鍵を渡す (5 章)。

### 4.2 部屋の内と外

`rooms_test.rb`。a1 は部屋 A で namespace `room/01` (zenoh-c)、a2 は部屋 A で前置きを自分で書く (zenoh-pico の今と同じ線の上の形)、b1 は部屋 B (`room/02`)、home は登録済みの端末 (`home-a` の証明書で直接 `tls/`)。

| 確かめたこと | 結果 |
|---|---|
| 間違ったパスワード | door が 403 を返し、繋がらない |
| a1 が `asterism/wasm-a1/game/othello/state` に put | a2 に `room/01/asterism/...` として届く。b1・home には届かない |
| a2 が `room/01/asterism/wasm-a2/...` に put | a1 に `asterism/wasm-a2/...` として届く |
| a2 が前置きなしの `asterism/...` に put | 誰にも届かない (ACL) |
| a2 が `room/02/...` に put | b1 に届かない |
| home が `room/01/asterism/h` に put | a1・a2 に届かない |
| 200 KB (a1) と 300 KB (a2) の put | 分割されて相手に届いた |
| a1 の queryable (呼び出しのキー) | a2 の get に返事。b1 の get (`room/02/...` になる)、home の get (`room/01/...`) には返事なし |
| a2 が home のキーに get | 返事なし |
| 生存の印 | a2 の `room/01/**` で a1 の印が見える。home の `**` では見えない |
| a2 が `**` を購読 (前置きなし) | **1 回目は何も届かなかった** (ACL がその宣言を断る)。`room/01/**` も購読すると両方に届いた (届いたものを zenoh-c が手元で両方に配る) |
| Asterism (CRuby、`ast_room.rb`) | 部屋 A の wasm-alice と wasm-bob は `nodes` に互いだけ、部屋 B の wasm-carol は自分だけ。bob から alice の `board.say` が 6.2 ms で返った |

### 4.3 advanced publisher/subscriber の注意

`adv.rb`: 部屋 A の中で、cache つきの advanced publisher に 2 回 put し、遅れて来た advanced subscriber (history) が受け取るか。

- ACL なしの zenohd: 2 件とも届く。
- ACL `room/01/**` だけ: 0 件。zenoh-ext は `<キー>/@adv/...` のキーを使い (`zenoh-ext/src/advanced_publisher.rs:390`、`advanced_subscriber.rs:742-746`)、`@` で始まる区切りは `**` に当たらない。
- ACL に `room/01/**/@adv/**` も並べると 2 件とも届いた。

部屋の ACL には両方を書く。console の今の役割の ACL も `@adv` を書いていないので、advanced の層を ACL の向こうで使うなら同じ直しが要る (今回は確かめていない)。

### 4.4 名前空間と管理用の空間

名前空間を付けたセッションの `@/**` への get は 0 件だった (2.2)。部屋の中から管理用の空間は見えない。部屋の ACL も `@/**` を許さないので、どちらにしても見えない。

### 4.5 測った値

往復 (`rtt.rb`): 2 つのセッションの間で 300 回、片方が put し、もう片方が受けて put を返すまでの時間 (最初の 20 回を除く)。2 回ずつ測って同じ傾向。

| 道 | 中央値 | 90% | 最大 |
|---|---|---|---|
| zenohd の ws/ に直接 | 1.04-1.05 ms | 1.30 ms | 1.83 ms |
| 今の `bin/ws_door` (中身を見ない中継) → ws/ | 1.06-1.07 ms | 1.54-1.57 ms | 2.37 ms |
| 部屋の door (ws → mTLS、詰め替え) → tls/ | 1.30 ms | 1.58 ms | 1.96 ms |

部屋の door が足すのは手元で約 0.25 ms (片道ごとに door を 2 回通る往復の値)。VPS 越しの手の往復 (94-156 ms、O1) から見れば小さい。door が繋ぐまで (鍵の確認・TLS の握手・101) は 2.5-9 ms。

メモリ (door の RSS、`many.rb`):

| 条件 | 0 本 | 10 本 | 30 本 | 1 本あたり |
|---|---|---|---|---|
| Ruby の既定 (thread の stack 1 MB)、通信中 | 44 MB | 83 MB | 151 MB | 約 3.6 MB |
| `MALLOC_ARENA_MAX=2`、何もしない | 37 MB | | 105 MB | 約 2.3 MB |
| `RUBY_THREAD_VM_STACK_SIZE=131072` + `MALLOC_ARENA_MAX=2`、何もしない | 31 MB | | 46 MB | **約 0.49 MB** |
| 同、通信中 (30 本がそれぞれ 1 KB を 0.2 秒ごと) | 31 MB | | 70 MB | 約 1.3 MB |

接続 1 本に thread を 2 本使う作りでは、thread の stack の大きさが効く。thread の stack を絞れば、VPS (2 GB) で 50 本でも 100 MB 以下に収まる見込み。今の `bin/ws_door` は同じ条件 (既定の stack、通信中) で 1 本約 1.6 MB だった。

## 5. 部屋の鍵・期限・試す回数

### 5.1 2 つの作りの比べ

| | (1) ハッシュを console に持つ | (2) console が署名した鍵 (勧める) |
|---|---|---|
| リンクに入るもの | 122 ビット以上の乱数の文字列 (UUID くらい) | 署名した鍵 (中に部屋・枠・期限・上限・乱数の ID) |
| 確かめる人 | console (nginx の `auth_request` で毎回聞く。今の合言葉と同じ道) | door が console の公開鍵だけで確かめる。毎回 console に聞かない |
| console に置くもの | パスワードの SHA-256 (乱数が十分長いので遅いハッシュは要らない)、比べは長さ一定の比べ | 署名の秘密鍵と、出した部屋の一覧 (パスワードそのものは持たない。鍵の中身も持たなくてよい) |
| console が止まっているとき | 新しく入れない | 入れる (取り消しの一覧が古いまま、という影響だけ) |
| 早めに閉じる | 行を消せば次から断る。入っている接続を切るには door に知らせる道が別に要る | console が取り消しの一覧に部屋の ID を書く → door が読んで新しく断り、入っている接続を切る |
| door に要るもの | console への道 | 公開鍵 (版の印 `kid` つきで複数持てるようにして入れ替えに備える)、正しい時計 (同じ VPS の NTP)、取り消しの一覧の置き場 |
| 長さ | リンクで 40-60 バイト | 鍵 253 バイト、リンク全体で約 290 バイト (試作の値) |
| ほかの router への広げ | console に繋がる door だけ | 公開鍵を持つ door ならどこでも |

(2) を勧める。door と console は同じ VPS にいるので、取り消しの一覧はファイル (今の `/generated/` のような共有の置き場) で足りる。一覧は「まだ期限の来ていない取り消した部屋の ID」だけなので短い。

鍵の中身 (試作、`mint.rb`): `{"v":1,"room":"othello-a","slot":"01","exp":<Unix 秒>,"max":8,"jti":"<UUID>"}` を base64url にし、`.` のあとに Ed25519 の署名 (base64url)。署名の確認は 1 回 67 µs (Ruby 3.2、OpenSSL)。

- **当てずっぽうで作れない理由**: 署名の秘密鍵が無ければ通る鍵は作れない。`jti` の乱数で鍵ごとに違う文字列になる。鍵を知っている人は入れる (持っている人が入れる仕組み。plan の「気をつけること」のとおり)。
- **リンクでの渡し方**: フラグメント (`#room=<鍵>`) に入れる。フラグメントはサーバに送られない。ブラウザ版は ws の URL のクエリ (`?t=<鍵>`) に付けて door へ送る (今の合言葉と同じ形)。nginx の記録はクエリを落とす形式 (`asterism_noquery`) のまま使い、door も鍵を記録しない。wss の上だけで送る。
- **部屋の中の役割**: 鍵には入れない (2026-10-14 ユーザ)。鍵は「その部屋に入れる」ことだけを表す。

### 5.2 期限と早めに閉じる

- door は繋ぐときに `exp` と取り消しの一覧を見て、過ぎていれば 410 で断る。入っている接続は door が 0.5 秒ごとに見て、期限か取り消しで両側を閉じる (`bin/ws_door` が今も持つ「接続ごとの寿命」の延長)。
- 試作 (`token_test.rb`): 期限の切れた鍵は 410、1 文字書き換えた鍵は 403。取り消しの一覧に部屋を書くと、入っていた 3 本が 0.3 秒で切れ、zenoh-c の自動のつなぎ直しは 410 で断られた。
- 期限の上限 (例: 24 時間) は console が署名するときに守る。door も上限を持ち、それより遠い `exp` は上限で切る。

### 5.3 試す回数の制限

- 試作では「1 つの住所から 60 秒に失敗 5 回で断る」にしたところ、**同じ住所からの正しい鍵まで断った** (間違いの試しの直後に、同じ PC から正しい鍵で繋げなくなった)。イベントの会場のように多くの人が 1 つの住所から来る場合、1 人の間違いで全員が入れなくなる。
- 鍵は署名で守られていて総当たりは成り立たないので、制限の目的は door の負荷と記録を守ることだけになる。案: (a) nginx の `limit_req` (今の `asterism_wss`) で住所ごとの頻度を抑える、(b) door は署名の正しくない鍵を住所ごとに数え、ゆるい上限 (例: 1 分に 30 回) を超えた住所だけ短く (例: 1 分) 断る、(c) 署名の正しい鍵は失敗の数に関係なく通す。
- 部屋ごとの接続の上限 (`max`) は door が数える (人数ではなく接続の数。ブラウザ 1 枚が 1 本)。

### 5.4 部屋の中の物 (卓) との関係

部屋の全員が同じ鍵を持つので、ACL が分けられるのは部屋の内と外までになる。部屋の中の役割 (卓に着く人・見る人) と、打つ人ごとに隠す情報 (麻雀の手牌など) は、部屋の中の物が守る:

- 卓を持つ物 (サーバの CRuby のプロセスか、部屋の 1 つのノード) が部屋の中に Asterism の物 (例: `table`) を出す。席に着く呼び出しで、その席だけの秘密の文字列を呼んだ人に返す。
- 席の人だけの呼び出し (自分の手牌を見る・打つ) は、その秘密の文字列を引数に要る。見る人は秘密を持たないので、皆に見せてよい状態 (場・捨て牌) だけを読める。
- 手牌は部屋の皆が読めるキーには書かず、呼び出しの返事 (問い合わせを出した人にだけ返る) で渡す。ただし部屋の全員が同じ証明書で入るので、部屋の誰かが卓と同じキーに queryable を出して卓を名乗ることは ACL では防げない。秘密の度合いは「部屋の中の気楽な遊び」の水準にとどまる (plan の「気をつけること」のとおり)。卓のキーを部屋の中で 1 つの名前に決め、卓を持つ物だけが別の証明書 (枠の証明書とは別、そのキーに queryable を出せるもの) で入る形にすれば、名乗りも防げる (ACL で `room/<枠>/asterism/table/**` の queryable を部屋の鍵の人には許さない。ACL の deny と allow の重なり方は未確認)。
- 試作の途中で、読むだけの証明書 (`room-01-watch`、購読と生存の印の問い合わせだけを許し、問い合わせは許さない) を試し、見る人の put と呼び出しが断られることは確認した。ただ役割を鍵に入れない方針なので、作りには入れない。

## 6. 案

### 6.1 選ぶ作り

1. **部屋の枠**: console の設定で枠の数 (例: 8) を決め、導入のとき (と枠の数を変えるとき) に枠ごとの証明書 (`room-01` … `room-08`) を signer で作り、ACL に「`room-<枠>` は `room/<枠>/**` と `room/<枠>/**/@adv/**` だけ」を書く。**部屋を作るたびに zenohd を起動し直さない** (zenohd 1.10.1 は動いているまま ACL を入れ替えられない、G1.5 の 3 章)。枠の証明書と鍵は door の置き場だけに置き、外に出さない。
2. **部屋を作る** (console): 名前・使うアプリ・期限 (上限あり)・接続の上限 → 空いた枠を割り当て、鍵を署名し、リンク (`https://<ブラウザ版>/#room=<鍵>&app=<アプリ>`) を出す。枠は部屋の期限か削除で空き、door がその枠の接続を全部切ってから次に使う。
3. **入る** (ブラウザ版): リンクのフラグメントから鍵を読み、鍵の中身 (署名の確認はしない、読むだけ) から枠を取り、binding の既定の名前空間を `room/<枠>` にする。ws の URL に `t=<鍵>` を付けて door へ繋ぐ。ノードの ID は今のブラウザ版の作り方 (`wasm-<16 進 6 桁>`) のまま (部屋ごとの網で、console の網の ID の決まりとは別)。
4. **door**: `bin/ws_door` を、鍵の確認・ws の終わり・詰め替え・InitAck の書き換え・枠の証明書での mTLS・期限と取り消しで切る、に広げる (部屋の口として別の口にする。今の合言葉の口は残す)。
5. **CRuby**: `Asterism.connect(..., room: <鍵>)` で door の URL と名前空間を鍵から決める (zenoh-c の `namespace`)。ただし zenoh-c の ws は URL にクエリを付けられないので、CRuby は door を通らず、部屋の枠の証明書を持つ人 (管理者が置く卓のプロセス) が `tls/` に直接繋ぐ形にする。

### 6.2 変えるところと手間

| どこ | 中身 | 手間の見込み |
|---|---|---|
| picoruby-asterism-zenoh | `namespace:` (と既定の名前空間)、キーの付け外し、host の試験 | 1 日 |
| asterism-zenoh | `Session.open(namespace:)` (`config` に入れる)、試験 | 半日未満 |
| asterism (gem) | `Asterism.connect(namespace:)` の受け渡し、鍵からリンクの中身を読む小さな部品 (CRuby と mruby で共通)、文書 | 半日 |
| asterism-console | 部屋のモデル (名前・アプリ・枠・期限・上限・取り消し)、作る・一覧・消すの画面、署名の秘密鍵 (signer の volume に置く案)、取り消しの一覧のファイル、枠の証明書と ACL (CloudConfig に枠の規則)、設定 (枠の数・期限の上限) | 2-3 日 |
| door | 5 章の作り。Ruby で約 350 行と試験、thread の stack を絞る設定、nginx の部屋の口 (443 番のサブドメインへ移す件と一緒に) | 2 日 |
| fmruby-core (ブラウザ版) | `web/main.js` で `#room=` を読む、ws の URL に `t=` を付ける、既定の名前空間を入れる、410・403 の表示 (「部屋は閉じました」など)、`rake wasm:dist` | 1 日 |

合わせて 1-1.5 週間。VPS での確認はユーザ。

### 6.3 決めてほしいこと

1. **部屋の枠の数** (同時に開ける部屋の数) と、1 部屋の接続の上限の既定。VPS 2 GB で door は 1 本 0.5-1.3 MB、zenohd の上限 `max_sessions` (今 64) も合わせて見直す。
2. **期限の上限** (plan の例は 24 時間)。
3. **署名の秘密鍵の置き場**: console の中か、CA と同じ signer の volume か (signer に置くと、console が乗っ取られても鍵は出ない)。
4. **キーの形**: 枠の番号 (`room/01/...`) にするか。枠は使い回すので、前の部屋の状態 (advanced publisher の cache など) は、door がその枠の接続を全部切ったあとに次の部屋へ渡す、で足りるか。
5. **CRuby の入り方**: 部屋の中の卓のプロセス (CRuby) は枠の証明書で `tls/` に直接入る (6.1 の 5) でよいか。誰でも CRuby で部屋に入れる必要があるなら、zenoh-c の ws に代わる道 (door に `tls/` の口を足し、鍵を最初に送る形など) を別に考える。
6. **door を Ruby のまま書くか**: 今回の値なら Ruby で足りる (遅れ +0.25 ms、1 本 0.5 MB)。接続が数百になるなら別の言語を考える。

## 7. 見立てと違った点

- `namespace` はルータの設定ではなく、繋ぐ側のセッションの設定だった。ルータが相手ごとに名前空間をかける形にはできない (2.1)。
- ACL の `**` は `@adv` に当たらず、advanced の層の履歴が部屋の中で動かなかった (4.3)。
- 前置きを自分で書く場合、`**` の購読は ACL に宣言ごと断られる (4.2)。
- 住所ごとの失敗の数で断ると、同じ住所の正しい鍵まで断る (5.3)。

## 8. 試したもの

`C:\Users\Public\asterism_deploy\g2_step1\` (コミットしていない):

- `room_door.rb` 部屋の door の試作 (パスワードのハッシュと署名した鍵の両方)
- `mkcfg.rb` console の `Relay::CloudConfig` で部屋の枠の ACL を作る
- `ns1.rb` `namespace` の働き (2.2)
- `rooms_test.rb` 部屋の内と外 (4.2)、`ast_room.rb` Asterism の 2 ノード、`adv.rb` advanced の層 (4.3)
- `rtt.rb` 往復の時間 (4.5)
- `mint.rb` 鍵の署名、`token_test.rb` 鍵・期限・取り消し (5)

使ったもの: zenohd 1.10.1 (docker の `eclipse/zenoh:1.10.1`)、asterism-zenoh 0.4.1 (zenoh-c 1.10.1)、asterism の main (`985740e`)、asterism-console の main (`f4e77e2`)、zenoh と zenoh-c と zenoh-pico のソース (1.10.1 のタグと zenoh-pico の main)。片付け: コンテナと door のプロセスは止めた。VPS・端末・`.env`・sdkconfig には触れていない。
