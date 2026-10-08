# Asterism V1 指示書: Rails の「見る・触る」画面と網のグラフ

> 状態: 指示 | 更新: 2026-10-09 | Rails のアプリ asterism-console を作り、Zenoh の網 (ルータ・セッション・Asterism のノードとオブジェクト・ROS 2 のノードとトピック) をブラウザでグラフとしてリアルタイムに見せ、公開されたメソッドを画面から呼べるようにする

ユーザ決定 (2026-10-09):
- Rails の画面は「見る・触る」形から先に作る。
- Zenoh の網の構造をグラフで見せる。
- ルータの中継 (W1) と管理 (W2) は、この後に画面に重ねる。

ユーザは寝ている間に「Rails でグラフを見られるところまで」やりきることを望んでいる。

## 1. 作るもの

### 1.1 リポジトリ

- 新しい独立のリポジトリ `/home/kishima/fmrb/family-mruby/asterism-console/`。
  - git init をして `main` にコミットする。remote は作らない。GitHub への作成と push は、ユーザの指示で親が行う。
  - 親リポジトリの `.gitignore` に足す。
- Rails の最新の安定版。ユーザの rbenv の Ruby 3.2 に `gem install rails` してよい。sudo は使わない。
  - 構成: SQLite、importmap (node のビルドを使わない)、Hotwire。グラフの描画は cytoscape.js を importmap で読む。
- Asterism の gem は、手元のチェックアウトを Gemfile の `path:` で使う (`../asterism`、`../asterism-zenoh`、0.3.0 の今のコード)。
  - 0.3.0 の公開の後に、rubygems の版に替えられるようにしておく (README に書く)。

### 1.2 網とつながる専用のプロセス (bridge)

- Rails (Puma) の中では Asterism につながない。網とつながるのは `bin/bridge` (Rails の環境を読む別のプロセス) 1 つだけ。
  - Puma が複数のプロセスやスレッドで動いても、同じノードが何個もできないようにするため。
- bridge がつなぐ先: zenohd。既定は `tcp/127.0.0.1:7447`、環境変数で変えられる。bridge が読むものは次の 3 つ。
  1. **zenohd の管理用の空間** (`@/**`): ルータの zid、つながっているセッション (相手の zid、client / peer / router の種類、
     リンクの住所と種類)、ルータどうしの経路 (linkstate)。
     - 何が取れるかは、zenohd 1.10.1 で実際に `get` して確かめ、report に例を残す。
     - 管理用の空間を読むのに zenohd の設定が要るなら、親リポジトリの zenohd の重ね合わせのファイルで足す (既存のファイルの意味は変えない)。
  2. **Asterism の生存のしるし** (`asterism/**`): ノード → アプリ → 公開しているオブジェクト。
     - オブジェクトの meta (メソッドの一覧) は、クリックされたときに取る。
  3. **ROS 2 の生存のしるし** (`@ros2_lv/**`): ノード、トピック (送り手・受け手)、サービス。形は fmruby-core の report/r1.md・r2.md。
- 集めたものを、点と線のグラフの形 (種類つき) にまとめ、変わったところを Action Cable で画面に配る。
  - bridge は別のプロセスなので、配る仕組みはプロセスをまたげるもの (solid_cable など) を使う。
- **画面からの操作** (メソッドの呼び出し) は、bridge に頼む形。例: DB に頼みごとを書き、bridge が実行して答えを返す。
  - 時間制限と、答えの表示 (戻り値、RemoteError、Timeout)。

### 1.3 画面

- **グラフ**: 点の種類ごとに形と色を分ける (ルータ、セッション、Asterism のノード・アプリ・オブジェクト、ROS 2 のノード・トピック・サービス)。
  - 層を切り替えられる (ルータとセッション / Asterism / ROS 2)。
  - 点が増えても配置が自動で決まる。
  - 現れた・消えたが、リロードせずに反映される。
- **点をクリックしたとき**: 詳細を横に出す。
  - Asterism のオブジェクトなら、公開しているメソッドを出し、引数 (JSON) を入れて呼べる。戻り値・例外・時間を表示する。
  - ROS 2 のトピックなら、型と、送り手と受け手。
- **値を眺める欄**: キーを入れると、そのキーに流れる値を表示し続ける (bridge が購読する)。
- **認証**: 付けない。手元の網だけで使う前提であることを README と画面の上部に明記する。
  - bridge は既定で 127.0.0.1 のルータにつなぐ。Rails も既定で 127.0.0.1 で待ち受ける。

## 2. 確かめ方 (受け入れ条件)

次を同時に動かし、画面のグラフに全部出ることを確かめる。

- 親リポジトリの zenohd。LAN 用の重ね合わせで、P4-Nano から届くようにする。
- sim の asterism_demo と ros2_talker。
- P4-Nano の asterism_demo。
- ROS 2 のコンテナの talker (`ros2 run demo_nodes_cpp talker` など)。
- CRuby の例 (asterism の `examples/node.rb`)。

1. 画面のグラフに、ルータ 1 台、つながっているセッション、Asterism のノード・アプリ・オブジェクト、ROS 2 のノード・トピックが出る。
2. 機体のアプリを閉じると点が消え、開くと現れる (リロードなし)。
3. 画面から P4-Nano の `screen.say` を呼ぶと、機体の画面に出る (tab5_screenshot で確かめる)。戻り値が画面に出る。
   公開していないメソッドは呼べない。
4. 値を眺める欄で、`fmrb/test/out` などの値が流れる。
5. 画面の見た目を、headless のブラウザで撮って report に添える。
   - 例: docker の Chrome / Playwright の像。
   - ホストに入れずに済む方法を使う。撮った画像はスクラッチパッドに置き、パスを報告する。
6. Rails のアプリの試験 (最低限: bridge のグラフのまとめ方の単体試験、画面の表示の試験) が通る。

## 3. 範囲・決まり

- **触ってよいもの**:
  - asterism-console (新しいリポジトリ)。
  - 親リポジトリの `.gitignore`、zenohd の重ね合わせのファイル (足すだけ)。
  - fmruby-core の `doc/ruby_asterism/report/v1.md`。fmruby-core のブランチは `feature/asterism-ruby4` から `feature/asterism-v1` を切る。
  - asterism / asterism-zenoh に不具合が見つかった場合: 直してよいが、`main` にコミットしたら report に書く。push はしない。0.3.0 の公開の前なので、親に知らせる。
- **触らないもの**: fmruby-core のビルドの仕組み、`.env`、sdkconfig。
- **公開・push**: GitHub への push、リポジトリの作成、gem の公開はしない。
- **本当の住所を書かない**: コミットするファイルに、本当の IP・機体の名前・MAC を書かない (192.0.2.x、fmruby-aaaaaa の形)。
- **機体**: P4-Nano の本当の IP は親が渡す。ミュート中。
  - 親のシリアルの capture が向いている (`serial_start` を呼ばない)。
  - 焼き直しは要らない見込み。
  - 機体のアプリは終了させてよい。
- **終わったら**: sim・zenohd・ROS 2・Rails・bridge は全部止める。sim は sim_down。`build/` は Linux の標準構成 (x86-64) のまま。
- **止まる条件**:
  - sudo やホストへのパッケージ (Rails の gem 以外) が要るとき。
  - 管理用の空間が読めず、zenohd の設定の意味を変える必要があるとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - 構成 (bridge と Rails の分け方、配り方)。
  - 管理用の空間から取れたものの例。
  - 画面の説明と画像のパス。
  - 受け入れ条件の結果。
  - 次の段階 (W1 のルータの中継、W2 の管理) への申し送り。
