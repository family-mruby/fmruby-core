# R4a 報告: 卓のクライアントの共通部分 FmrbAsterism::TableClient と、オセロの卓・お絵かきボードの載せ替え

> 状態: 完了 | 更新: 2026-10-25 | instruction_r4a.md の記録。fmrb-asterism の gem に `FmrbAsterism::TableClient` を作り (Link の上に乗る)、オセロの卓、お絵かきボードの順に載せ替えた。ホストの試験 (偽のホスト)、手元のゲームホストでの sim (Modern・Retro) とブラウザ版の確認、3 機種の DIRAM の比較まで済んでいる。止まる条件には当たらなかった

## 1. 結果の要約

- **部品**: `lib/add/picoruby-fmrb-asterism/mrblib/table_client.rb` (385 行、うちコメント 83 行) に `FmrbAsterism::TableClient`。Link を内に持ち、卓の一覧・入る・出る・席の札・view の問い合わせ・状態の購読・ホストがいなくなった・戻ったを受け持つ。アプリのメソッドを名前で呼ぶ (ブロックを保持しない)。
- **載せ替え**: 振る舞い (画面・操作・ホストへの呼び出し) は変えていない。小さな違いは 4 章。

| アプリ | 前 | 後 | 差 |
|---|---|---|---|
| オセロの卓 (`flash/app/game/othello/othello.app.rb`) | 1,012 | 797 | -215 |
| お絵かきボード (`flash/app/game/board/board.app.rb`) | 1,399 | 1,209 | -190 |

- **試験**: `test/asterism/table_run.rb` (偽の Asterism と偽のゲームホスト、88 項目) を足し、`rake test` に入れた (`asterism:table_test`)。`rake test` は全部通る。
- **確認**: 手元のゲームホスト (asterism-games の作業コピー、VPS には繋いでいない) で、sim の Modern (オセロ・ボード)、sim の Retro (オセロ)、ブラウザ版 (オセロ・ボード) を通した (5 章)。前のアプリと並べて同じ画面・同じ動きであることを見た。
- **内蔵 RAM**: 6 章 (DIRAM は 3 機種とも前後で同じ)。
- **コミット**: 3 つ (9 章)。push はしていない。

## 2. 3 アプリの違い (載せ替えの前)

接続と卓の部分は 3 つともほぼ同じ形で、名前も定数の値も同じ (RECONNECT_MS 3000、VIEW_EVERY_MS 3000、LIST_EVERY_MS 30000、RESCAN_MS 3000、RESCAN_STEP_MS 300、HOST_GONE_VIEW_MS 30000、WIFI_WAIT_MS 20000、CALL_TIMEOUT 3.0、APP "game"、HOST_APP "games")。違いは次のとおり。

| 項目 | オセロの卓 | お絵かきボード | 麻雀 (R4b) |
|---|---|---|---|
| 卓の名前 | `othello*` | `board*` | `mahjong*` |
| 一覧に覚える値 (状態の put から) | `[st, nm]` | `[n, mb の人数]` | `{st, sk, mb}` (初めは `{}`) |
| 状態の put のほかの使い方 | 札の前は put を view として表示 | 自分の卓の版 (`@host_v`、`@want_v` と同じ値) を録画の判定に使う | 札の前は put を view に、`hv` (ホストの版)、`@host_away` を戻す |
| 別の購読 | なし | 線 (`*/strokes`)。状態より先に読む | なし |
| 繋がったとき | 再描画 | 線の購読、`@note` を消す、卓にいれば絵を読み直す | 再描画 |
| 切れたとき | `@cmd_f` を捨てる | 絵・送る線・命令を捨て、卓にいれば「網から切れた」 | `@act_f`・`@cmd_f`・`@probe_f` を捨てる |
| join の名前 | プロフィールの名前 | 同じ | 名前を先に聞く (MJ5) |
| join の答え | 札 | 札・`owner` | 札・`seat` |
| view の呼び方 | `view(token)` | `view(token)` | `view(token, assist)` |
| 新しい版の view | すぐ | 前の view から 1 s 以上あけて | すぐ |
| view の答えの扱い | `dl` から締め切り | `own` | `take_view` (席・締め切り・待ち・want_v も上げる) |
| view が答えない | 「ホストが答えない: 待っています」 | 同じ | `@host_away` |
| ホストに忘れられた (view が error) | leave して一覧へ「卓から外れた」 | leave して一覧へ「ボードから外れた」 | leave を送らずに一覧へ、`@mine` を消す |
| ホストが去った・戻った | 「ホストとのつながりが切れた」/ 消す | 同じ + 戻ったら絵を読み直す | 再描画、`@host_away` を戻す |
| 一覧の画面にいる間 | - | - | 各卓に `mine()` を聞く (自分の席の印) |
| on_destroy | leave + close | leave + close | close だけ |

ゲームごとの部分 (オセロ: 座る・CPU・Jev・始める・打つ・投了、ボード: 線・絵・取り消し・消す、麻雀: 打牌・宣言ほか) は 3 つとも `@cmd_f` などの自前の呼び出しで、共通にしていない。

## 3. 部品の設計

### 3.1 形

```ruby
@tc = ::FmrbAsterism::TableClient.new(self, "othello", tag: "othello_table")
# on_update
if @tc.update               # Link + 状態 + 一覧 + join の答え
  collect_cmd if @tc.table  # ゲームの呼び出しの答え
  @tc.update_view(now)      # view の答え、期限なら問い合わせ
end
# on_destroy
leave_table if @tc.token   # アプリの leave_table は @tc.leave と自分の後始末
@tc.close
```

- `TableClient.new(owner, kind, tag:, app: "game", host_app: "games", retry_ms: 3000, wifi_wait_ms: nil, settings: nil)`。`kind` は卓の名前の頭。`wifi_wait_ms: nil` は ESP32 だけ 20 s 待つ (3 アプリの `network_ready?` と同じ)。
- Link は TableClient が持つ (Link の owner は TableClient)。`link_up` で on_join・on_leave と状態の購読を毎回出し直し、アプリの `table_link_up` を呼ぶ。`link_down` で購読と join・view の待ちを捨て、`table_link_down` を呼ぶ。
- 期間は既定値を持ち、属性で変えられる: `view_every_ms` (3000)、`view_min_ms` (0。ボードは 1000)、`list_every_ms` (30000)、`rescan_ms` (3000)、`rescan_step_ms` (300)、`host_gone_view_ms` (30000)、`call_timeout` (3.0)。
- 読める値: `table`・`token`・`tables` (一覧の値)・`table_list`・`want_v` (自分の卓の put の最新の版)・`view_v` (最後の view の版)・`net` (:connecting / :up / :down、前の `@net` と同じ意味)・`reconnecting?`・`joining?`・`host_gone?`・`link`。
- アプリから使う口: `join(path, name)` (送れなかったときだけ誤りの文字を返す)、`leave` (ホストに知らせる)、`forget` (知らせない。麻雀の形)、`refresh_view` (命令の答えのあと)、`proxy(path)` (ゲームの呼び出し用)。

### 3.2 アプリへの知らせ (名前で呼ぶ)

| メソッド | 必須 | いつ |
|---|---|---|
| `table_state(path, state)` | 必須 | ゲームの卓の put。戻り値が一覧に残る (`tables[path]`) |
| `table_joined(reply)` | 必須 | 札が来た (札は設定済み)。このあとすぐ view を聞く |
| `table_refused(why)` | 必須 | 断られた (ホストの言葉) か答えない (nil)。`table` は nil |
| `table_view(view)` | 必須 | 自分の view |
| `table_link_up(link)` / `table_link_down(link)` | 任意 | 繋がるたび / 失敗・切断 |
| `table_watch` | 任意 | 繋がっている間の毎回、poll のあと状態を読む前 (ボードの線) |
| `tables_changed(added)` | 任意 | 一覧に卓が増えた (true) / ホストの卓が消えた (false) |
| `table_host_gone` / `table_host_back` | 任意 | 自分の卓のホストが去った / 戻った |
| `table_view_failed` | 任意 | view が答えない |
| `table_dropped` | 任意 | ホストに忘れられた。無ければ `leave` |

- 任意のものは作るときに `respond_to?` で調べて覚えておき、毎回は調べない。
- アプリの `@view` はアプリが持つ (オセロは画面の至る所で読むため)。部品は `view_v` (版) だけを持ち、期限の判定 `view_due?` に使う。前の `@want_v > @view["v"]` と同じ判定になる (札の前に put を view にする形は、札を受けたときに `@view` も `view_v` も 0 に戻るので変わらない)。
- on_join・on_leave は前と同じく Asterism にブロックを渡す (`{ |n| node_joined(n) }`、self だけを捕まえる)。部品がアプリのブロックを保持することはない。

### 3.3 Spinel と picoruby

- 卓のアプリはどれも mruby の VM で動く (Spinel はカーネル・デスクトップ・エディタだけ)。止まる条件の「Spinel で動く構成」は無い。
- picoruby の罠: 定数は `::FmrbAsterism::Link`・`::Asterism` と根から書いた。Regexp・`defined?` は使っていない。`each_pending` に渡すブロックは呼ばれてすぐ終わる形 (前と同じ)。

## 4. 載せ替えで変わったところ (小さなもの)

- **接続は Link になった** (R1 の 4 章と同じ種類の違い): 接続が 1 更新遅れる (:connecting を挟む)。ログの行が Link の形になった (`othello_table: linux/game connected (...)`、`connect: ...`・`lost: ...` は warn)。`Asterism::Error` 以外の接続の誤りも失敗として拾う (前は on_update から外へ出た)。
- WiFi: ESP32 以外は待たない (`wifi_wait_ms` 0)。Link の判定は「経過 > 0」なので、wifi_info が「未接続」を返す端末では最初の 1 更新だけ待つことがある。sim・ブラウザ版では見えていない。
- ボード: view の問い合わせが絵の読み込み・線の送りの前に動く (答えを集める順 join → 命令 → view → 絵は前と同じ)。線の購読を出し直す条件が「線の購読が無いとき線だけ」になった (前は状態と線を両方出し直していた)。
- ボード: ログに「left the network: N table(s) off the list」が出るようになり、「on the list」の行に `table` が付いた。オセロのログは同じ文。
- 切れたときの後始末 (`table_link_down`) は接続の失敗のときにも流れる。すでに空なので結果は同じ。
- そのままにしたもの (振る舞いを変えないため): join の答えを待つ間に Esc で一覧に戻ると、答えが来ても集められず (卓が nil のため)、繋ぎ直すまで次の join が送れない (前からの形、8 章)。オセロで view の答えからホストの戻りを知ったときは「ホストとのつながりが切れた」の表示が残る (前と同じ)。

## 5. 確認

### 5.1 構成

- zenohd: sim の `fmruby_zenohd`。ブラウザ版のときは `docker/asterism/docker-compose.zenoh-ws.yml` を重ねて ws 7449 を足した (sim とブラウザ版と CRuby が同じルータ)。
- ゲームホスト: asterism-games の作業コピー (0.22.0、`ef9891a`) を `bundle exec bin/asterism-games` で、設定は scratchpad (node `r4agames`、`othello1` (turn_seconds 0、cpu_ms 300) と `board1`、状態も scratchpad)。AI は `ASTERISM_GAMES_AI_FAKE=1` (API の鍵を外した。外の API は呼んでいない)。asterism-games には何も書いていない。
- 打ち手: 卓の `table` を問い合わせ、端末の番なら最初の合法手の升をクリックする小さな CRuby の台本 (sim は `tools/fmrb_input.rb`、ブラウザ版は `tools/fmrb_web.rb`)。
- 前後の比較: 前のアプリ (HEAD の 2 ファイル) を `flash/app/tmp_r4a/` に一時的に置き、新しいファームで並べて動かした (終わってから消した)。

### 5.2 sim (Modern 426x240、標準構成)

| 確認 | 前のアプリ | 新しいアプリ |
|---|---|---|
| オセロ: 卓の一覧 (`r4agames/games/othello1 終局 ...`) | 出る | 同じ画面 |
| オセロ: Enter で入る、S で座る、C で CPU / A で Jev | 動く | 動く (Jev [AI] になり CPU[C] で戻せる形) |
| オセロ: CPU と終局まで | 10-54 | Modern では Jev と打った (次の行)。CPU とは Retro で 10-54 (CPU の手は決まっているので前と同じ結果) |
| オセロ: Jev と終局まで (途中でホストを止めて戻す) | - | 19-45。止めると「Jev が考え中」のまま「ホストとのつながりが切れた」、戻すと 「is back」で表示が消え、自分の番から続いた |
| オセロ: 端末のクライアント (`asterism-games play othello --name TTY`) が座り、sim が座り、TTY が start、TTY は auto | - | 64 手で終局 (TTY の勝ち 19-45)。TTY が抜けたあと sim が卓の主 (*) に |
| オセロ: 立つ (S) → 観戦の画面、出る (Esc) → 一覧、ホストの会員から消える | - | 合格 (卓の `mb` が空) |
| オセロ: ホストを止めて状態を消して戻す (札を忘れたホスト) | - | 一覧に戻り「卓から外れた」(`table_dropped`) |
| オセロ: ホストを止めて戻す (卓の中) | 「ホストとのつながりが切れた」→ 戻る | 同じ |
| オセロ: zenohd を止めて戻す (一覧) | 「網から切れた: つなぎ直し中」→ 繋がり直して卓が出る | 同じ (ログは Link の形) |
| オセロ: zenohd を止めて戻す (席に着いたまま) | - | 繋がり直し、席と卓の主のまま待ち合わせの画面に戻った |
| ボード: 一覧、入る、描く | 動く | 動く |
| ボード: 別のクライアント (CRuby で join して `line`) の線が届く | - | 赤い線がそのまま描かれた |
| ボード: U で取り消し、C → y で消す (主) | - | 合格 |
| ボード: ホストを止めて戻す | 「ホストと…」→ 戻ると絵を読み直す | 同じ (`picture read: 2 strokes`) |
| ボード: zenohd を止めて戻す | - | 「網から切れた」→ 繋がり直して絵を読み直した |
| ボード: R で読み直し、Esc で一覧 | - | 合格 |
| エディタを開いて `a` を 1 打鍵 | - | `Ln 1, Col 2 *` |

VM プール (30 秒ごとのログ): オセロ 前 550-569 KB / 後 502-551 KB、ボード 前 558-588 KB / 後 536-605 KB (描いた量で動く)。目立った違いは無い。

### 5.3 sim (Retro 320x240)

`.env` の `FMRB_HW_TARGET` を一時的に NARYAv3 にして `rake clean_all` → `rake build:linux` (確認のあとで戻した)。

- オセロ: 画面全体で一覧 (「入る」「閉じる[Ctrl+Q]」)、入る・座る・CPU・始める、CPU と終局まで (10-54、Modern と同じ)、ホストを止めて戻す (「ホストとのつながりが切れた」→ 戻る)、立つ・出る、「閉じる」をクリックしてアプリが終わる (`othello_table: closed`)。
- エディタを開いて `x` を 1 打鍵 (`Ln 1, Col 2 *`)。

### 5.4 ブラウザ版 (426x240)

`rake wasm:mruby` → `wasm:mrb` → `wasm:web` (gem の TableClient が束に入ったことを確認)。開発サーバは前から動いていたものを使った。`?app=...&zenoh=ws/localhost:7449&w=426&h=240`。

- オセロ: ブラウザ版 (黒、卓の主) と sim (白) が座って 1 局を終局まで (19-45、クリックはブラウザ版 28 回・sim 32 回、どれも通った)。ホストを止めると「ホストとのつながりが切れた」、戻すと消えた。
- ボード: ブラウザ版 (主、「消」がある) と sim (主でない) が同じボードに描き、互いの線が両方に出た。ホストを止めて戻すと絵を読み直した。

### 5.5 ホストの試験

- `ruby test/asterism/table_run.rb`: 接続 (購読・on_join・on_leave・一覧)、table_watch が poll のあと状態の前に来ること、状態の put の振り分け (ほかのゲーム・Hash でないものを除く)、探し直しの間隔 (on_join のあと 300 ms ごとに 3 s、ふだん 30 s、アプリが変えられる)、join (待ち・二重に送らない・札・すぐ view)、断られた・答えない、view の間隔 (3 s、新しい版ですぐ、view_min_ms)、leave と forget、ホストが去る・戻る (一覧から消える、30 s ごとの view、戻ったらすぐ view)、去ったあとに送った view の答えで戻る、view が答えない・忘れられた (table_dropped、無ければ leave)、切断と繋ぎ直し (購読と on_join の出し直し、待っていた view を捨てる)、reconnecting、必須だけのアプリ、ボードの名前、ESP32 の WiFi の待ち。88 項目すべて通る。
- `rake test`: 全部通る (golden 103 passed ほか)。

## 6. RAM

`idf.py size`。前 = table_client.rb を外したビルド (R3 のあとの HEAD と同じ gem)、後 = 入れたビルド。どちらも同じ作業ツリーで `rake clean_all` → `FMRB_HW_TARGET=<機種> rake build:esp32` で作った (アプリのファイルはファームの像に入らない)。

| 端末 | DIRAM 前 | DIRAM 後 | 像の大きさ 前 → 後 |
|---|---|---|---|
| Tab5 (P4) | 129,698 | 129,698 | 6,676,876 → 6,685,876 (+9,000) |
| Retro (NARYAv3) | 126,035 | 126,035 | 4,510,311 → 4,510,311 (gem が入らない) |
| StickS3 | 114,863 | 114,863 | 4,519,195 → 4,527,971 (+8,776) |

- 前の値は R2 の報告の「後」と同じ (R2 のあと gem は変わっていない)。
- mrblib の Ruby は flash に bytecode として入るだけで、静的な内蔵 RAM は増えない。TableClient のクラスは Asterism の gem を持つ VM ごとに定義され、VM のプール (PSRAM) に乗る。卓のアプリの VM プールの使用は前後で同じ幅 (5.2)。
- 作業の後、build/ は Linux (TAB5、x86-64) に戻した。

## 7. 麻雀 (R4b) の見通し

載せ替えられる形だが、部品に足すものが 2 つ、アプリの側の工夫がいくつかある。

- **部品に足す**: (1) view の呼び方。麻雀は `view(token, assist)`。アプリのメソッド (例 `table_ask_view(proxy, token)`) があればそれで呼ぶ形にする。(2) 一覧の値の初め。麻雀は `{}` で始めて `@tables[p]` を Hash として読む (今は nil)。アプリの側で nil を空として読めば足りる。
- **そのまま使える**: 一覧・探し直し・状態の購読 (`table_state` で `hv`・`@host_away`・札の前の view)、join (名前を聞いてから `@tc.join`)、`table_joined` で `enter_table` (席は `reply["seat"]`)、`table_dropped` は leave を送らない `@tc.forget` と `@mine`・`@probed` の片付け、`table_view_failed` で `@host_away`、`table_host_back` で `@host_away` を戻す、on_destroy は `@tc.close` だけ。
- **アプリに残す**: `mine()` の探り (`probe_tables`、一覧の画面の間。`@tc.proxy(p)` で呼ぶ)。`tables_changed(false)` のときに `@mine`・`@probed` から一覧に無い卓を消す。`take_view` の want_v の引き上げは要らなくなる (部品の判定は `want_v > view_v` で同じ結果)。
- 気をつけること: 麻雀の collect_view は error のときも答えたら `@host_gone` を戻している (オセロ・ボードは error でない答えのときだけ)。違いを残すかは R4b で決める。`return 500 if @unsupported` の前で `@tc.update` を呼ばない。打牌の答え (`@act_f`) と名前の変更 (`@rename_f`) はゲームの呼び出しとしてアプリに残る。
- 行数の見込み: 麻雀の本体の接続と卓の部分 (network の節) は約 330 行。オセロ・ボードと同じ割合なら 150-200 行減る。

## 8. 残件

- join の答えを待つ間に一覧へ戻ると、繋ぎ直すまで次の join が送れない (前からの形、4 章)。直すなら `leave`・`forget` で join の待ちも捨てる (振る舞いの変更になるので別に決める)。
- 実機 (Tab5・StickS3) では動かしていない (指示どおり)。gem を変えたので、実機で確かめるには焼き直しが要る。Retro の ESP32 のビルドは Asterism を持たない (FMRB_ASTERISM 0) ので、卓のアプリは Retro の実機では動かない (前と同じ)。
- 麻雀 (R4b)。

## 9. コミット (fmruby-core develop、push していない)

- `b0c514db` asterism: FmrbAsterism::TableClient, the device side of a game host's table
- `07dc76b2` game: the othello table app on FmrbAsterism::TableClient
- `3cfa2a1d` game: the drawing board app on FmrbAsterism::TableClient

## 10. 後片付け

- 手元のゲームホスト (pid で TERM)、端末のクライアント、sim (音を戻して sim_down)、自分が起こしたブラウザ (web_down) を止めた。開発サーバ (port 8006) は前から動いていたので残した。試しに起こした ws 付きの zenohd (`r4a-zenohd`) は使えず、すぐ消した。
- `flash/app/tmp_r4a/` (前のアプリの写し) は消した。`.env` は TAB5 に戻した。
- build/ は最後に Linux (TAB5、x86-64) で作り直した。TAB5 の esp32 ビルドは置き換わっているので、焼く前に `rake clean_all && rake build:esp32` が要る。
