# P4 の手順書: protocol 2 の公開

> 状態: 準備済 (発火はユーザの許可語の後) | 更新: 2026-11-13 | 各リポジトリの push、VPS の git 参照での更新、gem の公開、ピンの更新、ブラウザ版の配置を、この順に行う。手元のリハーサルで**止まる不具合が 1 つ見つかった** (8 章の 1、ワイルドカードの購読に値が来ない)。直し方をユーザが決めるまで 2 章以降に進まない

## 0. この文書の決まり

- **【許可】** と書いた段は、ユーザの明示の許可語を得てから行う。それ以外 (手元の確認・準備) は許可なしでよい。
- 秘密やホストに固有のものは `<...>` の置き場所にする。実在の IP・ドメイン・SSID・パスワード・鍵の中身は書かない。
- VPS の作業は sudo が要るのでユーザが流す。手元の作業は fmruby-core の作法どおり (`env -u ASTERISM_CREDENTIALS -u ASTERISM_ROUTER` で試験を流す)。
- 各段に「何を・コマンド・確認・戻し方」を書く。gem の公開は戻せない (同じ版の番号は二度と使えない) ので、その前に全部を git 参照で通す (review.md 5 章の 4)。

## 1. 版の番号 (ローカルでコミット済み、push していない)

| リポジトリ | 今の公開 | 次 | 理由 |
|---|---|---|---|
| asterism (gem) | 0.13.0 | **0.14.0** | ワイヤと API の両方が壊れる変更 (protocol 2、接続のオブジェクト、モジュールの関数の削除)。1.0 前なので minor を上げる |
| picoruby-asterism-zenoh (mrbgem、gem の公開なし) | 0.4.1 | **0.4.2** | 足すだけ (encoding・publisher・タイムスタンプ・16 KB の分割、未公開だった TLS・namespace)。CRuby の asterism-zenoh 0.4.2 と同じ API に追いついたので番号もそろえる (0.4.2 は CRuby だけの patch だった)。minor をそろえる決まり (CHANGELOG の冒頭) を守る |
| asterism-zenoh (gem) | 0.4.2 | **変えない** | 変更なし。asterism 0.14.0 の依存は今と同じ `~> 0.4, >= 0.4.1` |
| asterism-games (gem・イメージ) | 0.22.0 | **0.23.0** | protocol 2 の asterism が要る (gemspec `~> 0.14`)、観戦のキーの場所とロールが変わる |
| asterism-console (イメージ) | 0.3.21 | **0.4.0** | protocol 2 の網としか話さない。ロールの migration が一方通行。0.3.x の patch では足りない変わり方 |

依存の床:

- asterism-games の gemspec: `s.add_dependency "asterism", "~> 0.14"` (試験 `test_gem.rb` も 0.14.0 を満たし 0.13.0 を満たさないことを見る)。
- console の Gemfile の `ASTERISM_GEMS=rubygems` の枝: `gem "asterism", "~> 0.14"`。console の `Gemfile.lock` は手元の checkout (`path:`) の版 0.14.0 に合わせた (rubygems には問い合わせていない)。イメージは中で解き直すので、公開の後にロックを作り直す必要はない。
- asterism-games の `Gemfile` の公開版の枝 (`gem "asterism", "0.8.0"`) と `Gemfile.lock` は**まだ変えていない** (0.14.0 が rubygems に無いので解けない)。5 章の 3 で変える。
- `ASTERISM_DIR` (games・console)・`ASTERISM_ZENOH_DIR` (console)・fmruby-core の `ASTERISM_DIR`・`PICORUBY_ASTERISM_ZENOH_DIR` は今までどおり使える。

git 参照で VPS のイメージを作る入口を足した (4 章で使う):

- console: `ASTERISM_GEMS=git ASTERISM_REF=<asterism のコミット>` (Gemfile の枝、Dockerfile の build arg、`docker-compose.build.yml`)。asterism-zenoh は rubygems の 0.4.2。
- games: `ASTERISM_REF=<asterism のコミット>` (Gemfile の枝。イメージの中の `Gemfile.lock` の写しを `bundle lock --update asterism` で直す。イメージの Bundler は Gemfile の `lockfile` を知らないので、`ASTERISM_DIR` の枝のような別のロックにはしない。checkout では `ASTERISM_DIR` を使う)。
- どちらも asterism の git リポジトリ (既定は GitHub の公開のもの、`ASTERISM_GIT` で別の URL) から、push 済みのコミットを取る。

## 2. 全体の順序

| 順 | 段 | 許可 | 戻せるか |
|---|---|---|---|
| 0 | 8 章の 1 の不具合を直し、手元のリハーサル (7 章) を通し直す | 直し方はユーザが決める | - |
| 1 | 手元の全部の回帰 (3 章) | | - |
| 2 | push: picoruby-asterism-zenoh → asterism → asterism-games → asterism-console (4 章の 1) | **【許可】** | push は戻せる (revert)。タグはまだ打たない |
| 3 | fmruby-core のピンを上げ、`ASTERISM_DIR` なしで回帰 (6 章の 1・2) | | ピンのコミットを戻す |
| 4 | VPS の DB の写しを取り、console・games を git 参照のイメージで更新、通しで確認 (4 章の 2〜5) | **【許可】** (VPS、sudo はユーザ) | 写しで 0.3.21 に戻す (4 章の 6) |
| 5 | ブラウザ版を配置 (6 章の 3)。4 と同じ時間帯に | **【許可】** | 前の dist に戻す |
| 6 | gem の公開: asterism 0.14.0 → asterism-games 0.23.0 (5 章) | **【許可】** | **戻せない** |
| 7 | console・games のタグ (CI がイメージを出す) と VPS の公開イメージへの切り替え (5 章の 4・5) | **【許可】** | update.sh で戻す |
| 8 | fmruby-core の develop と直下の family-mruby の develop を push (6 章の 4) | **【許可】** | revert |
| 9 | 手元の道具の入れ替え (asterism の CLI・MCP、`regress:live` の CRuby の相手) (6 章の 5) | | gem を戻す |

- 2 と 3 の間で GitHub の README が protocol 2 を言い、rubygems は 0.13.0 のままになる。短いので許す。
- 4 と 5 は同じ時間帯に行う。console と games を上げた瞬間から、古いブラウザ版 (protocol 1) の呼び出しは `protocol` のエラーで断られる (7 章、9 章)。
- ファーム (Tab5・S3 の Web Installer) のリリースはこの企画に含めない (別のリリース、doc/release.md)。それまで古いファームの端末は卓に座れない (9 章)。

## 3. 手元の全部の回帰 (許可なし)

```
# asterism (CRuby・mruby・適合・cross・reconnect)
cd asterism && env -u ASTERISM_CREDENTIALS -u ASTERISM_ROUTER rake && rake test:mruby test:cross test:protocol_mruby test:reconnect
# picoruby-asterism-zenoh
cd picoruby-asterism-zenoh && rake clean && rake test && rake test_cruby
# asterism-games
cd asterism-games && ASTERISM_DIR=../asterism MJ_GAMES=4 bundle exec rake test && ASTERISM_DIR=../asterism bundle exec rake test:gem
# asterism-console
cd asterism-console && env -u ASTERISM_CREDENTIALS -u ASTERISM_GEMS bin/rails test && bin/rubocop
# fmruby-core (手元の checkout で)
cd fmruby-core && ASTERISM_DIR=../asterism PICORUBY_ASTERISM_ZENOH_DIR=../picoruby-asterism-zenoh rake regress:full
```

- 確認: どれも 0 failures。`regress:size` の内蔵 RAM の増分 0。
- 2026-11-13 の結果は 7.3。

## 4. push と VPS の git 参照での更新

### 4.1 push 【許可】

順番は依存の下から。どれもタグは打たない (gem・イメージの CI はタグで動くため)。

```
git -C picoruby-asterism-zenoh push origin main
git -C asterism push origin main
git -C asterism-games push origin main
git -C asterism-console push origin main
```

- 確認: `git -C <repo> log --oneline origin/main -1` が手元の HEAD。asterism の CI (main への push で動く) が緑。
- 戻し方: `git revert` して push (公開物は何も出ていない)。
- asterism-zenoh は変更が無いので push もしない。

### 4.2 VPS: DB の写しを取る 【許可・ユーザ】

console の `deploy/vps` で:

```
docker compose exec console bin/asterism backup --label before-0.4.0
docker compose exec console bin/asterism backups          # 名前を控える: asterism-<日時>-0.3.21-before-0.4.0.tar.gz
docker compose run --rm --no-deps -T backup tar -C /backups -cf - . > ~/asterism-backups-$(date +%F).tar   # サーバの外へも写す
```

- 確認: 一覧の先頭が今の写し。手元へ写したファイルの大きさが 0 でない。
- games の卓の保存も写す (games の docs/install.md 9 章、または `update.sh --when-idle` が `/state/before-update/` に写す)。
- **ロールの migration は一方通行** (足した行は 0.3.21 に戻しても残る、観戦者の古い行は消える)。戻すときはこの写しを戻す (4.6)。

### 4.3 VPS: console を git 参照のイメージで更新 【許可・ユーザ】

```
cd <console の checkout>
git fetch origin && git checkout <console の main のコミット>
# .env: CONSOLE_IMAGE=asterism-console-local、CONSOLE_VERSION=0.4.0 (sudoedit)
cd deploy/vps
ASTERISM_GEMS=git ASTERISM_REF=<asterism の main のコミット> \
  docker compose -f docker-compose.yml -f docker-compose.build.yml build console
docker compose run --rm -T console bin/asterism setup --from-update
docker compose up -d          # systemd の unit で動かしているなら、その unit を restart
```

- setup の出力に `relay role <名前> (protocol 2): ...`・`(spectate's profile)`・`(shared places)`・`(spectate calls)`・`(application tokens with the protocol)` が、変わったロールごとに出る (5 つの migration)。`ROUTER_CONFIG=written` ならルータは新しい設定で再起動している。
- 確認:
  - `docker compose ps` が全部 running、`docker compose exec console bin/asterism version` が 0.4.0。
  - Admin > Members > Roles: games-host に `asterism/${node}/*/@pubs`・`asterism/${node}/@shared/**`・`asterism/${node}/room$*`・`room$*/**`、Read に `asterism/*/game/protocol/*`。spectator は `asterism/*/@shared/spectate/**` ほか (docs/rooms.md と同じ)。
  - 手直ししてあるロール (例と違う名前で端末のアプリが状態やトピックを出すもの) は自動で直らない。docs/install.md の 20 章のとおり手で足して Apply。
- console のイメージの作りを手元で確かめたもの: 7.4。

### 4.4 VPS: games を git 参照のイメージで更新 【許可・ユーザ】

```
cd <games の checkout>
git fetch origin && git checkout <games の main のコミット>
# .env: GAMES_IMAGE=asterism-games-local、GAMES_VERSION=0.23.0
cd deploy/vps
ASTERISM_REF=<asterism の main のコミット> \
  docker compose -f docker-compose.yml -f docker-compose.build.yml build games
docker compose up -d
```

- 確認: `docker compose logs --tail 20 games` に `permanent: games: games/games up (...) via tls/...`、ルームを開くと `games/room<slot> up outside room/<slot>`。
- console の図で games の箱に `@shared` の置き場、アプリの詳細に Status と Topics。

### 4.5 VPS での通しの確認 【許可】

手元の PC から、利用者ごとの証明書 (console が出す `.tar`) で:

```
asterism topic list --credentials <viewer.tar>          # games/games/<卓>/state、games/@shared/spectate/<卓>/state
asterism call games/games/spectate state mahjong1 --credentials <spectator.tar>
asterism-games commentate mahjong --credentials <spectator.tar> --json --ai off --no-open   # 実況の行が出る
```

(手元の CLI は asterism の checkout の `exe/asterism`、games は `ASTERISM_DIR` 付きで動かす。gem はまだ 0.13.0。)

- ネイティブ版・ブラウザ版 (6.3 を配置したもの) から常設の卓に入り、座り、CPU と始め、相手の手が数秒で出る。
- Tab5 などの実機はユーザがつないだときに確かめる (protocol 2 のファームが要る)。

### 4.6 戻し方 (0.3.21 へ)

git 参照のイメージからは update.sh で戻れない (動いている版の公開イメージが要るため)。手で:

```
cd <console の deploy/vps>
docker compose stop console bridge backup
# .env: CONSOLE_IMAGE の行を戻す (消す)、CONSOLE_VERSION=0.3.21
git checkout v0.3.21
docker compose run --rm --no-deps -T console bin/asterism restore <4.2 の写しの名前>
docker compose run --rm -T console bin/asterism setup --from-update
docker compose up -d
```

- games: `.env` の `GAMES_IMAGE` を戻し `./update.sh 0.22.0` (卓の保存は 4.2 の写しから)。
- ブラウザ版も前の dist に戻す (6.3)。
- 公開イメージに切り替えた後 (5 章の 5) なら、console は `./update.sh 0.3.21 --restore <4.2 の写し>`。
- 写しを戻すと、更新の後に登録した利用者・出した証明書は消える。やり直す。

## 5. gem の公開とタグ (戻せない)

### 5.1 asterism 0.14.0 【許可】

```
cd asterism
rake gem                              # pkg/asterism-0.14.0.gem を作るだけ
gem push pkg/asterism-0.14.0.gem
git tag -a v0.14.0 -m "asterism 0.14.0" && git push origin v0.14.0
```

- 確認: `gem search -r '^asterism$'` が 0.14.0。`gem install asterism -v 0.14.0` が asterism-zenoh 0.4.2 と入る。
- 戻し方: なし。誤りがあれば `gem yank` しても **0.14.0 は二度と使えない**。直して 0.14.1 を出す。

### 5.2 picoruby-asterism-zenoh

gem の公開は無い。タグ `v0.4.2` を打つなら `git tag -a v0.4.2 -m "picoruby-asterism-zenoh 0.4.2" && git push origin v0.4.2` 【許可】。

### 5.3 asterism-games 0.23.0 【許可】

ここで初めて games の公開版のロックを作る (0.14.0 が rubygems にあるので解ける):

```
cd asterism-games
# Gemfile: else の枝を gem "asterism", "0.14.0" に
env -u ASTERISM_DIR -u ASTERISM_REF bundle lock --update asterism    # Gemfile.lock の asterism 0.14.0・asterism-zenoh 0.4.2 (ASTERISM_DIR があると別のロックを書くので外す)
env -u ASTERISM_DIR bundle exec rake test:gem                        # 公開の gem で通ること
git add Gemfile Gemfile.lock && git commit -m "build(gemfile): asterism 0.14.0 from rubygems"
git push origin main
gem build asterism-games.gemspec && gem push asterism-games-0.23.0.gem
git tag -a v0.23.0 -m "asterism-games 0.23.0" && git push origin v0.23.0     # CI がイメージを出す
```

- 確認: `gem search -r '^asterism-games$'` が 0.23.0。GitHub Actions の image が緑、`ghcr.io/ruby-asterism/asterism-games:0.23.0` がある。
- 戻し方: なし (5.1 と同じ)。

### 5.4 asterism-console 0.4.0 【許可】

```
cd asterism-console
git tag -a v0.4.0 -m "asterism-console 0.4.0" && git push origin v0.4.0     # CI (image.yml) が rubygems の asterism ~> 0.14 でイメージを作る
```

- 確認: image のワークフローが緑、`ghcr.io/ruby-asterism/asterism-console:0.4.0` がある。
- console の `Gemfile.lock` は作り直さない (イメージの中で解き直す。手元のロックは checkout の版を言う)。

### 5.5 VPS を公開イメージに切り替える 【許可・ユーザ】

```
# console: .env の CONSOLE_IMAGE を戻す (消す)。CONSOLE_VERSION は 0.4.0 のまま
cd <console の deploy/vps> && git checkout main && ./update.sh 0.4.0
# games: .env の GAMES_IMAGE を戻す
cd <games の deploy/vps> && git checkout main && ./update.sh 0.23.0
```

- console の update.sh は同じ版なので、タグを取り、公開イメージを pull し、写しを取り (`before-0.4.0`、git 参照の版のデータ)、setup を流し直す (変わらない)。**0.3.21 へ戻すのに使う写しは 4.2 のもの**。
- 確認: update.sh の最後の検査 (全部の service が新しいイメージ)、4.5 をもう一度。

## 6. fmruby-core とブラウザ版、手元の道具

### 6.1 ピンを上げる (4.1 の push の後、許可なし)

```
cd fmruby-core
# lib/add/ASTERISM_PIN の commit: を asterism の origin/main のコミットに
# lib/add/PICORUBY_ASTERISM_ZENOH_PIN の commit: を picoruby-asterism-zenoh の origin/main のコミットに
git add lib/add/ASTERISM_PIN lib/add/PICORUBY_ASTERISM_ZENOH_PIN
git commit -m "build(asterism): pin asterism 0.14.0 and picoruby-asterism-zenoh 0.4.2"
```

### 6.2 `ASTERISM_DIR` なしで作り直して回帰 (許可なし)

```
env -u ASTERISM_DIR -u PICORUBY_ASTERISM_ZENOH_DIR rake clean_all     # 見出し (Z_FRAG_MAX_SIZE) の変更は依存で追われない
env -u ASTERISM_DIR -u PICORUBY_ASTERISM_ZENOH_DIR rake build:linux && file build/fmruby-core.elf
env -u ASTERISM_DIR -u PICORUBY_ASTERISM_ZENOH_DIR rake regress
env -u ASTERISM_DIR -u PICORUBY_ASTERISM_ZENOH_DIR rake regress:size regress:checkpoint
ASTERISM_DIR=../asterism rake regress:live           # CRuby の相手 (step_send.rb) は gem 0.14.0 を入れるまで checkout が要る
```

- 確認: rake が「ピンのコミットでない」と言わない (vendor/asterism がピン)。regress 全部 ok、内蔵 RAM の増分 0。
- 戻し方: ピンのコミットを revert して `clean_all`。

### 6.3 ブラウザ版の配置 【許可】 (4.3・4.4 と同じ時間帯)

```
source ~/emsdk/emsdk_env.sh
cd fmruby-core
rake spinel:gen && rake wasm:mruby             # clean_all が wasm の libmruby を消すので作り直す
rake wasm:dist                                 # wasm/build/dist (version <12 桁>) を控える
rake wasm:site FMRB_SITE_DIR=<family-mruby.github.io の checkout>   # docs/studio をコミットして push (先に fetch)
FMRB_WEB_DEST=<host>:<dir>/ rake wasm:deploy   # 自前の置き場 (console のパスの頁が開くもの)。前の dist は写しておく
```

キャッシュ: `wasm:dist` はページが読むすべての URL に内容の要約 `?v=<12 桁>` を付ける (`core_web.js`・`.wasm`・`.data`・`main.js` ほか)。古い `main.js` と新しい `.data` の組は起こらない。**`index.html` 自身には付かない**ので、

- 自前の置き場: `index.html` に `Cache-Control: no-cache` (または `no-store`) を付ける (リハーサルの nginx の play.conf は `no-store`)。`curl -sI https://<page-host>/<path>/ | grep -i cache-control` で見る。
- GitHub Pages は `max-age=600` を付ける。最大 10 分は古い `index.html` (protocol 1 の `?v=`) を開く人がいる。
- 開いたままのタブは再読み込みまで protocol 1 のまま。
- 確認: 公開の `index.html` の `window.FMRB_WASM_VER = '?v=<hash>'` が手元の dist と同じ。`core_web.wasm` がバイト単位で同じ (doc/release.md 4 章)。新しいページでパスを作って開き、卓の一覧が出て座れる。
- 戻し方: 前の dist を同じ場所へ戻す (サイトは revert して push)。

### 6.4 fmruby-core と直下の push 【許可】

```
git -C fmruby-core push origin develop          # 6.1 のピンのコミットを含む。ピンを上げる前の develop は push しない (p3c 9 章)
git push origin develop                         # 直下 (tools/fmrb_zenoh.rb の call が protocol 2)
```

- fmruby-core の develop の CI が緑になるのを待つ。main への早送りはファームのリリースのとき (doc/release.md)。

### 6.5 手元の道具 (5.1 の後)

```
gem install asterism -v 0.14.0                  # CLI と MCP の橋 (今入っているのは 0.11.0 = protocol 1)
gem install asterism-games -v 0.23.0            # 実況を gem で使うなら
```

- Claude の MCP の `asterism` サーバ (CLI の `asterism mcp`) も 0.14.0 で動くようにする。0.11.0 のままだと protocol 2 の卓は呼べない (`protocol` のエラー)。
- `regress:live` の CRuby の相手が `ASTERISM_DIR` なしでも動くようになる。

## 7. リハーサルの結果 (2026-11-13、手元)

### 7.1 組み立て

本番と同じ形を手元に作った。作ったものはすべて scratchpad (コミットしていない) で、終わった後に止めた。

| もの | どう作ったか |
|---|---|
| ロール | 0.3.21 の種 (`db/seeds.rb` の match) と文書の例 (docs/rooms.md の games-host・spectator、docs/g1_5.md の viewer、games の docs/install.md の games) に、console の 5 つの migration の関数 (`with_app_profiles` ほか) を順に当てたもの。リハーサルだけのロール `shared-writer` (網全体の置き場に書ける) を 1 つ足した |
| ルータの設定 | `Relay::CloudConfig` に上のロールと台帳の行 (console・games・spectator-a・admin-viewer・fmruby-aaaaaa・member-a・writer-a) と設定 (ws/ の入口、`room_hosts` games、ルーム 01 の席 2 つ) を渡して生成 |
| 証明書 | console の `script/relay_certs --out <scratchpad>` で使い捨ての CA を作り、各名前を発行。`.tar` (鍵・証明書・ca.pem・asterism.json) は手で組んだ。本物の CA・`~/asterism_keys/` は使っていない |
| ルータ | `eclipse/zenoh:1.10.1` (本番の compose と同じ) を相互 TLS (tls/ 7448) と ws/ 7449 で |
| ゲームホスト | asterism-games (手元の asterism) で常設の卓 (mahjong1 は CPU 4 人、othello1) と、ルーム 01 のホスト (名前空間 room/01、外にアプリ room01) を games.tar で |
| console の bridge | `RAILS_ENV=test bin/bridge` (試験のデータベース) を console の証明書で |
| ネイティブ版 | P2e のビルド (手元の asterism・picoruby-asterism-zenoh) を別の run dir・user dir で、fmruby-aaaaaa.tar (match と games) |
| ブラウザ版 | web_up のページ、`/home/asterism/config.toml` に ws/ の入口 (証明書なし = 全ブラウザ共通の主体)。終わった後に消した |
| CLI | asterism の checkout の `exe/asterism` を各証明書で。protocol 1 は入れてある gem 0.11.0 |

ドアの代わりにブラウザ版を ws/ に直接つないだ (ルータから見た主体は同じ `link_protocols ws`)。

### 7.2 確かめたこと

| 何を | 結果 |
|---|---|
| migration の結果 | match に `*/@profile|@status|@pubs` と `asterism/@shared/**`、games-host に `@profile|@pubs`・`@shared/**`・`room$*`・`room$*/**`・Read `asterism/*/game/protocol/*`、spectator は古い spectate の行が消えて共有の置き場と送り手の `@profile|@pubs` と `spectate/call/${node}`、viewer に `@pubs`・`@profile` と置き場 2 つ。docs/rooms.md・docs/g1_5.md の例と同じ行 (games-host には 0.18.0 の `spectate`・`spectate/**` が残る、害なし) |
| viewer の `topic list` | `games/games/mahjong1/state` (`asterism-games/mahjong.state`)、`games/games/othello1/state`、`games/@shared/spectate/mahjong1/state` (by games/games)、`games/@shared/spectate/room01-mahjong1/state` (by games/room01) |
| spectator の `spectate.state` の呼び出し | games/games/spectate と games/room01/spectate の両方が答えた |
| spectator が卓の値を読めない | `games/games/mahjong1/state` の購読に 0 件 (Read に無い) |
| spectator の `topic list`・`topic info` | **何も出ない** (8 章の 2) |
| 網全体の置き場 | writer-a の `@shared/lobby` を member-a・fmruby-aaaaaa・viewer が読めた (12 件前後 / 6 秒)。spectator は 0 件。member-a が自分で `@shared/lobby2` に put したものは誰にも届かない (ルータが断る) |
| 版の印 | protocol 1 (gem 0.11.0) のアプリ `member-a/game` が来ると、ネイティブ版のログに `member-a/game speaks Asterism protocol 1, this program 2: calls between them are refused ...`。tree の `-o json` で protocol 2 のアプリに `"protocol": 2` |
| protocol 1 の呼び手 | gem 0.11.0 の `asterism call games/games/othello1 view` は `protocol: Asterism::Error: games/games speaks Asterism protocol 2, the call says no protocol (protocol 1 or older)` |
| つなぎ直し | ホストを kill -9 して 0.3 秒で起こすと、0.6 秒で `games/games up`。viewer の購読は回の識別子が 42698 → 63859、番号 0 から、`lost` 0 |
| console の bridge | 0.1 秒でつながり、ノードの自己紹介 (games・fmruby-aaaaaa の protocol=2) と図を作った。`console/transfer speaks Asterism protocol 1` と自分の受け取り口を版違いと言った (8 章の 3、直した) |
| ネイティブ版 | tls/ でつながり (26 ms)、othello1 が一覧に出て、入る・座る・CPU・始める・指すが通った |
| ブラウザ版 | ws/ で `wasm-a10ea0` としてつながり、othello1 に入って座れた。ネイティブ版とブラウザ版で対局し、相手の手がどちらにも 2〜3 秒で出た |
| ルータのセッション | 5 (常設のホスト 1、ルームのホストの中と外で 2、ネイティブ版 1、見た接続 1)。ホスト 1 つにつき常設 1 + ルームごとに 2。本番の `max_sessions` は 96 |

### 7.3 試験 (手元)

| 何を | 結果 |
|---|---|
| asterism `rake` (版上げと 8 章の 3 の直しの後) | 全部 0 failures、適合 cruby 250 passed |
| picoruby-asterism-zenoh `rake test` | 167 checks、0 failed |
| asterism-games `rake test` (`MJ_GAMES=4`) | 411 runs、1 failure → gemspec の床の試験を直して通過。`rake test:gem` 168 runs 0 failures |
| asterism-console `bin/rails test` | 665 runs、0 failures。8 章の 3 の後に transfers_test を通した。rubocop 323 files 違反なし |
| fmruby-core の回帰 | 流していない (P2e の後に asterism のコードの変更は 8 章の 3 だけで、端末は使わない部分) |

### 7.4 git 参照のイメージ (4.3・4.4 の作り方)

手元の asterism の bare clone を git daemon のコンテナ (既定のブリッジの上) で配り、`ASTERISM_GIT=git://<そのコンテナ>/asterism.git`・`ASTERISM_REF=da1c382` で両方のイメージを作った。

| | 結果 |
|---|---|
| console (`ASTERISM_GEMS=git`) | 作れた。イメージの中で `bin/asterism version` が `asterism-console 0.4.0`、`Asterism::VERSION` 0.14.0、`Asterism.app_token_key("console", "transfer")` が `.../protocol/2` |
| games (`ASTERISM_REF`) | 作れた。`Asterism::VERSION` 0.14.0、`bin/asterism-games --help` が動く |

途中で見つけて直したもの (どちらもコミット済み):

- Dockerfile の `COPY . .` が、bundle が解いたロックを checkout のものに戻していた。rubygems の枝ではイメージの中で解き直せたので表に出なかったが、git の源はリビジョンが無いと読めない。両方のイメージで、解いたロックを `COPY . .` の後に戻す。
- games のイメージの Bundler は Gemfile の `lockfile` を知らない (`ASTERISM_DIR` の枝はイメージで使わないので無事)。git の枝は別のロックにせず、イメージの中の `Gemfile.lock` の写しを直す。
- 手元の WSL の Docker では `docker build --network host` が効かず、ホストの git daemon に届かなかった。本番は GitHub から取るので関係しない。dumb HTTP は Bundler の浅い clone を受けない。

## 8. 見つかったこと

### 8.1 【止まる】ワイルドカードの購読に、宣言した publisher の値が来ない

- 見え方: ACL のあるルータで、`*/games/*/state`・`*/@shared/spectate/**` のように**ノードをワイルドカードにした購読だけ**が、ほかに購読者がいないと値を 1 つも受けない。`games/games/mahjong1/state`・`games/@shared/**` のようにノードを名指しすると受ける。名指しの購読者が同時にいると、ワイルドカードの購読者にも届く。
- 影響: 実況 (`commentate`、`*/@shared/spectate/*/state`) が 25 秒で 0 行 (卓は動いていた)。console の Watch・記録 (`asterism/**` とその置き場) も同じ見込み。ブラウザ版の観戦の購読も。卓の画面 (ネイティブ版・ブラウザ版) は `watch` の呼び出しで状態を取るので、見た目には動く。
- 原因 (確かめた): protocol 2 は値を Zenoh の**宣言した publisher** で出す。publisher は購読者がいるときだけ値を線に出す (p3c の 4.3 で見た「購読者がいないと出さない」)。購読者の宣言はルータから publisher へ送られるが、zenohd の ACL はその宣言を publisher の側の **egress の `declare_subscriber`** として調べる。ホスト (games-host) の許しは自分のキー (`asterism/games/games/**` など) だけで、`asterism/*/games/*/state` はそれに含まれないので落とされる。publisher は購読者がいないと思い、出さない。protocol 1 は publisher を宣言せず put していたので起きなかった。
- 確かめ方: 同じ設定にホストの policy へ egress `declare_subscriber` を `asterism/**`・`asterism/*/@shared/**`・`asterism/@shared/**` で足しただけのルータを並べて立てると、viewer のワイルドカードの購読が 15 件・11 件を受けた (足す前は 0 件)。設定を変えないルータで、新しく立てたホストに 1 つずつ購読すると、名指しは 10〜12 件、ワイルドカードは 3 回とも 0 件。
- 直し方の候補 (**仕様と ACL の選択なのでユーザが決める**):
  1. console の `Relay::CloudConfig` が、値を出す主体 (台帳の行・ブラウザ・席) に egress の `declare_subscriber` を `asterism/**` と置き場 2 つで共通に許す (推奨。購読の宣言のキー式が相手のノードに見えるだけで、値・呼び出しは今の規則のまま。信頼するメンバーの網の前提に合う)。console の版の変更 (0.4.0 に入れる) と試験。
  2. asterism が publisher を宣言せず put に戻す (encoding は put に付く)。design.md の「publisher の宣言」の決定を変える。端末の RAM・宣言の効き目にも関わる。
  3. 読み手がノードを名指して購読する (実況はホストを知っている)。console の記録・見張りは名指しできないので残る。
- 直すまで P4 に進まない (4.5 の実況の確認が通らない)。

### 8.2 spectator の `topic list`・`topic info` が空

- spectator にはアプリの生存の印 (`asterism/<node>/games/protocol/2`) を読む行が無いので、CLI がアプリを見つけられない。`topic info games/@shared/spectate/mahjong1/state` は `asterism/games/*/@profile` へのワイルドカードの問い合わせになり、spectator の Ask (`asterism/*/games/@profile` など、アプリを名指した行) に含まれず断られる。
- 実況そのもの (値の購読と `type_of(info)`、`spectate.state`) は名指しの get なので影響しない。
- 食い違い: console の docs/install.md (20 章の spectator の段)・docs/rooms.md は「spectator の `asterism topic list` に卓のトピックが出る」と言う。P2c の共有の置き場の migration が spectate の印の行を消した後は出ない。
- 直し方の候補 (ユーザが決める): spectator の Read に `asterism/*/games/protocol/*`・`asterism/*/room$*/protocol/*` を足す migration (印からアプリが分かり、`@profile` は名指しで聞ける。`topic info` のワイルドカードの問い合わせは CLI が送り手ごとに名指す形にするか、別に許す)、または文書から「topic list に出る」を消す。

### 8.3 (直した) console の受け取り口が protocol 1 の印を出していた

- `Asterism::Transfer::Endpoint` (console の `console/transfer/inbox`、素の Zenoh の接続の上の受け取り口) が protocol 1 の印 `asterism/console/transfer` を出し、頼みの `protocol` も見ていなかった。どの protocol 2 のプログラムも「console/transfer speaks Asterism protocol 1」と言い、console の図も赤い枠で描く。
- 直し: 印を `Asterism.app_token_key` (`.../transfer/protocol/2`) にし、版の違う頼みを `protocol` のエラーで断る (オブジェクトの層と同じ)。asterism の試験 (印のキー、protocol 1 の頼みの断り) と console の `transfers_test` の印のキーを直した。ACL は `asterism/<node>/transfer/**` の行が新しい印にも掛かるので変わらない。

### 8.4 文書の食い違いを直したもの (3 章の task)

| どこ | 直したこと |
|---|---|
| asterism-games docs/install.md 7 章 | games-host の Read に `asterism/*/game/protocol/*` (console の docs/rooms.md と P2e の migration に合わせた) |
| asterism-games README「Who is there」の表 | 同じ行 |
| asterism-games docs/install.md 8 章 | 「From 0.22.0 to 0.23.0」: console と一緒に上げること、migration が足す行 (games-host・spectator・viewer)、0.18.0 の `spectate` の行は使われなくなる、確認、戻し方 |
| asterism-games docs/install.md「Building the image yourself」 | git 参照のイメージ |
| asterism-console docs/install.md 20 章 | 版を 0.4.0 に、migration は 5 つ (`app_token_protocol` が数に入っていなかった)、protocol 1 の呼び出しは断られる、戻し方は写しで (`--restore`) |
| asterism-console docs/install.md 18 章 | git 参照のイメージと、そこからの戻し方 |
| asterism-console README「The Asterism gems」・Gemfile の注 | 0.14.0、`~> 0.14` |

spectator の行 (`@shared/spectate/**`、`games|room$*` の `@profile|@pubs`、`spectate/call/${node}`) と games-host の `@shared/**`・`room$*` の行は、games と console の文書と migration の結果が一致していた (7.2)。8.2 の「topic list に出る」の文は、直し方が決まるまで残した。

## 9. 古い版の利用者に見えること

| 誰 | 見えること |
|---|---|
| 古いブラウザ版 (キャッシュ・開いたままのタブ) | 卓の一覧は出る (値は流れる) が、入る・座る・指すの呼び出しが `protocol` のエラー (`games/games speaks Asterism protocol 2, the call says no protocol (protocol 1 or older)`) で断られる。再読み込みで直る |
| 古いファームの端末 (Tab5・S3) | 同じ。ファームのリリースまで卓では遊べない。端末どうしの protocol 1 の呼び出しは今までどおり |
| gem 0.13.0 以前の CLI・MCP | `call` が `protocol` のエラー。`watch`・`tree` は protocol 2 のアプリの印 (`.../protocol/2`) を知らないので、アプリが見えない・ずれる |
| asterism-games 0.22.0 の実況 | 観戦のキーが `@shared/spectate/...` に移ったので何も来ない |
| console 0.3.21 のまま games だけ 0.23.0 | 図にホストの Status・Topics が出ない (console が `@profile` を聞かない)。卓の値は流れる |
| protocol 2 の側 | protocol 1 のアプリはアプリとして見え、ログ (と console の赤い枠) で 1 回言われる。protocol 2 から protocol 1 の受け手への呼び出しは通る |

## 10. 片づけ (このリハーサル)

- 止めたもの: ルータ 2 つ (`p4-rehearsal-zenohd`、比べるための `-b`)、ゲームホスト、bridge、ネイティブ版 (別の run dir)、ブラウザ版 (web_down、置いた `config.toml` は消した)、試しの購読・put の CRuby。
- 7.4 のイメージの試しで使った git daemon のコンテナと試しのイメージ 2 つも消した (`docker builder prune` も)。
- 使い捨ての CA・鍵・`.tar` は scratchpad から消した。生成の台本 (ロールに migration を当てて CloudConfig を作るもの、ホストの台本) は scratchpad に残る (コミットしていない)。

## 関連

- review.md 5 章 (P4 の危険と順序)、p2e.md 9 章、p3c.md 9 章、p3b.md 8・9・11 章、p3a.md 2・8 章。
- asterism-console docs/install.md 15・16・18・20 章、asterism-games docs/install.md 8 章、family-mruby の doc/release.md (ファームと Web Studio)。
