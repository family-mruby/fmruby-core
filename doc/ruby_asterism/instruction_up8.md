# UP8 指示書: ゲームホスト・console・fmruby-core を asterism 0.8.0 に上げる

> 状態: 指示 | 更新: 2026-10-19 | asterism 0.8.0 (CLI、MCP、アプリの状態 @status、メタのメソッドの説明、on_app_join/leave、端末での credentials:) を rubygems に公開した (ruby-asterism/asterism main 94c3322)。それを使う 3 つを上げる

前提:
- asterism 0.8.0 (`/home/kishima/fmrb/family-mruby/asterism`、CHANGELOG の 0.8.0)。asterism-zenoh は 0.4.1 のまま。
- asterism-games 0.16.2: `Gemfile` で `gem "asterism", "0.7.1"` に固定、gemspec は `~> 0.7, >= 0.7.1`。0.7.1 より後の機能 (@status、メソッドの説明、on_app_join) は gem にあれば使う作り。
- asterism-console 0.3.9: `Gemfile` で、開発は隣の checkout (`path:`)、VPS の image は `ASTERISM_GEMS=rubygems` で `~> 0.7.1`。`Gemfile.lock`。
- fmruby-core: `lib/add/ASTERISM_PIN` (今 2d9ecd4) が端末用の層 picoruby-asterism (asterism の `mrblib/`) を固定。`rake asterism:setup` / `rake setup` で取り込む。lib/ を変えたら `rake clean`、ターゲットの切替は `rake clean_all` (fmruby-core/CLAUDE.md)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-* の文書は英語。

## 1. 決まっていること (2026-10-19 ユーザ)

- ゲームホスト・console・fmruby-core を asterism 0.8.0 に上げる。

## 2. 作るもの

### asterism-games (0.16.3)

1. `Gemfile` の固定を `0.8.0` に。gemspec の依存は、0.8.0 の機能を必須にするなら `~> 0.8`、今の「あれば使う」を保つなら今のまま (どちらにしたかと理由を report に)。`bundle update asterism` で `Gemfile.lock` を更新 (asterism-zenoh は 0.4.1 のまま)。
2. 試験 (`MJ_GAMES=4 bundle exec rake test`)。0.8.0 で @status・メソッドの説明・on_app_join が実際に効いていることを、手元のホストで確認する (例: asterism の CLI `asterism objects` / `asterism watch` で、ゲームホストの卓の @status やメソッドの説明が見える)。
3. CHANGELOG 0.16.3、VERSION、docs/install.md の 0.16.2 → 0.16.3 (update.sh だけ)。

### asterism-console (0.3.10)

1. `Gemfile` の rubygems の指定を `~> 0.8` に (コメントの版の記述も)。`bundle install` で `Gemfile.lock` を 0.8.0 に (隣の checkout は 0.8.0 の main)。
2. 試験 (`bin/rails test`、rubocop)。deploy/local を image から作り (rubygems の 0.8.0 を取るかを確認)、bridge が網に入り、図とアプリの状態 (@status) が出ることを確認する。
3. CHANGELOG 0.3.10、VERSION、compose の既定・env.example の版、docs/install.md の 0.3.9 → 0.3.10。

### fmruby-core

1. `lib/add/ASTERISM_PIN` を 0.8.0 のコミット (94c3322) に。`rake asterism:setup`、`rake setup`、`rake clean` の順で取り込む。
2. 端末で、0.8.0 の層が動くこと:
   - `rake test` (ネイティブの host テスト)。
   - Linux sim (標準構成): 起動し、エディタを 1 回起動して 1 打鍵する。Asterism を使うアプリ (麻雀とオセロの卓の一覧、`/app/test/asterism_demo.app.rb` など) が、手元の zenohd と一式 (`rake mahjong:up` など) につながって動く。
   - ブラウザ版 (`source ~/emsdk/emsdk_env.sh` の後 `rake wasm:web`、`rake spinel:gen` / `rake wasm:mrb` の罠に注意): 麻雀の卓の一覧に入れる。
   - ESP32 (TAB5、`.env` に従う): `rake clean_all` の後 `rake build:esp32` が通り、アプリの領域の空き・内蔵 RAM が増えていないこと (`idf.py size-components` などで、上げる前と比べる。内蔵 RAM の増分 0 の決まり)。**焼くのは親** (Tab5 に触らない)。最後は TAB5 の esp32 ビルドを残して終える。
3. 端末からアプリの状態を出す使い方を足すかは、この段では足さない (取り込みだけ)。

## 3. 受け入れ条件

1. 3 つの試験が通る (games、console、fmruby-core の `rake test`)。
2. 上の確認がそれぞれ通り、report に写す。
3. fmruby-core の内蔵 RAM が増えていない (増えたら止まって返す)。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)、asterism-console (main)、fmruby-core (develop、`lib/add/ASTERISM_PIN` とそれに伴うもの、report)。
- **触らないもの**: asterism (0.8.0 は公開済み)・asterism-zenoh・picoruby-asterism-zenoh、VPS、GitHub の設定、Tab5 (シリアル・WiFi)。牌のシート (`tiles_hand.bmp` / `tiles_river.bmp`) はユーザの手書き: 触らない。
- 外の AI の API を呼ばない。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- 起こしたもの (deploy/local、games の一式、sim、ブラウザ、試しの image と volume) は名前で止めて消す (sim は sim_down)。前からある image は残す。docker の prune はしない。
- **止まる条件**: 0.8.0 で端末側の層が picoruby/mruby で動かない、内蔵 RAM が増える、アプリの領域が足りない、1 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/up8.md`、常体): 3 つの変更、試験と確認の結果、内蔵 RAM とアプリの領域の比較、VPS の更新と Tab5 の焼き直しに要ること。
