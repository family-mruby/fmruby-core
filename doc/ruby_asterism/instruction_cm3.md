# CM3 指示書: 外から見た公開の状態を確認する道具 (exposure check)

> 状態: 指示 | 更新: 2026-10-18 | VPS の公開の範囲 (nginx の許可リスト、さくらのパケットフィルタ) を変えると、意図より広く開いたり、開いたはずの入口が閉じていたりする。外 (自宅でも VPS でもない場所) から HTTP の答えとポートのつながりを定期的に確認し、期待と違えば知らせる道具を作る

前提: asterism-console の deploy/vps (nginx の例: asterism-console.conf・asterism-router.conf・snippets の door-allow / admin-allow)、docs/install.md・rooms.md、bin/asterism、.github/workflows/image.yml、CHANGELOG。今の VPS の公開の形 (下の 1 章)。

言葉: 文書は常体、「確かめ」ではなく「確認」。asterism-console の文書とコードのコメントは英語。

## 1. 決まっていること (2026-10-18 ユーザ、選択形式)

- **GitHub Actions の定時実行で外から見る**。GitHub の実行環境は自宅でも VPS でもないので、パケットフィルタまで含めて外から見える。VPS から自分自身へ問い合わせても、パケットフィルタを通らないので確認にならない。
- **道具の本体は asterism-console に入れる**。console は OSS で各自が導入するので、作者の環境 (ドメイン・アドレス・ポートの値) を焼き込まない。違いは設定で吸収する。
- **定時に流す設定は利用者の非公開リポジトリに置く**。作者の分は作者の個人アカウント (kishima) の非公開リポジトリ (作るのはユーザ、または親がユーザの確認を得てから)。ドメインは GitHub の secrets に置き、ファイルにもログにも出さない。

今の VPS の形 (確認の初期値の参考。文書には実在の値を書かない):

| 対象 | 外から期待する答え |
|---|---|
| console `/invite` | 200 |
| console `/assets/<何か>.css` (`/invite` の本文から拾う) | 200 |
| console `/icon.png` | 200 |
| console `/` | 403 |
| console `/admin/rooms` | 403 |
| ルータのブラウザの入口 (wss、nginx) のポート | つながる (TCP) |
| ルータの mTLS のポート | つながらない (タイムアウトか拒否) |

## 2. 作るもの

1. **道具** (`bin/asterism-exposure` か `bin/asterism exposure`。既存の bin/asterism の作りに合わせて任せる): Ruby の標準ライブラリだけで書く (net/http・socket・yaml。gem を足さない。GitHub Actions の素の ruby で動くこと: Rails を起動しない、bundle install を要しない)。
   - 確認の一覧は YAML の設定ファイル (例 `deploy/vps/exposure.example.yml`)。項目は少なくとも:
     - `http`: パス、期待する状態コード (1 つか複数)、任意で「本文にこの文字列が含まれる」。
     - `asset_from`: 指定したページの本文から `/assets/...css` か `.js` を 1 つ拾い、その状態コードを見る (ファイル名がビルドごとに変わるため)。
     - `tcp`: ポート、`open` / `closed`、待ち時間 (既定は短め、例 5 秒)。`closed` はタイムアウトも拒否も合格。
   - ホスト名は設定ファイルに書かず、環境変数 (例 `ASTERISM_EXPOSURE_HOST`。複数ホストが要るなら名前つき) か引数で渡す。設定ファイルはホスト名を含まないので公開してよい形にする。
   - 出力: 項目ごとに 期待 / 実際 / 合否 の 1 行。**ホスト名を出力に出さない** (ログに残るので `<host>` と出す。secrets のマスクだけに頼らない)。1 つでも外れたら終了コード 1。つながらない・TLS の失敗はその項目の不合格として扱い、道具は落ちない。
   - リダイレクトは追わない (403 と 302 の違いを見たいので)。
2. **GitHub Actions の例** (`docs/exposure.md` に全文を載せる。asterism-console 自身の `.github/workflows` には入れない: 公開リポジトリの Actions から作者の VPS を叩かない): 利用者の非公開リポジトリに置く workflow。定時 (例 6 時間ごと) と手動 (`workflow_dispatch`)。asterism-console の公開リポジトリを決まったタグで checkout し、利用者のリポジトリの設定ファイルで道具を流す。ホストは secrets から環境変数へ。失敗すれば GitHub の通知 (メール) が届く、と書く。
3. **文書**: `docs/exposure.md` (何を見るか、なぜ外から見るか、設定の書き方、例の workflow、非公開リポジトリと secrets の作り方、手元から流す方法)。docs/install.md の公開の範囲を変える所 (door-allow、`/invite` の location) から 1 行で案内。CHANGELOG の **0.3.6** (未タグ。VERSION も 0.3.6 に。版の上げ方は既存の流儀に合わせる)。
4. **試験**: 道具の判定の部分 (期待との比較、`closed` のタイムアウトと拒否、asset の拾い方、ホスト名を出さないこと) を、手元に立てた小さな HTTP サーバと閉じたポートで試験する。既存の試験の流儀に合わせる。外のネットワークに出ない。

## 3. 受け入れ条件

1. console の試験全部と rubocop が通る。
2. 手元 (試験用の小さなサーバか deploy/local) に対して道具を流し、合格・不合格の両方の出力を report に写す。出力にホスト名が出ていないことを確認する。
3. 例の workflow を `actionlint` があればそれで、無ければ YAML として読めることを確認する (実際に GitHub で走らせるのは親とユーザ)。
4. **作者の VPS に対しては流さない** (外に向けた操作。親がユーザの確認を得てから行う)。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)。fmruby-core は report だけ (ファイルとして書くだけ。fmruby-core の git 操作はしない。親がコミットする)。
- **触らないもの**: asterism・asterism-games・fmruby-core のコード、VPS、GitHub の設定 (リポジトリ・secrets を作らない)、zenohd の設定。
- コミットのみ (asterism-console)。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査 (ドメイン・アドレス・端末 ID・鍵) をしてからコミットする。例のホストは `router.example.org` / `asterism.example.org` の形。
- 終わったら起こしたもの (試験のサーバ、deploy/local、試しの image と volume) を名前で止めて消す。前からある image は残す。docker の prune はしない。親が同時に Tab5 (シリアル、WiFi) を使うので触らない。
- **止まる条件**: 1 章に無い仕様の選択が要る (例: 通知を GitHub のメール以外にする、確認の項目を大きく増やす)。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/cm3.md`、常体): 道具の形と設定の書式、例の workflow、受け入れ条件の結果 (出力の写し)、残ったこと (非公開リポジトリと secrets の作成、初回の実行)。
