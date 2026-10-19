# GS13 指示書: オセロの卓に AI の席 (Jev)

> 状態: 指示 | 更新: 2026-10-19 | 麻雀の卓には AI の席 (Claude・Jev) があるが、オセロの卓は CPU だけ。サーバにつないだときにオセロでも Jev と対戦できるようにする。ゲームホスト・`play othello`・端末のオセロアプリの 3 つ

前提:
- asterism-games main (未 push の 0.17.0、17accb4)。AI の席の仕組みは `lib/asterism_games/ai.rb` (Settings・Budget・Fake・Claude・Jev。decider は `"game"` を載せた request に手の番号で答える)、`ai_hub.rb` (Hub: 卓の外で問い合わせ、時間切れ・失敗は CPU)、麻雀の卓の AI の席 (`app/asterism_games/mahjong/table.rb`: `cpu(token, seat, on)` の on に種類、view の `"ak"` / `"ai"`、@status の `ais`)。オセロは `lib/asterism_games/othello/{table,board,remote}.rb` (CPU は `Board::Cpu`)、`play/othello.rb` (GS12 の 1 キー操作)。README「AI players」。
- fmruby-core develop。オセロアプリ `flash/app/game/othello/othello.app.rb` (Asterism の卓の一覧・待ち合わせ)。麻雀アプリの AI の選び方 (`flash/app/game/mahjong/mahjong.app.rb` の `ai_kinds` / `ai_label`、`mahjong_panels.rb` の席の選び方 `["", "cpu"] + ai_kinds`) が手本。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-games の文書は英語。

## 1. 決まっていること (2026-10-19 ユーザ、選択形式)

1. オセロで座らせられる AI は **Jev だけ** (ホストに Claude のキーがあってもオセロでは出さない)。
2. **端末のオセロアプリと `play othello` の両方**から AI を座らせられる。端末のアプリは麻雀アプリの AI の選び方に合わせる。
3. AI が時間内に答えない・失敗した・上限額を超えたときは、**麻雀と同じく CPU が代わりに打つ** (対局は止まらない、上限を超えた日は CPU に戻して知らせる)。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **ホスト**: オセロの卓も麻雀と同じ形にする。`cpu(token, seat, on)` の on に種類 (`"jev"`)、view に `"ak"` (その卓で選べる種類。オセロは Jev のキーがあるときだけ `["jev"]`、無ければ空) と席ごとの `"ai"`、@status に `ais`。ゲームごとに選べる種類を決める所を 1 か所に置く (麻雀は今のまま claude・jev)。
- **問い合わせ**: Hub をオセロの卓にも回す。request の `"game" => "othello"`、`"instructions"` (オセロの決まりと view の読み方の英文)、`"view"` に番号つきの手 (`"moves"`: 置ける升の一覧)。答えの番号が範囲外・時間切れ・失敗はその手を `Board::Cpu` で打つ。待つ時間は麻雀の `ai_turn_ms` に相当する設定を卓の opts に (既定は麻雀と同じ程度)。費用は麻雀と同じ Budget で合算し、1 局あたりの呼び出し上限 (`calls_per_game`) も効かせる。
- **見せ方**: 名前は麻雀と同じく `Jev` (AI::LABELS)。AI の席が考えている間はそれと分かる表示を (play と端末のアプリ)。
- **`play othello`**: 待ち合わせのキーに AI を足す (卓の主。例: `A` で空き席に Jev、もう一度で CPU に戻す、などアプリと考え方をそろえる)。`"ak"` が空なら出さない。案内は英語のまま (GS12 のユーザの決定)。
- **端末のオセロアプリ**: 待ち合わせ画面で、空き席を CPU にする操作の隣に AI (Jev) を選べるようにする。`"ak"` が空なら今の画面のまま。Retro (320x240) と Modern (426x240、640x360) の両方で崩れないこと。内蔵 RAM を増やさない (アプリの Ruby だけの変更なら問題ないはず)。
- **古いものとの組み合わせ**: 新しいアプリが古いホスト (`"ak"` が無い) につないでも今どおり動く。古いアプリが新しいホストにつないでも今どおり CPU で遊べる。
- 外の API は**試験でも確認でも呼ばない**。`ai.rb` の Fake (環境変数で Fake の decider を使う仕組み) で試す。

## 3. 作るもの

1. asterism-games: オセロの卓の AI の席、Hub の対応、ゲームごとの種類、README「AI players」とオセロの節、CHANGELOG の 0.17.0 に足す (版は 0.17.0 のまま)。
2. asterism-games の試験: Jev の席を置く・外す、Claude は断られる (`"no ai"`)、キーが無いと `"ak"` が空、Fake の decider の答えで打つ、範囲外・時間切れ・失敗で CPU が打つ、上限で CPU に戻る、`ais` の数、`play othello` の AI のキー (キー → 命令の純粋な関数)。
3. fmruby-core: オセロアプリの待ち合わせで Jev を選べる。
4. 確認: 手元の zenohd + ゲームホスト (Fake の decider を Jev として出す設定) に対して、`play othello` (擬似端末) と Linux sim のオセロアプリ (標準構成。fmrb MCP の sim_up / sim_app / sim_input / sim_screenshot) の両方で、Jev の席を置いて 1 局を終局まで。sim の画面の写しを report に。sim の検証にはエディタを 1 回起動して 1 打鍵する確認も含める (fmruby-core/CLAUDE.md)。ブラウザ版は確認しなくてよい。

## 4. 受け入れ条件

1. `MJ_GAMES=4 bundle exec rake test` (asterism-games) と fmruby-core の `rake test` が通る。
2. 上の確認が通り、report に写す (擬似端末の画面と sim の画面)。
3. 古いものとの組み合わせ (新しいアプリ × 今の VPS と同じ 0.16.2 のホスト、など手元で再現できる範囲) で動く。

## 5. 範囲・決まり

- **触ってよいもの**: asterism-games (main)、fmruby-core (develop、オセロアプリと report)。
- **触らないもの**: asterism・asterism-console・riichi_engine・fmruby-graphics-audio、fmruby-core の麻雀アプリ (読むだけ)、牌のシート、VPS、GitHub の設定、Tab5。
- 手元で親が動かしているもの (zenohd コンテナ `try-zenohd`、`/tmp/try-games` のゲームホスト) には触らない。自分の試験には別の名前・別のポートで起こし、終わったら名前で止めて消す (sim は sim_down)。docker の prune はしない。
- 外の API を呼ばない。push・タグはしない。`git commit -a` は使わない。コミットは各リポジトリで (fmruby-core の件名は `game: ...` など履歴に合わせる、本文英語)。末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。fmruby-core の doc (この指示書・report) はコミットしない (親がする)。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs13.md`、常体): ホストの変更 (view・@status・設定)、play とアプリの操作、試験と確認の結果、VPS に出すとき要ること (Jev のキーの設定の有無など)。
