# Asterism C6 指示書: 機体側への持ち込みと残りの小さな点

> 状態: 指示 | 更新: 2026-10-08 | C5 で挙がった「機体側に持っていける候補」を共有の層 (mrblib) に入れ、試しのアプリの呼び合い (キー m) が CRuby に届かない件を直す。その後、親が push と公開を行う

ユーザ決定 (2026-10-08): 機体側に持っていける候補と、残っている小さな点を対応してから、push と公開をする。

## 1. やること

### 1.1 共有の層 (asterism の `mrblib/`) に足す

C5 の report の「機体側に持っていける候補」を入れる。機体 (mruby、Spinel ではないアプリの VM) と CRuby の両方で動く形にする。

1. **`Client#call_async` のシーケンス番号**: `@sequence` を一度だけローカルに読む形にする。
   - CRuby の層の Mutex による回避 (C5) は、これで要らなくなるなら外す。
2. **`on_join` / `on_leave`**: 機体 (ポーリング) では、`Asterism.poll` の中で、知っているノードの増減を見てブロックを呼ぶ。
   - CRuby の層 (C5) の `net.on_join` / `on_leave` は、これを使う形にそろえる。
   - 同じ名前の 2 つの仕組みが残らないようにする。
3. **ROS の `node.every(秒) { }` と、ブロックつきの `node.subscribe(topic, type) { |msg, info| }`**:
   - 機体では、ブロックは `node.poll` の中で呼ぶ。
   - ブロックの呼び出しは C のスタックを深くする (A1 の report: 16 KB のうち `on_event` から待つと 12.4 KB)。そのため、機体では `while` で回す今の書き方を中で使い、ブロックの中で答えを待つ呼び出しをしないよう README に書く。
   - CRuby の層 (C5) の同じ名前のものと、動きをそろえる。
4. **`deconstruct_keys`**:
   - アプリの VM (picoruby のコンパイラ) で `case/in` のパターンマッチが使えるかを、sim で確かめる。
   - 使えるなら、メッセージと Attachment の `deconstruct_keys` を共有の層へ移す。使えなければ CRuby 専用のまま。
   - どちらになったかを report に書く。

### 1.2 残っている小さな点

- **呼び合い (キー m) が CRuby に届かない**: P4-Nano の asterism_demo のキー m (`info.relay` を 10 回) を押しても、CRuby の `examples/node.rb` に呼び出しが来なかった (C5)。
  - 原因を調べて直す。どちらの側 (試しのアプリ、共有の層、CRuby の層) かを特定する。
  - 機体 ⇔ CRuby と、sim ⇔ CRuby の両方で、両方から同時に m を押しても詰まらないことを確かめる (A1 の受け入れ条件 6 の形)。

### 1.3 試しのアプリ (fmruby-core の `flash/app/test/`)

- `asterism_demo` に `on_join` / `on_leave` を使った表示を足す。
- `ros2_types` か `ros2_talker` に `node.every` とブロックつきの `subscribe` を使った例を足す。
- 機体のスタックの余り (`fmrb_task:` の Free) を書く。

## 2. 受け入れ条件

1. asterism の全部の試験 (`test:msgs`、`test:objects`、`test:api`) と、asterism-zenoh の `rake` が通る。
2. fmruby-core の PIN を新しい asterism に上げる。
   - sim の標準構成・互換構成と、P4 (NARYAv4) のビルドが通る。
   - sim と P4-Nano で、次のアプリが動く:
     - asterism_demo (`on_join` / `on_leave` の表示、キー m)
     - ros2_types / ros2_talker (`node.every`、ブロックつきの `subscribe`)
3. 機体 ⇔ CRuby の呼び合い (キー m) が、両方から同時に押しても詰まらない。
4. 起動時の内蔵 RAM の増分 0。アプリのスタックの余りが、C5 以前より大きく減らない (減った分を書く)。
5. fmruby-core の `rake test` が通る。

## 3. 範囲・決まり

- **触ってよいもの**:
  - asterism と asterism-zenoh のリポジトリ。`main` にコミットする。push は親が行う。
  - fmruby-core の `lib/add/ASTERISM_PIN`、`flash/app/test/asterism_*` と `ros2_*`、`doc/ruby_asterism/report/c6.md`。
  - fmruby-core のブランチは `feature/asterism-c5` から `feature/asterism-c6` を切る。
- **触らないもの**:
  - picoruby-asterism-zenoh の C。直す必要があれば止まって返す。
  - fmruby-core のビルドの仕組み、`.env`、sdkconfig。
- **公開・版**: 版は 0.2.0 のまま (未公開なので上げない)。rubygems.org への公開はしない。
- **機体**: P4-Nano。IP は親が渡す。文書には本当の IP と機体の名前を書かない。
  - ミュート中。
  - 親の capture が向いている (`serial_start` を呼ばない)。
  - 焼き直しは MCP の `flash` (`app_only`)。
  - storage のファイルは `tab5_fs` で送る。
- **zenohd・ROS 2**: 重ね合わせのファイルで上げ、終わったら `down`。
- **止まる条件**:
  - 利用者から見た今の動きを変える必要があるとき。
  - 共有の層の変更で、機体のスタックが足りなくなるとき。
  - 範囲の外の変更が要るとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - 何をどちらの層に入れたか。
  - `case/in` の可否。
  - 呼び合いの件の原因と直し方。
  - 試験と実機の結果、スタックと内蔵 RAM。
