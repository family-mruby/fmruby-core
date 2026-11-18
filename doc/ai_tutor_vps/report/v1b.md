# V1b 報告: 1 日の区切りを日本時間に、止めたせいの失敗を回数に数えない

> 状態: 完了 | 更新: 2026-11-19 | asterism-tutor 0.2.0 のまま (ブランチ vps-container に 2 コミット、未 push)。コンテナの `TZ` の既定を `Asia/Tokyo` にし (plan 2 章の決定 11)、`stopped` の頼みを 1 日の回数から外した (決定 12)。試験を足し、`bundle exec rake test` は通った

指示は親の V1 検収の後の伝言 (V1b)。決定の正は plan.md の 2 章の 10〜13。

## 1. 1 日の区切り (決定 11)

- `deploy/vps/docker-compose.yml` の `environment` に `TZ: ${TUTOR_TZ:-Asia/Tokyo}` を足した。`env.example` は `TUTOR_TZ=Asia/Tokyo`。
  - 変数の名前を `TZ` でなく `TUTOR_TZ` にしたのは、compose の `environment` が `env_file` (.env) より強く、`${TZ}` と
    書くと compose を流す側のシェルの `TZ` も拾ってしまうため。`.env` に `TZ=` の行を書いても使われないことを
    compose の注記・env.example・docs/install.md に書いた。
  - `docker compose config` で、`.env` が無くても env.example を渡しても `TZ: Asia/Tokyo` になることを確かめた。
- gem のコードは変えていない (上限の「1 日」は前から `Time#strftime` の地方時の日付、記録のファイル名も同じ)。
  それが `TZ` に従うことを試験で確かめた: `test_the_day_of_the_limits_and_the_records_follows_tz`。UTC の 14:50 と
  15:10 (日本の 23:50 と翌 0:10) に 1 回ずつ頼むと、`Asia/Tokyo` では記録のファイルが 2 日に分かれ回数は 1、
  `UTC` では 1 ファイルで回数は 2。起こし直して記録から読み戻しても同じ。記録の時刻も `+09:00` で書かれる。
- docs/install.md・README.md・README.ja.md・CHANGELOG を合わせた。

## 2. 止めたせいの失敗を数えない (決定 12)

- `Jobs#stopped` (SIGTERM の猶予切れと、次の起動で失敗にしたもの) が `Limits#uncount(device, 頼んだ時刻)` を呼ぶ。
  頼んだのが今日 (TZ の日付) のときだけ 1 減らす (昨日の頼みを今日の回数から引かない。0 より下にしない)。
- 起動時に記録から回数を読み戻すとき、`failed` で `error: "stopped"` の頼み (ticket) の `ask` を数えない。
- 費用は今のまま数える: LLM を呼んだ分は `llm` の行として記録に残っていて、それを足す。
- 試験 (test/test_container.rb、e2e):
  - 1 日 1 回の設定で、検査に落ち続ける作成 (`[[regexp_always]] [[slow]]`) の 1 回目の答えが来た後に止める →
    回数は 0 に戻り、費用は残る。起こし直しても同じで、その日の 1 回がまだ使える。
  - kill された gem が残した進行中の頼み → 次の起動で回数は 0。
  - 昨日の頼みは今日の回数から引かない、0 より下にしない。
  - e2e の SIGTERM と起こし直しの試験に、止められた端末の `hello` の `used` が 0 であることを足した。
- `bundle exec rake test`: unit 40 runs 271 assertions、e2e 25 runs 179 assertions、失敗 0。

## 3. 確認していないこと

- 実際のコンテナで日付をまたぐ通し (試験は時刻を差し替えて確かめた)。
- 本物の LLM での費用の数え方 (偽物の LLM の usage で確かめた)。

## 4. コミット

asterism-tutor (ブランチ `vps-container`):

- `feat(tutor): a request failed by the stop is not counted in the day's requests`
- `feat(tutor): the container's day is Japan's by default`
