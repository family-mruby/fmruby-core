# Asterism C8 指示書: コンパイラの case/in の直しと、gem 0.4.0 (API の見直しの第 1 段)

> 状態: 指示 | 更新: 2026-10-09 | (1) PicoRuby のコンパイラの case/in の不具合を、上流の直しを lib/patch で当てて直す。(2) asterism の docs/api_review.md の推奨案で 0.4.0 (壊さない追加・非推奨の警告・不具合の直し) を 3 つの gem に入れる

ユーザ決定 (2026-10-09): 「おすすめで進めて」。API の見直しの判断は、`asterism/docs/api_review.md` の推奨案に従う。

1. 単位: 秒の `timeout:` をすべての層に足す。ポーリングの層には `timeout_ms:` も残す。
2. 1.0 で外すのは時間の位置引数だけ。
3. `peers` (数) は `connection_count` に改名する。`peers` は非推奨にして 1.0 で外す。
4. エラーは全部 `Asterism::Error` の下に置く。`TimeoutError` も `Asterism::Error` の下 (古い名前は別名)。`Disconnected` は名前を変えない。
5. 非推奨の警告は名前ごとに 1 回 (機体も)。
6. `each { }` の戻り値は 1.0 で Ruby の慣習に合わせる (0.4.0 では変えない。非推奨の警告も要らない。文書に書く)。
7. 1 引数の `reply` は、ポーリングの層 (機体) もブロックの層と同じ意味にそろえる。
8. `Proxy#methods` は 1.0 で Ruby 本来の意味に戻す。0.4.0 で `remote_methods` を足し、`methods` の上書きに非推奨の警告を出す。
9. 文書での呼び方は「portable API」と「CRuby API」。
10. 0.4.0 の次に 1.0。picoruby-asterism-zenoh も同じ版番号で出す。
11. 機体の C の変更 (get のキーワード、`connection_count`、エラーの系統、キーワードの形) は 0.4.0 で入れ、ファームを焼き直して確かめる。

## 1. コンパイラの case/in の直し (先に)

- 不具合と上流の直しは、`doc/ruby_asterism/upstream/picoruby_case_in.md` と `report/k1.md` にある。
  - ハッシュのパターンの値の位置のリテラルが何にでも合う。
  - ブロックの中の束縛ができない。
  - 直した上流のコミット: mruby `0e6bac0e5a7e` と `680084ac1275`。
  - 直しの候補: `picoruby_case_in.patch`。
- `lib/patch/compiler/` の今の仕組み (`rakelib/setup.rake` が写す) で当てる。
  - 当てる先の写しは、vendored の版 (mruby-compiler2 `10408c3`) に、上流の 2 コミットだけを足したものにする。
  - submodule の中は直接書き換えない。
- 確かめること:
  - K1 の再現 4 本が、sim の標準構成と互換構成で直る。
  - プロファイルの試し (`asterism/profile`) の case/in の行が、ok に変わる。表を作り直す。
  - fmruby-core の `rake test` が通る。
  - 標準構成で、エディタを起動して 1 打鍵する。
- 誤って合っていたパターンに頼っている所が無いかを見る。例: CRuby 用の `examples/ros2_talker.rb` の `in {linear: {x: 0.0}}`。共有の層とアプリを grep する。
- vendored の PicoRuby そのものを新しくするのは、別の課題として report に書く (この作業ではしない)。

## 2. 0.4.0 (3 つの gem)

### 2.1 対象

- **リポジトリ**: asterism、asterism-zenoh、picoruby-asterism-zenoh。
- **入れるもの**: `docs/api_review.md` の 0.4.0 の範囲 (壊さない追加、非推奨、文書)。
- **加えて直す不具合** (review で見つかったもの):
  - 小数の時間制限が切り捨てられる件 (`s.get(key, 2.0)` が 2 ms になる)。0.4.0 では、位置引数のミリ秒に Float が来たら警告を出し、四捨五入でなく秒と誤解されやすい旨を伝える。`timeout:` (秒) を案内する。
  - `ros2_talker.app.rb` の整数の割り算。
  - サービスの要求のフィールドが `timeout` / `timeout_ms` と同じ名前のとき、キーワードで呼べない件。時間制限は別の名前の指定 (例: `call(req, timeout:)`) で渡せるようにし、フィールドは `request:` か Hash で渡す形を案内する。今の呼び方は壊さない。
- **非推奨の仕組み**: 共有の小さな関数 1 つ。名前ごとに 1 回だけ警告し、CI 用に例外にする設定を持つ。機体 (mruby) でも動く書き方にする。

### 2.2 版とリリース

- 3 つとも `0.4.0`。asterism の依存は `asterism-zenoh ~> 0.4.0`。CHANGELOG を新しく作る (0.1.0 から)。
- **README**:
  - 「Which API?」(portable API と CRuby API の使い分け) の表を足す。
  - スレッドの安全性の表を足す。
  - 非推奨の一覧と、1.0 で何が変わるかを書く。

### 2.3 機体

- fmruby-core の PIN (ASTERISM_PIN、PICORUBY_ASTERISM_ZENOH_PIN) を新しいコミットに上げる。PIN の先は push 済みである必要がある。push の段取りは 3 章。
- 試しのアプリ (`flash/app/test/zenoh_*`、`asterism_*`、`ros2_*`) を、新しい名前を使う形に直す。古い名前のままの 1 本を残し、警告が 1 回出ることを確かめる。
- NARYAv4 (P4-Nano) で焼き直して (app_only)、次の 3 つが動くことを確かめる。
  - asterism_demo (CRuby との呼び合い)
  - ros2_talker / ros2_service (ROS 2 との往復)
  - zenoh_echo
- 起動時の内蔵 RAM の増分は 0。アプリのスタックの余りも書く。

## 3. 試験・CI・push

- 3 つのリポジトリの試験と CI (Ruby 3.2〜4.0、Linux・macOS) が通る。非推奨の警告を例外にする設定で、全部の試験が通る (古い名前を使う試験だけは、警告が出ることを確かめる)。
- **push**: 3 つのリポジトリの main への push を許す。CI を見て、緑になるまで直してよい。
  - asterism-zenoh と picoruby-asterism-zenoh を先に push し、asterism、fmruby-core の PIN の順にそろえる。
- **fmruby-core**:
  - `feature/asterism-c8` (`feature/asterism-k1` から切る) にコミットする。push はしない (親が develop へ入れる)。
  - report は `doc/ruby_asterism/report/c8.md` に書く。
- **してはいけないこと**:
  - `gem push` はしない。公開は親とユーザが行う。
  - タグとリリースを作らない。

## 4. 範囲・決まり

- **触らないもの**: `.env`、sdkconfig、パーティションの表、submodule の中 (lib/patch で当てる)、fmruby-graphics-audio。
- **本当の値を書かない**: 本当の IP・機体の名前・MAC をコミットしない。
- **機体**:
  - P4-Nano。本当の IP は親が渡す。ミュート中。
  - 親のシリアルの capture が向いている (`serial_start` を呼ばない)。
  - 焼くのは MCP の `flash` (app_only)。ビルドは `FMRB_HW_TARGET=NARYAv4` を環境変数で渡す。
- **zenohd・ROS 2**: 重ね合わせのファイルで上げ、終わったら `down`。
- **終わったら**: sim は sim_down、`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**:
  - コンパイラの直しで、ほかの言語 (BASIC・Lua・MicroPython) や Spinel の生成が壊れるとき。
  - 0.4.0 の範囲の中で、壊れる変更を避けられないとき。
  - 内蔵 RAM が増えるとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - コンパイラの直し (当て方、直った試し、誤って合っていた所)。
  - 0.4.0 の変更の一覧 (追加・非推奨・直した不具合)。
  - 機体の結果、CI の結果。
  - 1.0 に残すもの。
