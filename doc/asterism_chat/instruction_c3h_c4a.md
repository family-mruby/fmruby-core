# C3h・C4a 指示書: チャットホストのスタンプの受け渡しと、VPS に置くための一式 (gem の側だけ)

> 2026-11-21 | 親セッションからサブ (Opus) への指示。**fmruby-core の checkout では別のサブが作業中なので、
> fmruby-core は読むだけ** (ビルド・sim・コミットをしない)。先に読むもの: fmruby-core の doc/asterism_chat/plan.md
> (決定 1〜21)、report/c0.md (3 章のスタンプ・1.5 の ACL の見込み。**線の形の正**)、report/c1.md・c2.md、
> `../asterism-chat` の全部、手本の `../asterism-tutor` (`deploy/vps/` の Dockerfile・compose 2 つ・env.example・
> update.sh・README、`docs/install.md` の役割と配置の節、`.github/workflows/`、CHANGELOG の 0.2.0〜0.2.2)。
> fmruby-core の doc/ai_tutor_vps/plan.md と report/v1.md (役割 tutor-host・tutor の行と、VPS の置き方の経緯)。

作業場所は `../asterism-chat/` (直下の隣) だけ。

## 1. スタンプ (ホストの側。c0.md 3.2・3.3)

- 発言にスタンプを載せる: `post({"room", "stamp" => sha, "cid"})` (文字とスタンプのどちらか)。ホストがその sha の絵を
  持っていなければ `{"error" => "no_stamp"}`。発言の形に `"stamp" => sha` が入る (文字の発言には無い)。
  履歴・知らせ・番号・1 日の上限の扱いは文字と同じ。
- `stamp_put({"sha", "png"})`: 絵を受ける。検査は c0 のとおり (PNG の先頭の印、IHDR の幅・高さが 48 以下、色の形、
  インターレースなし、8 KB 以下。PNG は解かない)。sha256 は受けた中身から計算し直して照らす。合わなければ断る
  (誤りの種類を足す: bad_stamp・too_big など。README の表に足す)。
- `stamp_get({"sha"})`: 絵を返す。無ければ `no_stamp`。
- 置き場は `state_dir/stamps/<sha>.png`。上限は枚数と合計の大きさ (既定 2,000 枚・16 MB。設定で変えられる)。
  上限を超えたら、**どの発言からも指されていない絵から先に**、古い順に捨てる (履歴から消えた発言の絵)。
  それでも超えるなら新しい絵を断る。1 日に 1 端末が置ける新しい絵の数にも上限 (既定は決めて README に)。
- バイナリの渡し方: 端末 (mruby) の文字列が msgpack でどう届くか (str か bin か) は端末の側で確かめる段 (C3) で決まる。
  ホストは **str でも bin でも受け、返すときの形は設定か引数で選べる**ようにしておく (既定は c0 の見立てに合わせる)。
  試験用の送り手 (`test/sender.rb`) にスタンプを送る・取るを足す。
- 記録 (JSONL): 絵の中身は残さない (sha・大きさ・端末だけ)。発言の本文を残さないのは今のまま (決定 16)。
- 自動の試験: 送る → 別の送り手が取って同じ中身、同じ絵の 2 回目は置き直さない、大きすぎる・48 を超える・PNG でない・
  sha が合わない、の断り、再起動の後も絵が残る、上限での捨て方、指されている絵は捨てない。

## 2. VPS に置くための一式 (asterism-tutor 0.2.2 の形を写す)

- `deploy/vps/`: Dockerfile (`ruby:3.4.13-slim`)、`docker-compose.yml` (project 名 `asterism-chat`、image
  `ghcr.io/ruby-asterism/asterism-chat`、`restart: unless-stopped`、healthcheck は `asterism-chat status`、
  状態の volume、console の network への入り方は tutor と同じ)、`docker-compose.build.yml`、`env.example`
  (`ASTERISM_CHAT_*`。部屋の一覧の書き方、TZ)、`update.sh` (**状態の問い合わせは `</dev/null` つき**。
  `--sudo`・`--when-idle` に当たるものが要るかは、チャットに「進行中の仕事」が無いので決めて README に書く)、
  `credentials/` の置き方と `.gitignore`、`deploy/vps/README.md`。
- `docs/install.md` (英語): console の役割 2 つ (**chat-host**: 自分のノードの下の公開と要る読み。**chat**: ホストの
  `room` を呼べる・`news` を購読できる。「Browsers have this role」は off、ブラウザは利用者ごと) の行を、tutor の
  行を写して具体に書く。証明書の入れ方、`.env`、起こし方、更新、確認 (`status`)、止め方、ブラウザの利用者の扱い。
  README.ja.md にも要点。
- イメージを作る CI (`.github/workflows/`): tutor と同じ形でファイルだけ置く (**GitHub のリポジトリは無いので動かない。
  push しない**)。
- 手元で確かめる: `docker compose -f docker-compose.yml -f docker-compose.build.yml build` でイメージができ、
  ローカルの試験用のルータ (tutor の rehearsal の形があればそれ) につないで `status` が up になる所まで。
  mTLS の console の rehearsal まで行けるならそこまで (行けなければ、どこまで確かめたかを書く)。
- 版: 0.1.0 のまま CHANGELOG に足すか、スタンプと配置で 0.2.0 にするかは任せる (CHANGELOG に書く)。

## 3. 受け入れ条件

1. `bundle exec rake test` が通る (1 章の試験が入っている)。
2. イメージが手元で作れて、コンテナが up・healthy になり、`status` がつながりを言う。SIGTERM で終了コード 0。
   volume を残して起こし直すと履歴とスタンプが残る。
3. `docs/install.md` の役割の行が、tutor の行と同じ形で具体に書いてある (推測で書いた行には印を付ける)。
4. 実在のドメイン・IP・端末の ID・ローカルの絶対パスが無い。コードのコメントは ASCII だけ。
5. 起こしたコンテナ・ルータは止めて消してある。`git status` に取りこぼしなし。

## 4. 範囲と止まる条件

- 触ってよい所: `../asterism-chat/` (直下の隣) の中だけ。その中でコミットする (英語、
  `<type>(<place>): <summary>`、Co-Authored-By のトレーラ)。**push しない。GitHub のリポジトリを作らない。**
- report は `asterism-chat/` の外に書けないので、**最後の返事に全部書く** (親が fmruby-core の report にまとめる)。
- **触らない**: fmruby-core (読むだけ。ビルド・sim・web・コミットをしない)、asterism-tutor・asterism-games・console・
  asterism 本体 (読むだけ)、VPS・端末・本物の鍵。sim の zenohd (`fmruby` の compose のもの) や、ほかのサブが使っている
  コンテナ・ポート (8006・7447・7449) に触らない。試験のルータは自分のポート (17530 など) で起こし、終わったら止める。
- 止まる条件: 線の形を c0 から変える必要が出た (発言の形・誤りの形)。asterism 本体や console の直しが要る。
  ユーザの作業が要る。選ばずに、選択肢にして返す。
- 呼び名: 「チャットホスト」「端末」。
