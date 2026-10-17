# GS7 指示書: 枠組みを gem に、ゲームホストの image を ghcr に

> 状態: 指示 | 更新: 2026-10-18 | games_showcase.md の GS7。asterism-games の中で、gem に入れるもの (枠組みと見本のゲーム) と image にだけ入れるもの (麻雀) を分け、gem として配れる形に整える。image はタグで CI が作って ghcr に出す

前提: games_showcase.md (2.1 配り方、GS7)、report/gs1.md〜gs6.md・gs5b.md、asterism-games の全体 (lib/asterism_games.rb と lib/asterism_games/、mahjong/・othello/・board/、bin/、deploy/vps/、Gemfile・Gemfile.lock、README、docs/install.md、CHANGELOG.md)、asterism-console の image を出す CI (.github/workflows/ の image のもの) と deploy/vps の image の名前の付け方。

言葉: 文書は常体。asterism-games の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

1. **gem の名前は `asterism-games`** (require は今の `asterism_games` のまま)。
2. **同じリポジトリで分ける**: gem に入れるのは枠組みと、見本のゲームのオセロとボード。麻雀は riichi_engine の Fork に頼るので gem に入れず、image にだけ入れる。リポジトリは 1 つのまま。
3. **image はタグで CI が出す**: console と同じく、`v<版>` のタグを打つと GitHub Actions が image を作って ghcr に出す。VPS は手元で作って運ぶ代わりに ghcr から取れる。

ほか (前から): gem の公開 (rubygems への push) と、タグを打つのはユーザ。

## 2. 作るもの

1. **gemspec** (`asterism-games.gemspec`): 入れるファイル (枠組み・オセロ・ボード・その bin)、入れないファイル (麻雀・deploy・試験の重いもの)、依存 (asterism。版の下限は @status・args/doc・on_app_join を使うなら次の版、使わない形でも動くなら 0.7.1 以上。どちらにしたかを report に。riichi_engine と anthropic は gem の依存に入れない)。Gemfile は image と開発のためのものとして残し、麻雀と AI の打ち手の依存はそこに。
2. **麻雀と AI の打ち手の置き場所**: gem から外れる麻雀 (と、Claude の SDK を使う AI の打ち手。どちらに置くかは任せ、理由を report に) が、gem の枠組みを使うアプリとして読み込まれる形にする (例: `lib/` の外か、require の道を分ける)。ゲームの登録 (`Game.register`) は今のまま効くこと。
3. **gem だけで動く見本**: gem を入れただけの環境 (麻雀なし) で、オセロとボードのゲームホストを起こせる (bin とその使い方を README に)。試しに `gem build` → 一時的な場所に `gem install` して、平文のルータでオセロとボードが動くことを確かめる。
4. **image の CI**: `.github/workflows/` に、タグ `v*` で image を作って `ghcr.io/ruby-asterism/asterism-games:<版>` に出す仕事を足す (console の作りに合わせる)。deploy/vps の compose が ghcr の image を既定で使い、手元で作る道 (`build`) も残す。update.sh を console と同じく「版を指定して取る」形に。
5. **文書**: README (gem の入れ方、自分のゲームの書き方を gem で、image の取り方)、docs/install.md (ghcr から取る、手元で作る)、CHANGELOG。VERSION は 0.12.0。

## 3. 受け入れ条件

1. `MJ_GAMES=8 bundle exec rake test` が通る。gem に入る部分だけの試験 (麻雀を読み込まない) も通る。
2. `gem build` が警告なく通り、作った gem の中身の一覧に麻雀・deploy・鍵らしいものが入っていない。一時的な GEM_HOME に入れて、オセロとボードのゲームホストが平文のルータで起き、bin/othello-tty で打てる。
3. CI の仕事は、手元で同じ docker build が通ることと、workflow の書式の検査 (actionlint などがあれば) で確かめる。本物の ghcr への push はしない (タグを打つのはユーザ)。
4. deploy/vps の compose で、ghcr の image の名前で起こせる形 (手元で同じ名前を付けた image で確かめる) と、手元で作る道の両方が通る。healthy まで。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は文書 (report) だけ (develop に直接)。asterism-console は読むだけ。
- **触らないもの**: riichi_engine の Fork、asterism、zenohd、VPS。rubygems への公開・タグ・ghcr への push はしない。
- コミットのみ。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。鍵と実在の値の検査をしてからコミットする。
- 別の作業が同じ asterism-games の image を使って本物の API の確かめをすることがある。asterism-games の作業コピーを書き換える前に `git status` が clean かを見て、自分の作業中は image の名前に `gs7` を入れる。終わったら作ったものを名前で消す。前からある image は残す。docker の prune はしない。
- **止まる条件**: gem に入れる・入れないの境で、1 章に無い仕様の選択が要る (例: 麻雀も gem にしないと動かない)。asterism の変更が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に、各選択肢で何が変わるか一言) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs7.md`、常体): gem に入れたもの・入れないものとその理由、依存の版、gem だけで動かした結果、CI の形、受け入れ条件ごとの結果、残ったこと (ユーザがすること: gem の公開、タグ、ghcr の公開設定)。
