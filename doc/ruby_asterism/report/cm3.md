# CM3 報告: 外から見た公開の状態を確認する道具 (exposure check)

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | asterism-console に `bin/asterism-exposure` (本体は `lib/exposure_check.rb`) を足し、0.3.6 (未タグ) とした。YAML の一覧で HTTP の状態コード・本文の文字列・ページが読む asset・TCP のポートの開閉を外から確認し、ホスト名を出力に一切出さない。利用者の非公開リポジトリに置く GitHub Actions の例を docs/exposure.md に載せた。試験全部と rubocop が通り、例の workflow は actionlint で 0 件。コミットは asterism-console の 1ca9a30 (push・タグなし)

## 1. 結果の要約

- 道具は独立したコマンド `bin/asterism-exposure` にした。既存の `bin/asterism` はコンテナの中で Rails の設定 (config/boot) を前提に動く作りなので、素の ruby で動くことを要する道具は別の入口にした。判定の本体は `lib/exposure_check.rb` (module `ExposureCheck`、net/http・socket・yaml・openssl だけ)。Rails も bundle も要らない (試験でも `Bundler.with_unbundled_env` の素の ruby で起動して確認した)。
- 1 章の決まりの範囲で作れたので、止まる条件には当たらなかった。
- 1 章に無いが小さな作りの選択として決めたこと (いずれも docs/exposure.md に書いた):
  - 終了コードは 合格 0 / 不合格 1 / 使い方・設定の誤り 2 (ホストが無い、YAML が読めない)。設定の誤りは 1 件も問い合わせる前に止まる。
  - 複数ホストは「名前つき」: 項目に `host: router` と書くと `ASTERISM_EXPOSURE_HOST_ROUTER` を使う。無指定の項目は `ASTERISM_EXPOSURE_HOST`。`--host [name=]value` でも渡せる。出力では `<host>` / `<router>`。
  - HTTPS は証明書を検証する (期限切れ・名前違いは不合格)。
  - `closed` の合格は 拒否・タイムアウト・到達不能。名前が引けないときは不合格 (ポートについて何も言えないため)。
  - 版の上げ方は既存の流儀どおり VERSION・docker-compose.yml の既定値・env.example の 3 か所と CHANGELOG・install.md の「From 0.3.5 to 0.3.6」。

## 2. 道具の形と設定の書式

```
ASTERISM_EXPOSURE_HOST=asterism.example.org ASTERISM_EXPOSURE_HOST_ROUTER=router.example.org \
  ruby bin/asterism-exposure deploy/vps/exposure.example.yml
```

```yaml
scheme: https        # 全項目の既定 (項目ごとに上書きできる)
timeout: 5
checks:
  - http: /invite              # GET。リダイレクトは追わない
    status: 200                # 1 つか [302, 403] のような一覧
    body: "Make a room"        # 任意: 本文にこの文字列
  - asset_from: /invite        # ページ中の最初の /assets/*.css|js の状態 (既定 200)
  - http: /
    status: 403
  - tcp: 7443
    host: router
    expect: open
  - tcp: 7448
    host: router
    expect: closed             # タイムアウトも拒否も合格
```

- 項目のキー: `http` / `asset_from` / `tcp`、共通に `host`・`scheme`・`port` (HTTP のポート)・`timeout`。
- 出力は項目ごとに `ok|FAIL  種類  対象  expected 期待  actual 実際` の 1 行と、最後に `N passed, M failed`。
- ホスト名を出さない作り: 対象の表示はラベル (`<host>`) で組み立てる。例外の文言は種類に置き換え (refused / timeout / unreachable / name not resolved / 例外のクラス名)、TLS の失敗だけは理由を残すが `peeraddr=...` を落とし、さらに全ホストの名前と解決したアドレスを置換で消す。リダイレクト先は、自分のホストなら `<host>`、他なら `<another host>` と出す。secrets のマスクには頼っていない。
- 例の設定 `deploy/vps/exposure.example.yml` は 1 章の表の 7 項目 (/invite 200、asset、/icon.png 200、/ 403、/admin/rooms 403、7443 open、7448 closed)。

## 3. 例の workflow

docs/exposure.md に全文 (asterism-console 自身の .github/workflows には入れていない)。要点:

- `schedule: cron "17 */6 * * *"` と `workflow_dispatch`、`permissions: contents: read`。
- 利用者のリポジトリ (exposure.yml) を checkout し、`ruby-asterism/asterism-console` を `ref: v0.3.6` で `asterism-console/` に checkout、`ruby/setup-ruby@v1` (3.3)、`ruby asterism-console/bin/asterism-exposure exposure.yml`。
- ホストは `secrets.ASTERISM_EXPOSURE_HOST` / `secrets.ASTERISM_EXPOSURE_HOST_ROUTER` から環境変数へ。
- 失敗は GitHub の通知 (既定はメール) で届く。定時の workflow の通知は cron の行を最後に変えた人へ行く、と書いた。非公開リポジトリの Actions の分数 (月に約 120 分) にも触れた。
- 非公開リポジトリと secrets の作り方の手順も同じ文書に書いた。

## 4. 受け入れ条件の結果

1. 試験: `bin/rails test` 480 runs, 4835 assertions, 0 failures, 0 errors。新しい試験は test/lib/exposure_check_test.rb (14 件: 状態コードの比較、本文、リダイレクトを追わないことと行き先の伏せ字、asset の拾い方、TCP の open/closed (拒否は実ポート、タイムアウトと名前解決の失敗は差し替えた接続関数)、タイムアウト値の受け渡し、拒否と TLS の失敗で落ちないこと、伏せ字の置換、ホストの取り方、未知のホストで何も問い合わせずに止まること、コマンドの終了コード 0/1/2 と出力にホストが無いこと)。手元のループバックだけで、外のネットワークには出ない。rubocop: 259 files, no offenses (bin/asterism-exposure も明示して検査し 0 件)。
2. 手元の小さな HTTP サーバ (127.0.0.1:18431、スクラッチの使い捨て) に対して流した。ホストは `ASTERISM_EXPOSURE_HOST=localhost`、`ASTERISM_EXPOSURE_HOST_ROUTER=127.0.0.1`。3 つの出力に `localhost`・`127.0.0.1` は 0 件 (grep で確認)。

合格:

```
ok    http  http://<host>:18431/invite         expected 200            actual 200
ok    asset http://<host>:18431/assets/application-0a1b2c3d.css expected 200            actual 200
ok    http  http://<host>:18431/icon.png       expected 200            actual 200
ok    http  http://<host>:18431/               expected 302|403        actual 302
ok    http  http://<host>:18431/admin/rooms    expected 403            actual 403
ok    tcp   <router>:18431                     expected open           actual open
ok    tcp   <router>:18432                     expected closed         actual timeout
7 passed, 0 failed
exit=0
```

不合格 (本文の文字列なし、/ が 302、開いているポートに closed を期待):

```
ok    http  http://<host>:18431/invite         expected 200            actual 200
ok    asset http://<host>:18431/assets/application-0a1b2c3d.css expected 200            actual 200
FAIL  http  http://<host>:18431/invite         expected 200 +body      actual 200 (body lacks the text)
ok    http  http://<host>:18431/icon.png       expected 200            actual 200
FAIL  http  http://<host>:18431/               expected 403            actual 302 -> http://<host>/session/new
ok    http  http://<host>:18431/admin/rooms    expected 403            actual 403
FAIL  tcp   <router>:18431                     expected closed         actual open
ok    tcp   <router>:18432                     expected closed         actual timeout
5 passed, 3 failed
exit=1
```

TLS の失敗 (平文のサーバに https。道具は落ちず項目の不合格になる):

```
FAIL  http  https://<host>:18431/invite        expected 200            actual tls error (state=error: wrong version number)
FAIL  asset https://<host>:18431/invite -> asset expected 200            actual page: tls error (state=error: wrong version number)
FAIL  http  https://<host>:18431/icon.png      expected 200            actual tls error (state=error: wrong version number)
FAIL  http  https://<host>:18431/              expected 302|403        actual tls error (state=error: wrong version number)
FAIL  http  https://<host>:18431/admin/rooms   expected 403            actual tls error (state=error: wrong version number)
ok    tcp   <router>:18431                     expected open           actual open
ok    tcp   <router>:18432                     expected closed         actual timeout
2 passed, 5 failed
exit=1
```

3. 例の workflow を docs/exposure.md から抜き出し、rhysd/actionlint:1.7.7 の image で検査した: 0 errors。YAML としても読めた。image は検査後に名前で消した (使ったのは 2 回の pull と rmi のみ)。
4. 作者の VPS・実在のサーバには一度も流していない。

## 5. 気づいたこと

- この WSL の環境では、ループバックの閉じたポートでも番号によって拒否ではなくタイムアウトになる (18432 は 3 秒のタイムアウト、試験で使う一時ポートは拒否)。どちらも `closed` の合格なので判定には影響しない。試験の拒否の確認は一時ポートで行っている。
- リダイレクト先の表示はポート番号を落とす (`http://<host>/session/new`)。行き先の見当がつけば足りるので直していない。

## 6. 残ったこと

- 作者の非公開リポジトリ (kishima の個人アカウント) の作成、secrets 2 つ (`ASTERISM_EXPOSURE_HOST`・`ASTERISM_EXPOSURE_HOST_ROUTER`) の登録、exposure.yml (例から) と workflow の配置。ユーザ (または親がユーザの確認を得てから)。
- asterism-console の push と v0.3.6 のタグ。例の workflow は `ref: v0.3.6` を参照するので、タグが出るまで定時の実行は checkout で失敗する。
- 初回の手動実行で、今の VPS の答えが例の期待どおりかを確認する (例えば `/` が 403 でなく 302 の作りなら exposure.yml 側を直す)。
- 実行環境の後始末: 試しのサーバは止めた。docker の image は消した。コンテナは起こしていない (actionlint は --rm)。
