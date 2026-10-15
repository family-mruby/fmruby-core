# MJ1 報告: 麻雀の Step 1 (ゲームホストと CPU 3 人、牌の画像の卓)

> 状態: 完了 (検収待ち) | 更新: 2026-10-15 | instruction_mj1.md の実装と確認の記録。新しいリポジトリ asterism-games にゲームホスト (CRuby、riichi_engine 0.1.0) を作り、fmruby-core に卓の画面 (flash/app/game/mahjong/、640x360、牌は BMP のシート) と牌の生成スクリプト (tools/mahjong_tilegen.rb) を置いた。手元の zenohd とブラウザ版で、自分 1 人と CPU 3 人の半荘を注入の操作で最後まで打ち、途中でゲームホストを止めて起動し直しても同じ場面から続いた。1 通の最大は 516 B。riichi_engine 0.1.0 には直しの要る点が 10 あり、ゲームホストの側で回避した (6 章)

## 1. 作ったもの

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism-games (新規、手元だけ) | main | ゲームホスト、麻雀の卓、試験、README |
| fmruby-core | feature/mj1-mahjong (develop から) | 卓の画面 `flash/app/game/mahjong/`、`tools/mahjong_tilegen.rb`、この報告、game_rooms_comparison.md・plan.md |
| family-mruby (親) | develop | `.gitignore` に `asterism-games/` (ほかの asterism-* と同じ形) |

- asterism・asterism-zenoh・picoruby-asterism-zenoh・asterism-console は変えていない。
- asterism-games の Gemfile は rubygems の版 (asterism 0.7.1、riichi_engine 0.1.0) を使う。手元の asterism の作業コピーは 0.7.1 の公開のあとのコミット (端末の credentials を純 Ruby で読む、2d9ecd4) が入っていて rubygems の 0.7.1 と `lib/asterism.rb`・`mrblib/asterism.rb`・`lib/asterism/cruby/credentials.rb` が違う。ゲームホストはその部分を使わないので、公開された版に合わせた。
- push はしていない。端末には焼いていない。

## 2. ゲームホスト (asterism-games)

### 2.1 作り

- 1 つのプロセスが Asterism のノード 1 つ (既定のノード ID `games`、アプリ `games`)。gem の接続は 1 プロセスに 1 本なので、ゲームはすべて同じアプリの下に、卓ごとのオブジェクトとして出す (`games/games/mahjong1`)。ゲームの種類は `Host::GAMES` に足す (麻雀以外もここに入る)。
- 主ループは portable API (`Asterism.poll`) を 10 ms ごとに回し、同じループで卓の時間を進める (CPU の手、待ち時間、局の結果の表示時間)。呼び出しの受け付けも同じスレッドなので、卓に鍵は要らない。
- 設定は YAML (`config/example.yml`) と環境変数 (`ASTERISM_GAMES_ROUTER`・`MAHJONG_TURN_SECONDS` など。README "Settings")。
- 接続が切れたら 3 秒ごとにつなぎ直す。ルータの起動し直しは gem のつなぎ直しで戻る。

### 2.2 卓の呼び出し

| メソッド | 引数 | 返す値 |
|---|---|---|
| `sit` | 名前 | `{"seat", "token"}` か `{"error" => "full"}` |
| `view` | 合言葉 | その席の見え方 (2.3) か `{"error" => "token"}` |
| `act` | 合言葉, sv, 種類, 牌 | `"ok"` か理由 (`token`・`stale`・`not offered`・`not playing`) |
| `table` | なし | 全員に見せてよい見え方 (手牌なし) |
| `leave` | 合言葉 | 席を CPU に戻す |

- 座った端末は呼び出しのキーの最後の塊 (`Asterism.current_call.from`) で分かる。同じノードがもう一度 `sit` すると同じ席と合言葉が返る (アプリを開き直した端末が元の席に戻れる)。最初の人が座ったときに半荘が始まり、席はくじで決める。
- 合言葉は `SecureRandom.hex(16)` (32 桁)。手牌に触れる呼び出し (`view`・`act`) は合言葉が無い・違うと断る。
- `act` は、そのときエンジンが出すその席の一覧 (見え方の `a`) にあるものだけ受ける。`sv` (状態の版) が今のものでなければ `stale` で断る (古い画面を見て押したものを通さない)。
- 変わるたびに、全員の見え方を `asterism/<ノード>/<アプリ>/<卓>/state` へ put する (変化が無くても 5 秒ごと)。卓の画面は、そこで新しい版を見たら自分の見え方を取りに行く。手牌は put に載せない。

### 2.3 見え方の符号化

MessagePack の Hash で、牌は 1 バイトの番号を並べたバイナリ (bin)。番号は牌のシートと同じ並び (3 章)。キーは 1-3 文字 (`r` 局の情報、`sc` 点数、`kw` 河 4 本、`fu` 鳴き 4 組、`dora`、`dl` 待っている席と残り ms、`res` 局の結果、`fin` 終局の順位、席の見え方だけが `me`・`h` 手牌・`dt` 自摸の牌・`a` 今できること)。河の 1 バイトは `0x40` がリーチの宣言牌、`0x80` が鳴かれた牌。全体は asterism-games の README "Views"。

### 2.4 時間と書き出し

- 自摸はゲームホストが自動で行う (エンジンの一覧に自摸は無く、エンジンの文書も自動でよいとしている)。
- 打牌の待ち時間 (既定 20 秒) を過ぎると自摸の牌を打つ (鳴いたあとは手の最後の牌)。鳴きの待ち時間 (既定 8 秒) を過ぎるとパス。CPU は 1 手ごとに 700 ms 待つ。局の結果は 8 秒出して次の局へ (画面の「次へ」で早められる)。
- 変わるたびに卓の状態を `var/state/<卓>.json` に書く (一時ファイルに書いて rename)。中身は局の状態 (riichi_engine の `RoundState#to_json`)、席と合言葉、半荘の点数、鳴きの宣言の途中、待ち時間の残り。起動し直すとそこから続ける。
- 待ち時間は「残り」で書く。SIGTERM で止めたときはその時点の残りを書き、kill -9 のときは最後に変わった時点の残りから数え直す。

## 3. 牌の画像

- ユーザの `mahjong_tilegen.rb` (CC0) を `tools/mahjong_tilegen.rb` に写した。絵柄の部分は変えていない (元のスクリプトとの差は先頭の説明と、末尾に足した `--sheets` の出力だけ)。
- `ruby tools/mahjong_tilegen.rb --sheets DIR` で、Family mruby が読むシートを書く。

| ファイル | 大きさ | 中身 | バイト |
|---|---|---|---|
| `tiles_hand_0.bmp` | 210x224 | 手牌 42x56 の萬子・筒子 (5 列 x 4 行) | 48,566 |
| `tiles_hand_1.bmp` | 210x224 | 手牌の索子・字牌・裏 | 48,566 |
| `tiles_river.bmp` | 240x128 | 河 24x32 の全部 (10 列 x 4 行) | 31,798 |

- 並び: 牌の番号 n は 0..8 が 1m..9m、9 が 0m (赤五)、10..19 が筒子 (19 が 0p)、20..29 が索子 (29 が 0s)、30..36 が 1z..7z (東南西北白發中)、37 が裏。すき間の無い格子で、手牌は シート n / 20、k = n % 20、src_x = (k % 5) * 42、src_y = (k / 5) * 56。河は src_x = (n % 10) * 24、src_y = (n / 10) * 32。表はスクリプトの先頭と卓のアプリの先頭にある。
- **手牌のシートを 2 枚に分けた**: BMP を読む `fmrb_bmp332_parse` (picoruby-fmrb-bmp332) は幅・高さとも 256 px までしか受けない。手牌 38 枚を 1 枚に並べると 256 を超えるので、萬子と筒子 / 索子と字牌の 2 枚にした (指示の「大きさごとに 1 枚」とは違う。1 枚にするなら読み込みの上限を上げる直しが要る)。
- 形式: 8 bit の無圧縮 BMP。画素の値をそのまま RGB332 の色として読む (パレットは見ない)。パレットには RGB332 を戻した色を入れたので、画素のエディタで開くと端末と同じ色に見える。色は生成のときに RGB332 の一番近い値に丸める。
- 透明: 角の丸めと空いたマスは RGB332 `0xE3` (マゼンタ) で塗り、`SpriteImage.new(..., transparent_color: 0xE3, use_transparent: true)` で抜く。どの牌の色も 0xE3 に丸まらないことを生成のときに確かめている (丸まれば止まる)。テーマの 0x01 は使っていない。
- **シートを直したあとの正はシート**。`--sheets` は同じ名前のシートがあると書かずに止まる (`--force` で上書き)。
- 確認用に `sheets_preview.png` (3 倍) と、元の 1 枚ずつの PNG (`ruby tools/mahjong_tilegen.rb OUT_DIR`) も出せる。
- 絵柄について (直していない、案だけ): RGB332 に丸めると裏の 2 色 (`#e0a33c`・`#c48a2c`) と側面の 2 色が近い値になり、裏の内側の枠がほとんど見えない。直すなら裏の濃い方をもう 1 段暗くする。

### 3.1 BMP の読み込みと色 (対象ごと)

| 対象 | 描く側 | シートの持ち方 | 透明の比べ方 | 結果 |
|---|---|---|---|---|
| sim | graphics-audio (`graphics_handler.cpp`) | RGB332 の 8 bit のスプライト | 画素の値と 0xE3 を比べる | 画面の色はパレットの色と同じ (縁 72,72,85)。角は卓の緑 |
| ブラウザ版 | display_local (`display_local_task.cpp`、Tab5・P4-Nano と同じコード) | RGB565 のスプライトに RGB332 から広げて描く | 0xE3 を同じ変換で RGB565 にして比べる | 角は卓の緑。色は RGB332 を RGB565 経由で戻した値 (縁 74,72,82) で、パレットの色と 1-3 の差 |
| Tab5・P4-Nano | display_local (ブラウザ版と同じコード) | 同上 | 同上 | 実機では見ていない (コードがブラウザ版と同じ) |

- どちらの描き方も RGB332 から先に色を作るので、牌の色は RGB332 で決まる。RGB332 から RGB565 への広げ方は 1 対 1 なので、0xE3 以外の色が透明になることは無い。
- `draw_tile` は 1 画素ずつ写す (display_local は `readPixelValue` と `drawPixel`)。卓を全部描き直すと手牌 14 枚と河・鳴き 90 枚ほどで約 10 万画素。ブラウザ版で打牌から画面までが 62-78 ms (5 章) なので、Step 1 では足りている。
- アプリは `sync_file` でシートを `/cache/app/mahjong/` に写してから読む (rpg_demo と同じ)。sim では最初の 1 回に 1 枚約 2 秒かかった (2 回目からは CRC が同じなら写さない)。

## 4. 卓の画面 (flash/app/game/mahjong/)

- 全画面、`fullscreen_hires = true` で、Modern とブラウザ版では 640x360。`single_instance`。
- 640x360 の配置: 上の帯に局・本場・供託・残り枚数・ドラ表示 (河の大きさの牌) と自分の名前と点数。左右に上家・下家の名前・点数・鳴き・河 (6 枚 x 5 行)、上に対面の名前と鳴きと河 (12 枚 x 3 行)、真ん中の下に自分の河 (12 枚 x 3 行)、その下に操作のボタンと残り秒、いちばん下に手牌 (42x56) と自摸の牌 (少し離す)、右下に自分の鳴き。手番の席の名前は黄色、最後に打たれた牌は黄色の枠、リーチの宣言牌の下に赤い線。鳴かれた牌は河に描かない。
- 局の結果は真ん中に箱で出す (和了: 和了った人・放銃した人・手牌と和了牌・役・符と翻と点・4 人の点の動き。流局: 聴牌の人と点の動き)。終局は順位と点とポイント。
- 操作: 手牌のクリックで打牌 (Enter は選んだ牌か自摸の牌)。ボタンはそのとき出ている一覧だけ: リーチ (R、押してから牌を選ぶ)・ツモ / ロン (T)・ポン (P)・チー (C、組ごと)・カン (K)・パス (Esc)・九種九牌 (N)・次へ (Enter)。
- 文字は英語と日本語 (`FmrbI18n`、システムの `language` に従う)。役の名前はエンジンが日本語で返すので、英語のときも日本語で出る。
- つなぎ直し: othello と同じ形 (接続は機械の設定、切れたら 3 秒ごとにつなぎ直す)。ゲームホストが答えないあいだは「ホストが答えない: 待っています」を出し、3 秒ごとに見え方を取りに行く。戻ったら見え方を取り直す。`view` が合言葉を断ったら (ゲームホストの状態が消えた) 座り直す。
- 卓が満席なら網の次の卓を探す (Step 2 で卓が増えたとき用。今回の sim の確認でも使った)。
- 小さい画面 (sim の 426x240): 手牌を河の大きさの牌で描き、河と鳴きは文字 (`5m`・`東` など) で描く。開発の確認用。
- Retro (`FmrbConst::HW_FAMILY` が modern でない): 卓を出さず「この卓は Modern とブラウザ版で動きます」とだけ出す。ランチャーからは消していない (案は 8 章)。
- Asterism のアプリ名は `game` (G0 の ACL の範囲 `asterism/<ID>/game/**`)。そのため同じ端末でオセロと同時には開けない (2 つ目の接続が「同じ `<node>/<app>` がある」で断られる)。

## 5. 受け入れ条件の結果

| # | 条件 | 結果 |
|---|---|---|
| 1 | 自分 1 人と CPU 3 人の半荘をブラウザ版で最後まで | 打てた (5.1) |
| 2 | 1 通がすべて 4096 B 未満 | 最大 516 B (5.2) |
| 3 | 他人の手牌が入らない・合言葉・一覧に無い操作 | 試験で確認 (5.3) |
| 4 | ゲームホストを止めて起動し直しても続く | 同じ局の同じ場面から続いた (5.4) |
| 5 | sim でも卓が描ける | 描けた (5.5) |
| 6 | `rake test`・wasm のビルド・asterism-games の試験、区画 | 全部通った。足した大きさは 5.6 |

### 5.1 ブラウザ版での半荘

- 構成: 親リポジトリの `docker compose -f docker-compose.yml -f docker/asterism/docker-compose.zenoh-ws.yml up -d zenohd`、ゲームホスト (`MAHJONG_CPU_MS=500`、ほかは既定)、ブラウザ版 (`?app=/app/game/mahjong/mahjong.app.rb`、426x240 のページで卓が全画面の高解像度になり 640x360)。
- 操作は `tools/fmrb_web.rb` のキーの注入 (web_input と同じ口)。どのキーを押すかは、ゲームホストが書き出した状態からその席の一覧を計算して決める小さなスクリプトに任せた (自摸の牌を打つ、ツモ / ロンは必ず、リーチは出たら必ず、ポン / チーは最初の 3 回)。
- 結果: 半荘が南 4 局で終わるまで打てた (13 局: ロン 6、ツモ 4、流局 3)。自分の席の操作は 227 回 (打牌、チー 3 回、リーチ 1 回、ポンを見送ってパス、局の結果の「次へ」、終局)。途中で 1 回ゲームホストを止めて起動し直した (5.4)。終局の画面は順位・点・ポイント (CPU4 51,000 から自分 4,800)。
- 自分の和了はこの半荘では出なかった (和了は CPU の 10 局)。自分のツモの画面は、次の半荘で自分の手番のときにゲームホストを止め、書き出した局の状態の自分の手を和了の形に書き換えて起動し直し、「ツモ T」のボタンが出てクリックで和了の結果 (門前清自摸和・断么九、30 符 2 翻) が出ることを確かめた。
- 画面の写し: `C:\Users\Public\asterism_deploy\screenshots\mj1\` (チーの前後、リーチの前後、和了・流局の結果、終局、ゲームホストを止めたとき、sim)。

### 5.2 1 通の大きさ

| 測ったもの | 最大 |
|---|---|
| 席の見え方 (呼び出しの答え `{"value" => ...}` と、長めの呼び出しのキー 56 B を足したもの)、CPU 4 人の半荘 20 回の全ての手のあと、4 席とも | 516 B |
| 全員の見え方 (put と同じ中身)、同じ | 494 B |
| 手で作った最悪の場合 (河 24 枚 x 4、4 人とも加槓 4 組、32 バイトの名前 x 4、役 14 個、終局の順位) | 908 B |

- Zenoh の見出しを足しても 4096 B から遠い。試験は全部の見え方を 3800 B 未満で確かめている。ゲームホストも、送った答えと put の最大を数えて 1 KB を超えたらログに出す。
- 家の compose の zenohd の `transport.link.rx.max_message_size` は既定の 1 GiB だった (audio.md の未確認の 1 つ)。

### 5.3 見せる範囲と断ること (試験)

- CPU 4 人の半荘 20 回の全ての手のあとに 4 席の見え方を調べ (合わせて 194,632 回)、自分の手牌 (`h` と `dt`) が局の状態の自分の手と一致し、ほかの席の手牌のキーが無く、全員の見え方に手牌が無いことを確かめた。
- 合言葉: 無い (`""`・nil)、違う、大文字に変えたものはどれも `view` が `{"error" => "token"}`、`act` が `"token"`。ルータを通した試験 (`test_network.rb`) でも同じ。
- 一覧に無い操作: 手に無い牌の打牌、出ていないロン・ポン、知らない種類 (99)、牌の番号として読めないバイトは `"not offered"`。古い `sv` は `"stale"`。断ったあと局は進んでいない。

### 5.4 ゲームホストの起動し直し

- ブラウザ版で打っている途中 (東 2 局、自分の手番、状態の版 217) にゲームホストを kill -9 で止め、11 秒後に起動し直した。止めているあいだ、卓は「ホストが答えない: 待っています」を出した。起動し直すと `resumed (play, state 217)` で同じ局・同じ手番・同じ手牌から続き、ブラウザ版は見え方を取り直して元の画面に戻った (待ち時間は最後に書いた残りから数え直し)。
- ブラウザ版のページを読み直した (アプリが起動し直した) ときも、同じノード ID なので元の席と合言葉が返り、同じ画面に戻った。
- 試験 (`test_mahjong_rules.rb`) でも、局の途中で書き出して読み戻した卓の見え方が元と同じで、同じ打牌のあと次の自分の手番まで同じに進むことを確かめた。

### 5.5 sim

- sim (Linux の標準構成、Modern 426x240) で、2 つ目のゲームホスト (ノード ID `games2`) の卓に座り、局の途中まで描けた (小さい画面の配置)。透明の角と牌の色は 3.1 のとおり。
- アプリの VM のプール: sim で 520 KB 前後 (1,536 KB のプールの 34%)。

### 5.6 試験・ビルド・区画

- fmruby-core の `rake test`: 通った (golden 103、MicroPython の smoke)。
- wasm のビルド (`rake wasm:web`): 通った。
- asterism-games の試験: `bundle exec rake test` (半荘 20 回、8 プロセスで約 5 分) と `ASTERISM_TEST_ROUTER=tcp/127.0.0.1:7447` の試験が通った。
- 区画: ファームの区画 (factory) には何も足していない (Ruby のアプリと画像だけ)。storage の区画 (Tab5・P4 は 8 MB) に足したのは、シート 3 枚 128,930 B とアプリ約 31 KB、合わせて約 160 KB (LittleFS の 4 KB の区切りで数えて 164 KB)。今の flash/ と flash_local/ は 4 KB の区切りで数えて約 3.9 MB。初めて開いたときに `/cache/app/mahjong/` にシートの写しがもう 1 組 (約 130 KB) できる。

### 5.7 操作から画面までの時間

- ブラウザ版: 打牌のクリックから、その打牌が入った見え方を描き終えるまで 62-78 ms (卓の画面に「応答」として出している。act の往復、見え方の取り直し、描き直しを含む)。
- CRuby から同じゲームホストを呼んだ往復は 15 ms 前後。

## 6. riichi_engine で気づいたこと

0.1.0 で、ゲームホストの側で回避したもの (`engine.rb` に `engine 0.1.0:` の印。試験のうちエンジンそのものを調べるものは、エンジンが直ると落ちて知らせる):

1. `evaluate_win` がドラをドラ表示牌そのもので数える (表示牌の次の牌でなく)。裏ドラも同じ。→ ドラの牌に置き換えて評価する。
2. 河底撈魚が付かない: ロンの評価のときには局の状態がもう `round_end` で、`is_houtei?` が偽になる。→ 和了ったときの段階に戻して評価する。
3. リーチした席に `available_round_actions` が打牌を出さない (ツモ和了があるときはそれだけ)。そのままでは手番が止まる。→ 自摸の牌の打牌を出す。
4. リーチの一発の資格が、本人の次の打牌で消えない (鳴きと槓でしか消えない)。→ 消す。
5. `Hand#remove` が同じ種類の最初の牌を取る。赤五と五を持って五を切ると赤五が抜けて河には五が出る (赤五が局から消える)。チー・ポンでも違う牌が動く。→ 動かす牌を手の先頭に並べ替えてから当てる。
6. 暗槓・大明槓が、名前の牌 4 枚で記録する (赤五が消える)。→ 手から抜けた牌で記録し直す。
7. 鳴きの優先の確かめが同じ席のほかの選択肢も見るので、ロンもできる席はポンを選べない。→ 選んだものだけを残してから当てる。
8. `resolve_pending_action` の同じ優先の決め方が席の番号順で、打った人からの順 (頭はね) でない。ダブロンはルールの `double_ron` に関係なく無い。→ 打った人からの順で決める (ダブロンは無いまま)。
9. `Wall.build` が seed を `String#to_i(36)` で数にするので、0-9a-z でない文字で止まる。README の例の `"round-001"` と `"round-002"` は同じ山になる。→ seed は 0-9a-z の 24 文字。
10. CPU の打牌 (normal・hard) が、受け入れ枚数の少ない順に並べて先頭を取る。受け入れのいちばん少ない形を残すので、CPU だけの半荘で 9 局中 8 局が流局だった。→ 受け入れの多い方を残す打ち手にした (`cpu_level: engine` で元のまま)。直したあとは半荘 20 回 256 局で、ロン 112・ツモ 78・流局 66。

直していないもの: README の `"tonpuu"` がコードでは `"tonpuusen"` (`"tonpuu"` だと終わらない)。槓で生きている山が減らない。トビが 0 点ちょうどでは起きない (文書は「0 点以下」)。`RiichiEngine.loader.eager_load` が失敗する (`api.rb` が `API`、`version.rb` が `VERSION` を定義していて、Zeitwerk は `Api`・`Version` を待つ)。CPU の 1 手に CRuby で約 50 ms かかる (受け入れの数え方で向聴数の計算を牌ごとに回す)。和了・流局の点の動きは使う側が計算する (文書どおり)。

作者への報告の下書き (英語、Issue にするならこのまま):

> riichi_engine 0.1.0: notes from a host built on it. (1) evaluate_win counts dora on the indicators (state.dora_hyouji), not on the tiles they point to; can_agari? uses wanpai.dora_tiles correctly. Same for uradora. (2) Houtei never applies: the phase is round_end when evaluate_win runs after a ron. (3) available_round_actions offers no discard to a seat in riichi, so its turn cannot continue (the validator accepts any discard). (4) The ippatsu flag of a riichi is not cleared by the player's own next discard. (5) Hand#remove removes the first tile of the same kind, so discarding "5m" from [0m, 5m] removes the red five while the river shows 5m; chi/pon may move the wrong tile likewise. (6) ankan/daiminkan record Array.new(4, tile.to_s), losing a red five. (7) The priority check (highest_priority_action?) also counts the caller's own other options, so a seat that can ron cannot pon. (8) ActionResolver breaks ties by seat number rather than turn order from the discarder; double_ron has no effect. (9) Wall.build uses seed.to_i(36), which stops at the first non-base-36 character: the README's "round-001" and "round-002" produce the same wall. (10) DahaiSelector.select_normal sorts by ukeire ascending and takes the first, keeping the shape with the least acceptance. Also: "tonpuu" (README) vs "tonpuusen" (GameFlow); a kan does not shorten the live wall; tobi triggers below 0 only; Zeitwerk eager loading fails on api.rb / version.rb.

## 7. 見立てと違った点

- 手牌のシートを 1 枚にできなかった (BMP の読み込みが 256 px まで。3 章)。
- ブラウザ版のページを 640x360 にすると (`?w=640&h=360`)、headless のブラウザの写し (web_screenshot) が真っ黒だった。卓だけでなく、デスクトップ・hires_test・rpg_demo でも同じなので、卓とは別の問題 (ページの 640x360 か写しの側)。今回は 426x240 のページで卓が全画面の高解像度 (640x360) になる形で確かめた。調べていない。
- CPU の打ち手がそのままでは流局ばかりで、受け入れ条件の「和了」が出にくかった (6 章の 10)。
- エンジンの CPU が遅い (1 手 50 ms)。対局には十分だが、半荘 20 回の試験は 1 本で 20 分を超えるので、プロセスを分けて回している。

## 8. Step 2 への申し送り

- 人間 4 人: 今は `humans: 1`。鳴きの割り込みは「全員の宣言がそろうまで待つ」作りで、人間が何人いても動くが、待ち時間の扱い (先に宣言した人を待たせる長さ、優先の高い人だけ待つ) は決めていない。ダブロンを入れるならエンジンの外で組む (6 章の 8)。
- 観戦: put の見え方 (手牌なし) がそのまま使える。数手遅らせるならゲームホストの put を遅らせる。
- `sit` の席の決め方は「呼んだノード ID」で、ID は名乗り (ACL で縛らない限り偽れる)。部屋 (G2) では合言葉と組み合わせているので Step 1 ではこれで十分だが、部屋の中で ID を偽ると他人の席に入れる。座るときにも部屋の鍵から取った値を添える形が要るかもしれない。
- Retro での扱い (案): (a) 今のまま「Modern とブラウザ版で動きます」と出す、(b) `config/storage_exclude_<Retro の各ボード>.txt` に `app/game/mahjong` を書いて Retro のイメージから外す (シート 130 KB も消える)。(b) を勧める。
- 手牌のシートを 1 枚にするなら、picoruby-fmrb-bmp332 の 256 px の上限 (行の入れ替えの一時領域が 256 バイトの配列) を上げる。
- 卓の画面の仕上げ (Step 3): 局の結果の「30符 2翻」で数字と漢字が重なって見える、上家・下家の鳴きが 3 組を超えると河に重なる、横向きの牌 (リーチの宣言牌・鳴いた牌) は絵が無いので、今は赤い線と並びで表している。役の名前の英語、局の結果の箱の配置、鳴きの牌の選び方 (赤五を残すかどうか) もここ。
- ゲームホストを docker の image にして update.sh で更新する形、console の図への見張りは未着手。
- 端末 (Tab5・P4-Nano) では動かしていない。焼かずに `tab5_fs` で `/app/game/mahjong/` を送れば試せる (ルータは `/home/asterism/config.toml`)。
