# BA1 報告: asterism-console で、ブラウザ版で開くアプリの一覧を役割から切り離す

> 状態: 完了 (検収待ち) | 更新: 2026-10-24 | 「Launch Web Studio」で開けるアプリを新しい表 `browser_apps` (Admin > Studio apps) に移した。役割は権限だけになり、`relay_roles.browser_app` は移行の中で値を写してから消す。部屋の画面は `BrowserPage.apps` を通して役割の値を使っていたので、同じ一覧に寄せた (並ぶパスは同じで、部屋の利用者から見た動きは変わらない)。試験 503 件と rubocop は通った。asterism-console 0.3.12 = 72112b0 (push・タグ・image 作成はしていない)

## 1. 今の作りの読み取り (0.3.11 = 147c9ff)

- `relay_roles.browser_app` (string)。`RelayRole::APP` (`\A[A-Za-z0-9_./-]{1,200}\z`) で検証し、空は許す。役割の _form・show・index に「Opens in a browser page」があり、`Relay::RolesController#role_params` がこの欄を受けていた。
- `BrowserPage.apps` は「Nothing (the page's own start)」に、`browser_app` が空でない役割を名前順に並べて足し、パスで重複を除いていた (表示は `パス (役割名)`)。`BrowserPassesController#open` はこの一覧にある値しか受けない (無ければ 422)。
- メニューの「Launch Web Studio」(shared/_nav) は `app=""` を送る。Make a pass (browser_passes/index) の Start の select は `BrowserPage.apps` から作る。
- 部屋: `RoomPage.apps` は `ASTERISM_ROOM_APPS` の後ろに `BrowserPage.apps` (空を除く) を足していた。つまり部屋の画面の「Opens」の選択肢は `browser_app` に依っていた。「Or another app」は自由入力で、`Room::APP` で検証するだけなので一覧とは関係がない。部屋の招待リンク (`RoomInvite`) は `ASTERISM_ROOM_APPS` だけを見ていて、`browser_app` に依っていない。

## 2. 新しい表と画面

- 表 `browser_apps`: `label` (任意)、`path` (必須、一意の索引)、`position` (任意の整数)、timestamps。
- モデル `BrowserApp` (app/models/browser_app.rb): `PATH` は今の `APP` と同じ形。path は前後の空白を落とし、label は空なら nil にする。並びは `in_order` で、position のあるものが先 (小さい順)、次に label のあるものを label 順 (大文字小文字を区別しない)、最後に label の無いものを path 順。選択肢の表示は `Label (パス)`、label が無ければパスだけ。
- 画面 Admin > Studio apps (`/admin/studio-apps`、`Admin::BrowserAppsController`): 一覧と「Add an app」のフォーム、各行に Edit と Remove (確認つき)。`require_admin` なので、管理者だけが使え、`CONSOLE_ADMIN_NETS` の決まりもほかの Admin の画面と同じ。メニューの Admin の組に「Studio apps」を足した。`ASTERISM_BROWSER_PAGE` が無いときは、その旨を一覧の上に出す。
- 選択肢: `BrowserPage.apps` は「Nothing」と `BrowserApp.in_order` になった。`open` の守り (一覧に無い値は 422) はそのまま。消したアプリは、もう作ってある pass の `app` には残るが、新しくは開けない。
- 役割: `browser_app` を _form・show・index・コントローラ・モデル (`APP` 定数と検証) から外した。`for_browsers` (「Browsers have this role」) は権限なので残した。

## 3. 移行のしかた

db/migrate/20261024100000_browser_apps_out_of_roles.rb (`BrowserAppsOutOfRoles`):

1. `browser_apps` を作る。
2. `relay_roles` から `browser_app` が NULL でも空でもない行を `for_browsers DESC, name` の順に読み、前後の空白を落とし、パスごとに最初の 1 行だけを残して写す。label にはその役割の名前を仮に入れる (同じパスを複数の役割が持つときは、ブラウザ向けの役割の名前を優先する)。position は入れない (label 順に並ぶので、今の「役割の名前順」とほぼ同じ並びになる)。
3. `remove_column :relay_roles, :browser_app`。SQLite は表を写し直して列を消すが、`relay_peer_roles` の外部キーには ON DELETE CASCADE が無い (*_relay_roles.rb の注のとおり) ので、行と役割の結び付きは残る。

down は列を戻し、役割の名前が label になっているアプリをその役割の `browser_app` に戻してから表を消す (label を書き換えたものや後から足したものは戻らない)。本番で戻すときは、今までどおり update.sh が取った backup を戻すのが正しい手順で、install.md にもそう書いた。

手で古い schema から上げて確認した (使い捨ての DB、`DATABASE_URL` と `CABLE_DATABASE_URL` を scratchpad の sqlite に向けた。本来の storage/development.sqlite3 の更新時刻は 10-18 のままで触れていない):

- 0.3.11 の schema.rb を読み込んだ DB に役割を 5 つ入れた: match=/app/test/othello.app.rb (ブラウザ向け)、games=/app/game/mahjong/mahjong.app.rb (ブラウザ向け)、board=同じ mahjong (ブラウザ向けでない)、viewer="" 、spectator=NULL。行 1 つに 5 役割を付けた。
- `db:migrate` 後: `browser_apps` は `games → /app/game/mahjong/mahjong.app.rb` と `match → /app/test/othello.app.rb` の 2 行 ("copied 2 app(s) from the roles")。`relay_roles` の列から `browser_app` が消え、5 役割と `for_browsers` はそのまま、`relay_peer_roles` は 5 件のまま。
- `db:rollback:primary` (この app は複数 DB なので名前つきの task が要る) で match・games に値が戻り、表が消えた。もう一度 migrate して同じ結果になった。schema.rb はこの DB から出した (db/cable_schema.rb も書き換わったが、関係が無いので戻した)。

## 4. 部屋の扱い

部屋の画面は `RoomPage.apps` → `BrowserPage.apps` で役割の値を使っていたので、同じ一覧に寄せた。コードとしては `BrowserPage.apps` の中身が変わるだけで、`RoomPage` は注だけを直した。移行で同じパスが一覧に写るので、部屋の画面の「Opens」に並ぶパスは変わらない。変わるのは管理者に見える表示 (`パス (役割名)` → `役割名 (パス)`) と、一覧を後から直したときの並びだけで、部屋のリンクで開くアプリ (rooms.json の app) も、部屋の利用者から見た動きも変わらない。止まる条件には当たらない。

## 5. 文書と版

- README (Passes の項、Admin pages の項)、docs/install.md (12 章、部屋の「What a link opens」、「From 0.3.11 to 0.3.12」)、docs/g1_5.md (Open the browser build)、docs/rooms.md (常設の卓の `games` 役割の例から「Opens in a browser page」を外し、Studio apps に置くと書いた)、docs/v4.md (描画ボードの手順)。
- 版は 0.3.12: `VERSION`、deploy/vps/docker-compose.yml の既定、deploy/vps/env.example、CHANGELOG (「What an update needs」に移行 1 つ、戻すには backup が要る、apply 不要と書いた)。
- 気づいたこと (直していない): CHANGELOG の 0.3.10 の項は `## 0.3.10` の見出しが抜けていて、0.3.11 の項の続きに見える (0.3.11 のコミットで消えたらしい)。

## 6. 試験の結果

- 足した試験:
  - test/models/browser_app_test.rb: パスの検証 (空・空白・`#`・201 文字・重複・小数の position)、並び順、`BrowserPage.apps` と `RoomPage.apps` がこの一覧から出ること (`ASTERISM_ROOM_APPS` と同じパスは 1 回)。
  - test/controllers/admin/browser_apps_controller_test.rb: 管理者の足す・直す・消す (重複と不正なパスは 422)、利用者は見えず変えられない、`CONSOLE_ADMIN_NETS` の外からは管理者でも 403、Make a pass の Start が「Nothing」+ 一覧の順に並ぶこと、一覧に無いアプリ・消したアプリを open が 422 で断り、足したアプリなら URL に `app=` が入ること。
  - test/db/browser_apps_out_of_roles_test.rb: down で列を戻して上の 5 役割を入れ、up で 2 件が写ること、役割と行の結び付きが残ること、もう一度 down で値が戻ること (試験の transaction が schema の変更ごと戻す)。
- 直した試験: 役割の fixture から `browser_app` を外して `browser_apps.yml` を足した。役割の画面に `browser_app` の欄と「Opens in a browser page」が無く、`for_browsers` があることを確かめる行を足した。メニューの Admin のリンク数 4 → 5。旧移行の `RelayRoles.match_role` を使う 2 つの試験は `.except(:browser_app)` を付けた (旧移行自体は変えていない)。
- `bin/rails test`: 503 runs, 5387 assertions, 0 failures, 0 errors。`bin/rubocop`: 269 files, no offenses。`RAILS_ENV=test bin/rails db:seed:replant` も通る。brakeman は 10 件の警告で exit 3 だが、すべて今回触れていない app/controllers/recordings_controller.rb と lib/exposure_check.rb のもの。
- 手元で起動して確認 (上の移行済みの使い捨て DB、ポート 3917、relay・rooms・storage・recordings の置き場も scratchpad に向けた、`ASTERISM_BROWSER_PAGE` は play.example.org の仮の値): 管理者でサインイン → Studio apps に移行の 2 件が出る → Board (/app/game/board/board.app.rb、order 1) を足す (303) → Make a pass の Start が Nothing / Board / games / match の順 → Board で open すると URL の fragment が `...&zenoh_node=wasm-ada&app=%2Fapp%2Fgame%2Fboard%2Fboard.app.rb`、一覧に無い /app/other.app.rb は 422、空は `app=` → 役割の一覧・編集画面に「Opens in a browser page」も `browser_app` の欄も無い (`for_browsers` はある) → 部屋の画面の「Opens」も同じ 4 つ。確認後にサーバを止めた。

## 7. 本番に入れるときの手順 (親とユーザが後でする)

1. image `ghcr.io/ruby-asterism/asterism-console:0.3.12` を作って出す (今回はしていない)。
2. VPS で `./update.sh 0.3.12`。update.sh が backup を取り、console の起動時に移行 `BrowserAppsOutOfRoles` が走る。`.env`・nginx・router の設定は変えない。router の規則は変わらないので apply も要らない。
3. 写る値: 役割の「Opens in a browser page」に入っていたパスが、重複を除いて Admin > Studio apps に入る。label はそのパスを持つ役割の名前 (ブラウザ向けの役割を優先)。運用中の console なら、たとえば `games` 役割の /app/game/mahjong/mahjong.app.rb などが写るはず。起動ログに "copied N app(s) from the roles" が出る。
4. 更新後に Admin > Studio apps で label や並びを好みに直す。Make a pass の Start と部屋の画面に同じアプリが並ぶことを確かめる。
5. 戻すときは update.sh が表示した backup を戻すコマンドを使う (更新後に足したアプリは消える)。

## 8. コミット

asterism-console (main、push していない):

- 72112b0 `console 0.3.12: the Studio apps, a list of their own; roles are permissions only`

作業ツリーの `Gemfile.lock` の未コミットの変更 (ユーザのもの) には触れておらず、コミットにも入れていない。この報告 (fmruby-core) はコミットしていない。
