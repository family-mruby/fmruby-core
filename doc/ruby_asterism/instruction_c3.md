# Asterism C3 指示書: 公開に向けたライセンスの整理

> 状態: 指示 | 更新: 2026-10-08 | ruby-asterism の 3 つのリポジトリと fmruby-core のライセンスの表示を、公開できる状態に整える。zenoh-pico・zenoh-c は Apache-2.0 の側を選んで使う

ユーザ決定 (2026-10-08): ライセンスの整理ができたら、ruby-asterism のリポジトリを公開してよい。公開そのものと PIN の URL の
切り替えは親が行う (このサブではしない)。

## 0. 決めたこと

- zenoh-pico と zenoh-c (どちらも `EPL-2.0 OR Apache-2.0`) は、**Apache-2.0 の側を選んで使う**。
  - fmruby-core (GPL-3.0) と組み合わせられるのは Apache-2.0 の側だけ。EPL-2.0 には GPL を二次ライセンスとして認める但し書きが無い。
  - Asterism のリポジトリでも、同じ選択にそろえる。
- Asterism 自身のコードは MIT のまま。

## 1. やること

### 1.1 picoruby-asterism-zenoh

- zenoh-pico を元にしたファイル (`zp_tcp_posix.c`、`zp_network_posix.c`、`zp_tcp_esp32.c`、`zenoh_espidf_platform.h`、ほかにあれば全部)
  について、次の 3 つを入れる。
  1. ヘッダに「zenoh-pico (EPL-2.0 OR Apache-2.0) を元にし、Apache-2.0 の下で使う」と、変更したことを書く。
     元の著作権の表示は残す。
  2. Apache-2.0 の本文 (`LICENSE-APACHE` など)。
  3. zenoh-pico の NOTICE の要る部分 (`NOTICE`)。
- 境界のファイル (`zp_system_esp32.c`: 元のファイルを #include するだけ、`zenoh_generic_config.h`: 自前の設定値) の扱いを判断して、
  理由を report に書く。
- LICENSE (MIT) に、「上のファイルは MIT ではなく Apache-2.0」と分かる節を足す。README にライセンスの節を書く。
- PIN で取ってくる zenoh-pico 本体と、host の試験で取ってくる mruby は同梱していないことを、README に書く。

### 1.2 asterism-zenoh

- zenoh-c は同梱していない (ビルドのときに取ってくる) ことと、その条件 (Apache-2.0 の側を選ぶ) を README に書く。
- 将来ビルド済みの gem で zenoh-c を同梱するときに要るもの (本文・NOTICE) を README の「配布の注意」に書く。
- C 拡張のソースに、zenoh-c の例やヘッダから写した部分があれば、1.1 と同じ扱いにする。

### 1.3 asterism

- 同梱の ROS 2 の型 (Apache-2.0) の表示が、型を置く場所 (`data/msgs/`) とリポジトリの最上位の NOTICE の両方から辿れることを確かめる。
- 型の変換器が作るファイルの先頭に、出所 (パッケージ名) と条件 (Apache-2.0 の定義から作った) が入っているかを確かめ、無ければ入れる。
  - 入れた場合は、同梱の型を作り直す。型のハッシュの試験が通ること。
- 依存の gem (msgpack など) は同梱していないことを README に書く。

### 1.4 fmruby-core

- `THIRD_PARTY_LICENSES.md` の zenoh-pico の項に、「Apache-2.0 の側を選んで使う (GPL-3.0 と組み合わせるため)」と明記する。
- 同じ一覧の Asterism の項を、公開後の形に直す。
  - 「private for now」を消す。
  - URL は https にする。PIN の URL の切り替えは親が行うので、文書だけ先に直してよい。
- ROS 2 の型が storage に入ることの表示を確かめる。

### 1.5 全体の点検

- 3 つのリポジトリの全部の追跡ファイルを見て、次を確かめ、表にして report に書く。
  - 著作権・ライセンスの表示が、ファイルの出所と合っているか。
  - 出所が外にあるのに表示の無いファイルが無いか。
  - 鍵や個人の情報が入っていないか。
- gemspec の `license` が実態と合うか。

## 2. 受け入れ条件

1. 1.1-1.4 が入り、各リポジトリの試験が今も通る:
   - asterism の `test:msgs` と `test:objects`
   - asterism-zenoh の `rake`
   - picoruby-asterism-zenoh の `rake`
2. 全ファイルの点検の表がある。出所の分からないファイルが 0。
3. fmruby-core の `rake test` が通る。ASTERISM_DIR などの上書きを使って、手元の新しい版で sim (標準構成) のビルドが通る。
4. PIN を新しいコミットに上げる。fmruby-core の `feature/asterism-c3` で PIN を更新する。
   - リポジトリの push は親が行うので、PIN の値は手元のコミットでよい。
   - 親が push してから、取得を確かめる。

## 3. 作業の決まり

- **ブランチとコミット**:
  - 各 Asterism のリポジトリは `main` にコミットする (push はしない)。
  - fmruby-core と親リポジトリは、`feature/asterism-c2` から `feature/asterism-c3` を切る。
  - コミットは英文で `<領域>: <要約>` の形にし、Co-Authored-By を付ける。
  - develop へのマージはしない。
- **触らないもの**:
  - `.env`、sdkconfig、パーティションの表、submodule の中、fmruby-graphics-audio、CI の設定。
  - 各リポジトリの公開の設定 (親が行う)。
- **判断が要るとき**: ライセンスの判断が要るものは止まって返す。
  - 例: 出所が分からない、条件が合わない。
- **終わったら**:
  - sim は sim_down する。
  - `build/` は Linux の標準構成 (x86-64)。
- **report**:
  - 置き場所: `report/c3.md` (fmruby-core の `feature/asterism-c3`)。
  - 書くこと: 何をどう表示したか、点検の表、親が公開の前に見るべき点。
