# UP8 報告: ゲームホスト・console・fmruby-core を asterism 0.8.0 に上げた

> 状態: 完了 (検収待ち) | 更新: 2026-10-19 | instruction_up8.md の結果。asterism-games 0.16.3・asterism-console 0.3.10 は rubygems の asterism 0.8.0 で動き、fmruby-core は ASTERISM_PIN を 94c3322 にして sim・ブラウザ版・TAB5 のビルドが通った。内蔵 RAM の増分は 0。止まる条件には当たっていない

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `93a25a2` games 0.16.3 | `Gemfile` の固定 0.7.1 → 0.8.0、`Gemfile.lock` (asterism 0.8.0、asterism-zenoh 0.4.1 のまま)、CHANGELOG 0.16.3、VERSION、docs/install.md「From 0.16.2 to 0.16.3」、README と `Host::TablesStatus` のコメントの「0.7.1 に固定」の記述 |
| asterism-console | main | `05a69c1` console 0.3.10 | `Gemfile` の rubygems の指定 `~> 0.7.1` → `~> 0.8` とコメントの版、`Gemfile.lock` (隣の checkout の asterism 0.8.0)、CHANGELOG 0.3.10、VERSION、`deploy/vps/docker-compose.yml` の既定と `env.example` の `CONSOLE_VERSION`、docs/install.md「From 0.3.9 to 0.3.10」、README の rubygems の行、`Bridge::Statuses` のコメント |
| fmruby-core | develop | この報告と同じコミット | `lib/add/ASTERISM_PIN` を `94c3322a74b21b431d211aa12c956ade25e38df1` に、この報告と画面 (`report/up8/*.png`)、指示書 |

push・タグはしていない。コミットの前に 3 つの差分から実在の IP・メールアドレス・鍵・token・ホスト名を検査して 0 件だった。

## 2. games の gemspec: 今のまま (`~> 0.7, >= 0.7.1`)

`~> 0.8` にせず、今の「あれば使う」を保った。理由:

- ホストは 0.7.1 より後の機能を `respond_to?` / `defined?(::Asterism::DOC_MAX)` で見分けて使う作り (`net.status`、`net.on_app_join` / `on_app_leave`、meta の `doc:`)。0.8.0 では 3 つとも gem の道に入り、0.7.1 では手の道 (`Host::TablesStatus` など) に落ちる。gem の利用者に 0.8 を強いる理由が無い。
- image と手元の実行は `Gemfile` の固定 (0.8.0) で決まるので、gemspec を上げても動くものは何も変わらない。
- `~> 0.7` は 0.8.0 を含む (1.0 未満)。`test/test_gem.rb` も 0.7.1 を満たすことを確認している。
- 手の道を消す (コードを減らす) のは別の判断で、この段では触っていない (1 章に無い)。

## 3. 試験

| | 結果 |
|---|---|
| games `MJ_GAMES=4 bundle exec rake test` (asterism 0.8.0) | **261 runs / 33943 assertions、0 failures、0 errors、3 skips** (68.9 s) |
| console `bin/rails test` | **491 runs / 5054 assertions、0 failures、0 errors、0 skips** |
| console `bin/rubocop` | 262 files、no offenses |
| fmruby-core `rake test` | fmrb_ui・services・assoc・WAV・tts・secret_path (159)・麻雀の画面のはみ出し (234 drawings、0 failed、426x240 の warning 51 は従来どおり)・picoruby-ti (test_call_context)・BASIC golden 103 passed・MicroPython smoke、すべて合格 |
| fmruby-core `rake asterism:test` / `asterism:config_test` (追加で) | 2775 checks 0 failed / all passed |

## 4. games: CLI で @status・メソッドの説明・アプリの出入りを確認

手元の zenohd (sim の `fmruby_zenohd`、tcp 7447) に、作業コピーのゲームホスト (`bundle exec bin/asterism-games`、`config/example.yml` の写しで state_dir だけ scratchpad) をつなぎ、asterism の 0.8.0 の checkout の CLI (`ruby -Ilib exe/asterism`) で見た。ホストの bundle の asterism は `Gem.loaded_specs` で 0.8.0、`Asterism::DOC_MAX` = 2048。

`asterism tree`:

```
games  (cruby) "asterism-games"
  games  status {"mahjong1":{"st":"wait","members":0,"humans":0,"cpus":0,"ais":0,"away":0},"mahjong2":{...},"othello1":{...},"board1":{"st":"draw",...,"strokes":0}}
    mahjong1  join(name) mine() sit(token, seat) stand(token) cpu(token, seat, on) start(token) rule(token, item, value) view(token, assist) act(token, sv, type, tiles) table() describe() look(token) choose(token, sv, n) points(token) records(from, *) record(id, round, part, *) stats(days, game, scope, from, *) leave(token)
    othello1  join(name) mine() sit(token, seat) ... move(token, n, square) resign(token) view(token) table() leave(token)
linux  (family_mruby/sim)
  demo
    info  status(0) relay(1) boom(0) echo(1) profile(2)
```

`asterism objects 'games/games/othello1'` は引数の名前に加えて説明を出した (抜粋):

```
games/games/othello1  join(name) mine() ... move(token, n, square) ...
  An othello table (two seats). join, sit, then view / move in a loop.
    move(token, n, square)  Plays square ("d3": column a-h, row 1-8) as the seat to move. n is the view's "n" ...
```

`asterism watch` (sim の麻雀アプリで卓に入り、座り、アプリを kill した):

```
19:36:23.947 + app linux/game
19:36:30.498 ~ status games/games {"mahjong1":{"st":"wait","members":1,"humans":0,...
19:36:36.136 ~ status games/games {"mahjong1":{"st":"wait","members":1,"humans":1,...
19:36:36.297 - app linux/game
19:36:36.301 ~ status games/games {"mahjong1":{...,"humans":1,...,"away":1},...
```

ホストのログ: `19:36:36 mahjong: mahjong1: linux left the network: away`。卓のアプリ (`linux/game`) が去ってから 4 ms で away が @status に出た。@status は gem の `net.status` の道 (変わったときだけ put)、away は `net.on_app_leave` の道で出ている (0.8.0 では両方とも `respond_to?` が真)。

## 5. console: deploy/local の image と bridge

- `rehearse prepare → build`: image `asterism-console-rehearsal:0.3.10` の `bundle install` が **rubygems の asterism 0.8.0** (と asterism-zenoh 0.4.1) を取った (build のログ `Fetching asterism 0.8.0`)。container の中の `bundle list` も 0.8.0。
- `bootstrap → up`: bridge が `tls/zenohd:7448` に console/console で入り、図の v1 (5 nodes) を出した。
- ゲームホスト: docs/rooms.md どおりの役割 `games-host` (`asterism/${node}/*/@status` の行あり) と行 `games` を console の container の `bin/rails runner` で作り、`Relay::Issuer#issue_local` で `.tar` を作った (「Save all (.tar)」と同じ 4 ファイル)。`relay:generate` → zenohd を再起動。games の作業コピー (0.16.3、asterism 0.8.0) をその `.tar` で手元から起こした (`asterism.json` のルータだけ、この PC に公開された `tls/localhost:17448` に書き換えた)。`games/games up ... via credentials`。
- 図に @status: `GraphState.current.snapshot` (ページが描く図そのもの) の v8 に、app `games/games` の data `status` として `{"mahjong1":{"st":"wait","members":0,...},"mahjong2":{...},"othello1":{...},"board1":{"st":"draw",...,"strokes":0}}` が入った。図のノード (console・games・4 卓・router・session) と線は従来どおり。ブラウザでの画面の撮影 (管理者のログインと TOTP) はしていない。

## 6. fmruby-core

### 6.1 取り込み

`ASTERISM_PIN` を 94c3322 に → `rake asterism:setup` (vendor/asterism を 94c3322 に) → `rake setup` → `rake clean`。picoruby の `mrbgems/picoruby-asterism/mrblib/` は vendor の `mrblib/` と一致 (diff 0)。前の pin (2d9ecd4) からの端末の層の差分は `mrblib/asterism.rb` (+204/-14) と新しい `mrblib/status.rb` (198 行) だけ。ELF とブラウザ版の wasm に `on_app_join`・`status_of` の名前が入っていることも見た。

### 6.2 Linux sim (標準構成、TAB5 426x240)

`rake clean_all` → `rake build:linux` (`file build/fmruby-core.elf` は x86-64)、graphics-audio も `rake build:linux`。sim_up → 音を消して確認。

| 確認 | 結果 | 画面 |
|---|---|---|
| エディタを起動して 1 打鍵 | `a` が入り、ステータス行が `Ln 1, Col 2 *` | `report/up8/sim_editor_key.png` |
| `/app/test/asterism_demo.app.rb` | `linux connected (tcp/zenohd:7447, 4 ms)`、`Joined: games`。CLI から `call linux/demo/info status` → `{"name"=>"linux", ...}`、`ping linux` 5 of 5 (平均 9.1 ms) | `sim_asterism_demo.png` |
| 麻雀 (`/app/game/mahjong/mahjong.app.rb`) | 卓の一覧に `games/mahjong1`・`games/mahjong2`、Enter で待ち合わせ (「ゲームホスト 0.16.3」)、席 1 に座れた | `sim_mahjong_lobby.png` |

気づいたこと (0.8.0 とは関係ない): 全画面の麻雀を開いている間、ほかのアプリ (エディタ・asterism_demo・デスクトップ) は SUSPENDED になり、asterism_demo のアプリ (`linux/demo`) は約 9 秒後に網から消え、麻雀を閉じて RUNNING に戻ると入り直した (watch の `- app linux/demo` → `+ app linux/demo`、core のログの `State: RUNNING -> SUSPENDED`)。止まっている間は zenoh の更新が回らないための、全画面の作りの挙動と見ている。

### 6.3 ブラウザ版

`source ~/emsdk/emsdk_env.sh` → `rake wasm:mruby` (clean_all が wasm の libmruby を消すため) → `rake spinel:gen` → `rake wasm:mrb` → `rake wasm:web` → `rake wasm:dist`。zenohd を WebSocket つき (`docker/asterism/docker-compose.zenoh-ws.yml`、ws 7449) で起こし、同じゲームホストをつないで web_up (`app=/app/game/mahjong/mahjong.app.rb`)。卓の一覧に `games/mahjong1`・`games/mahjong2` が出て、Enter で mahjong1 の待ち合わせに入れた (観戦: tester、「ゲームホスト 0.16.3」)。`report/up8/web_mahjong_lobby.png`。

### 6.4 ESP32 (TAB5) と内蔵 RAM・アプリの領域

前: 上げる前の木 (pin 2d9ecd4) で、そのとき残っていた TAB5 の build/ に `rake build:esp32` (差分ビルド) を通して測った。後: pin 94c3322 で `rake clean_all` → `rake build:esp32`。どちらも `idf.py size` と `idf.py size-components`。

| | 前 (2d9ecd4) | 後 (94c3322) | 差 |
|---|---|---|---|
| DIRAM (内蔵 RAM) 使用 | 129690 B (.text 85210 / .bss 29692 / .data 14788) | **129690 B** (同じ内訳) | **0** |
| HP core RAM / LP RAM | 892 / 56 B | 892 / 56 B | 0 |
| External RAM (.bss) | 18148796 B | 18148796 B | 0 |
| Flash (.text + .rodata) | 6551066 B | 6559418 B | +8352 |
| fmruby-core.bin | 0x6580d0 | 0x65a170 | +0x20a0 (8352 B) |
| アプリの領域 (0x700000) の空き | 0xa7f30 (687920 B, 9%) | **0xa5e90 (679568 B, 9%)** | -8352 |

size-components で差が出たのは 2 つだけ: `libmruby.a` の Flash +7498 (.rodata +7496、.text +2。picoruby-asterism の mrblib のバイトコード)、`libesp_app_format.a` の .rodata +772。どちらも Flash だけで、DIRAM・External RAM の差はどの archive にも無い。

TAB5 の esp32 ビルド (pin 94c3322) を build/ に残してある。焼いていない。

## 7. VPS の更新と Tab5 の焼き直しに要ること

- **games**: タグ `v0.16.3` を push して CI が image `ghcr.io/ruby-asterism/asterism-games:0.16.3` を作ったあと、VPS の deploy/vps で `./update.sh 0.16.3` (または `--when-idle 0.16.3`)。`.env` の変更なし。image が rubygems の asterism 0.8.0 を取る (Gemfile の固定)。
- **console**: タグ `v0.3.10` → image `ghcr.io/ruby-asterism/asterism-console:0.3.10` のあと `./update.sh 0.3.10`。移行・`.env`・nginx・ルータの設定の変更なし。games とどちらが先でもよい (wire protocol 1 のまま)。
- **Tab5**: build/ の TAB5 ビルドを全体 flash (`flash` ツール)。storage の中身は今回変えていない (端末の層は firmware の libmruby に入る) が、`rake flash` は /home を消す点は従来どおり。焼いたあとシリアルで `Guru|abort` 0 件、`M1|` と周期ダンプの `IRAM free:` が前の焼き込みと同じこと、麻雀かオセロの卓の一覧に入れることを見る。内蔵 RAM の静的な使用は 6.4 のとおり同じ。

## 8. 後片付け

sim は音を戻してから sim_down。web_down (開発サーバとブラウザ)。手元のゲームホスト (3 回: 素の tcp・console の網・ブラウザ版) と CLI の watch を pid で止めた。WebSocket つきで起こした `fmruby_zenohd` を消した。console の deploy/local は `VOLUMES=1 rehearse down` で volume ごと消し、`deploy/local/.env` と `certs/` を消し、作った image `asterism-console-rehearsal:0.3.10` を消した。scratchpad の `.tar`・管理者のパスワード・runner の台本・卓の保存を消した。前からある image (`asterism-console-rehearsal:0.3.2`、`eclipse/zenoh:1.10.1`、nginx ほか) は残した。docker の prune はしていない。

最初に `bin/asterism-games --help` を試したところ、`--help` を取らずにホストとして起動した (asterism-games の作業コピーの `var/state` の卓を読み込み、7447 へのつなぎ直しを続けた。すぐ止めた)。`var/state/records.sqlite3` の更新時刻だけが変わった (gitignore、中身の変更は見ていない)。牌のシート (`tiles_hand.bmp` / `tiles_river.bmp`)、asterism / asterism-zenoh / picoruby-asterism-zenoh、VPS、GitHub の設定、Tab5 には触っていない。fmruby-graphics-audio の作業コピーの前からの変更 (`flash/etc/display_conf_linux.txt`) は触っていない (linux ビルドだけ通した)。
