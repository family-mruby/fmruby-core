# Asterism G1b 指示書: 接続を 1 本に (Asterism.session)、poll を軽く、自分の名前 (asterism 0.6.0)

> 状態: 指示 | 更新: 2026-10-12 | asterism の gem に `Asterism.session` を公開して、アプリが Asterism の接続 1 本で状態の配信と ROS 2 のノードを扱えるようにし、オセロ・実時間の測り・台車の頭脳・操縦機を 1 本に直す。`Asterism.poll` を軽くする。自分の名前をプロフィールに置いて全アプリで共通にし、オセロの待ち合わせで変えられるようにする。版は 0.6.0

前提: plan.md の G の節 (G1b の行)、report/g1.md (特に 8 章: 合言葉と 2 本の接続、poll の重さの見立て)、report/s4.md (頭脳の 2 本の接続)、report/v2_5.md (プロフィール)、asterism の README・docs/api_review.md・CHANGELOG、doc/ruby_asterism/browser.md・game.md。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 決まっていること (2026-10-12 ユーザ)

1. **接続を 1 本に (案 a)**: `Asterism.session` で、Asterism が使っている Zenoh の接続 (portable API では `Asterism::Zenoh::Session`、CRuby API では対応するもの) を返す。つながっていなければ今の `session!` と同じく例外。アプリは `Asterism.session.put(...)` / `subscribe` / `Asterism::ROS::Node.new(Asterism.session, ...)` と使う。閉じるのは Asterism (アプリが `close` しても Asterism の接続が壊れないよう、扱いを決めて docs に書く)。
   - 合言葉は「1 回で使い切る」のまま。短い時間だけ使い回す案などは入れない。
2. **`Asterism.poll` を軽く**: ノードの一覧 (`tell_nodes` / `nodes`) は生存の印が変わったときだけ作り直す。毎回の見回りでは、届いたものを渡すだけにし、確保 (配列・文字列・Hash の作り直し、`include?` の総当たり) を減らす。portable と CRuby の両方。振る舞い (on_join / on_leave の順や回数) は変えない。
3. **自分の名前**: 名前はプロフィールの `name` に置く。保存先は全アプリで共通 (Family mruby の端末は `/home/asterism_profile.toml` などファイル 1 つ、ブラウザ版はその保存先がブラウザの保存領域にある今の作りのまま)。端末の既定のプロフィール (fmruby-core の `picoruby-fmrb-asterism`) がそこから `name` を読む。オセロの待ち合わせの画面に「名前」の欄を置き、変えると保存し、すぐにプロフィールにも反映する (console の図にも出る)。未設定ならノードの ID を出す。日本語の名前も使える (表示の幅に注意)。

## 2. 作るもの

1. **asterism (gem)**: 上の 1・2。版を 0.6.0 (`version.rb` と `mrbgem.rake`)、CHANGELOG・README・docs。テスト (CRuby と `rake test:mruby`)。`Asterism.session` のテスト (同じ接続で put / subscribe / ROS 2 のノード)、poll の振る舞いが変わらないテスト、poll の重さの測り (ノード数・オブジェクト数ごと)。
2. **fmruby-core**:
   - オセロ (`othello.app.rb`)・実時間の測り (`rt_probe.app.rb`)・台車の頭脳 (`rover_brain.app.rb`)・操縦機 (`rover_pilot.app.rb`) を、接続 1 本に直す。台車の頭脳の ROS 2 のノードが、console の図で端末の枠の中に入ることを確かめる (S4 の残り)。
   - オセロの「名前」の欄と保存、既定のプロフィールが名前を読むところ。
   - `ASTERISM_PIN` を新しいコミットに (push しないので、ビルドは `ASTERISM_DIR=../asterism`)。
3. **測り**: P4-Nano と AtomS3R で、ノード 5・オブジェクト 5 のときの 1 回の `Asterism.poll` の時間を、直す前 (0.5.0) と後で比べる (目標 1 ms 前後)。実時間の測りの端末の送る回数と更新の 1 周も、直す前と後で比べる。

## 3. 受け入れ条件

1. オセロ・実時間の測り・操縦機が、G0 の手元の通し稽古の構成 (`deploy/local/rehearse`) で、1 回で使い切る合言葉の `wss://` からつながって動く (G1 では 2 本目がはじかれていた)。家の中でも今までどおり動く。
2. 台車の頭脳が接続 1 本で動き、console の図で ROS 2 のノードが端末の枠の中に入る。
3. `Asterism.poll` の時間が、ノード 5・オブジェクト 5 で目標に近づく (数字を書く)。on_join / on_leave の振る舞いが変わらない (テスト)。
4. オセロで名前を変えると、相手の待ち合わせの一覧と console の図にその名前が出る。端末を起動し直しても名前が残る。ブラウザ版も同じ。
5. asterism のテスト (CRuby、`rake test:mruby`)、fmruby-core の `rake test`、console のテスト、sim の標準構成 (エディタの 1 打鍵)、ブラウザ版のビルドが通る。内蔵 RAM が増えない (共通の部分を変えたら Retro・Tab5・AtomS3R のビルドで確かめる)。

## 4. 範囲・決まり

- **asterism**: ブランチ `feature/session-poll` にコミットのみ。push・タグ・gem の作成・公開はしない (親が検収し、公開はユーザ)。asterism-zenoh を直す必要があれば同じ扱いで版をそろえ、理由を書く。
- **fmruby-core**: develop から `feature/asterism-g1b`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` は変えない)。
- **asterism-console**: 要る直しがあれば `feature/g1b` にコミットのみ。
- **触らないもの**: sdkconfig、submodule、VPS、公開済みの版の中身。
- **端末**: P4-Nano (192.0.2.10、`/dev/ttyACM0`。アプリは tab5_fs。ファームの焼き直しが要るなら、アプリの区画だけ、ポートを明示して)、AtomS3R (`/dev/ttyACM1`、WiFi。焼き直しが要るならアプリの区画だけ、`scratchpad/h1/flash_atoms3r.sh`)。どちらもミュート中。端末の IP・mDNS の名前・MAC はリポジトリに書かない。
- **終わったら**: 起動したものを全部止める。`build/` は Linux の標準構成 (x86-64)、ブラウザ版のビルドが通る状態。
- **止まる条件**: ここに無い仕様の選択 (使う人から見える名前・形)、公開済みの版を変える必要。
- **report**: `doc/ruby_asterism/report/g1b.md`、Japanese plain style (常体)。
