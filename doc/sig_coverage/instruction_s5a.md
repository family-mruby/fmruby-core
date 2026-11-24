# S5a 指示書: データ・機器・絵の計算のクラスを sig に書く

> 2026-11-25 | 親セッションからサブ (Sonnet) への指示。計画は plan.md (5 章の S5、7 章の決定 15・16・20・22)。
> 書き方の正は `sig/README.md`。前の一括は report/s4b.md (同じ進め方。報告の形はこれに合わせる)。
> S5 は 2 つに分ける。これは前半 (データ・機器)。後半 (Asterism の残り・ROS 2 の橋・卓のゲームの部品) は S5b。

## 目的

明細 `tmp/sig_coverage/report.md` の「1. used by apps」「2. implemented, not in sig/」に残っているクラスのうち、次を埋める。

1. **データ**: `JSON` (`parse`・`generate`) と **`JSON::Digger`** (12。決定 15。`JSON` の内部向けの関数は書かない: 載せない
   一覧に入っている)、`MessagePack` (`pack`・`unpack` など実装にあるもの)、`FmrbToml`、`FmrbColors` (色の名前。14)、
   `Digest::SHA256` (6。決定 16)。
2. **機器**: `I2C`、`GPIO` (実装に 20。`GPIO::Error` も)、`FmrbHw` (ピンの定数など)、`FmrbNet` (HTTP の問い合わせなど、
   アプリが使うもの)、`Time` 以外の時計まわりで残っているもの。
   - 機器は、**端末に無い・つながっていないときの振る舞い** (例外の名前か nil か)、**使ってよいピンと使えないピン**
     (`FmrbHw` の定数や端末の案内 `flash/usr/share/doc/` に書いてあること)、sim・ブラウザ版・ネイティブ版で
     どうなるかが、つまずく所。実装と案内に書いてあることだけを書く。
3. **絵と音の計算**: `Fmrb::Fft` (13)、`Fmrb::Raycast` (15〜17)。裏側 (`FftNative`・`FftCore`・`RaycastNative`・
   `RaycastCore`) は載せない一覧に入っているので書かない。
4. **storage にあるライブラリのクラス** (決定 20): `FmrbReceiveConf` (`flash/usr/share/transfer/receive_conf.rb`)、
   `Unit8Encoder` (`flash/lib/`)。Ruby のファイルなので実装はそのファイル。`require` のしかたも長文に書く。
5. 明細に残る、上のどれにも当たらない小さなクラス (`RbConfig`・`ENV` など) は、アプリが使っていて実装があれば書く。
   判断に迷うものは書かずに「迷ったもの」に並べる。**Asterism・FmrbAsterism・TableClient・ROS は S5b なので書かない**。

## やること (fmruby-core、ブランチ `sig-s5a`)

1. `rake sig:coverage` を流し、`tmp/sig_coverage/report.md` から上のクラスの行を控える (前の数)。
2. **実装を読む** (推測で書かない)。どの gem がビルドに入るかを build config と CMake で確かめてから読む
   (`components/picoruby-esp32/picoruby/mrbgems/` の picoruby の gem、`lib/gems/`、`lib/upstream/` の差し替え・patch)。
   機種で入る・入らないが変わる gem (S3 と P4、sim) は、どのビルドに入るかを長文に書く。
   - キーワード引数と省略できる引数は、シグネチャに実装のとおりに書く (決定 22)。
   - CRuby の同じ名前のライブラリと違う所 (無いメソッド、違う戻り値、違う例外) は必ず書く。
3. **sig に書く**。`JSON` は `sig/json.rbs`、`MessagePack` は `sig/message_pack.rbs`、`FmrbToml`・`FmrbColors`・`FmrbNet`・
   `FmrbHw` は `sig/fmrb_misc.rbs` か、まとまりごとの新しいファイル (`sig/fmrb_net.rbs` など。今ある宣言があればそこ)、
   `I2C`・`GPIO` は `sig/i2c.rbs`・`sig/gpio.rbs`、`Digest` は `sig/digest.rbs`、`Fmrb::Fft`・`Fmrb::Raycast` は
   `sig/fmrb_fft.rbs`・`sig/fmrb_raycast.rbs`、ライブラリのクラスは `sig/fmrb_lib.rbs`。
   - 要約は `日本語 <<en>> English` で 111 バイトまで。素直なものは「例だけ」、つまずくものは型紙の順。
   - 例は同梱のアプリでの実際の使い方に合わせる (39 桁以内)。強調 `**` は 1 行の中で閉じる。
   - 新しいファイルの置き方で、サイトのリファレンスの領域分け (`tools/ti/gen_site.rb` の `AREAS`) が変わる。
     道具は触らず、足したファイル名を報告に並べる (親が領域に入れる)。
4. **確かめる**: `rake ti:test`、`rake regress`、`rake sig:coverage` (対象の「無い」が載せない候補だけになる。
   「3. doc gaps」は全部 0 のまま)。`rake sig:coverage:update` で基準を更新し、別コミットにする。型データベースの
   各領域の大きさの前後を控える。**ビルドと sim の確認はしない** (親が検収で行う)。

## 報告に必ず書くこと (doc/sig_coverage/report/s5a.md)

1. **根拠の表**: 書いたメソッドごとに、読んだ実装の `パス:行` と、どのビルドに入るか。失敗のしかた・呼べる場面を
   書いたものは、その根拠の行も。
2. 書いた数 (クラスごと)、素直 / つまずくの内訳。足した sig のファイル名。
3. 直した既存のシグネチャと長文 (前 → 後、根拠)。
4. **載せない候補** (内側と判断したもの。クラス#メソッドと理由)。迷ったものは別に並べる。
5. アプリが使っているのに実装に見つからなかったもの。
6. 数える道具の前後の数、型データベースの大きさの前後。
7. **親が sim で確かめるとよい項目 10〜15 個** (sim のシェルの irb に打つ式 → sig に書いた期待の値か例外。
   機器は sim に無いので、sim で何が返るか・どの例外かを書く。1 行 60 文字以内)。
8. 読んでいて見つけた実装の不具合の疑い (直さずに書く)。確かめていないこと。迷った所。

## 範囲と止まる条件

- 触ってよい: 上に挙げた sig のファイル (新規と、今ある宣言への追加)、`sdk/` (境界の作り直しが要るときだけ)、
  `tools/sig_coverage/baseline.json`、`doc/sig_coverage/report/s5a.md`、`rake docs:index` による `doc/README.md`。
- 変えない: 実装・アプリ・上の一覧に無い sig・`sig/README.md`・plan.md・`tools/` の道具と ignore.yml・型エンジンの試験の
  期待値 (試験が落ちたら止まって報告)。既にある宣言の要約。Asterism 関係の sig (`sig/fmrb_asterism.rbs`)。
- ビルドをしない (sim・ESP32・ブラウザ版・ネイティブ版とも。`rake clean`・`rake build:*` を流さない)。
- 量が多いので、まとまりごとにコミットしてよい。時間が足りなくなりそうなら、済んだ所までをコミットして報告に残りを書く。
- push しない・マージしない。ユーザに質問しない。実在の値 (ドメイン・IP・ノード名・鍵) を書かない
  (例の URL は `example.org` などの仮のものにする)。コメントとコミットは英語、報告は日本語 (常体)。
