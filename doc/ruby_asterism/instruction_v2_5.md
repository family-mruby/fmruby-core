# Asterism V2.5 指示書: ノードのプロフィール (asterism 0.5.0) とアイコンの表示

> 状態: 指示 | 更新: 2026-10-12 | asterism の gem に `Asterism.profile` (ノードの種類・表示名・どこで動いているか・アイコン) を足し、Family mruby の端末・CRuby・ブラウザ版が既定のプロフィールを出す。asterism-console はプロフィールを取りに行き、印とアイコンを図に出す

前提: plan.md の「V2.5: ノードの自己紹介とアイコン」(決定事項)、asterism の README・docs/api_review.md・CHANGELOG、asterism-console の docs/v2_2.md (今の印の決まり)、report/c8.md (1.0 に向けた API の残り)、doc/ruby_asterism/browser.md、doc/headless_s3/report/h3.md (S3 の端末に Asterism が入った)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 決まっていること (2026-10-12 ユーザ)

- API の名前は **`profile`**。`Asterism.profile(kind:, name:, icon:, host:)` (引数の細かい形は、今の portable API と CRuby API の流儀に合わせる。読み出す側の形も含めて docs に書く)。
- キーは `asterism/<ID>/@profile`。queryable で、MessagePack の Hash を返す。
  - `kind`: `family_mruby` / `picoruby` / `cruby` / `console`。sim とブラウザ版も `family_mruby`。`board` という呼び方はやめる。
  - `name`: 表示名 (任意)。
  - `host`: 任意。`atoms3r` / `tab5` / `p4nano` / `browser` / `sim` など。Family mruby のファームは自動で埋める。
  - `icon`: PNG のバイト列 (任意。PNG だけ。8 KB まで。受け取る側は PNG の先頭の印と大きさを確かめ、ほかは捨てる)。
  - `icon_hash`: アイコンの要約 (受け取る側が覚え、同じものは取り直さない。要約の計算は端末で軽いものを選ぶ)。
- 既定のプロフィール:
  - Family mruby の端末: ファームが出す。`kind: family_mruby`、`host` は端末の種類から自動、アイコンは `/usr/share/asterism/icon.png` (置き場所は Family mruby の流儀で決めてよい。無ければアイコンなし)。アプリが `Asterism.profile` で上書きできる。
  - CRuby: 既定で `kind: cruby` (アイコンは gem に同梱の既定、または無し。選んで docs に書く)。
  - ブラウザ版: `kind: family_mruby`、`host: browser`。
  - console 自身: `kind: console`。
- アイコンの絵: 既定のアイコン (Family mruby、ブラウザ版、CRuby、console) は、簡単なものを作ってよい (32x32 程度の PNG。生成は Ruby の zlib で書ける。tools/fmrb_png.rb などの流儀)。使う人が差し替えられる。
- 見送るもの (この段ではしない): アプリ単位のアイコン (ランチャーのアイコン)。プロフィールの形は、後でアプリ単位を足せる余地を残す。
- asterism の既存の「Ruby プロファイル」の表 (`docs/ruby_profile.md`、`profile/`) は、`profile` の API と紛れないよう、呼び名を「Ruby の対応表」(英語は feature table など) に言い換える (文書の言葉だけ。ファイル名を変えるなら、リンクも直す)。
- console:
  - 新しいノードが入ったら bridge がプロフィールを問い合わせる (1 回。`icon_hash` で覚え、変わったときだけ取り直す)。
  - 答えがあれば、`kind` で印を決め、アイコンを枠の名前の前に出す。`host` と `name` はノードの詳細に出す。
  - 答えが無いノード (古い版・ROS 2) は、今の名前の決まり (V2.2) と印のまま。
  - 凡例と印の説明を `board` から処理系の名前 (Family mruby / PicoRuby / CRuby / console) に直す。
  - 記録 (V4) と巻き戻しでも、プロフィールが出る (記録に入れるか、今の DB から引くかは選んで docs に書く)。

## 2. 作るもの

1. **asterism (gem)**: portable API (mrblib) と CRuby API に `profile` を足す。版を 0.5.0 にし、CHANGELOG・README・docs を直す。テスト (host_test と CRuby のテスト) を足す。CI が通る。
   - asterism-zenoh を直す必要があれば (無い見込み)、同じく 0.5.0 にそろえる (今までの版の付け方の流儀に従う)。
2. **fmruby-core**: ファームの既定のプロフィール (どこで出すかは、カーネルか Asterism を使い始めるアプリの側か、今の Asterism の取り込みの作りに合わせる)、既定のアイコン、`ASTERISM_PIN` を新しいコミットに。sim・P4-Nano (または AtomS3R)・ブラウザ版で確かめる。
3. **asterism-console**: 上の 1 章のとおり。
4. **docs**: asterism の README (profile の節)、console の docs (v2_5.md)、計画の言葉。

## 3. 受け入れ条件

1. CRuby・sim・ブラウザ版・端末 (AtomS3R か P4-Nano) が、それぞれ既定のプロフィールを出し、console の図に処理系の印とアイコンで出る。`host` が詳細に出る。
2. アプリが `Asterism.profile` で表示名やアイコンを上書きすると、console に反映される (アイコンを変えたら取り直す)。
3. プロフィールを出さないノード (古い版の CRuby など) は、今の印のまま。PNG 以外・8 KB を超えるアイコンは捨てられる (テストで)。
4. asterism のテスト (手元の Ruby で。CI は push のときに親が回す)、fmruby-core の `rake test`、console のテストが通る。Retro・Tab5 のビルドが通り、内蔵 RAM が増えない。
5. 端末の内蔵 RAM の増加が無い (プロフィールとアイコンは PSRAM か flash)。

## 4. 範囲・決まり

- **asterism / asterism-zenoh**: main に直接ではなく、ブランチ `feature/profile` にコミットする (push しない。CI と同じ確かめは手元で回す)。**main へのマージ・タグ・gem の公開はしない** (親が検収し、ユーザが公開する)。`.gem` を作るのは親が行う。
- **fmruby-core**: develop から `feature/asterism-v2-5`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない**。`ASTERISM_PIN` は `feature/profile` のコミットを指してよい (マージ後に親が直す)。
- **asterism-console**: ブランチ `feature/v2-5` にコミットする (push しない。main へのマージは親が検収後に行う)。Gemfile は今の path 指定のまま。
- **触らないもの**: `.env`、sdkconfig、submodule、fmruby-graphics-audio、公開済みの gem の版 (0.4.x) の中身。
- **端末**: AtomS3R (`/dev/ttyACM1`、WiFi、ip は mDNS 名から) と P4-Nano (192.168.10.10) を使ってよい。焼き直しが要るなら AtomS3R で。端末の IP・mDNS の名前・MAC はリポジトリに書かない。
- **終わったら**: 起動したものを全部止める。`build/` は Linux の標準構成 (x86-64)。`rake clean_all` を使ったら wasm の libmruby を作り直す (emsdk の環境を読み込んでから)。
- **止まる条件**: ここに無い仕様の選択 (使う人から見える名前・形)。公開済みの版を変える必要。
- **report**: `doc/ruby_asterism/report/v2_5.md`、Japanese plain style (常体)。
