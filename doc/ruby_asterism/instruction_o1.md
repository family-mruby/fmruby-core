# Asterism O1 指示書: asterism-console を誰でも自分のサーバに入れられる形にする

> 状態: 指示 | 更新: 2026-10-13 | asterism-console を、初めての人が自分のサーバとドメイン 1 つで、公開した image と一般の手順だけを使って立てられる OSS にする。作者の環境を前提にしない作り、一般の導入の手順、版ごとに公開する image、最初の設定、証明書を tar 1 つで渡し CRuby はその 1 ファイルで繋ぐ

前提: plan.md の「決まっていること」の先頭 (console は OSS として各自が入れる) と O1 の節 (1-6)、report/g0_w4.md・g1_5.md・g1_5b.md・g1_5b_p4.md、asterism-console の `deploy/vps/`・`deploy/local/`・`docs/`・`lib/relay/`・`bin/`、asterism の `lib/asterism/cruby/` (`Asterism.connect` の `config:`)。

言葉: fmruby-core の文書は日本語の常体、asterism-console・asterism の文書と画面は英語。「端末」を使い「機体」は使わない。

## 1. 決まっていること (2026-10-13 ユーザ)

- **作者の環境を前提にしない**: 既定値・画面・文書・コードから、作者のドメイン、作者の VPS の流儀、Family mruby のブラウザ版、アプリ名 `game` の固定、作者の名前の残り (`fmruby` を前提にした分岐など) を外す。プロフィールの `kind` (family_mruby・picoruby・cruby・console) のような網の決まりにある値は残してよい (網の仕様であって作者の環境ではない)。nginx・systemd は「例」として示し、別の置き方 (Caddy、素の docker compose) でも動くことを書く。
- **`deploy/vps/` の場所は変えない**: 作者の VPS は `asterism-console/deploy/vps/docker-compose.yml` を include している。名前を変えるなら、古い場所も残して移し方を書く (変えないのがおすすめ)。
- **一般の導入の手順** (英語、`docs/install.md` などを入口に): サーバの用意 (必要なメモリ・ディスクの目安を測って書く。2 GB の VPS でほかのサービスと同居している例がある)、DNS、`.env`、CA、起動、TLS の証明書、最初の管理者、最初のルータと端末、確かめ、更新、戻し方、控え、止め方 (止めたあと `docker ps` で残っていないことを確かめる。docker 本体が途中で起動し直すと、止まりきらなかったコンテナが restart の設定で戻ることがあった)。
- **版ごとに公開した image**: GitHub Actions で、タグ (`v*`) を打つと image を作って GitHub Container Registry (`ghcr.io/ruby-asterism/asterism-console`) に公開する workflow を用意する。amd64 と arm64。compose は `image:` で公開した image を引き、`.env` の版で選ぶ (手元で作る道も残す)。console の版 (`VERSION` や CHANGELOG) を置く。最初の版の番号は案を出す (0.1.0 など)。**タグを打つこと・公開することはしない** (ユーザが行う)。
- **最初の設定**: 最初の管理者 (今は手でコマンド)、CA、役割の初期の例、運用の形を、コマンド 1 つ (例: `bin/asterism setup`、docker の中で走る) か画面でたどれるようにする。運用の形は今あるもの (管理者だけが登録する) だけを選べる形にし、承認つき・自己登録 (G2 構想) は作らない (設定の場所だけ決めて文書に「将来」と書くのはよい)。
- **証明書を tar 1 つで渡す**: 発行の画面に「Save all (.tar)」を足す (今の 3 つの Save to file は残す)。tar は**ブラウザの中で**作る (鍵はブラウザで作ってサーバに送っていない)。中身は `<名前>.key` (600)・`<名前>.pem` (644)・`ca.pem` (644)・設定の例 `zenoh.json5` (家のルータ用と client 用の違いは案を決める)・`asterism.json` (名前、クラウドのルータの宛先、期限、作った console)。圧縮しない `.tar`。
- **CRuby は tar 1 つで繋ぐ**: asterism gem の `Asterism.connect` (と `Asterism::Zenoh.open`、`Asterism::ROS.connect`) に `credentials:` (tar のパス) を足す。gem が tar を読み (標準の範囲で)、zenoh の設定の `transport/link/tls/root_ca_certificate_base64`・`connect_certificate_base64`・`connect_private_key_base64` と `enable_mtls` に入れる (zenoh 1.10.1 にこの 3 つがあることは確かめてある)。一時ファイルは作らない。宛先と `node:` は `asterism.json` から既定で埋め、引数で渡せば上書き。tar がグループやほかの人に読める権限なら警告する。gem の版は上げない (公開はユーザ)。README と CHANGELOG の Unreleased に書く。
- **作者固有の手順は書かない**: 公開する文書には作者のドメイン・IP・端末の名前を書かない (`router.example.org`、`192.0.2.x`、`fmruby-aaaaaa`)。

## 2. 作るもの

1. **asterism-console** (main): 作者の環境の切り離し、`docs/install.md` ほかの導入の手順、`.github/workflows/` の image の workflow、compose の `image:`、`VERSION` と CHANGELOG、最初の設定のコマンド、発行の画面の tar、`docs/o1.md` (この段の設計)。
2. **asterism** (main): `credentials:`、試験 (手元の mTLS の zenohd に tar 1 つで繋ぐ)、README・CHANGELOG。
3. **fmruby-core** (`feature/asterism-o1`、develop から): `doc/ruby_asterism/report/o1.md`。
4. **作者の VPS の入れ直しの手順** (report の最後、ユーザがそのまま打てる形): 今の VPS (ead120b の頃に入れ、644efe0 に更新済み、今は止めてある) を、公開した image と一般の手順で入れ直す流れ。G1.5・G1.5b の移行 (役割への移行、nginx に足す行、名前の変わった snippet)、nginx の error log の掃除 (G1.5b で見つかった、合言葉が残っている分)、データの控えと戻し方、止めたあと `docker ps` で確かめること。本当のドメイン・IP は書かない (ユーザの非公開の手順書の値で置き換えて使う、と書く)。

## 3. 受け入れ条件

1. **まっさらな通し稽古**: volume も image も無い状態から、`docs/install.md` だけを見て、手元の docker (nginx を含む、ドメインは `*.localtest` などの手元の名前) で一式を立てる。最初の管理者・CA・役割の例ができ、ルータ 1 つ (家のルータ役) と CRuby 1 つを登録して tar で証明書を受け取り、家のルータは tar を展開した設定のまま、CRuby は `credentials:` 1 つで繋がる。ブラウザ版 (fmruby-core の wasm) も console から開けて、オセロが指せる。手順書に無い手作業が要ったら、それは穴として直す。
2. 作者の環境の残り: 公開するリポジトリの中を検索して、作者のドメイン・作者の環境を前提にした既定値が残っていないことを示す (検索の語と結果を report に)。
3. image の workflow: 手元で同じ Dockerfile が amd64 で作れる。workflow の書式が正しい (actionlint があれば通す)。arm64 は workflow に書くだけでよい (手元で確かめられなければその旨)。
4. `credentials:` の試験が通り、権限の警告が出る。tar を作る画面の試験 (中身・権限・`asterism.json`) がある。
5. console のテストと rubocop、asterism の試験 (`rake`、適合の試験) が通る。
6. 測ったメモリとディスクの目安が install.md にある。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)、asterism (main)、fmruby-core (`feature/asterism-o1`)。すべてコミットのみ、push しない。**タグを打たない、image を公開しない、gem の版を上げない**。fmruby-core は `git commit -a` を使わない (`.env` にユーザの手元の変更がある)。コミットは必ずファイルを指定する。
- **触らないもの**: `.env`、sdkconfig、fmruby-graphics-audio、**VPS (ssh で何かをすることは一切しない)**、`~/asterism-home/`、端末 (P4-Nano は使わない)。
- **終わったら**: 起動したものを全部止める (compose、ブラウザ、rails、zenohd)。試し用の image と volume は消す。fmruby-core の `build/` は Linux の標準構成 (x86-64) のまま (wasm を作り直したならその後で確かめる)。作業コピーは develop に戻す。
- **止まる条件**:
  - 作者の環境を外すと、今の作者の VPS が動かなくなる変更が要る (移し方で済まない)。
  - 運用の形・版の付け方などで、1 章で決めきれない選択が出た。案を書いて返す。
  - 公開・タグ・外への書き込みが要る段に来た (そこで止めて返す)。
- **report** (`doc/ruby_asterism/report/o1.md`、常体): 切り離したもの (検索の結果つき)、導入の手順の作り、image の workflow、最初の設定、tar と `credentials:`、通し稽古の記録 (手順書に無い手作業が要った箇所と直し)、受け入れ条件ごとの結果、測ったメモリとディスク、ユーザがすること (タグ・公開・VPS の入れ直し)、残った課題。
