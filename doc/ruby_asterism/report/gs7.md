# GS7 報告: 枠組みを gem に、ゲームホストの image を ghcr に

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | instruction_gs7.md の結果。asterism-games 0.12.0 で、gem `asterism-games` (枠組み・オセロ・ボード) と image (それに麻雀を足した全部) を同じリポジトリの中で分けた。image はタグ `v*` で CI が作り `ghcr.io/ruby-asterism/asterism-games:<版>` に出す形にし、deploy/vps は ghcr の image を既定にした。止まる条件には当たっていない。gem の公開・タグ・ghcr の公開設定はユーザが行う

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism-games | main `6997d8b` | games 0.12.0: `asterism-games.gemspec`、麻雀を `lib/` から `app/` へ移す、`bin/asterism-games --require`、`rake test:gem` と `test/test_gem.rb`、`.github/workflows/image.yml`、deploy/vps (compose の既定を ghcr に、`docker-compose.build.yml`、`update.sh` を版の指定に)、README「The gem and the image」、docs/install.md、CHANGELOG |
| fmruby-core | develop | この報告だけ |

push・タグ・rubygems への公開・ghcr への push はしていない。asterism-console は読んだだけ (image の CI と compose の形を写した)。asterism・riichi_engine の Fork・zenohd・VPS には触っていない。コミットの前に差分を鍵の形 (`sk-ant`・`Bearer`・`BEGIN ... PRIVATE`)・IP の形・実在のドメインとメールで検査し、該当なし。

## 2. gem に入れたもの・入れないもの

### 2.1 入れたもの (作った gem の中身の一覧そのもの)

```
CHANGELOG.md  LICENSE  README.md  VERSION
bin/asterism-games  bin/othello-tty
lib/asterism_games.rb
lib/asterism_games/{ai,ai_hub,config,game,host,remote,rooms,store,version}.rb   枠組み
lib/asterism_games/othello.rb  othello/{board,remote,table}.rb                 見本 1
lib/asterism_games/board.rb    board/{remote,table}.rb                         見本 2
```

- `VERSION` を入れるのは、`version.rb` がそれを読むため (gemspec の版も同じファイルから)。
- 入れるファイルは gemspec に名前と型で書き、`git ls-files` には頼らない。作業コピーにある `var/`・`deploy/vps/credentials/`・`.env` のようなものが紛れ込む道を作らないため。

### 2.2 入れないもの

| もの | 理由 |
|---|---|
| 麻雀 (`app/asterism_games/mahjong.rb`・`mahjong/`) | riichi_engine の Fork (git の取得元) に頼る。rubygems の gem は git の依存を持てない (1 章の決まり) |
| `bin/mahjong-bots` | 麻雀の道具 |
| `deploy/`・`config/` | サーバ用の一式と、麻雀の卓を含む設定。gem だけの人には README に 8 行の設定を載せた |
| `test/` | 重い (麻雀の半荘) うえ、gem を使う人には要らない |
| `.github/`・`Gemfile*`・`Rakefile` | 開発と image のもの |

### 2.3 麻雀と AI の打ち手の置き場所

- **麻雀は `app/asterism_games/mahjong*` へ移した** (`lib/` の外)。require の名前は `asterism_games/mahjong` のまま。中のファイルは枠組みを `require "asterism_games"`・`require "asterism_games/game"` のように**名前で**読む (前は `require_relative "../game"`)。誰かが自分のゲームを書くときと同じ形にした。
- `bin/asterism-games` は `app/` があればそれを load path に足して麻雀を読み、次にオセロとボードを読む。麻雀が最初に登録されるので、`game` を書かない卓は今どおり麻雀になる。gem には `app/` が無いので、gem だけの環境では最初のゲームはオセロ (設定ファイルなしなら卓は `othello1`)。
- `Game.register` は今のまま効く (麻雀は `app/` から読まれたときに `mahjong` を登録する)。
- **AI の打ち手は gem (枠組み) に残した**。理由: `ai.rb`・`ai_hub.rb` はゲームに依らない仕組み (ホストのループが手番の判断を API に頼み、費用を数える) で、ホスト (`host.rb`) が読む枠組みの一部。前から `test_framework.rb` の枠組みの一覧に入っていて、麻雀の名前を含まない。Claude の SDK (`anthropic`) は呼ぶときに `require` し、無ければ `LoadError` を拾って「Claude は出さない」とログに書くだけなので、gem の依存に入れなくても動く。Jev は `net/http` だけ。gem で Claude を出したい人は `gem install anthropic` (README に書いた)。image は Gemfile の `anthropic` を持つので今どおり。

### 2.4 依存の版

- gemspec: `asterism ~> 0.7, >= 0.7.1` だけ。**0.7.1 以上を選んだ**。ホストは 0.7.1 の後の機能を `respond_to?` / `defined?` で見てから使う形になっている (`net.status` が無ければセッションで @status を手で出す、`on_app_join` が無ければ liveliness を見る、`Asterism::DOC_MAX` が無ければ meta に doc を付けない)。gem を 0.7.1 に入れて動かした結果は 3 章。
- riichi_engine と anthropic は gem の依存に入れず、Gemfile に残した。Gemfile は image と開発のためのもの (asterism は `0.7.1` に固定のまま、`gemspec` 行は使っていない: Dockerfile の bundle の段が Gemfile と Gemfile.lock だけを先に写して cache を効かせる形を崩さないため)。

### 2.5 そのほか足したもの

- `bin/asterism-games --require FILE` (繰り返し可): 自分のゲームのファイルを、ほかのゲームの後に読む。gem だけで「自分のゲームのホスト」を起こす道。README の「Writing a game」と「The gem and the image」に書いた。
- `rake test:gem`: `lib/` だけを load path に置き (`app/` を置かない)、`ASTERISM_GAMES_TEST_GEM=1` で麻雀を読まずに `test_framework.rb`・`test_othello.rb`・`test_board.rb`・`test_gem.rb` を回す。`test_helper.rb` はこの環境変数で麻雀の require と `MahjongTestHelper` の定数を外す。オセロの試験が使っていた `MahjongTestHelper::CALL_KEY` は、ゲームに依らない `GamesTestHelper` に移した。
- `test/test_gem.rb`: gemspec の中身 (麻雀・`app/`・`deploy/`・`config/`・`test/`・鍵らしい名前が無い、`lib/` が全部入っている、コマンド 2 つ、版が `VERSION`、実行時の依存は asterism だけで 0.7.1 を満たす)。`rake test:gem` のときは、登録されたゲームがオセロとボードだけで、riichi・mahjong・anthropic が読まれていないことも見る。
- `test_framework.rb`: `lib/` に mahjong の名のファイルが無いこと、オセロとボードが枠組みのほかを require しないことを足した。

## 3. gem だけで動かした結果

`gem build` → 一時的な `GEM_HOME` (scratchpad の下) に `gem install` → 環境変数を空にして (`env -i`、Bundler なし) その bin を使った。ルータは平文の zenohd (`eclipse/zenoh:1.10.1`、container `gs7-zenohd`、終わって止めた)。

1. `gem build asterism-games.gemspec`: 警告なし。`Successfully built RubyGem / Version: 0.12.0`。
2. `gem install`: msgpack 1.8.5・asterism-zenoh 0.4.1 (C 拡張を組む)・asterism 0.7.1・asterism-games 0.12.0 の 4 つが入った (riichi_engine・anthropic は入らない)。
3. `asterism-games --config games.yml` (オセロ `othello1` とボード `board1` の 8 行の設定): `games: gs7games/games up (othello1, board1) via tcp/127.0.0.1:17747`。
4. `othello-tty --commands "sit,cpu,start,auto" --until-over`: 席に着き CPU と最後まで打って `over: CPU (white) wins, 10-54` で終わった。ホストのログも `game started` → `game over`。
5. ボード: 入れた asterism で `join` → `line` → `view` / `table` を呼び、線 1 本 (`{"id"=>1}`、`n=1`) が載った。
6. `--require`: 枠組みの試験の小さなゲーム (`test/framework/tally.rb` を写したもの) を `--require ./my_tally.rb` で読ませ、`tally1` と `othello1` (game を書かない卓 → gem では最初のゲームのオセロ) で `up (tally1, othello1)`、INT で `stopped (state saved)`。

## 4. CI の形

- `.github/workflows/image.yml`: console の `image.yml` と同じ作り。タグ `v*` の push で、タグと `VERSION` が合うかを見てから `deploy/vps/Dockerfile` を amd64・arm64 で作り、`ghcr.io/ruby-asterism/asterism-games:<版>` と `:<major>.<minor>`・`:latest` を push する。手で回す (`workflow_dispatch`) と作るだけで push しない。build-arg は `GAMES_VERSION`。
- deploy/vps:
  - `docker-compose.yml` の image を `${GAMES_IMAGE:-ghcr.io/ruby-asterism/asterism-games}:${GAMES_VERSION:-0.12.0}` にし、`build:` を外した。
  - `docker-compose.build.yml` (新): 手元で作る道 (console の同名ファイルと同じ形)。
  - `update.sh`: console と同じく**版を指定**する形に (`./update.sh 0.12.0`)。タグ `v<版>` を checkout (無ければ fetch) → `docker compose pull games` (`--build` なら build の重ね書きで作る) → (`--when-idle`) 誰も座っていなくなるまで待つ → 保存した卓の写し → `.env` の `GAMES_VERSION` → `up -d` → 新しい image で status が答えるかを確かめる。`GAMES_REF` はもう書かない。卓の写しに常設の卓 (`/state/permanent`) も入れた (前は部屋の卓だけ)。
  - `env.example`: `GAMES_IMAGE=ghcr.io/ruby-asterism/asterism-games`・`GAMES_VERSION=0.12.0`。
  - Dockerfile は説明と label (`org.opencontainers.image.source` を足す) だけ変えた。`.dockerignore` に `.github`・`*.gem`。
- docs/install.md: 「4. Pull and start」(ghcr から取る)、「Building the image yourself」(手元で作る)、「8. Updating」を版の指定に、「From 0.11 to 0.12.0」(VPS の `.env` の `GAMES_IMAGE` を直す、古い update.sh は pull できないので新しいものをタグから 1 回だけ取る)。

## 5. 受け入れ条件ごとの結果

| # | 条件 | 結果 |
|---|---|---|
| 1 | `MJ_GAMES=8 bundle exec rake test` が通る。gem の部分だけの試験も通る | 通った。全体 197 runs, 0 failures, 0 errors, 3 skips (ルータが要る 2 つと、gem だけのときの 1 つ)。`bundle exec rake test:gem` 35 runs, 1332 assertions, 0 failures, 1 skip (麻雀の登録の試験) |
| 2 | `gem build` が警告なし、中身に麻雀・deploy・鍵らしいもの無し、一時的な GEM_HOME でオセロとボードのホストが平文のルータで起き、othello-tty で打てる | 通った (2.1 の一覧、3 章) |
| 3 | 手元で同じ docker build が通る、workflow の書式の検査 | `docker build -f deploy/vps/Dockerfile --build-arg GAMES_VERSION=0.12.0 .` が通った (image の中で麻雀・オセロ・ボードが登録され、版 0.12.0)。actionlint 1.7.7 (docker の `rhysd/actionlint`、中の shellcheck で run の部分も検査) で指摘なし。update.sh は shellcheck で警告 1 つ (`cd` の失敗) を直した。残る info は前からの書き方 (`$GIT` を引用符なしで使う: `sudo git` を許すためわざと) |
| 4 | compose で ghcr の名前で起こせる形と、手元で作る道の両方が healthy まで | 両方 healthy。(a) 作った image に `ghcr.io/ruby-asterism/asterism-games:0.12.0-gs7` を付け、`GAMES_VERSION=0.12.0-gs7` だけの env で `docker compose up -d` (build なし) → healthy、status は `asterism-games 0.12.0 ... idle`、部屋の卓に mahjong1・othello1・board1。(b) `GAMES_IMAGE=asterism-games-gs7` で `-f docker-compose.yml -f docker-compose.build.yml build` → `up -d` → healthy、image `asterism-games-gs7:0.12.0`。console の volume と network の代わりに `gs7test_rooms`・`gs7test_router` を作り、project 名 `gs7-games` で動かした。`update.sh --dry-run` (pull と `--build` の両方) の手順の表示も確かめた |

後片付け: 作った container・volume (`gs7-games_state`・`gs7test_rooms`)・network (`gs7test_router`)・image (`*gs7*`、検査に取った `rhysd/actionlint:1.7.7`・`koalaman/shellcheck:v0.10.0`) を名前で消した。前からある image (`asterism-games:<commit>` など) と、名前に `api` を含む container・image、`/home/kishima/fmrb/wt/` の worktree には触っていない。prune はしていない。

## 6. 残ったこと (ユーザがすること)

1. **gem の公開**: asterism-games の main で `gem build asterism-games.gemspec` → `gem push asterism-games-0.12.0.gem` (gemspec に `rubygems_mfa_required` を付けた)。名前 `asterism-games` が rubygems で空いているかは公開の前に確かめる (今回は見ていない)。
2. **タグ**: `v0.12.0` を打って push すると CI が image を作り ghcr に出す。タグと `VERSION` が違うと CI が止まる。
3. **ghcr の公開設定**: 組織の package は最初の push で private になる。`ruby-asterism/asterism-games` の package を public にし、リポジトリとつなぐ (console のときと同じ手順)。
4. **VPS の切り替え**: VPS の `.env` は `GAMES_IMAGE=asterism-games` のはず。docs/install.md「From 0.11 to 0.12.0」のとおり、`GAMES_IMAGE` を ghcr の名前に直し、新しい `update.sh` をタグから取って `0.12.0` で更新する (今回 VPS には触っていない)。
5. push (asterism-games の main は origin より 7 コミット先)。

## 7. 気づいたこと

- 本物の API の確かめ (`/home/kishima/fmrb/wt/games-api`) は `lib/asterism_games/mahjong` の古い配置で動いている。そちらの worktree を main に合わせるときは、麻雀の場所が `app/` に変わったことに注意 (bin から起こす限りは何もしなくてよい。`ruby -Ilib -e 'require "asterism_games/mahjong"'` のように直接読む道は `-Iapp` が要る)。
- 設定ファイルなしのときの卓は「最初のゲームの卓」のまま変えていない。gem だけの人にオセロとボードの両方を出すには設定が要る (README に 8 行の例)。「登録された全ゲームの最初の卓」を既定にする案もあるが、リポジトリの既定 (mahjong1・mahjong2) が変わるので入れなかった。
