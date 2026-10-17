# GS5 指示書: 麻雀を AI が読める形にし、AI の打ち手 (Claude・Jev) を卓に座らせる

> 状態: 指示 | 更新: 2026-10-17 | games_showcase.md の GS5。meta に引数名と説明を足し、居る・居ないをアプリ単位にし、麻雀の卓を AI が読める形 (MCP のツールの説明) にする。VPS のゲームホストの中で動く AI の打ち手 (Claude API と Jev) を、卓の主が CPU と同じく空いた席に入れられるようにする

前提: games_showcase.md (2.5 MCP・2.6 AI の打ち手)、report/gs4.md (7 章 GS5 に向けて: meta に引数名が無い、view の短い鍵、1 MCP サーバ = 1 打ち手、手番待ちの呼び出し、持ち時間)、gs3.md (居る・居ない)、gs4.md 6 章 (アプリ単位の材料)、asterism の docs/protocol.md 5 章 (meta は知らない欄を読み飛ばす)・mrblib・exe/asterism (`asterism mcp`)、asterism-games の lib/asterism_games/ (game.rb・mahjong/table.rb の CPU の扱い・engine.rb)、卓の麻雀のアプリ (fmruby-core flash/app/game/mahjong/)、claude-api の skill (Claude API の呼び方。モデル ID は `claude-sonnet-5-5`)。

言葉: 文書は常体。asterism・asterism-games の文書とコードのコメント、fmruby-core のコードのコメントは英語。

## 1. 決まっていること (2026-10-17 ユーザ、選択形式)

1. **meta に引数名と説明を足す**: meta の各メソッドに `args` (引数名の配列) と `doc` (説明) の欄を足し、`expose` で書けるようにする。読む側は知らない欄を読み飛ばすので古いノードと共存できる。`asterism mcp` のツールの入力は arg1.. でなく名前に、説明は doc に。
2. **アプリ単位の居る・居ない**: 卓のアプリはアプリ名 `game` という決まりにし、ゲームホストは会員を `asterism/<ノード>/game` の印で見る (網の決まりは変えない。今の端末・ブラウザ版はそう、常設の卓の ACL `Read asterism/*/game` とも合う)。
3. **@status の鍵の卓の名前はこのまま**。
4. **AI の打ち手は VPS で動かす** (ゲームホストの隣、誰でも AI と打てる)。API の鍵は VPS に置く。
5. **費用の上限**: 使ったトークン数を数え、設定の **1 日の額** (.env で変えられる) を超えたらその日は AI を卓に入れない (入っている AI は CPU に代わる)。加えて **1 局あたりの呼び出し回数** に上限。
6. **AI を呼べるのは卓の主** (CPU を入れるのと同じく空いた席に。部屋・常設とも)。
7. **Claude のモデルは Sonnet 5.5** (`claude-sonnet-5-5`。設定で変えられるようにする)。
8. **Jev (Cloudflare Workers AI 経由) も入れる**。Claude と並べて比べられるようにする。
9. **AI の席だけ持ち時間を長くする**: AI の席には別の持ち時間 (例: 1 手 30 秒、鳴きの判断も別に。設定で変えられる) を使い、人間の席は今のまま。切れたらその手だけ CPU が代わりに打つ。

## 2. 作るもの

1. **meta の args / doc** (asterism、portable の層): protocol.md 5 章に欄を足し、`expose` の書き方を決める (例: `methods: { "move" => { args: %w[token n square], doc: "..." } }` のように今の引数の数の表と両立する形)。適合の試験 (CRuby・mruby) に足す。`asterism mcp` と CLI の `objects` が名前と説明を使う。asterism-games の麻雀とオセロの `Remote` に引数名と説明を書く。
2. **アプリ単位の居る・居ない** (asterism-games、必要なら asterism に `on_app_join` / `on_app_leave` の類): 1 章 2 のとおり。同じノードで別のアプリが動いていても、卓のアプリが閉じれば途切れになること。
3. **麻雀を AI が読める形に** (asterism-games):
   - 卓の見え方の短い鍵 (`st`・`tn`・`lg`・`dl` など) の説明と、合法手の一覧を、AI が読める形で返すメソッド (例: `describe` か、見え方の別の形) を足すか、MCP のツールの説明に載せる。どちらにするかは任せる (理由を report に)。
   - 点数の問い合わせ (今の手で和了ったら何点か)、支援表示 (assist) の情報を AI にも渡せるように。
   - 外の MCP の客 (例: Claude Code) が `asterism mcp --allow 'games/mahjong1.*'` で麻雀に座って半荘を打てること。
4. **AI の打ち手** (asterism-games、VPS のゲームホストの中):
   - 打ち手の「判断のつなぎ」を 1 つの形に決め (見え方と合法手を渡し、選んだ手を受け取る)、Claude (Messages API、`claude-sonnet-5-5`) と Jev (Cloudflare Workers AI) のつなぎを書く。判断はゲームの錠 (`Asterism::LOCK`) を握ったまま待たない (別のスレッドで待ち、結果を卓に戻す)。
   - 卓の主が空いた席に「AI (Claude)」「AI (Jev)」を入れられる (CPU を入れるのと同じ呼び方の延長)。卓の麻雀のアプリ (fmruby-core) とブラウザ版に、その操作と、AI の席の表示 (名前に Claude / Jev と分かる印) を足す。オセロにも入れるのは手間が小さければ。
   - 1 章 9 の AI の席の持ち時間と、切れたときの CPU の代打ち。
   - 1 章 5 の費用の上限: トークン数と額を数えて状態のファイルに残し (日付ごと)、上限を超えたら新しい AI を断り、入っている AI を CPU に代える。1 局あたりの呼び出し回数の上限。数えた値は `bin/asterism-games status` と @status (合計だけ) に出す。単価は設定 (.env) に持たせ、既定値は使う前に公式の料金を確かめて決める (確かめた日付を report に)。
   - API の鍵は deploy/vps の `.env` (600) から読む (`ANTHROPIC_API_KEY`、Cloudflare の account id と token)。鍵をログ・状態のファイル・@status・例外の文に出さない。鍵が無いときは AI の選択肢を出さない (卓のアプリにも出さない)。
   - Jev は、使う前に Cloudflare Workers AI での実際の名前・API・料金を確かめる。見つからない・使えない・料金が分からないなら、Jev の分だけ止まって返す (Claude の分は進める)。
5. **試験**: 判断のつなぎは偽物 (決まった手を返す、遅れる、失敗する、トークンを返す) で試す: 卓に AI を入れる・判断が遅れて持ち時間が切れたら CPU が打つ・判断が失敗したら CPU が打つ・1 日の上限を超えたら新しい AI を断り入っている AI が CPU になる・鍵を出さない。meta の args / doc の適合の試験。アプリ単位の居る・居ないの試験。
6. **文書**: asterism の README・protocol.md、asterism-games の README (AI の打ち手、設定、費用の上限、鍵の置き方)・docs/install.md (VPS で鍵を置く手順)、CHANGELOG。VERSION は asterism-games 0.10.0、asterism は CHANGELOG の Unreleased。

## 3. 受け入れ条件

1. asterism の試験全部 (CRuby・mruby・protocol の適合)、asterism-games の `MJ_GAMES=8 bundle exec rake test`、asterism-console の試験と rubocop (触ったなら) が通る。
2. 家の LAN の卓で、偽物のつなぎの AI を入れた半荘 (人間 1 + AI 1 + CPU 2 でよい、ボットでもよい) が終局まで進む。持ち時間切れ・失敗で CPU が代わることを 1 回ずつ見せる。
3. 外の MCP の客から `asterism mcp --allow 'games/mahjong1.*'` で麻雀に座り、ツールの説明 (引数名・doc・見え方の説明) だけを頼りに東風戦を最後まで打てる (MCP の客は手元の台本でよい。AI に打たせられるならなおよい)。
4. アプリ単位: 同じノードで別の Asterism アプリを動かしたまま卓のアプリを閉じると、ゲームホストがすぐ途切れにする (数字)。
5. **本物の API での確かめ**: 環境変数 `ANTHROPIC_API_KEY` (と Cloudflare の鍵) がこの作業の環境にあるときだけ行う (鍵を探しに行かない)。あれば、Claude の AI を卓に入れて東風戦を 1 回、使ったトークン数と額、1 手の判断時間 (平均・最大) を出す。無ければ「本物の API での確かめは保留」と report に書いて返す (親がユーザに鍵を頼む)。
6. 卓のアプリ (ブラウザ版 1280x720・640x360、sim) で、卓の主が AI を入れる操作と AI の席の表示を写す (偽物のつなぎでよい)。卓のアプリの VM の使用量が GS3 から大きく増えない (数字)。
7. ゲームホストの RSS (AI の打ち手を入れた卓があるとき) と、1 通の最大。

## 4. 範囲・決まり

- **触ってよいもの**: asterism (main)、asterism-games (main)、asterism-console (main、AI の席の表示や @status の欄で要るなら)。fmruby-core はブランチ `feature/gs5` (develop から) で、卓の麻雀のアプリ (とオセロ、入れるなら) と、この Step の文書 (report)。
- **触らないもの**: riichi_engine の Fork (CPU の強さはそのまま)、asterism-zenoh・picoruby-asterism-zenoh、zenohd、VPS、実機の端末、sdkconfig。rubygems への公開・タグはしない。API の鍵を探さない・作らない・コミットしない。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core のコミットの件名は `<領域>: <要約>`、本文は英文。実在の値と鍵の検査をしてからコミットする。
- fmruby-core のビルドは本体の checkout で。ブラウザ版は `source ~/emsdk/emsdk_env.sh` のあと `rake spinel:gen wasm:mruby wasm:mrb wasm:web` で作り直す。終わったら build/ は Linux の標準構成 (x86-64) に戻し、`file build/fmruby-core.elf` で確かめる。sim の標準構成でエディタを 1 回起動して 1 打鍵する。
- 終わったら起こしたもの (zenohd、ゲームホスト、MCP サーバ、deploy/local、試しの image と volume、sim・web) を名前で止めて消す。前からある image は残す。docker の prune はしない。sim は音を出す前にミュート。
- **止まる条件**: Jev が見つからない・使えない・料金が分からない (Jev の分だけ止まり、Claude は進める)。1 章に無い仕様の選択 (利用者から見た動き・既定値で 1 章を越えるもの、例: AI に渡す情報の範囲を人間より広げる) が要る。riichi_engine・asterism-zenoh の変更が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に、各選択肢で何が変わるか一言) を書いて返す。
- **公平さ**: AI に渡すのは、その席の人間に見えるものと同じ範囲だけ (他人の手牌・山・裏ドラは渡さない)。
- **report** (`doc/ruby_asterism/report/gs5.md`、常体): meta の args / doc の決まり、アプリ単位の居る・居ないの形、AI が読む形 (選んだ方とその理由)、判断のつなぎの形、費用の数え方と既定の単価 (確かめた日付)、受け入れ条件ごとの結果と数字、見立てと違った点、GS6 に向けて分かったこと、残ったこと (本物の API の確かめが保留ならそう書く)。
