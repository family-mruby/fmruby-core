# GS5b 指示書: Jev を TypeSafe の直接の API で呼ぶ

> 状態: 指示 | 更新: 2026-10-18 | GS5 で Cloudflare Workers AI 経由にした Jev のつなぎを、TypeSafe の直接の API に替える (2026-10-18 ユーザ決定、選択形式)

前提: report/gs5.md (判断のつなぎの形、Jev のつなぎ、費用の数え方)、asterism-games の lib/asterism_games/ai.rb・README の「AI players」・docs/install.md の「AI players」・deploy/vps/env.example、TypeSafe の公式の文書 (Quick Start: base URL `https://api.typesafe.ai`、`POST /v1/systemone`、`Authorization: Bearer <API_KEY>`、鍵は console.typesafe.ai の Keys、環境変数の例 `TYPESAFE_API_KEY`、request は `state`・`model` (例 `jev-latest`)・`questions` (choice / score / noul))。

言葉: 文書は常体。asterism-games の文書とコードのコメントは英語。

## 1. 決まっていること

- Jev は TypeSafe の直接の API で呼ぶ。鍵は `TYPESAFE_API_KEY` だけ。Cloudflare 経由のつなぎと設定 (`CLOUDFLARE_ACCOUNT_ID`・`CLOUDFLARE_API_TOKEN`) は消す。
- ほかの GS5 の決まり (卓の主が入れる、費用の上限、AI の席の持ち時間、AI に渡すのはその席に見える範囲だけ、鍵をログ等に出さない) はそのまま。

## 2. 作るもの

1. Jev のつなぎを直接の API に (request / response の形は公式の文書の API reference を読んで合わせる。使ったトークン数の欄 `usage` で費用を数える)。モデル名は設定で変えられ、既定は文書の推奨 (例 `jev-latest`) に。
2. 単価: 直接の API の料金を公式の文書かサイトで確かめ、既定値を合わせる (確かめた日付を report に)。分からなければ今の値 (入力 0.042 USD / 100 万トークン) のままにし、そう書く。
3. 試験 (偽物の HTTP で: 成功・失敗・時間切れ・usage の数え方・鍵を出さない)。
4. 文書: README・docs/install.md (鍵の作り方: console.typesafe.ai の Keys)・env.example・CHANGELOG。VERSION は asterism-games 0.11.1 (GS6 の 0.11.0 の後)。

## 3. 受け入れ条件

1. `MJ_GAMES=8 bundle exec rake test` が通る。
2. 環境変数 `TYPESAFE_API_KEY` がこの作業の環境にあるときだけ、本物の API で Jev の AI を卓に入れて東風戦を 1 回 (トークン数・額・1 手の時間)。無ければ保留と report に書く (鍵を探さない)。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は文書 (report) だけ (develop に直接)。
- コミットのみ。push・タグはしない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。鍵と実在の値の検査をしてからコミットする。
- **止まる条件**: 直接の API の形が文書から分からない、鍵の種類が違う (例: 組織の id も要る) など、1 章を越える選択が要る。
- **report** (`doc/ruby_asterism/report/gs5b.md`、常体): 直接の API の形、単価と確かめた日付、結果。
