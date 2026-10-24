# BA1 指示書: asterism-console で、ブラウザ版で開くアプリの一覧を役割から切り離す

> 状態: 指示 | 更新: 2026-10-24 | いまは役割 (RelayRole) が `browser_app` を 1 つ持ち、「Launch Web Studio」(Make a pass の Start) の選択肢はその値の集まりになっている。役割は権限 (キーの読み書き) であって、開くアプリとは別のものなので、1 対 1 で結び付くのはおかしい (2026-10-24 ユーザ)。アプリの一覧を役割と別に持ち、役割は権限だけに戻す (ユーザが選択肢から決定)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語 (このリポジトリの流儀)。

## 1. 今の作り (2026-10-24、asterism-console 0.3.11 = 147c9ff)

- `relay_roles.browser_app` (db/migrate/20261013100000_relay_roles.rb)、`RelayRole` の検証 (`APP` の形)、役割の画面 (_form・show・index)、`Relay::RolesController` の許すパラメータ。
- `BrowserPage.apps` (app/models/browser_page.rb): 「Nothing (the page's own start)」+ 役割の `browser_app` を名前順に。`BrowserPassesController#open` はこの一覧にあるアプリしか受けない。
- メニューの「Launch Web Studio」(shared/_nav) は app を空で送る。Make a pass の画面 (browser_passes/index) に Start の選択がある。
- 部屋 (admin/rooms) の画面にも「Or another app」がある。部屋のアプリの決め方が `browser_app` に依っているかは読んで確認する (依っていれば同じ一覧に寄せる。依っていなければ部屋は変えない)。

## 2. 作るもの

1. **ブラウザ版のアプリの一覧** (新しい表、例 `browser_apps`): 表示名 (任意) とパス (今の `APP` の形の検証をそのまま)、並び順 (任意)。Admin の画面で足す・直す・消す (管理者だけ、`CONSOLE_ADMIN_NETS` の決まりも今の Admin の画面と同じ)。
2. **選択肢**: Make a pass の Start (と、使っていれば部屋の画面) は、「Nothing」+ この一覧から選ぶ。`BrowserPassesController#open` が受けるのも一覧にあるものだけ (今の守りを保つ)。
3. **役割を権限だけに戻す**: `browser_app` の欄を役割の画面・コントローラ・モデルから外し、列を消す移行を作る。**移行の中で、今の役割の `browser_app` の値 (空でないもの、重複は 1 つに) を新しい一覧へ写してから列を消す** (運用中のデータを失わない。表示名は役割の名前を仮に入れてよい)。「Browsers have this role」(`for_browsers`) は権限なので残す。
4. **文書**: docs/ の該当する説明 (install.md の役割の項目、g1_5.md など `browser_app`・「Opens in a browser page」に触れているところ) と README の版の記録を直す。CHANGELOG があれば足す。版は 0.3.12 (lib/ や version の置き場所は今の流儀で)。
5. **試験**: 一覧の CRUD、選択肢がそこから出ること、一覧に無いアプリを open が断ること、移行で値が写ること (移行の試験が流儀に無ければ、手で古い schema から上げて確認した記録を report に)。

## 3. 確認

- `bin/rails test` (このリポジトリの試験の流儀で) が通る。rubocop など今の lint があれば通る。
- 手元で console を起動し (使い捨ての DB、`DATABASE_URL` で切り替え。本来の development DB には触れない)、Admin でアプリを足して、Make a pass の Start に出て、Launch Web Studio のリンクに `app=` が入ること (リンクの中身を確認するだけでよい。ブラウザ版を開く必要はない)。
- 移行: 役割に `browser_app` が入った状態の DB を作り、移行して一覧に写ること、役割の画面から欄が消えたこと。

## 4. 範囲・決まり

- **触ってよいもの**: /home/kishima/fmrb/family-mruby/asterism-console (main) の上の変更と、その docs。
- **触らないもの**: VPS (本番の console の更新はしない。配布の image を作る・タグを打つ・リリースもしない。親とユーザが後でする)、`~/asterism_keys/`、ほかのリポジトリ、作業ツリーにある未コミットの `Gemfile.lock` の変更 (ユーザのもの。コミットに含めない、戻さない)。
- push・タグはしない。`git commit -a` は使わない。件名はこのリポジトリの履歴の流儀、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。
- 実在の値 (ドメイン、IP、ノード名) を docs・試験・コミットに書かない (router.example.org などの仮名)。
- **止まる条件**: 部屋のアプリの決め方を変える必要があり、それが部屋の利用者から見た動きを変える。移行で運用中のデータが失われる形しか取れない。範囲外の変更が要る。
- **report**: fmruby-core/doc/ruby_asterism/report/ba1.md (常体): 今の作りの読み取り、新しい表と画面、移行のしかた、部屋の扱い、試験の結果、本番に入れるときの手順 (移行が走ること、写る値)、コミット。fmruby-core の doc はコミットしない (親がする)。
