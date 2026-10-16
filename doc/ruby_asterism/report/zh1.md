# ZH1 報告: console の bridge が zenohd を止める問い合わせをやめる

> 状態: 中断 (止まる条件に当たった) | 更新: 2026-10-17 | 2 章の 2 の調べで、管理用の空間の `@/*/router` と bridge がすでに持っているものだけでは、今と同じグラフが作れない部分が残ると分かった。指示書 4 章の止まる条件 (今と同じグラフが作れない・網の決まりの変更が要る) に当たるので、コードは変えずに止めた。何が消えるかと選択肢を 4 章・5 章に書く

## 1. やったこと

- zenohd 1.10.1 のソース (`zenoh/src/net/runtime/adminspace.rs`、`protocol/network.rs`) で、`@/*/router` (`local_data`) と `route/successor` が何を返すかを読んだ。
- 手元で素の zenohd 1.10.1 (公式の glibc 版) を 3 台 (cloud ← home ← home2 の鎖) 起こし、cloud の客から `@/*/router`、`@/*/router/route/successor/**`、liveliness の見張り・取得、`@profile` への問い合わせを試した。ポートは 27591-27593 (別に動いている `zh-pr` の試しとは別)。docker は使っていない。試しの後、3 台とも止めた。
- asterism-console のコードと試験は変えていない (コミットなし)。

## 2. `@/*/router` の sessions に 1.10.1 が出している欄

ソースの `transport_unicast_to_json` と実物が一致した。欄は `peer` (相手の zid)・`whatami`・`links` (`src` / `dst`)・`weight`・`shm`・`region` だけで、**証明書の名前 (auth_identifier) は無い**。実物の例 (手元の試し。zid は伏せた):

```json
{
  "zid": "<cloud zid>",
  "version": "v1.10.1-1211779c...",
  "metadata": { "name": "cloud" },
  "locators": [ "tcp/127.0.0.1:27591" ],
  "plugins": {},
  "sessions": [
    { "peer": "<home zid>", "whatami": "router", "region": "north", "shm": false,
      "links": [ { "src": "tcp/127.0.0.1:27591", "dst": "tcp/127.0.0.1:37236" } ],
      "weight": { "actual_weight": 100, "dst_weight": null, "src_weight": null } },
    { "peer": "<reader zid>", "whatami": "client", "region": "south:0:client", "shm": false,
      "links": [ { "src": "tcp/127.0.0.1:27591", "dst": "tcp/127.0.0.1:39856" } ],
      "weight": null }
  ]
}
```

証明書の名前は `@/<zid>/session/**` にあるが、そこは自分のプロセスの中からの問い合わせにしか答えない (bridge の `own_links` の説明のとおり)。

## 3. 代わりに使えるものの調べ

| 今の材料 | 代わりの候補 | 結果 |
|---|---|---|
| linkstate の dot (ルータどうしの線) | 読めるルータの sessions の `whatami: "router"` | 読めるルータとその隣のルータの線は今と同じに出る (今の `add_session` がすでに引いている)。**読めないルータどうしの線** (家のルータの奥にもう 1 台あり、どちらも読めない) は出ない |
| 同上 | `@/*/router/route/successor/**` (錠を外してから返す道) | 各ルータの表の successor は**そのルータから見た次の一手**で、値はそのルータの隣のルータだけだった (手元の鎖で、cloud の表は home だけ、home の表は cloud と home2、home2 の表は home)。読めるルータの sessions と同じことしか分からず、読めないルータどうしの線は出ない |
| token 表の `clients` / `peers` (Asterism のノードを運ぶセッション) | liveliness の見張り・取得 | 見張りは `[key, alive]` だけ (asterism-zenoh が source を出さず、zenoh 1.10.1 も liveliness の返答に replier を付けない: `replier_id: None`)。zid は分からない |
| 同上 | `asterism/<node>/@profile` への get の `replier_zid` | **答えたセッションの zid が取れる** (家のルータの奥の客でも、ルータを 2 つ越えて届いた。zenoh-pico 1.10.1 も返答に responder の zid を載せる)。ただし答えるノードに限る (下の 4 章) |
| 同上 | 証明書の名前 = ノード ID の決まり | sessions に証明書の名前が無いので使えない |
| token 表の `routers` (V2.6 の `via_router` の線) | sessions の消去法 (どの読めるルータの sessions にも無い zid は、読めないルータの奥) | 読めないルータが 1 台だけなら同じ線になる。2 台以上あると、どのルータの奥かが分からない |
| ROS 2 ノードの carries | token の中の zid (今と同じ) | 変わらない (token 表を使っていない) |

## 4. 今と同じ線が引けない部分 (止まった理由)

`@profile` の `replier_zid` で carries を作る形を取っても、次の線がグラフから消える (または変わる):

1. **部屋のノードの carries の線がすべて消える。** 部屋の中では console の ACL が liveliness の見張りと取得しか許さず、問い合わせ (`@profile` を含む) はできない (docs/rooms.md、0.2.1 からの決まり)。今は cloud ルータの token 表がページのセッションを教えているので、ページのノードがセッションに繋がって描かれている。代わりが無いと、部屋のページのセッションは「others」に入り、部屋のノードは線の無いまま枠の中に浮く。
2. **`@profile` に答えないノードの carries の線が消える。** asterism 0.5.0 より前のノード、時間内に答えられない忙しいノード。今は token を出しているだけで線が引けている。
3. **`via_router` の線 (V2.6) が、読めないルータが 2 台以上あるとき引けない** (どの奥かが分からない)。1 台のときは今と同じ。
4. **読めないルータどうしの `router_link` が消える** (linkstate にしか無い)。今の VPS の形 (cloud と家のルータ、家のルータは既定で読める) では起きないが、指示書の「今と同じ」には当たらない。
5. 小さな違い: carries の線が `@profile` の返答の後に出る (token が出てから最大 1.5 秒遅れる)。ノードの「Through」(registry の `via`) も carries から決まるので同じだけ遅れる。

1 は 0.3.x の部屋の見え方そのものなので、利用者から見た動きが変わる。直すには網の決まり (ACL か asterism のプロトコル) を変える必要があり、どちらも指示書の止まる条件に当たる。

## 5. 選択肢 (ユーザが決める)

| 案 | 中身 | 部屋の線 | 答えないノード | 変えるもの |
|---|---|---|---|---|
| A | carries を `@profile` の `replier_zid` で作り、1-5 の欠けを受け入れる (部屋のノードは枠の中に線なしで出す、など見せ方を決める) | 消える | 消える | console だけ。見え方が変わる |
| B | A に加えて、cloud ルータの ACL で console に `room/*/asterism/*/@profile` の get だけを許す (席は 0.3.0 から自分の `@profile` を出せるので答えはある) | 引ける | 消える | console が作る ACL (網の決まり)。「console は部屋で見張るだけ」の説明が変わる |
| C | asterism のプロトコルで、プロフィール (get と put の両方) に自分のセッションの zid を載せる。put は部屋でも console に届く形にする (ACL の `@` の扱いも要る) | 引ける (ACL 次第) | 古いノードは消える | asterism・各実装 (CRuby / mruby / wasm)・ACL |
| D | token 表と linkstate はやめずに、問い合わせを間隔を延ばす (2 秒→30 秒など) / グラフの画面が開いている間だけにする | 今と同じ | 今と同じ | console だけ。止まる率が下がるだけで無くならない (返答 1 件でも錠の二重取りの窓はある) |
| E | 対策 2 (zenohd の直し) を待ち、直した image に替えてから今の bridge のまま使う。それまでの保険に対策 3 (新しいセッションが開けるかの見張りと zenohd の起こし直し) | 今と同じ | 今と同じ | zenohd の image (自前、上流に PR) |
| F | A + E: 直った zenohd では token 表を読む今の作り方、1.10.1 では A に落とす (版を見て切り替える) | 直った zenohd でだけ引ける | 同左 | console と zenohd の image |

すぐ止まらなくするだけなら、A (欠けを受け入れる) か D+E の組。グラフの見え方を一切変えないなら E (と保険の対策 3) が筋。

## 6. 見立てと違った点・気づいたこと

- 指示書 1 章の見立て (`@/*/router` は当たらない、`route/successor` の主な道は錠を外してから返す) はソースどおりだった。ただし `route/successor` は**ルータどうしの線の代わりにならない** (各ルータの表はそのルータの隣しか言わない)。
- `@/*/router` の `local_data` も中で `get_links_info` が読み錠を取るが、すぐ外してから返答するので二重取りにはならない。
- **bridge 以外にも同じ問い合わせができる客がいる。** console が作る ACL は、registry で「May read the cloud router's status」を付けた客に `@/**` の問い合わせを許す (`lib/relay/cloud_config.rb` の `admin_read`)。そういう客が `@/*/router/token/**` を問い合わせれば同じく止まりうる。bridge を直しても zenohd を直すまで残る危険として、対策 2・3 の理由になる。
- 試しの中で、`@profile` の get に cloud ルータからも key 無しの返答 (`replier_zid` が cloud) が 1 件来た。返答の数え方には影響しないが、A を取るなら key のある返答だけを使う。

## 7. 残ったこと

- 5 章の案の選択 (ユーザ)。選ばれた案で ZH1 をやり直す。受け入れ条件 2 (30 分の負荷と見張り)・3 (グラフの比較)・4 は未実施。
- 試しの道具 (手元の 3 台の zenohd の設定と問い合わせの客) は会話の scratchpad の `zh1/probe/` にある。
