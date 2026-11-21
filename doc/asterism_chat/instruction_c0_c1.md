# C0・C1 指示書: チャットの線の形を決め、チャットホストの gem の最小を作る

> 2026-11-21 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/asterism_chat/plan.md (全部)、
> asterism-tutor (隣の `../asterism-tutor`。`lib/tutor/` の helper・store・jobs・server・status・log・config、`test/`、
> `deploy/vps/`、`docs/install.md`、CHANGELOG)、doc/ai_coding/plan.md と report の a0〜a2 (端末と gem の線の形・
> 小分けの送り方・上限の決め方)、doc/asterism_protocol2/design.md (呼び出し・トピック・`@pubs`・`from`)、
> 端末の側の `lib/gems/picoruby-fmrb-asterism/mrblib/` (Link・呼び出し・購読の API)、
> `main/ruby/default_app/appstore.app.rb` (App Store の入れ方と置き場)、`flash/app/tool/sprite_editor.app.rb`、
> `sig/fmrb_gfx.rbs` の画像 (`load_image`・`create_image`・`draw_image`)。

## C0: 調べて決める (コードは書かない。report/c0.md)

plan.md の 3 章・6 章を、実物を読んで具体の値にする。**決められるものは決め、利用者から見た動きに関わるものは
選択肢 (推奨つき) にして親に返す**。

1. **線の形**: オブジェクトの名前とメソッド (`hello`・`rooms`・`post`・`history`・スタンプの小分け・`status` 用のもの)、
   引数と答えの Hash の形、誤りの形、トピックの名前と流す値。新しい発言の知らせに本文を載せるか、番号だけにするかを
   比べて決める (端末のメモリ、取りこぼし、順番、Zenoh の 1 つの値の大きさ)。
   `from` (ルータが確認したノード名) の取り方は asterism-tutor に合わせる。
2. **上限の値**: 発言の長さ、履歴の件数/部屋、端末が 1 回に取る件数と持つ件数、表示名の長さ、1 日の発言の数、
   スタンプ 1 枚の大きさ・ホストが持つ枚数。端末の VM プール (Modern 約 1 MB、AI ヘルパーの実測が report にある) から
   見積もる。
3. **スタンプ**: 端末の `load_image` などで出せる形式と大きさ、RGB332 との関係、スプライトエディタが書く形
   (16x16 のタイルのシート) をそのまま 1 枚のスタンプにできるか。**App Store の今の作りで「絵だけのパック」を
   入れられるか** (`/app/usr/<id>/` に何が要るか、ランチャーに出てしまわないか、一覧 (family-mruby-apps) の形)。
   入れられないなら、要る直しの大きさを書く (直さない)。チャットのアプリがパックを見つける方法の案。
4. **画面の割り振りの案** (426x240 と 640x360。文字だけの図でよい): 部屋の一覧、会話、入力の行、スタンプの一覧、
   表示名を決める所。AI ヘルパーのどの部品を流用するか。
5. plan.md の 6 章の各項目について、決めたもの・親に返すものの仕分け。

## C1: ホストの gem の最小 (文字だけ。スタンプは C3)

- 置き場: `../asterism-chat/` (直下の隣) (新しいディレクトリ。`git init` して、その中でコミット。
  **GitHub にリポジトリを作らない。push しない。** asterism-tutor と同じく直下のリポジトリからは追跡しない)。
- 形は asterism-tutor に倣う (gemspec・`exe/asterism-chat`・`lib/chat/`・`test/`・README.md (英語)・README.ja.md・
  CHANGELOG・MIT の LICENSE・`.ruby-version` 3.4.13)。コードのコメントは **ASCII だけ** (公開する前提)。
  実在のドメイン・IP・端末の ID・ローカルの絶対パスを書かない (例は 192.0.2.x・fmruby-aaaaaa・router.example.org)。
- 入れるもの: 設定 (環境変数 `ASTERISM_CHAT_*` > ファイル > 既定。部屋の一覧はここ)、公開するオブジェクト
  (C0 で決めた `hello`・`rooms`・`post`・`history`)、部屋ごとの通し番号、トピックの知らせ、ディスクへの保存
  (再起動の後も履歴と表示名が残る。直近 N 件を超えた分は捨てる)、受け付ける端末の絞り (役割で届いたものは全部、
  が既定。asterism-tutor の `devices` と同じ形)、JSONL の記録、`asterism-chat status`、SIGTERM の扱い、
  1 日の上限。試験用の送り手 (`test/sender.rb`)。
- 入れないもの: スタンプ、`deploy/vps/` (C4)、端末のアプリ (C2)。ただし C2・C3 が足しやすい形にしておく。
- LLM は関係ない。本物の鍵・VPS・console・端末 (GPD・P4-Nano) に触らない。試験のルータはローカルの zenohd
  (asterism-tutor の試験と同じ起こし方。終わったら止める)。

## 受け入れ条件

1. report/c0.md に、1〜5 の決めた値と根拠、親に返す選択肢。
2. ローカルのルータで: 送り手 A・B が同じ部屋で発言し、互いに知らせを受けて続きを取れる。番号は部屋ごとに
   1 から続き、2 つの送り手で同じ順番。後から入った送り手 C が履歴を取れる (件数の上限、「この番号より後」)。
3. gem を止めて起こし直しても、履歴・番号の続き・表示名が残る。上限を超えた古い発言は捨てられている。
4. 長すぎる発言・無い部屋・受け付けない端末・1 日の上限は、決めた誤りの形で断る。
5. `bundle exec rake test` が通る (自動の試験に 2〜4 が入っている)。rubocop の設定が asterism-tutor にあれば同じに。
6. fmruby-core は文書 (doc/asterism_chat/report/) と `rake docs:index` だけを変える。`git status` に取りこぼしなし。
   起こしたものは止めてある。

## 範囲と止まる条件

- 触ってよい所: 新しい `asterism-chat/`、fmruby-core の `doc/asterism_chat/report/c0.md`・`c1.md` と索引。
- **触らない**: fmruby-core のコード・flash・回帰の道具、asterism-tutor・asterism-games・console・asterism 本体
  (読むだけ)、graphics-audio、直下のリポジトリ、VPS・端末。
- fmruby-core は develop からブランチ `asterism-chat` を切って report をコミットする (この指示書は develop にコミット済み)。
  コミットの書き方は fmruby-core/CLAUDE.md。push しない・マージしない・`git add -A` を使わない。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る (asterism 本体や console の直しが要ると分かった、など)。
  利用者から見た動きを plan.md に無い形で決める必要が出た (選ばずに、選択肢にして返す)。
- 呼び名: 「チャットホスト」「AI ホスト」「端末」「Window」。
