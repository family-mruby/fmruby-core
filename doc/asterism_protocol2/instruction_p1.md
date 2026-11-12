# P1 指示書: 部品に encoding と publisher の Ruby の API を足す

> 親: plan.md の P1。決まりの正は design.md (2 章・6 章)。

## 目的

protocol 2 の Asterism 本体 (P2) が、CRuby と端末 (picoruby) で同じ書き方で次をできるようにする。

1. put に encoding を付ける (付けないときは今と同じく何も送らない)。
2. 受け取った値 (subscriber の sample、get の reply) の encoding を読む。
3. publisher を宣言し、それで put し (attachment・encoding つき)、取り下げる。
4. (zenoh-pico で読めるなら) 受け取った値のタイムスタンプ (ルータが付けた HLC) を読む。

CRuby の部品 (asterism-zenoh) は encoding・`Publisher`・`Timestamp` を既に持つ。**picoruby-asterism-zenoh の Ruby の形は、asterism-zenoh の
今の形に合わせる** (名前・キーワード・戻り値)。asterism-zenoh は、合わせるうえで足りないものがあるときだけ直す。

## やること

1. 調べる (report に書く): asterism-zenoh の `Publisher`・encoding・`Timestamp` の Ruby の形。zenoh-pico 1.10.1 (picoruby-asterism-zenoh の
   ZENOH_PICO_PIN) で `z_declare_publisher`・`z_publisher_put` の encoding と attachment・`z_sample_encoding`・`z_sample_timestamp` が
   使えるか (`Z_FEATURE_*` の設定を含む)。端末のビルド (fmruby-core の esp32・wasm・linux) で有効になっている機能。
2. picoruby-asterism-zenoh (`src/zenoh.c`・`mrblib/`) に足す:
   - `session.put(key, payload, attachment:, encoding:)` の `encoding:` (文字列。Zenoh の決まった番号の名前 `"image/jpeg"` など。
     nil・省略は今と同じ)。
   - 受け取った値の encoding: subscriber の取り出しと get の reply で、encoding を受け取れる形 (asterism-zenoh と同じ形に。
     既定の encoding のときは nil)。**今の `[key, payload, attachment]` の取り出しを使っている呼び手 (asterism の mrblib) を壊さない形を
     先に考え**、壊すしかないなら止まって返す。
   - `session.declare_publisher(key, encoding: nil)` (名前は asterism-zenoh に合わせる) → `pub.put(payload, attachment:, encoding:)`、
     `pub.undeclare`。セッションを閉じるときに残った publisher も取り下げる (subscriber などと同じ扱い)。
   - タイムスタンプ: 読めるなら取り出しに足す。読めないなら理由を report に書いて足さない。
   - host_test に試験を足す (2 つの mruby プロセスの間で encoding・publisher・attachment が届く)。
3. asterism-zenoh: 足りない分があれば足し、その試験も足す。無ければ触らない。
4. RAM を測る (report に数値):
   - publisher 1 つ・queryable 1 つあたりに zenoh-pico が確保する量 (ホストで malloc を数える、または z_malloc に計数を挟む一時的な計装。
     計装はコミットしない)。
   - fmruby-core で `PICORUBY_ASTERISM_ZENOH_DIR=<手元の checkout>` を指して `rake regress:size` を流し、内蔵 RAM とファームの大きさの
     増分を記録する。**内蔵 RAM の静的な増分は 0 であること**。増えるなら止まって返す。
5. fmruby-core で手元の checkout を指して `rake regress` を流し、通ることを確かめる (P1 は API を足すだけなので、今の動きは変わらない)。
   ネイティブ版の Asterism 接続が今どおり動くことも `rake regress:live` で確かめる (通しの場面に Asterism を使うものが無ければ report に書く)。

## 触ってよい範囲

- picoruby-asterism-zenoh (全体)、asterism-zenoh (足りない分だけ)、fmruby-core/doc/asterism_protocol2/report/。
- **触らない**: asterism (P2 で触る)、asterism-games、asterism-console、fmruby-core のソース・ピン (`lib/add/*_PIN`)。fmruby-core の
  作業ツリーにある既存の未コミットの変化 (`components/` のサブモジュール、`wasm/vendor/`) には触らない。
- fmruby-core のビルド (regress:size・regress) はしてよい (この間、ほかの作業は fmruby-core を触らない)。

## git

- picoruby-asterism-zenoh・asterism-zenoh は main に直接コミットしてよい (件名は `<種類>(<場所>): <要約>`、英語、Co-Authored-By を付ける)。
  版の番号と CHANGELOG はまだ上げない (P4 でまとめる)。
- **push・gem の公開はしない**。
- fmruby-core には report だけコミットしてよい (`docs(asterism_protocol2): ...`)。

## 止まる条件

- 今の取り出しの形を壊さずに encoding を足せないとき (呼び手を直す必要があるとき)。
- 内蔵 RAM の静的な増分が 0 にならないとき。
- zenoh-pico の機能を有効にするのに fmruby-core の sdkconfig や zenoh-pico の設定の変更が要るとき (提案を書いて返す)。
- design.md の決まりと食い違う事実が出たとき、仕様が変わる直し方を選ぶ必要が出たとき。
- ユーザの作業 (sudo・実機) が要るとき。実機での測定は要らない (つながっていない)。

## report (fmruby-core/doc/asterism_protocol2/report/p1.md)

調べた結果 (1)、足した API の形 (CRuby と並べた表)、試験、RAM の数値 (publisher・queryable 1 つあたり、regress:size の増分)、
見立てと違った点、撤回した仮説、残件 (P2 で気にすること)。

## 後片付け

- 一時的な計装を消す。fmruby-core の build/ はそのまま (本体 checkout)。picoruby-asterism-zenoh の host build は残してよい。
