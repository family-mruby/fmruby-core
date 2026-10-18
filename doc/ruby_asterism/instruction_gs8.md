# GS8 指示書: AI の 1 日の上限を種類ごとに

> 状態: 指示 | 更新: 2026-10-18 | ゲームホストの AI プレイヤーの 1 日の上限 (今は Claude と Jev の合計 1 つ) を、AI の種類ごとに持てるようにする。Jev はとても安いので Claude と別枠にしたい

前提: asterism-games 0.14.0 (`lib/asterism_games/ai.rb` の Settings と Budget、`ai_hub.rb` の step (上限に届くと全員 CPU に)、`host.rb` と `rooms.rb` の Budget の作り方と @status の summary、README の AI の節、deploy/vps/env.example、CHANGELOG、update.sh)。

言葉: 文書は常体、「確かめ」ではなく「確認」。asterism-games の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

- **種類ごとに上限**: `ASTERISM_GAMES_AI_DAILY_USD_<KIND>` (例 `_CLAUDE=10`、`_JEV=2`) で種類ごとの 1 日の上限。届いた種類だけが止まり、その種類の席は CPU が打つ。ほかの種類は続ける。
- **後方互換**: 種類ごとの指定が無い種類は、今の合計の上限 `ASTERISM_GAMES_AI_DAILY_USD` に従う (今と同じ判定: その日の全種類の合計が届けば止まる)。どの種類にも種類ごとの指定が無ければ、今とまったく同じ動き。
- VPS の設定は Claude $10、Jev $2 にする予定 (VPS の変更はユーザ。親が scripts を渡す)。

## 2. 作るもの

1. **Settings**: 種類ごとの上限を読む (環境変数の `<KIND>` は種類の名前の大文字。今の種類は claude と jev)。値が数でない・負なら無視して合計の上限に従う (起動の log に一言)。
2. **Budget**: `over?(kind)` (その種類が止まるか)。種類ごとの上限がある種類は、その種類のその日の額で判定。無い種類は今どおり合計で判定。`usd_today(kind)` など必要な読み出し。記録の形 (JSON の days → kind) は変えない。
3. **Hub**: 上限に届いた種類だけを止める。その種類の席を CPU にし (今の `ai_off!` と同じ理由の出し方で、どの種類か分かるように)、届いていない種類には新しい要求を出し続ける。log は種類ごとに 1 回 (「today's limit for claude ($10) is reached」の形)。日付が変わったら戻る (今と同じ)。
4. **@status / summary**: 今の合計 (`usd`・`limit_usd`) は残す。種類ごとの額と上限 (`kinds` の中か、別の欄) を足す。fmruby-core の画面 (成績・卓の一覧など) が今の欄を読んでいる場合に壊さない。
5. **起動の log**: 「AI players: claude, jev (at most $10 a day for claude, $2 for jev; ...)」のように、種類ごとの上限が分かる形。
6. **試験**: 種類ごとの上限で片方だけ止まる・もう片方は続く、指定の無い種類は合計に従う、指定がまったく無いときは今と同じ、日付が変わると戻る、不正な値は無視。
7. **文書**: README の AI の節、deploy/vps/env.example (`ASTERISM_GAMES_AI_DAILY_USD_CLAUDE` / `_JEV` の例と説明)、CHANGELOG の **0.15.0** (VERSION も 0.15.0。版の上げ方は既存の流儀)。更新の手順 (update.sh 0.15.0、.env に 2 行足す) を書く。

## 3. 受け入れ条件

1. asterism-games の試験全部と rubocop (使っていれば) が通る。
2. 手元で、偽の AI (`ASTERISM_GAMES_AI_FAKE`) と小さな上限 (例 claude だけ $0.0001) で卓を動かし、claude の席だけが CPU に代わり jev の席は AI のまま、を log と @status で確認する (外の API は呼ばない)。
3. 種類ごとの指定を外すと 0.14.0 と同じ動き。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は report だけ (ファイルとして書くだけ。fmruby-core の git 操作はしない。親がコミットする)。
- **触らないもの**: asterism・asterism-console・riichi_engine・fmruby-core のコード、VPS、GitHub の設定。
- **外の API を呼ばない** (Claude・Jev の鍵を使わない。偽の AI で試す)。
- コミットのみ (asterism-games)。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査 (ドメイン・アドレス・端末 ID・鍵) をしてからコミットする。
- 終わったら起こしたもの (試しのホスト、zenohd、image、volume) を名前で止めて消す。前からある image は残す。docker の prune はしない。親が Tab5 (シリアル、WiFi) を使うので触らない。
- **止まる条件**: 1 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs8.md`、常体): 設定の形、判定、@status の欄、受け入れ条件の結果、VPS の更新に要ること。
