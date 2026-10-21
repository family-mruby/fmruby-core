# R1・R2 指示書: 手元を Ruby 3.4 にし、全リポジトリの試験を通し、版を上げる

> 状態: 指示 | 更新: 2026-10-22 | plan.md の R1 と R2 を 1 回で行う。手元 (WSL の rbenv) を 3.4 系の最新にし、全部のリポジトリの試験を 3.4 で流して直し、console と games の image と gem の下限を上げる。push・タグ・gem の公開はしない

前提: plan.md (この指示書と同じフォルダ) を先に読む。決まったこと: 3.4 系の最新、gem の下限 `>= 3.4`、image はパッチまで固定。

言葉: 文書 (report) は常体、「確かめ」ではなく「確認」。コードのコメントと各リポジトリの英語の文書は英語。

## 1. 手元を 3.4 に (R1 の前半)

1. ruby-build を更新する (`~/.rbenv/plugins/ruby-build` で `git pull`)。`rbenv install --list` と ruby-lang.org のお知らせで **3.4 系の最新**を確かめ、その番号を report に書く。
2. `rbenv install <3.4 の最新>`。足りないビルドの部品 (libssl-dev など) があって sudo が要るなら、**止まって**返す (要る apt のパッケージ名を report に)。
3. 切り替える前に、3.2.6 で入っている gem の一覧を控える (`gem list > report 用のファイル`)。default 以外で、ユーザが直接使っているもの (asterism、asterism-games、asterism-zenoh、rbs、bundler 4.0.6、rails など、`gem list` で確かめる) を 3.4 に**同じ版で**入れ直す。特に **`asterism` のコマンド** (Claude Code の MCP サーバ `asterism mcp` が使う) と **`asterism-games` のコマンド** が 3.4 で動くこと (`asterism --version`、`asterism-games --help`)。
4. `rbenv global <3.4 の最新>`。**3.2.6 は消さない** (戻せるように)。戻し方 (`rbenv global 3.2.6`) を report に。

## 2. 全部のリポジトリの試験を 3.4 で (R1 の後半)

場所は本体の checkout (`/home/kishima/fmrb/family-mruby/<リポ>`)。各リポジトリの README・Rakefile・CI の設定 (`.github/workflows`) から試験の流し方を読み、3.4 で流す。落ちたら直す (3.4 の変更点は plan.md の「3.4 で気をつける変更」)。

| リポジトリ | 少なくとも流すもの |
|---|---|
| fmruby-core | `rake test` (host 試験。rbs gem が要る) と `rake voice:test`、`rake mahjong:overflow` |
| asterism | CI と同じ試験 (適合の試験 test/protocol/ を含む) |
| asterism-zenoh | 拡張の組み立てと試験 (zenoh-c) |
| asterism-games | `MJ_GAMES=4 bundle exec rake test` |
| asterism-console | Rails の試験 (`bin/rails test` など、CI か README の通り) |
| picoruby-asterism-zenoh | CI の host 試験 |
| ルートの道具 | `ruby tools/fmrb_web.rb --help`、`ruby tools/fmrb_zenoh.rb --help` など、`tools/*.rb` が 3.4 で読み込めること (`ruby -c` と --help 程度)。MCP サーバ `ruby tools/mcp/fmrb_mcp_server.rb` が起動して tools/list に答えること (標準入出力の JSON-RPC で 1 往復) |

- **Linux sim・実機・ブラウザ版は使わない** (Ruby の版と関係が薄く、1 組しか無いため)。
- 試験のための zenohd などは自分で起こし、名前か pid で止める。docker の prune はしない。
- 3.2.6 で落ちていたものが 3.4 でも落ちるだけなら、直さずに report に書く (3.2.6 でも落ちることを確かめる)。

## 3. 版を上げる (R2)

| リポジトリ | 変えるもの |
|---|---|
| asterism-console | `.ruby-version`、`deploy/vps/Dockerfile` の `RUBY_VERSION` を 3.4 の最新に。README などの Ruby の版の記述 |
| asterism-games | `deploy/vps/Dockerfile` の `RUBY_VERSION`。gemspec の `required_ruby_version` を `>= 3.4`。README の記述 |
| asterism | gemspec の `required_ruby_version` を `>= 3.4`。CI の Ruby を 3.4 と 4.0 に (3.2・3.3 を外す)。README の記述 |
| asterism-zenoh | 同上 |

- **gem の版 (VERSION) と CHANGELOG は上げない** (公開のときに親が決める)。CHANGELOG の Unreleased に 1 行書くのはよい。
- games・console の Gemfile の asterism 系 gem の版の指定は**変えない** (image は rubygems の今の版を使う)。
- **image を手元で作って確かめる**: console と games の Dockerfile で `docker build` し (タグは `asterism-console:ruby34-check` などの自分の名前)、中で `ruby -v`、`ls /etc/ssl/certs | grep -i emsign` (**emSign_ECC_Root_CA_-_G3 が無いこと**)、`dpkg -l ca-certificates`、Debian の版、games は試験か起動 (`asterism-games --help` と、手元の zenohd に当てて卓が 1 つ立つ程度)、console は起動して `/up` などが答える程度。確かめた後、作った image は名前で消す (prune はしない)。
- `ruby:3.4.x-slim` の土台が Debian 13 になるなら、asterism-zenoh の拡張の組み立てと sqlite3 などが通ることを image の中で確かめる。

## 4. 受け入れ条件

1. 手元の `ruby -v` が 3.4 系の最新。`asterism mcp` と `asterism-games` のコマンドが 3.4 で動く。
2. 2 章の試験が全部通る (3.2.6 でも落ちていたものは report に理由つき)。
3. console と games の image が 3.4 の最新で作れ、中に emSign ECC Root CA - G3 が無い。
4. 各リポジトリにコミットがある (下の決まり)。

## 5. 範囲・決まり

- **触ってよいもの**: `~/.rbenv` (ruby-build の更新、3.4 の追加、global の切り替え、gem の入れ直し)、asterism・asterism-zenoh・asterism-games・asterism-console・fmruby-core・picoruby-asterism-zenoh・ルートの tools/ (3.4 で落ちるものの直しと、3 章の版の変更)、report。
- **触らないもの**: riichi_engine、fmruby-graphics-audio (CI は 3.4 済み。落ちるものがあれば report に書くだけ)、VPS、GitHub の設定、ユーザの `~/asterism_keys/`・API キー・ユーザの VOICEVOX、Tab5・シリアル・sim・ブラウザ版。**3.2.6 を消さない**。sudo を使わない。
- **push・タグ・gem push はしない**。`git commit -a` は使わない。コミットは各リポジトリの main / develop (今のブランチ) に、件名は履歴に合わせ (例 `ruby: ...`、`ci: ...`、fmruby-core なら `tools: ...`)、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値 (IP・ドメイン・端末の ID・鍵) を書いていないか差分を grep してからコミットする。fmruby-core の doc (この指示書・report・plan) はコミットしない (親がする)。
- **止まる条件**: sudo が要る、試験を直すのに仕様が変わる直し方 (振る舞いを変える・機能を外す) が要る、範囲外のリポジトリの変更が要る、3.4 でどうしても動かない gem がある。止まったら report に状況と、ユーザが選べる選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby34/report/r1.md`、常体): 入れた 3.4 の番号、入れ直した gem、リポジトリごとの試験の結果 (件数) と直したもの (3.4 のどの変更が原因か)、image の確認の結果 (ruby -v、Debian の版、ca-certificates の版、emSign の一覧)、コミットの一覧、戻し方、残り。
