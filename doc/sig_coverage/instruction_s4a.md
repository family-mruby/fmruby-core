# S4a 指示書: UI の部品・FmrbApp の残り・アプリ間のメッセージ・FmrbI18n・FmrbAssoc を sig に書く

> 2026-11-25 | 親セッションからサブ (Sonnet) への指示。計画は plan.md (5 章の S4、7 章の決定 11・16・18・22)。
> 書き方の正は `sig/README.md`。前の一括は report/s2c.md・s3.md (同じ進め方。報告の形はこれに合わせる)。
> S4 は 2 つに分ける。これは前半 (画面の部品とアプリの土台)。後半 (音・タイル・P5) は S4b。

## 目的

アプリが画面の部品とアプリの土台を使うときの API のうち、実装にあって sig に無いものを埋める。

1. **`FmrbUI` と部品** (`lib/gems/picoruby-fmrb-app/mrblib/fmrb-ui.rb`): 部品を作るメソッド (`label`・`button`・`toggle`・
   `stepper`・`text_field`・`scrollbar` ほか、実装にある全部)、値を読む・変えるメソッド (`value`・`set_value`・
   `field_text`・`set_field_text`・`set_text`・`set_range`・`set_visible`・`move`・`focus` ほか)、`handle`・`flush`・
   `invalidate_all` など。部品のクラス (`FmrbUI::Label`・`Button`・`Toggle`・`Stepper`・`TextField`・`Scrollbar` ほか) の
   **アプリが呼ぶメソッド**。
   - **決定 11**: 部品を動かすための内側 (`FmrbUI::Widget` の `draw_widget`・`press`・`release`・`relayout`・`activate`・
     `fit_widget_text` など、FmrbUI 自身が呼ぶもの) は**書かない**。アプリ (`flash/app/`・雛形・見本) が呼んでいるか、
     mrblib のコメントがアプリ向けと言っているものだけ書く。書かなかった内側は、報告に「載せない候補」として
     クラス#メソッドと理由を並べる (ignore.yml は親が直す)。
   - `FmrbUI::HitAreas` (画面を毎回描き直すアプリのクリックの当たり判定) も同じ基準。
2. **`FmrbApp` の残り** (実装 80 のうち sig に無い 18 ほど: `wheel_rows`・`wheel_notches`・`app_settings`・`closable=`・
   `resize_corner`・`draw_resize_mark` ほか)。アプリ向けのものを書き、内側は「載せない候補」へ。
3. **アプリ間のメッセージ** (決定 18): `send_message`・`receive_message`・`broadcast_message`・`message_queue_exists?`
   (Kernel の C の 4 件。`FmrbApp` の `send_message` との関係も実装のとおりに)。メッセージの形 (Hash の鍵)、
   届かないときの戻り値、大きさの上限は、つまずく所なので実装を読んで書く。
4. **`FmrbI18n` の残り 3** (`strings`・`forget`・`half_kana?`) と **`FmrbAssoc`** (5。ファイルの拡張子と開くアプリの
   関連付け。決定 16)。

## やること (fmruby-core、ブランチ `sig-s4a`)

1. `rake sig:coverage` を流し、`tmp/sig_coverage/report.md` の「2. implemented, not in sig/」から上の 1〜4 のクラスの行と、
   「1. used by apps」からそれらのメソッドの行を控える (前の数)。
2. **実装を読む** (推測で書かない)。mrblib の Ruby と、C の定義。アプリでの使い方も読む。
   - キーワード引数と省略できる引数は、**シグネチャに実装のとおりに書く** (決定 22。`?` と `name:`)。
     既にある宣言のシグネチャが実装と食い違っていたら、**直してよい** (S3 の検収で親が `label`・`button`・`toggle`・
     `stepper`・`text_field`・`publish` を直した。同じ直し方)。直したものは報告に、前の形・後の形・根拠の行を並べる。
   - 部品は「作れるのは `on_create` の中だけか」「作った後に変えられるもの・変えられないもの」「`handle` が何を返すか」
     「`flush` をいつ呼ぶか」が、つまずく所。mrblib のコメントに書いてあることを長文に書く。
3. **sig に書く**。`FmrbUI` と部品は `sig/fmrb_ui.rbs`、`FmrbApp` は `sig/fmrb_app.rbs`、メッセージは実装の受け手に
   合わせたファイル (`sig/kernel.rbs` か `sig/fmrb_app.rbs`)、`FmrbI18n` は今ある宣言のファイル、`FmrbAssoc` は
   `sig/fmrb_misc.rbs` か新しい `sig/fmrb_assoc.rbs`。
   - 要約は `日本語 <<en>> English` で 111 バイトまで。素直なものは「例だけ」、つまずくものは型紙の順。
   - 例は同梱のアプリでの実際の使い方に合わせる (39 桁以内)。強調 `**` は 1 行の中で閉じる (`rake sig:coverage` が数える)。
   - `%a{fmrb.boundary}` の付いたクラス (`FmrbApp` など) に足すときは、`sig/README.md` の「本体との境界の印」の決まりを
     読む (省略できる引数・キーワードを持つ Ruby の包みは `%a{fmrb.internal}` を付ける)。境界の検査が落ちたら
     `ruby tools/boundary/gen.rb` で作り直して一緒にコミットし、`sdk/` の差分の中身を報告に書く。
4. **確かめる**: `rake ti:test`、`rake regress`、`rake sig:coverage` (対象の「無い」が、載せない候補だけになる。
   書き方の欠け「3. doc gaps」は 0 のまま)。`rake sig:coverage:update` で基準を更新し、別コミットにする。
   型データベースの各領域の大きさの前後を控える。**ビルドと sim の確認はしない** (親が検収で行う)。

## 報告に必ず書くこと (doc/sig_coverage/report/s4a.md)

1. **根拠の表**: 書いたメソッドごとに、読んだ実装の `パス:行`。失敗のしかた・呼べる場面を書いたものは、その根拠の行も。
2. 書いた数 (クラスごと)、素直 / つまずくの内訳。
3. **直した既存のシグネチャ** (前の形 → 後の形、根拠)。
4. **載せない候補** (内側と判断したもの。クラス#メソッドと理由。親が ignore.yml に入れる)。判断に迷ったものは別に並べる。
5. アプリが使っているのに実装に見つからなかったもの。
6. 数える道具の前後の数、型データベースの大きさの前後。
7. **親が sim で確かめるとよい項目 10〜15 個** (打つ式か、エディタに打つ行 → sig に書いた期待。1 行 60 文字以内。
   UI の部品はシェルの irb では作れないかもしれないので、Ctrl+E で「指摘されない / される」を見る形でもよい)。
8. 読んでいて見つけた実装の不具合の疑い (直さずに書く)。確かめていないこと。迷った所。

## 範囲と止まる条件

- 触ってよい: `sig/fmrb_ui.rbs`・`sig/fmrb_app.rbs`・`sig/fmrb_misc.rbs`・`sig/kernel.rbs`・`sig/object.rbs` (メッセージの
  受け手に合わせて)・`FmrbI18n` の宣言のあるファイル・`sig/fmrb_assoc.rbs` (新規でもよい)、`sdk/` (境界の作り直しが
  要るときだけ)、`tools/sig_coverage/baseline.json`、`doc/sig_coverage/report/s4a.md`、`rake docs:index` による `doc/README.md`。
- 変えない: 実装・アプリ・上の一覧に無い sig・`sig/README.md`・plan.md・`tools/sig_coverage/` の道具と ignore.yml・
  型エンジンの試験の期待値 (試験が落ちたら止まって報告)。既にある宣言の**要約**は変えない (シグネチャは上の決まりで
  直してよい。長文に足すのもよい)。
- ビルドをしない (sim・ESP32・ブラウザ版・ネイティブ版とも。`rake clean`・`rake build:*` を流さない)。
- 量が多いので、まとまりごとにコミットしてよい。時間が足りなくなりそうなら、済んだ所までをコミットして報告に残りを書く。
- push しない・マージしない。ユーザに質問しない。実在の値 (ドメイン・IP・ノード名・鍵) を書かない。
  コメントとコミットは英語 (件名は `feat(sig): ...`・`fix(sig): ...`・`test(sig_coverage): ...`・`docs(sig_coverage): ...`)、
  報告は日本語 (常体)。
