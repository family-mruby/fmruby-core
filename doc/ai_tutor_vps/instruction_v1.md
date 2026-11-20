# V1 指示書: asterism-tutor をコンテナで動く形にする

> 2026-11-19 | 親セッションからサブ (Opus) への指示。計画の正は plan.md (2 章の決定 1〜9 を含めて全部)。
> 先に読むもの: plan.md、直下の `asterism-tutor/` の README.md と `lib/tutor/`、手本の
> `asterism-games/deploy/vps/` 一式・`asterism-games/docs/install.md`・`.github/workflows/image.yml`、
> `asterism-console/deploy/local/` と `docs/` (手元の stack・役割・台帳・証明書の出し方)、
> doc/ai_coding/report/a2.md (sim で AI ヘルパーを通す手順)。

## 1. 作るもの (asterism-tutor。ブランチ `vps-container` を main から切る)

**リポジトリは公開済み**。コミット・文書に、実在の IP・ドメイン・端末の ID・ローカルの絶対パス・鍵を
書かない (`router.example.org`・`192.0.2.x`・`fmruby-aaaaaa`・相対パス)。コメントは ASCII だけ (英語)。

1. **環境変数の設定**: `.env` だけで動くように、設定の項目を環境変数でも渡せるようにする
   (名前は `ASTERISM_TUTOR_*` の流儀。asterism-games の `ASTERISM_GAMES_*` に合わせる)。対象: ルータ、
   証明書 (.tar)、ノード ID、端末の一覧、LLM の種類とモデル、上限 2 つ、記録の置き場、状態の置き場、
   `modes.make`・`modes.fix`。優先は オプション > 環境変数 > ファイル > 既定。LLM の鍵は今のまま
   `ANTHROPIC_API_KEY` だけ。起動時の 1 行 (モデルと上限) は今のまま出す。
2. **状態を volume に**: コンテナを作り直しても、記録・取りに来ていない結果・その日の回数と費用が残ること。
   今の gem がどれをファイルに持ち、どれをメモリだけに持つかを洗い、足りない分を足す (置き場は 1 つの
   ディレクトリにまとめられる形)。会話の続き (30 分で忘れるもの) は残さなくてよい。
3. **status**: ルータにつながり helper を公開できているかを返す口と、それを見る
   `asterism-tutor status` (healthcheck 用。つながっていなければ 0 以外で終わる)。作りは asterism-games の
   `status` を手本にする (外へ port を開かない)。
4. **SIGTERM**: 進行中の頼みをどうするかを決めて実装する (親の見立て: 新しい頼みを断り、進行中のものは
   短い猶予の間待ち、終わらなければ「AI ホストが止まった」の失敗として残す。端末は後で `pending`・`result` で
   読める)。見立てと違う作りが良ければ、理由を report に書いてそちらで作ってよい。
5. **`deploy/vps/`**: `Dockerfile` (ruby slim、2 段、uid 1000 で動く)、`docker-compose.yml`
   (project `asterism-tutor`、console の `<project>_router` の network に名前で入る、volume `state`、
   `./credentials:/credentials:ro`、`mem_limit` は実測して決める、ログの回し、healthcheck、
   `stop_grace_period`)、`docker-compose.build.yml`、`env.example`、`update.sh`、`README.md`、
   `credentials/.gitignore`。`docs/install.md` に、console の隣に入れる手順 (台帳の行・役割・証明書・
   .env・起こす・更新・戻す・記録の読み方) を書く。
6. **イメージの CI**: `.github/workflows/image.yml` (タグ `v*` で amd64 のイメージを
   `ghcr.io/ruby-asterism/asterism-tutor` へ。タグと VERSION の一致の確認。手で流すと作るだけ)。
   ブランチの push で走る CI は足さない。**タグは打たない。push しない。**
7. 版を 0.2.0 にし、CHANGELOG.md・README.md・README.ja.md を合わせる。

## 2. 役割の鍵式 (console の画面で作るもの。コードは変えない)

- 役割 **tutor-host** (AI ホスト) と **tutor** (使う端末) の ACL の行を決める。今の gem と端末 (AI ヘルパー) が
  実際に使う鍵 (queryable・@profile・@status など) を、手元の mTLS のルータで洗う。
- **役割 tutor の無い端末からは呼べない**ことを、手元の stack で確認する。
- console のコードの変更が要ると分かったら、止まって返す。

## 3. 端末の種類ごとの確認 (調べるだけ。端末の側は直さない)

- sim (Modern、標準構成): 手元の console の stack のルータに証明書でつないだ tutor のコンテナ
  (`--llm fake` 相当) に対して、AI ヘルパーから 質問・ヒント・作成・修正 を 1 回ずつ。
  sim の端末が mTLS のルータにつなぐ手順が重い場合は、tutor のコンテナ側の確認を `test/sender.rb` 相当
  (証明書つき) で行い、sim は a2.md の素の TCP の手順で gem の変更後の回帰として 1 回通す、でよい
  (どちらで確認したかを report に書く)。
- **ブラウザ版・ネイティブ版**: AI ヘルパーが今どこまで動くかを調べる (アプリが束・storage に入っているか、
  起動するか、Asterism の経路で `*/tutor/helper` を探せるか、エディタの口が動くか)。動かない所は、
  原因と直しの見立てを report に書く (**直さない**。ユーザが決める)。

## 4. 受け入れ条件

1. gem の試験が通る (`bundle exec rake test`)。足した環境変数・状態・status・SIGTERM に試験がある。
2. `deploy/vps` の compose (build 版) で起こしたコンテナが、手元の console の stack のルータに
   証明書でつながり、healthy になる。ルータを止めると unhealthy になる。
3. 4 つの頼みが通る (3 章のどちらかの形)。役割の無い ID からは届かない。`devices` に無い ID は gem が断る。
4. コンテナを作り直した後も、記録・取りに来ていない結果・その日の回数と費用が残っている。
5. イメージの大きさと、動いているときの RSS を report に書く (`mem_limit` の根拠)。
6. 鍵が、イメージ・記録・ログに出ていない (偽物の鍵の文字列を環境変数に入れて起こし、grep で確かめる。
   **本物の鍵・鍵のファイルは使わない。`--llm fake` だけ**)。
7. 終わったとき: 起こしたコンテナ・stack・sim・ブラウザ版・ネイティブ版は止めてある。手元の console の
   stack に足した台帳の行・役割は、足したものを report に書く (消せるなら消す)。各リポジトリの
   `git status` に取りこぼしが無い。

## 5. 範囲と止まる条件

- 触ってよい所: `asterism-tutor/` (ブランチ `vps-container`)、fmruby-core の
  `doc/ai_tutor_vps/report/v1.md` と `rake docs:index` の結果 (develop に直接コミットしてよい。文書だけ)。
- **触らない**: 端末の側 (fmruby-core のコード・flash/)、graphics-audio、console・asterism・
  asterism-games のコード、VPS、P4-Nano (焼かない・ファイルを送らない)、ユーザの鍵と設定
  (`~/asterism_keys/`・`~/.config/fmrb/`)、VPS 用の scripts (V2 で親が書く)。
  手元の console の stack を起こすときは、ユーザのデータがある既存の volume を壊さない
  (既に動いているものがあれば、そのまま使うか、別の project 名で起こす。消さない)。
- **push しない。タグを打たない。main へマージしない。** `git add -A` を使わない。
  コミットの書き方は fmruby-core/CLAUDE.md (`feat(tutor): ...` など)、末尾に
  `Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>` 相当の自分の行。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る (console のコード、端末の側)。
  **利用者から見た動きを、plan・この指示書に無い形で変える必要が出たとき** (選ばずに止まる)。

## 6. report (fmruby-core doc/ai_tutor_vps/report/v1.md)

- 環境変数の一覧、状態の置き場の作り、status と SIGTERM の作り、compose の形、`mem_limit` の根拠。
- 役割 2 つの ACL の行 (そのまま console の画面に入れられる形) と、洗った鍵の一覧。
- 受け入れ条件 1〜7 の結果と根拠。
- ブラウザ版・ネイティブ版の現状と、直しの見立て。
- 自分で決めた細部、見立てと違った点、確認していないこと、ユーザに決めてほしいこと、
  V2 (VPS の手順) に要る材料、コミットの一覧。
