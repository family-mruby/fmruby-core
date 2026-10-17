# GS3 指示書: 居る・居ないを on_join / on_leave で知る、卓のアプリは現れたのを合図に探し直す、見え方の場合分けを case/in で

> 状態: 指示 | 更新: 2026-10-17 | games_showcase.md の GS3。ゲームホストの「途切れ」を呼び出しの間隔で数えるのをやめて Asterism の on_join / on_leave で知り、卓のアプリは数秒ごとの一覧の取り直しをやめて on_join を合図に探し直す。端末の見え方の場合分けを case/in で書く

前提: games_showcase.md (2.3 Asterism らしい書き方)、report/gs1.md・gs2.md・gs2b.md (枠組みの形、オセロの卓、席の管理を基底に上げるかの判断を GS3 に残したこと)、asterism の README (`Asterism.poll`、`Asterism.on_join` / `on_leave`、CRuby の `net.on_join` / `net.on_leave`、liveliness の鍵 `asterism/<node>/<app>[/<object>]`)、asterism-games の lib/asterism_games/game.rb (`seen`・`away_ms`・`member_away?`・忘れる処理・`seat_reuse_ms`)、host.rb、rooms.rb、卓のアプリ (fmruby-core flash/app/game/mahjong/、flash/app/game/othello/)、bin/othello-tty、report/mje.md (case/in のコンパイラの直し lib/patch/compiler/mruby-compiler-codegen.c)。

言葉: 文書は常体。asterism-games の文書とコードのコメント、fmruby-core のコードのコメントは英語。

## 1. 決まっていること

- games_showcase.md の GS3: 居る・居ないを on_join / on_leave で、端末の探し直し、case/in。受け入れの目安は「途切れの判定が速くなる。試験」(2026-10-16 ユーザ)。
- 席の管理 (座る・立つ・卓の主・CPU) を枠組みの基底 (例: `Seats`) に上げるかは、麻雀とオセロを見比べてこの Step で判断してよい (2026-10-17)。上げるなら、麻雀とオセロの見た目・動き・保存の形・呼び方を変えない。
- 互換は気にしなくてよい (今の develop の卓のアプリ・ブラウザ版・bin/othello-tty が動けばよい)。
- 手牌のような席だけの情報は、今どおり呼び出しの答えで渡す。

## 2. 作るもの

1. **ゲームホストの居る・居ない** (枠組み):
   - 卓の会員のノードの on_leave で、その会員をすぐ「途切れ」(away) にし、on_join で戻す。途切れたときの扱い (持ち時間の短縮、CPU の代打ち、表示) は今のまま。
   - 呼び出しの間隔で数える今の仕組み (`seen` と `away_ms`) は、liveliness が届かない場合の備えとして残すか、やめるかを調べて決める (部屋の中でゲームホストが席のノードの liveliness を見られるか — ACL の部屋の規則 — を必ず確かめる。見られないなら部屋では今の仕組みを残す)。決めた理由を report に書く。
   - 長く居ない会員を忘れる処理 (`away_ms` の 6 倍)・`seat_reuse_ms` は、意味を変えない (時間の数え始めを on_leave にしてよい)。
   - 部屋 (席の ID) ・常設 (証明書の名前)・家の LAN (平文) のどれでも同じに効くこと。
2. **卓のアプリの探し直し** (fmruby-core の麻雀とオセロの卓のアプリ、bin/othello-tty):
   - 卓の一覧の取り直しを、数秒ごとから「ゲームホストのノードの on_join を合図に」へ。合図が来ない場合の備えとして、長い間隔 (例: 30 秒) の取り直しは残してよい。
   - 対局中のホストの on_leave で「つながりが切れた」表示にし、on_join で取り直して戻る (今のつなぎ直しの動きと合わせる)。
3. **case/in**: 卓のアプリの見え方の場合分け (状態 `st` ごと、届いた答えの形ごと) を、読みやすくなる所だけ case/in で書く。Spinel ではなく mruby の VM で動くアプリなので、case/in のコンパイラの直しが効いていることを確かめる (端末の pattern match が動く試験か、sim とブラウザ版で通る道を動かす)。無理に全部を書き換えない。
4. **席の管理を基底に上げるか** (1 章): 判断して、上げるなら上げる。上げないならその理由を report に。
5. **試験**: 枠組みの試験 (on_leave ですぐ away、on_join で戻る、liveliness が来ないときの備え)、麻雀とオセロの試験は今のまま通る。
6. **文書**: asterism-games の README (居る・居ないの仕組み、新しいゲームの書き方の該当部分)、CHANGELOG があれば。VERSION を asterism-games 0.8.0 に。

## 3. 受け入れ条件

1. asterism-games の `MJ_GAMES=8 bundle exec rake test` が全部通る。足した試験も通る。
2. **途切れの判定が速くなる** (数字で): 座った客 (ブラウザ版のタブを閉じる、sim の卓のアプリを kill、CRuby のクライアントを Ctrl+C) が居なくなってから、ゲームホストがその席を途切れにするまでの時間を、直す前と後で測る (家の LAN の卓・部屋の卓の両方。部屋は console の deploy/local)。戻ったとき (同じ席で開き直す) に途切れが解けるまでの時間も。
3. **探し直し**: ゲームホストを止めて起こしたとき、卓のアプリの一覧に卓が戻るまでの時間を前と後で測る。待っている間に一覧を取り直す回数 (網に出る get の回数) が減ること。
4. 麻雀とオセロが今どおり遊べる: 家の LAN の卓で、ブラウザ版と sim で麻雀に座って打つ、オセロを終局まで (CPU 相手でよい)。部屋の卓で 1 回 (console の deploy/local)。
5. 1 通の最大・ゲームホストのメモリ (RSS)・卓のアプリの VM の使用量が GS2 と同じくらい (数字を出す)。卓の麻雀のアプリは P4-Nano で large_memory = 1 で動いている (gs2.md・mj5.md)。VM の使用量が増えるなら数字を出し、増えた分を減らす工夫をする。
6. sim の標準構成で、エディタを 1 回起動して 1 打鍵する (fmruby-core の CLAUDE.md の決まり)。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core はブランチ `feature/gs3` (develop から) で、卓のアプリ (麻雀・オセロ) と、この Step の文書 (report)。asterism と asterism-console は読むだけ (on_join / on_leave に足りないものがあれば止まる)。
- **触らないもの**: riichi_engine の Fork、zenohd、VPS、実機の端末、sdkconfig、今の端末どうしのオセロ (flash/app/test/othello)。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。fmruby-core のコミットの件名は `<領域>: <要約>`、本文は英文。実在の値と鍵の検査をしてからコミットする。
- fmruby-core のビルドは本体の checkout で。ブラウザ版は `source ~/emsdk/emsdk_env.sh` のあと `rake spinel:gen wasm:mruby wasm:mrb wasm:web` で作り直してから確かめる。終わったら build/ は Linux の標準構成 (x86-64) に戻し、`file build/fmruby-core.elf` で確かめる。
- 終わったら起こしたもの (deploy/local の一式、zenohd、ゲームホスト、試しの image と volume、開発サーバ、ブラウザ、sim) を名前で止めて消す。前からある image は残す。docker の prune はしない。sim は音を出す前に sim_audio でミュートし、終わったら戻す。
- **止まる条件**: asterism の API (on_join / on_leave) が足りない、網の決まりや ACL の変更が要る (部屋の中で liveliness が見えないのを見えるようにする変更を含む)。麻雀・オセロの見た目・動き・保存の形を変えないとできない。1 章に無い仕様の選択 (利用者から見た動きや既定値を変えるもの) が要る。
- **report** (`doc/ruby_asterism/report/gs3.md`、常体): 居る・居ないの仕組みの前と後 (コードの抜粋)、残した備えとその理由、探し直しの形、case/in にした所、席の管理の判断とその理由、受け入れ条件ごとの結果と数字 (途切れの判定の時間、一覧が戻るまでの時間と get の回数)、見立てと違った点・撤回した仮説、GS4 に向けて分かったこと、残ったこと。
