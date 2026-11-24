# A6b 報告: 通信の型と見本を足し、0.3.0 を通しまで確認した

> 状態: 実装済み (fmruby-core ブランチ `ai-sources`、asterism-tutor ブランチ `sources`。push・タグなし)。受け入れ条件 1〜4 は満たした | 更新: 2026-11-24 | `sig/fmrb_asterism.rbs` に FmrbAsterism と Asterism のアプリが使う側の型を書き、見本 2 つ (`flash/app/demo/talk_board`・`talk_client`) を足した。gem の検索の大きさの上限を `flash/app/` だけにした。通し (c) で LLM は型と見本を見つけ、FmrbAsterism::Link とトピックで 2 台の間の文のやり取りを作った (検査を通り、ネイティブ版 2 つで実際にやり取りできた)。指示書は instruction_a6b.md

ESP32 のビルドはしていない (`build/` には触っていない)。VPS・端末・家のサーバには触っていない。本物の LLM は 3 回 (合計 0.0132 ドル)。
本物の GitHub から取ったのはイメージの中からの 1 回だけ。鍵は環境変数で渡しただけで、記録・scratch・両リポジトリの木と最近の
コミットに鍵の文字が無いことを grep で確認した (中身は表示していない)。

## 1. 作ったもの

### 1.1 fmruby-core (ブランチ `ai-sources`)

| コミット | 中身 |
|---|---|
| 75864337 `feat(asterism): types of the app side of FmrbAsterism and Asterism in sig` | `sig/fmrb_asterism.rbs` (新、910 行) |
| 376f5ee8 `feat(demo): talk board and talk client, two machines passing text over Asterism` | `flash/app/demo/talk_board.app.rb` (75 行)・`talk_client.app.rb` (107 行) |
| (この文書) `docs(ai_coding): ...` | この報告と doc/README.md の索引 |

### 1.2 asterism-tutor (ブランチ `sources`)

| コミット | 中身 |
|---|---|
| 18c86fe `feat(lookup): the size limit for search is for the apps only` | `Lookup::RB_MAX_UNDER = "flash/app/"`。試験 (32 KB を超えるライブラリが検索に出る)・README (英・日)・設定の例・CHANGELOG |
| db07e10 `build(bundle): the bundle in the gem made again with the Asterism types` | 同梱の束 `bundles/3.1.0` を ai-sources (376f5ee8) から作り直した。113,409 → 119,531 バイト |
| 0a82a81 `test(container): the cost read back from the records may be a few millionths less` | 束が変わって落ちるようになった試験の直し (5.2) |

## 2. 型 (`sig/fmrb_asterism.rbs`)

実装 (`lib/gems/picoruby-fmrb-asterism/mrblib/`・`vendor/asterism/mrblib/` の pin 済みの asterism da1c382) と、動いているアプリ
(chat・ai_helper・asterism_demo・receiving) の使い方を読んで書いた。**アプリが使う側だけ**:

| クラス | メソッド |
|---|---|
| `FmrbAsterism` | `self.host`・`self.settings` |
| `FmrbAsterism::Settings` | `router`・`node`・`name`・`error` |
| `FmrbAsterism::Link` | `self.new(owner, app)`・`update`・`connected?`・`up?`・`state` (10 個の Symbol の選択肢)・`reason`・`conn`・`node`・`app`・`router`・`took_ms`・`connects`・`lost`・`close`・`screen_ready` |
| `Asterism::Connection` | `expose`・`[]`・`each`・`nodes`・`apps`・`path`・`publish`・`subscribe`・`status`・`status_of`・`on_join`・`on_leave`・`current_call` |
| `Asterism::Proxy` / `AsyncProxy` / `Future` | `async`・`asterism_call`・`asterism_path`・`remote_methods` / `asterism_call` / `done?`・`value`・`took_ms` |
| `Asterism::Topic` / `Subscription` / `SampleInfo` / `CurrentCall` | `put`・`name`・`close` / `close`・`closed?`・`received` / `from`・`node`・`app`・`name`・`lost`・`seq` / `from`・`object`・`method` |
| `Asterism::Error` と `RemoteError`・`TimeoutError`・`Disconnected`・`EncodeError` | `message` / `remote_class`・`remote_message`・`remote_kind` |

- ファイルの頭のコメントに使う順番 (Link を作る → link_up で expose・publish・subscribe → on_update で update → conn[path] で
  呼ぶ・async → on_destroy で close)、送れる値、同じ端末の中の Pub/Sub との違い、見本 2 つのパスを書いた。
- 各メソッドの長文に、引数の意味・戻り値と失敗のしかた (どの例外か)・いつ呼ぶか・例 (39 桁以内) を書いた。要約の行は全部 112 バイト以内
  (scratch の検査の台本で確認)。
- 書かなかったもの: `poll`・`session`・`close` (Connection の。Link の close が閉じる)・`Asterism.connect` (アプリは Link を使う)・
  `define_type`・`pubs_of` などの型の問い合わせ・`profile`・`unexpose`・`unpublish`・`on_app_join` など。
- **キーワード引数は型のエンジンに無い** (sig/README の決まり) ので、いちばん多い呼び方の位置引数の形にし、キーワードは長文に書いた:
  `expose(name, obj, methods)` (実際は `methods:` キーワード)、`[](path)` (`timeout:`)、`Link.new(owner, app)` (`tag:`・`retry_ms:` ほか)、
  `publish(name)` (`type:` ほか)、`subscribe(path)` (`depth:`)、`status(h)` (キーワードの形もある)。ブロックは書けないので長文に書いた。
- `Asterism::Error` は本当は `StandardError` の子だが、sig に `StandardError` が無いので親なしで書いた (`message` だけ持つ)。
- Proxy の上の相手のメソッド (`board.post(...)`) は `method_missing` なので型に書けない。名前で呼ぶ `asterism_call` だけを型にし、
  普通の書き方は長文に書いた。

エディタの型 db (tidbgen で scratch に作って比べた): クラス 51 → 68、メソッド 2,050 → 2,540 (増えた分の大半は各クラスに写る Object の
メソッド)、要約の文字列の表 (document pool) 48,633 → 53,094 バイト (上限 65,535)。F1 のヘルプは `flash/help/` に
`FmrbAsterism*.md`・`Asterism__*.md` ができた (生成物、git 外)。回帰の道具の基準は変わらなかった (基準のコミットは無い)。

## 3. 見本と受け入れ条件 1

`flash/app/demo/` に置いた (ほかの見本と同じ)。`.app.toml` は作らず、**先頭に `#---fmrb` から `#---` のメタ情報コメント**を置いた
(asterism-tutor 0f2eca9 の決まりに合わせた)。Retro の storage からは外していない (同じ Asterism の見本の avatar と同じ扱い。
数 KB)。

- **talk_board** (公開する側): Link でつなぎ、link_up で自分を `board` として `expose(methods: { post: 1 })`、トピック `posts` を
  publish。`post(text)` は `current_call.from` で呼んだ端末を知り、`posts` に `{"from", "text"}` を put して行数を返す。
  空・60 字を超える文は例外 (呼んだ側に RemoteError)。
- **talk_client** (呼ぶ側): FmrbUI の入力欄と「送る」。`conn.each("*/talk_board/board")` で伝言板を探し、`async.post(text)` の
  Future を on_update で `done?`・`value`。link_up で `*/talk_board/posts` を subscribe して画面に出す。

### 3.1 ネイティブ版 2 つでの確認 (受け入れ条件 1)

ネイティブ版 (`rake native:core`、標準の構成) を 2 つ (`--run-dir`・`--debug-port 5571/5572`・別の `--user-dir`) 起こし、各
`/home/asterism/config.toml` を `router = "tcp/127.0.0.1:17520"`・`node = "talk-a" / "talk-b"` にした。ルータは asterism-tutor の
`rake router:up` (docker の zenohd)。

- A で talk_board、B で talk_client を起動: ログに `talk_board: talk-a/talk_board connected (... 14 ms)`・
  `talk: talk-b/talk connected (... 15 ms)`。
- B で "hello from b" を打って Enter: B の画面に「送りました (1 件目、38 ms)」と `talk-b: hello from b` (トピックで戻った分)、
  A の画面に `talk-b: hello from b`。
- A の talk_board を kill して B から送る: 「伝言板が見つかりません」(入力は残る)。
- 両方のログに例外・エラーの行は無い。VM プールはコンパイルの後 used 424 KB (board)・429 KB (client)、`ps` の mem_used は board 514 KB
  (64 ビット、プール 1.5 MB)。
- 画面の写しは scratch の `a6b/a2.png`・`b2.png`・`b3.png` (会話の場所のスクラッチ。消える)。

`rake regress` は通った (`host: 33 suites, 33 passed`・`regress: 310 runs, 310 passed`)。

## 4. asterism-tutor と受け入れ条件 2

- 上限 (`sources.rb_max_kb`、32 KB) を `flash/app/` の `.rb` だけに効かせた。`lib/gems/*/mrblib` のライブラリ (fmrb-app.rb 57 KB・
  fmrb-ui.rb 43 KB・picorabbit_renderer.rb 66 KB) は大きくても検索できる。設定の名前は変えていない。
- 試験: 試験のリポジトリに 48 KB の `lib/gems/picoruby-fmrb-app/mrblib/fmrb-app.rb` を足し、`test_scope_leaves_out_tests_mahjong_big_rb_and_c`
  で「検索に出る、42 KB の `flash/app/game/huge.app.rb` は出ない」を確かめる。前の lookup.rb ではこの試験が落ちることも確認した。
- `bundle exec rake test`: 単体 68 件・511 確認、通し 26 件・336 確認、全部通った。

## 5. 通し (受け入れ条件 3)

### 5.1 イメージの中から GitHub へ

`deploy/vps/Dockerfile` で手元にイメージ (`asterism-tutor:a6b`) を作り、ルータのコンテナのネットワークに同居させて
(`--network container:asterism_tutor_zenohd`、uid 1000、`/sources`・`/state` は scratch に bind) 起こした。取る元は既定 (公開の GitHub)。

| | 値 |
|---|---|
| 取った版 | develop (core b06145fc、asterism da1c382e) |
| 時間 | 3.82 秒 (起動から 4.3 秒で記録の `sources ok=true`) |
| ディスク | 4.04 MB |
| 束 | 115,770 バイト (公開の develop にはまだ新しい型は無い) |

`status` は `up, joined ... sources develop@b06145fc962e`。ここまでで止めた (LLM は偽物、頼みは流していない)。

### 5.2 手元の fmruby-core から、ネイティブ版の AI ヘルパーで (c)

同じイメージを、fmruby-core を `/core` に読み取り専用で mount し、設定 (`ASTERISM_TUTOR_CONFIG`) で `sources.core = "file:///core"`・
`sources.branch = "ai-sources"` にして起こした (LLM は anthropic、既定の claude-haiku-5-5、1 日の上限 0.5 ドル)。

- 取得: ai-sources@376f5ee8、2.18 秒、束 120,867 バイト。ディスク 32.98 MB (file:// の元が `uploadpack.allowFilter` を持たず
  `--filter=blob:none` が効かないため。GitHub からのときは効く。試験の段取りだけの話)。
- ネイティブ版 A (talk-a) でエディタ (新しい無題の文書) と AI ヘルパーを開き、「作成」を選んで「ほかの端末と文をやり取りするアプリを作って」
  (ime で入れた) を送った。22.7 秒で「入れかえました」。

| 往復 | 道具 | 引数 | 結果 |
|---|---|---|---|
| 1 | search | `FmrbAsterism::Link` (全体) | 17 件 (sig/fmrb_asterism.rbs・見本 2 つ・chat・ai_helper・link.rb ほか) |
| 1 | search | `Asterism` (`flash/` の中) | 58 件 (30 件を返した) |
| 2 | read | `flash/app/demo/talk_client.app.rb` 1〜107 行 (全部) | - |
| 3 | search | `publish\|conn.subscribe\|\.put\(` (`flash/app/`、**regexp を付けず**) | 0 件 |
| 4 | search | `\.publish\(\|\.subscribe\(` (全体、**regexp を付けず**) | 0 件 |

| 呼び出し | 往復 | 出力トークン | キャッシュ書き / 読み | 費用 (USD) | 時間 | 検査 |
|---|---|---|---|---|---|---|
| 5 | 4 (上限) | 4,609 | 54,517 / 210,175 | 0.01122 | 22.7 秒 | 通った (問題 0)、メタ情報コメント kept |

返ったコード (2,277 バイト、91 行):

- **先頭に `#---fmrb` から `#---` のメタ情報コメントがある** (`app_screen_name = "MsgShare"`・`app_screen_name_ja`・窓 240x130)。
- `FmrbAsterism::Link.new(self, APP)`、`link_up(link)` で `link.conn.subscribe("*/msgshare/msg") { |v, info| ... }` と
  `@topic = link.conn.publish("msg")`、`link_down(link)` で `link.reason`、送るときは `@topic.put({ "text" => text })`、
  `rescue ::Asterism::Error`、`info.node`、`on_destroy` で `@link.close`。**どれも型のとおり**。見本 talk_client をほぼ写した形で、
  同じアプリどうしがトピックだけでやり取りする (公開・呼び出しは使っていない。頼みには足りる)。
- 説明は「FmrbAsterism::Link で…つながる」「確かでない点が 2 つ (自分の文が戻るか、送る場所の名前)」。作り話は無い。
- **実際に動いた**: 返ったコードをネイティブ版 A・B の `/home/msg.app.rb` に置いて起動し、B で "hi from b" を送ると A に「相手: hi from b」。

### 5.3 (a)(b) をもう 1 回 (同じイメージ・同じ写し、`test/sender.rb` から)

| 頼み | 時間 | 呼び出し | 往復 | 出力トークン | キャッシュ書き / 読み | 費用 (USD) | 検査 | A6 (0.3.0) |
|---|---|---|---|---|---|---|---|---|
| (a) 「変数って何?」(質問) | 1.9 秒 | 1 | 0 | 224 | 139 / 49,275 | 0.00062 | 通った | 2.0 秒・0.00596 (キャッシュが冷えていた) |
| (b) 「/home/cat.bmp の絵を画面のまんなかに出すアプリ」(作成) | 7.9 秒 | 1 | 0 | 1,668 | 492 / 49,275 | 0.00139 | 通った | 5.9 秒・0.00118 |

- (a): 調べずに答えた (箱のたとえと `score = score + 1` の例)。
- (b): 調べずに `@gfx.picture_size(PATH)` と `@gfx.draw_picture(PATH, x, y)`、無いときは文字で知らせる (A6 と同じ形)。先頭に
  メタ情報コメントがある。出力が 290 トークン多く 2 秒遅いが、中身は同じで悪くなっていない。
- 束が大きくなった分、1 回の読みは 46,742 → 49,275 トークン (+5.4%)。

合計の費用は 0.01122 + 0.00062 + 0.00139 = **0.0132 ドル**。

## 6. 見立てと違った点・型と実装の食い違い・残件

1. **LLM が `|` を正規表現のつもりで、`regexp` を付けずに検索した** (3・4 往復目、どちらも 0 件)。publish・subscribe の型を確かめに
   行ったが空振りし、往復の上限まで使った。それでも見本を丸ごと読んでいたので答えは正しかった。道具の説明 (`data/tools_ja.md` か
   道具の定義) に「`|` で並べるなら regexp: true」と書く余地がある (範囲の外なので変えていない)。
2. **返ったコードの小さな難**: `draw_screen if @link.update || @dirty` で、つながっている間は毎回 (100 ms ごと) 全面を描き直す。
   型の使い方ではなく描き方の問題 (見本は `check_call if @link.update` と `draw_screen if @dirty` を分けている)。
   `return if info.node == @link.node` も不要 (自分の put は自分に届かない。型の説明にも書いた) だが害は無い。
3. **FmrbUI の入力欄は IME の文字を受けない** (`type_into_focus` が `ev[:character]` の 1 バイトだけを見る)。見本 talk_client の入力欄に
   ネイティブ版の ime で「こんにちは」を入れても入らなかった。見本で日本語の文は送れない (英数字は送れる)。gem の実装なので変えていない。
4. 型と実装の食い違いは見つからなかった。書き方の制約 (キーワード・ブロック・method_missing・StandardError が sig に無い) は 2 章。
5. **試験の直し (asterism-tutor 0a82a81)**: 束が大きくなると偽の LLM の費用の桁が増え、`test_a_request_cut_by_the_stop_is_not_counted_but_its_cost_is`
   が `Expected 0.003762 to be >= 0.003762025` で落ちた。記録は 1 回ごとの費用を 6 桁に丸めて書き、起動中の合計は丸めずに足すため、
   読み直した合計が 2.5e-8 ドル小さくなる。試験に丸めの分 (1e-5) の余裕を足した。実装 (丸めの扱い) は変えていない。
6. ネイティブ版の `--run-dir` を scratch に置くと、ソケットのパスが長すぎて起動しなかった (`run directory too long for a socket path`)。
   `/tmp/a6b_ra`・`/tmp/a6b_rb` を使い、終わりに消した。
7. 型の db の要約の表は 53,094 / 65,535 バイト。型を足し続けると 2〜3 ファイル分で上限に近づく。

## 7. 確認していないこと

- ESP32 (P4-Nano・Tab5・Retro) での見本・型。Retro の storage の像に見本が入ること (`storage_exclude_*` に書いていないので入るはず)。
- エディタの Ctrl+E (まちがい探し) が Proxy の上の呼び出し (`boards[0].async.post(text)`) をどう扱うか。型に無い名前として
  指摘するかもしれない (`asterism_call` 以外は型に書けないため)。補完と F1 の表示も端末では見ていない (ヘルプのファイルができたことだけ)。
- 本物の GitHub に新しい型が載った後の通し (develop に入るまで不可)。Sonnet での通し。VPS。

## 8. 後片付け

ルータ (asterism_tutor_zenohd)・AI ホストのコンテナ 2 つ・ネイティブ版 2 つは止めた。試しのイメージ (asterism-tutor:a6b) と
`/tmp/a6b_r*` は消した。写し・記録・画面の写しは scratch にだけある。両リポジトリの `git status` は、fmruby-core の submodule の
印 (`components/esp_littlefs`・`components/picoruby-esp32/picoruby`、`rake setup` の写しで前から出るもの) のほかは空。
