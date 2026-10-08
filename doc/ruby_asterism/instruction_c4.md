# Asterism C4 指示書: `gem install asterism` で入るようにする

> 状態: 指示 | 更新: 2026-10-08 | asterism-zenoh の extconf が、入れるときに zenoh-c のビルド済みのものを機種に合わせて取ってきて C 拡張をコンパイルする。素の環境で gem のファイルから入れて動くことを確かめる。rubygems.org への公開はしない

ユーザ決定 (2026-10-08):
- `gem install asterism` で入るようにする。
- 配布の形は「入れるときに C をコンパイルする」。

## 1. やること

### 1.1 asterism-zenoh の extconf

1. `ZENOH_C_DIR` があればそれを使う (今と同じ)。
2. 無ければ、`ZENOH_C_PIN` の版のビルド済みの zip を、その機械の CPU と OS に合わせて GitHub のリリースから取る。
   - 対象は zenoh-c が公開している組み合わせのうち、少なくとも x86_64 / aarch64 の Linux (gnu) と、x86_64 / arm64 の macOS。
   - 機種ごとの sha256 を `ZENOH_C_PIN` に持ち、取ったものを確かめる。
   - 取得は Ruby の標準ライブラリ (net/http、リダイレクトを追う) と、展開は標準で使えるもの (unzip が無ければ Ruby で zip を読む、など) で。
   - 取得の先 (URL の元) は環境変数で差し替えられるようにする (社内のミラー用)。
3. 対応していない機種、取れない (ネットに出られない) ときは、何をすればよいか (`ZENOH_C_DIR` で手元の zenoh-c を指す) を書いて止まる。
4. 取ってきたライブラリ (`libzenohc.so` / `.dylib`) を C 拡張の隣に置き、`rpath` (`$ORIGIN` / `@loader_path`) で見つかるようにする。
   - gem を消せば一緒に消える場所にする。
5. 取ってきた zenoh-c の LICENSE / NOTICE も一緒に置く。
   - zip に入っていなければ、gem に Apache-2.0 の本文と zenoh-c の NOTICE を入れておく (C3 の README の「配布の注意」に合わせる)。

### 1.2 gem の中身と版

- 2 つの gem の版を `0.1.0` にする (`.pre` を外す)。
  - `asterism` は `asterism-zenoh` の同じ版に依存する (今と同じ形)。
- gemspec の `files` に、入れるときに要るものが全部入っていることを確かめる。
  - 例: `ZENOH_C_PIN`、ライセンスの文面。
- gemspec の metadata に次を入れる。
  - `homepage` / `source_code_uri`: `https://github.com/ruby-asterism/...`
  - `required_ruby_version`
- `gem build` で警告が出ないこと。

### 1.3 確かめ方

1. **素の環境**: docker の公式の Ruby の像 (例: `ruby:3.3` と `ruby:3.2`、Debian) で、手元で作った 2 つの gem のファイルを入れる。
   - 入れ方は `gem install --local ./asterism-zenoh-0.1.0.gem ./asterism-0.1.0.gem`。msgpack は rubygems.org から入ってよい。
   - 入ったあとに次が通ること:
     - `ruby -e 'require "asterism"; p Asterism::Zenoh::C_VERSION'`
     - 2 つのセッションの間の put / subscribe の短い試し
   - zenoh-c は extconf が取ってくる。この環境には git も zenoh-c も前もって無いこと。
2. **arm64 の Linux**: docker の `--platform linux/arm64` (qemu) が使えれば、同じことを試す。
   - 使えなければ「未確認」と書く。macOS も未確認でよい。
3. **ユーザの rbenv の Ruby 3.2**: 手元の rbenv に入れて、C1 の受け入れ条件 2 (CRuby ⇔ P4-Nano の asterism_demo の呼び出し) を 1 回試す。
   - 試したあと、入れた gem は消してもとに戻す。消したことを report に書く。
4. **asterism の各リポジトリの試験が通る**:
   - asterism-zenoh の `rake`
   - asterism の `test:msgs`、`test:objects`

## 2. 受け入れ条件

1. 素の Ruby の像 (3.2・3.3) で、gem のファイルから入れて、require と put / subscribe が通る。
2. 取ったものの sha256 を確かめている。取れないとき・対応しない機種のときの説明が出る。
3. ライブラリとライセンスの文面が gem の入った場所にあり、gem を消せば消える。
4. `gem build` の警告が無い。版は 0.1.0。
5. 各リポジトリの試験が通る。

## 3. 範囲・決まり

- 触ってよいのは、asterism-zenoh と asterism の 2 つのリポジトリと、`doc/ruby_asterism/report/c4.md` (fmruby-core)。
  - 2 つのリポジトリでは `main` にコミットしてよい。push は親が行う。
  - fmruby-core の report は develop から切った `feature/asterism-c4` にコミットする。
- **rubygems.org への公開 (`gem push`) はしない**。親がユーザの指示を受けてから行う。
- fmruby-core のビルドの仕組み、`.env`、sdkconfig には触らない。
- 機体は P4-Nano (IP は親が渡す。文書には実の IP を書かない)。
  - ミュート中。
  - 親の capture が向いている (`serial_start` を呼ばない)。
  - 焼き直しは要らない見込み。
  - 確かめに使う zenohd は、LAN 用の重ね合わせで上げて、終わったら `down`。
- 止まる条件:
  - zenoh-c のビルド済みのものの配り方が想定と違い、機種ごとに取れない。
  - 素の環境で入れるのに、gem の外の準備 (システムのパッケージなど) が要る。
  - 範囲の外の変更が要る。
- report:
  - Japanese plain style (常体) で書く。
  - 書くこと: extconf の動き、機種ごとの確認の結果、gem の中身の一覧、公開の前に親とユーザが決めること (版、公開の順番、yank の方針など)。
