# Ruby を 3.4 系の最新に上げる

> 状態: 進行中 (R5 だけ) | 更新: 2026-10-22 | 手元 (WSL の rbenv) と asterism-console・asterism-games の image が、サポートの終わった Ruby 3.2.6 のまま。3.4 系の最新に上げる。手元とリポジトリはまとめて一気に、公開と VPS は許可を得て 1 回で

## 目的

- サポートの終わった Ruby (3.2 は 2026-03-31 で EOL、3.2.11 が最後。使っている 3.2.6 はその中でも古い) をやめ、セキュリティ修正の出る版にする。
- 移る先は **3.4 系の最新** (2026-10-22 ユーザ決定。通常の保守が 2028 年 3 月ごろまで。4.0 は見送り)。
- console の image が古い信頼リスト (eMudhra の emSign ECC Root CA - G3 が残る、ca-certificates 20250419~deb12u1) なので、作り直しで一緒に直す。

## 今の状態 (2026-10-22 に確認)

| 対象 | Ruby | 備考 |
|---|---|---|
| WSL の手元 (rbenv) | 3.2.6 (global)。ほかの版は入っていない | ruby-build は 2024-11-15 のまま (新しい版を入れるには更新が要る) |
| asterism-console | `.ruby-version` 3.2.6、Dockerfile `RUBY_VERSION=3.2.6` | Rails 8.1.4、Bundler 4.0.6。image は CI (`image.yml`、タグで ghcr へ)。VPS は 0.3.9、最新 0.3.10 |
| asterism-games | Dockerfile `RUBY_VERSION=3.2.6` | Bundler 4.0.6。image は CI。VPS は 0.20.0、最新 0.21.0 |
| asterism・asterism-zenoh | gemspec `>= 3.2` | CI は 3.2・3.3・3.4・4.0 を通している |
| fmruby-core・fmruby-graphics-audio・picoruby-asterism-zenoh | — | CI は 3.4 |
| riichi_engine (asterism-games が使うフォーク) | `.ruby-version` 4.0.2 (上流のまま) | 触らない |
| 周辺の道具 (ルートの tools/、MCP サーバ fmrb) | 手元の Ruby で動く (bundler なし) | `require "base64"` あり (3.4 で bundled gem になったが、bundler の外なら読める) |

## 3.4 で気をつける変更 (試験で見るところ)

- **default gem から bundled gem に移ったもの** (base64・bigdecimal・csv・drb・getoptlong・mutex_m・nkf・observer・racc・resolv-replace・rinda・syslog): bundler の下で Gemfile に無いと読めない。console は base64 が Gemfile.lock にある (Rails 経由)。ほかは試験で確かめる。
- **`Hash#inspect` の形が変わった** (`{"a" => 1}` の空白、`{a: 1}`): inspect の文字列を比べる試験が落ちうる。
- **エラーの文の引用符が変わった** (`` `foo' `` → `'foo'`): CRuby のエラーの文をそのまま比べる試験が落ちうる (asterism の決まりは自前の文なので影響しない見込み)。
- 文字列リテラルの「凍結の予告」の警告 (`-W:deprecated` のときだけ)。
- `ruby:3.4.x-slim` の image の土台は Debian 13 (trixie) になる (今は 12)。asterism-zenoh の拡張 (zenoh-c) の組み立てと、sqlite3 などの共有ライブラリを確かめる。

## 段階

| 段 | 中身 | 誰が | まとめ方 |
|---|---|---|---|
| R1 (完了 2026-10-22、report/r1.md: 3.4.11、全リポジトリの試験が通った。3.4 で壊れたのは console の image の起動 1 件 (RubyGems 3.6 が Gemfile.lock の Bundler 4.0.6 に切り替え、lock を touch して root の持ち物で止まる → lock を rails の持ち物に)) | **手元を 3.4 に**: ruby-build を更新し、3.4 系の最新を rbenv に入れて global にする (3.2.6 は戻せるよう残す)。手元で使う gem (bundler・rbs など) を入れ直す。**全部のリポジトリの試験を 3.4 で一気に流す**: fmruby-core `rake test`、asterism・asterism-zenoh・asterism-games (`MJ_GAMES=4 bundle exec rake test`)・asterism-console の試験、picoruby-asterism-zenoh の host 試験、ルートの道具 (`ruby tools/...` の --help 程度) と MCP サーバの起動。落ちたものを直す | サブ Opus (指示書)、親が検収 | 1 回で |
| R2 (完了 2026-10-22: asterism a251807・asterism-zenoh 8231a77・asterism-games 14a5758・asterism-console 9082e65、未 push。image は Debian 13、G3 無し) | **リポジトリの版を上げる**: console の `.ruby-version` と Dockerfile、games の Dockerfile を 3.4 系の最新に。asterism・asterism-zenoh・asterism-games の gemspec の下限を `>= 3.4` に、CI を 3.4 と 4.0 に。手元で両方の image を作り、中の `ruby -v`、試験、信頼リスト (G3 が無いこと) を確かめる | 同上 (R1 と同じサブで続けて) | R1 と一緒に |
| R3 (完了 2026-10-22: asterism 0.11.0・asterism-zenoh 0.4.2 (端末側と同じマイナーを保つためパッチに)・asterism-games 0.22.0・console 0.3.11 を push とタグ、image の CI 成功。gem は asterism・asterism-zenoh が公開済み) | **公開**: 各リポジトリの push、console と games の版を上げてタグ (CI が ghcr に image を出す)、gemspec を変えたなら asterism・asterism-zenoh の gem の公開 | push・タグはユーザの許可、gem push はユーザ | 許可をもらって 1 回で |
| R4 (完了 2026-10-22: VPS は console 0.3.11・games 0.22.0。卓の状態が網に出て、console が答える) | **VPS に入れる**: console (0.3.9 → 新しい版) と games (0.20.0 → 新しい版) を**1 本のスクリプトで**入れ替える。中の `ruby -v`、G3 が無いこと、console の画面、部屋・卓、AI の 1 回の呼び出し、MCP からの網の見え方を確かめる | スクリプトは親、流すのはユーザ (sudo) | 1 回で |
| R5 | **残り**: tdiary のコンテナの Ruby の版 (IP 制限の内側なので急がない)、asterism-watch (非公開リポ) の CI の版、手元から 3.2.6 を消すか | 親 | 必要なら |

## 受け入れ条件

1. 手元の `ruby -v` が 3.4 系の最新。R1 の試験が全部通る (落ちたものは直したか、直せない理由を report に)。
2. console と games の image の中が 3.4 系の最新で、信頼リストに emSign ECC Root CA - G3 が無い。
3. VPS で console と games が新しい版で動き、部屋・卓・AI が使える。
4. 3.2.6 に戻す手順が report にある (rbenv global を戻す、image の版を戻す)。

## 決まったこと (2026-10-22 ユーザ、選択形式)

1. 移る先は 3.4 系の最新。
2. **gem の下限は `>= 3.4`** (asterism・asterism-zenoh・asterism-games の `required_ruby_version`)。CI は 3.4 と 4.0 だけにする。反映は各 gem の次の版の公開 (R3)。
3. **image の Ruby はパッチまで固定** (`ruby:3.4.x-slim`、今と同じ流儀)。

image は rubygems の今の版の asterism 系 gem を使う (`>= 3.2` の版は 3.4 で動く) ので、image の公開と gem の公開は順番に縛られない。

## 未確定事項

1. 各版の番号 (R3 で決める): gem は下限を変えるので次のマイナー (asterism 0.11.0、asterism-zenoh 0.5.0、asterism-games 0.22.0 の案)、console は 0.3.11 の案。
