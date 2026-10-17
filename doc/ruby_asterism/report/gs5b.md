# GS5b 報告: Jev を TypeSafe の直接の API で呼ぶ

> 状態: 完了 (検収待ち、本物の API での確かめは保留) | 更新: 2026-10-18 | instruction_gs5b.md の結果。Jev のつなぎを Cloudflare Workers AI 経由から TypeSafe の直接の API (`POST https://api.typesafe.ai/v1/systemone`) に替え、鍵を `TYPESAFE_API_KEY` だけにした。asterism-games 0.11.1。止まる条件には当たっていない (API の形は公式の API reference に揃っていて、鍵は Bearer の 1 種類だけ)。鍵がこの環境に無いので、本物の API での東風戦は保留

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism-games | main `230a5ca` | games 0.11.1: `ai.rb` の `AI::Jev` を直接の API に、`Settings` の鍵と既定のモデル、試験 (偽物の HTTP)、README・docs/install.md・deploy/vps/env.example・CHANGELOG.md (新規)・VERSION |
| fmruby-core | develop (この報告) | この報告だけ |

push・タグはしていない。コミットの前に差分を鍵の形 (`sk-ant`・`TYPESAFE_API_KEY=` の値・長い Bearer)・IP の形・メールアドレスで検査した。出てくるのは試験の偽の鍵 (`ts-key-test-...`) と試験の偽物のサーバの `127.0.0.1` だけ。

## 2. 直接の API の形 (公式の文書で確かめた、2026-10-18)

読んだもの: docs.typesafe.ai の Quick start、API reference (`/api`)、Models (`/models`)。

- 要求: `POST https://api.typesafe.ai/v1/systemone`、`Authorization: Bearer <API_KEY>`、`Content-Type: application/json`。本文は `state` (文字列・object・配列、必須)、`model` (必須、推奨は `jev-latest`)、`questions` (名前 → 問い)。
- choice の問い: `{"type": "choice", "instructions": ..., "criteria": {選択肢の鍵: 説明}}`。選択肢は 255 個まで (麻雀の手は多くて 20 ほど)。
- 答え: `{"model": "jev-1.13.0", "answers": {"move": {"type": "choice", "choice": 鍵, "probabilities": {...}, "confidence": ...}}, "usage": {"input_tokens", "output_tokens"}}`。Cloudflare の `result` の包みは無い。
- 失敗: 400 (規則違反、文脈長の超過 `max_tokens_exceeded`)、401 (鍵)、413、415、422 (検証)、429 (レート)、529 (混雑)。いずれも本文は JSON。
- 鍵: console.typesafe.ai の Keys で作る 1 本だけ。組織の id などほかの値は要らない (レートの上限は組織ごとだが、要求には出てこない)。
- モデル: `jev-latest` は今 `jev-1.13.0` を指す。`jev-preview` は `jev-1.14.0` (2026-10-14 から)。版つきの id を送れば別名の移動の影響を受けない。

## 3. 作ったもの

- `AI::Jev.new(key, model: "jev-latest", base: "https://api.typesafe.ai")`。要求は `{"state" => look から moves を除いたもの, "model", "questions" => {"move" => choice}}`、criteria は手の番号 (文字列) → 手の文。答えの `answers.move.choice` を手の番号にし、`usage` の `input_tokens` / `output_tokens` を費用の数えに渡す。手に無い番号は `TokenError` (トークンは数える、CPU が打つ)。失敗は `jev: HTTP <番号>`・`jev: not JSON`・`jev: <例外のクラス名>` だけで、鍵も本文も出さない。`base` は試験のためで、設定にはしていない。
- `Settings`: Jev は `TYPESAFE_API_KEY` があるときだけ出す。`CLOUDFLARE_ACCOUNT_ID`・`CLOUDFLARE_API_TOKEN` は読まない (残っていても Jev は出ない)。`ASTERISM_GAMES_JEV_MODEL` の既定を `jev-latest` に。
- 持ち時間: 前と同じく残りの持ち時間を `read_timeout` (と `write_timeout`) に、`open_timeout` は 5 秒まで。SDK の自動の再試行 (429・529) は持ち時間が短いので入れていない (失敗はその手だけ CPU が打つ)。

## 4. 単価

公式の Models の頁 (2026-10-18 に確かめた): Jev 1.14・1.13 とも 42 USD / 10 億トークン = **0.042 USD / 100 万トークン、入力のみ課金、出力は無料**。今の既定値 (入力 0.042、出力 0) と同じなので値は変えず、コメントと README に確かめた日付を書いた。

## 5. 受け入れ条件ごとの結果

### 5.1 試験 (条件 1)

`MJ_GAMES=8 bundle exec rake test`: **194 runs、37,997 assertions、0 failures、0 errors**、skip 2 (ルータなしの 2 本、前と同じ)。

足した試験 (`test_ai_players.rb` の `test_the_jev_request`、127.0.0.1 に立てた偽物の HTTP サーバ相手に本物の Net::HTTP の道を通す):

- 成功: `POST /v1/systemone`、`Authorization: Bearer <鍵>`、本文の鍵が `model`・`questions`・`state` の 3 つ、既定のモデル `jev-latest`、state に moves が無い、criteria が番号 → 文、本文に鍵が入っていない。答え `"choice": "1"` → 手 1。
- usage の数え方: `{"in" => 812, "out" => 21}` が Decision に入り、Budget で 812 × 0.042 / 10^6 USD (出力は 0)、日のファイルに calls 1・in 812・out 21。
- 手に無い choice: `TokenError` (`jev: move 7 is not offered`) でトークンは残る。
- 失敗: 429・529・422 → `jev: HTTP <番号>`、HTML の本文 → `jev: not JSON`、どれも鍵を含まない。
- 時間切れ: 3 秒遅れる偽物に持ち時間 0.5 秒 → `jev: Net::ReadTimeout`、3 秒より前に戻る。
- 鍵を出さない (前からの試験を直した): 401 → `jev: HTTP 401`、送った頭に Bearer が入っていることも確かめる。ログ・使用量のファイル・卓のまとめに鍵が無い。`TYPESAFE_API_KEY` だけで Jev が出て、Cloudflare の 2 つだけでは何も出ない。

### 5.2 本物の API での東風戦 (条件 2)

**保留**。この作業の環境に `TYPESAFE_API_KEY` が無い (探していない・作っていない)。鍵が渡されたら: deploy/vps/.env か作業の環境に `TYPESAFE_API_KEY` を置き、`ASTERISM_GAMES_AI_FAKE` を外してホストを起こし、卓の主が `+Jev` を入れて東風戦。トークン数・額は `bin/asterism-games status`、1 手の時間 (平均・最大) はホストを止めるときのログ (`AI players: ... N decisions, avg ms, max ms`) に出る。見積もり: look が 2.5 KB ほど (英語の JSON で 800 トークン前後) + 指示書 1.1 KB が毎回入力になる (キャッシュの割引は文書に無い) ので 1 手 1,000-1,300 トークン、0.00005 USD ほど。東風戦 1 回で 60-80 手なら 0.004 USD ほど。

## 6. 判断したこと・見立てと違った点

- CHANGELOG: asterism-games には CHANGELOG のファイルが無かった (GS5 の報告のとおり、版は README と VERSION で追っていた)。指示書が CHANGELOG を挙げているので `CHANGELOG.md` を新しく作り、0.11.1 から書いた (それより前は README の「games x.y.z」の記載を見る、と冒頭に書いた)。
- 既定のモデルは文書の推奨どおり `jev-latest`。別名は新しい版が出ると黙って動くので、答えの具合を固定したいときは `jev-1.13.0` のように版つきを設定する、と README に書いた。
- 更新の注意: VPS の .env に Cloudflare の 2 行だけがある状態で 0.11.1 にすると Jev は出なくなる (ボタンが消える。エラーにはならない)。CHANGELOG に書いた。
- Jev は英語が主で、日本語は精度が落ちると文書にある。look の中身は英語の欄名と牌の名前 (`1m` ほか) なので影響は小さいと見ている。

## 7. 残ったこと・ユーザの判断

- **本物の API での東風戦 (保留)**: 鍵が要る (5.2 の手順)。
- VPS の .env: 0.11.1 を入れるときに `CLOUDFLARE_*` の行を消し、`TYPESAFE_API_KEY` を足す (ユーザの作業)。
