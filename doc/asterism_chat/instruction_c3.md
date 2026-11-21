# C3 指示書: 端末のチャットのアプリにスタンプを足す

> 2026-11-21 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/asterism_chat/plan.md (決定 8・8b・11・13・15・20・21)、
> report/c0.md の 3 章、**report/c3h_c4a.md (ホストの側の線の形。正は隣の gem `../asterism-chat` の README.md)**、
> report/c2.md (今のアプリ)・c3a.md・c3b.md (スプライトエディタ、ビューワー、PNG の透明、`0x01`)、
> 今の `flash/app/modern/chat.app.rb`・`chat/`、`main/ruby/default_app/appstore.app.rb` (パックの入り方と `/app/usr/<id>/`)、
> 画像の API (`sig/fmrb_gfx.rbs` の `SpriteImage`・`draw_tile`・`BMP332.parse`・`encode_png`)、sha256 の関数 (端末にあるものを grep)、
> アプリの書き方の skill (直下の `.claude/skills/fmrb-app-new/SKILL.md`)。

## 1. 作るもの

1. **スタンプの一覧**: 入力の行の近くのボタンで開く。出すのは
   - App Store で入れた**スタンプのパック** (`/app/usr/<id>/<id>.app.toml` に `stamp_pack = true` があるもの。その中の `*.png`)、
   - **自分の絵** (`/home/stamps/*.png`。スプライトエディタの既定の保存先。48x48 以下)。
   どちらも無いときは、どうすれば増やせるか (App Store でパックを入れる・スプライトエディタで描く) を短く出す。
   **配布イメージ (flash/ の追跡分) にスタンプの絵を 1 枚も入れない** (決定 8b)。
2. **送る**: 一覧で選ぶと送る。ホストの流れは `post(stamp: sha)` → `no_stamp` なら `stamp_put` → 同じ `post`。
   - 送る前に絵を**端末の中で読み直して PNG に書き直す** (`BMP332.parse` → `encode_png`): PC で描いた α つきの PNG も
     透明が `0x01` になり (c3b)、パレットの小さい PNG になり、描画側の `load_bmp` で黒くならない。sha256 は書き直した
     中身で取る。48x48 を超える・8 KB を超える絵は一覧に出さないか、選んだときに分かる文を出す。
   - バイナリの渡し方 (mruby の文字列が msgpack で str と bin のどちらで出るか) を最初に実物で確かめる。ホストは
     どちらでも受ける。`stamp_get` は `"as"` で形を選べるので、端末が受けやすいほうを使い、report に書く。
3. **出す**: スタンプの発言は絵で出す (決定 15: `0x01` は透明)。大きさは c0 の案 (16x16 は 2 倍の 32、それ以上は等倍か
   収まる整数倍。細部は決めて report に)。手元に無い sha は `stamp_get` で取り、端末に貯める (置き場は c0 の案
   `/home/stamps/.cache/` か、`/home` を汚さない別の所 (`/cache/app/chat/` など) を読んで決める。**自分の絵の一覧に
   貯めた絵が混ざらないこと**)。取っている間は場所を空けて「(スタンプ)」などを出し、届いたら描き直す。
   取れなかった (ホストから消えた) ときの表示。
   - 絵の持ち方: スプライトの絵は機械全体で 64 枚の共有。アプリは最近使った順に数枚 (見立て 8) だけ持ち、超えたら
     古いものを離す。Window を閉じる・部屋を変えるときに離す。1 画面に種類が多いときの重さを sim で見る。
4. 貯めた絵を、受け取った人が自分のスタンプとして使えるようにするか (一覧に「もらった絵」を出す) は**入れない**。
   入れるかどうかの選択肢を report に書く。
5. 端末の案内 (`flash/usr/share/doc/chat*.md`) に足す。
6. (c3b の Q2) ブラウザ版の束に `config/associations.toml` が入っていない件: `rakelib/wasm.rake` で `/etc` に写す
   (2 行の見立て)。絵・md などの関連付けがブラウザ版でも効くようにする。別コミット。

## 2. スタンプのパック (試験用。公開はしない)

- 形: `/app/usr/<id>/` に `<id>.app.toml` (`stamp_pack = true`、名前、版) と PNG を平らに (c0 3.5)。
- 試験用のパックを 1 つ、**リポジトリの外** (スクラッチ) に作る (絵は自分で 6〜8 枚、16x16 か 32x32 のドット絵。
  顔・ハート・OK・星など。`0x01` の透明を使う)。sim とブラウザ版には手で置いて (`web_fs`・sim の flash_local か /app/usr への
  置き方を読む) 試す。App Store の別の置き場 (`/home/appstore_base.txt`) から入れる所まで試せるなら試す
  (手元の HTTP サーバに一覧と絵を置く。family-mruby-apps の道具がスクリプトなしを断る件は、**直さない**:
  公開のリポジトリなので。手元で一覧を手書きして試すのはよい)。
- 見本のパックをどこに置いて残すか (family-mruby-apps に出す形にした一式を、スクラッチでなく残る所へ) は、親に返す。

## 3. 受け入れ条件

1. ローカルのルータ (c2 と同じ起こし方) + チャットホスト (`../asterism-chat` 0.2.0) + sim の Modern + ブラウザ版:
   sim にだけパックを置き、sim から送ったスタンプがブラウザ版 (パックなし) に絵で出る。逆向きも。透明が抜けている。
   画面の写しで確認 (何が見えたかを report に)。
2. 同じ絵の 2 回目は `stamp_put` しない (ホストの記録で `new` が 1 回)。受ける側も 2 回目は取りに行かない。
3. スプライトエディタで描いて `/home/stamps/` に保存した絵が、アプリを開き直すと一覧に出て、送れる。
   α つきの PNG (PC で描いた想定。スクラッチで作る) を `/home/stamps/` に置いて送ると、相手に透明が抜けて出る。
4. パックも自分の絵も無いとき、一覧に案内が出る。大きすぎる絵・1 日の上限・ホストの満杯は分かる文。
5. スタンプの混ざった 50 件の部屋でスクロールして、落ちない・重くない。VM プール (開いて、スタンプ 8 種類を出した後) を report に。
6. `git ls-files flash | grep -i stamp` に絵が無い (案内の文書は可)。
7. `rake regress` が通る。最後に 1 回 `rake regress:screens`。`git status` に取りこぼしなし。起こしたものは止めてある。

## 4. 範囲と止まる条件

- 触ってよい所 (fmruby-core): `flash/app/modern/chat*`、`flash/usr/share/doc/chat*.md`、`rakelib/wasm.rake` (1 章の 6 だけ)、
  `doc/asterism_chat/report/c3.md`、`rake docs:index` の結果。`../asterism-chat` は、端末とつないで見つかった不具合の直しと
  試験の追加だけ (線の形を変えるなら止まって返す)。
- **触らない**: カーネル・C・画像の gem・App Store・ランチャー・スプライトエディタ・ビューワー・ほかのアプリ、
  graphics-audio (描画側の `load_bmp` の透明は直さない。1 章 2 の書き直しで避ける)、回帰の道具と基準、sdkconfig、submodule、
  family-mruby-apps (公開)、asterism-tutor・console。**GPD・P4-Nano・VPS に触らない。端末に焼かない。**
- ブランチ `asterism-chat` の上にコミット。push しない・develop へマージしない・`git add -A` を使わない。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る (App Store の直しが要る、sha256 が端末に無い、など)。
  利用者から見た動きを plan・c0 に無い形で決める必要が出たとき (選ばずに、選択肢にして返す)。
- この mruby には `NilClass#to_i`・Regexp・`Array#pack` が無い。ブロックは重い。呼び名: 「チャットホスト」「端末」「Window」。
