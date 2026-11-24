# sig (API の型定義) を充実させる

> 状態: 進行中 | 更新: 2026-11-24 | `sig/*.rbs` を、アプリを書く人と AI ホストの両方が頼れる所まで埋める。抜けを数える道具 (S0)・書き方の型紙 (S1) を先に作り、アプリがよく使う順に足す (S2〜S5)。調査の数字は 2 章、決定は 7 章 (上流由来のファイルも直接直す・キーワード引数は今のまま・道具は最初は報告だけ)。S0〜S3 済 (道具 `rake sig:coverage`、型紙は sig/README.md、FmrbConst・Math・Machine・GC・乱数・File・Dir・Time・IO、書き方の欠けは 0、report/s0.md〜s3.md)。S0〜S5 済 (sig 125 クラス・1,470 メソッド)。S6 済。次は S7 (サイトの公開)

## 1. 目的

`sig/*.rbs` は Family mruby の API の形の正で、3 つの読み手がいる。

- **エディタ**: 補完 (Tab)・型の表示 (Ctrl+T)・まちがい探し (Ctrl+E)・F1 のヘルプ。
- **AI ホスト (asterism-tutor)**: 毎回 LLM に渡す知識のバンドルに型の要約を入れ、足りないときは LLM が `sig/` を
  検索して長文と例を読む (doc/ai_coding 7c)。**sig に無い API は、AI が使えないか、推測で書く**。
- **境界の生成** (doc/app_boundary): 各言語のつなぎ。

A6・A6b の通しで分かったこと: 型と短い見本を足すと、軽いモデル (Haiku) でも通信のアプリを正しく書けた。逆に、
型が無かった間は作れなかった。sig を充実させることが、AI が作るアプリの質にそのまま効く (2026-11-22 ユーザ
「sig を充実させるのが大切」)。

目標は次の 2 つ。

1. **アプリが実際に使っている API が sig にある** (同梱のアプリ・雛形・見本が呼ぶものを基準にする)。
2. **sig の各メソッドに、呼び方が分かる例がある** (引数の意味・失敗のしかた・使う順番は、つまずく所だけ)。

## 2. 今の状態 (2026-11-24 の調査)

正規表現での粗い集計 (道具は S0 で作り直す)。対象のアプリは `flash/app/` (試験を除く)・雛形・見本の 80 ファイル。

### 2.1 量

| | 数 |
|---|---|
| sig 全体 | 67 クラス・815 メソッド |
| うち Family mruby の分 (`fmrb_*.rbs` ほか) | 47 クラス・609 メソッド |
| 要約あり | 609 (全部) |
| 英語あり (`<<en>>`) | 584 (無し 25) |
| 例 (` ```ruby `) あり | 517 (**無し 92**) |

例の無いメソッドの内訳: `fmrb_app.rbs` 27、`fmrb_gfx.rbs` 26、`fmrb_system.rbs` 20、`fmrb_ui.rbs` 15、ほか 4。

### 2.2 アプリが使っているのに sig に無いもの

クラス (呼び出しのある定数。数は出てくる回数):

| クラス | 回数 | 中身 |
|---|---|---|
| `Machine` | 122 | 時刻・待ち (`board_millis` が 104 回、`delay_ms` ほか) |
| `Math` | 41 | `sin`・`cos`・`sqrt`・`atan2` |
| `GC` | 32 | `start`・`stat` |
| `Dir` | 19 | 一覧・作る |
| `FmrbReceiveConf` | 15 | 受け取りの設定 (システム寄り) |
| `Fmrb::Fft` | 12 | FFT |
| `Fmrb::Raycast` | 8 | レイキャストの核 |
| `FmrbColors` | 5 | 色の名前 |
| `MessagePack`・`FmrbToml`・`JSON` | 4・4・3 | データの読み書き |
| `FmrbAsterism::TableClient` | 3 | 卓のゲームの部品 |
| `I2C`・`FmrbNet`・`FmrbHw`・`Time` | 2〜3 | 機器・通信・時刻 |

メソッド名 (どのクラスの sig にも無い名前、多い順): `board_millis` 104、`byteslice` 34、`sin` 20、`freeze` 20、
`cos` 15、`round` 11、`pack` 11、`draw_text_mixed` 9、`random_int` 8、`micros` 6 ほか (全部で 166 種)。
`byteslice`・`freeze`・`round` は上流由来の `string.rbs`・`object.rbs`・`float.rbs` に足りない分。

### 2.3 実装にあって sig に無いもの (Ruby の側)

mrblib の公開メソッドと sig の突き合わせ。内部用が多く混じるので、S0 で「アプリ向けか内部か」を分ける。目立つもの:

| クラス | 実装 | sig にある | 無い | 例 |
|---|---|---|---|---|
| `FmrbApp` | 80 | 62 | 18 | `wheel_rows`・`app_settings`・`closable=` |
| `FmrbUI` の部品 (`HitAreas`) | 34 | 4 | 30 | `label`・`button`・`toggle`・`stepper`・`text_field` |
| `MmlPlayer` | 38 | 12 | 26 | `load_file`・`load_text`・`tempo_scale` |
| `SmfPlayer` | 47 | 13 | 34 | `load_string`・`timing_stats` |
| `FmrbAudio` | 7 | 3 | 4 | `mic_available?`・`mic_read` |
| `FmrbMidi` | 9 | 0 | 9 | `gm_name`・`device`・`scheduler` |
| `File` | 8 | 1 | 7 | `join`・`basename`・`extname` |
| `TileMap` | 11 | 5 | 6 | `load_json`・`load_packed` |
| `P5` | 47 | 41 | 6 | `font_height` |

C で定義したメソッド (`mrb_define_method`) は、この調査では数えていない (S0 で足す)。

### 2.4 器の上限

- 型データベースの要約の領域: 24 ビットの位置になり、上限は実質なくなった (2026-11-24、picoruby-ti `75fdbc3`)。
  今は 53,094 バイト。
- 名前の領域 6,685 バイト、シグネチャの領域 27,821 バイト (どちらも上限 65,535 のまま)。メソッドを倍にすると
  シグネチャの領域が上限に近づく。
- クラスの番号は 8 ビット (上限 255)。今は 68。
- どれも flash に入る (内蔵 RAM は使わない)。ファームの大きさは `rake regress:size` で見る。
- 長文と例は `flash/help/` のファイルになり storage に入る。今は 123 フォルダ・約 700 KB。storage は Retro で
  余裕が小さい (doc/flash_layout)。足すたびに大きさを測る。

## 3. 方針

1. **数えてから書く**: 抜けを数える道具を先に作る (S0)。足す順番と、終わったかどうかの判定をこの道具に任せる。
2. **アプリがよく使う順**: 2.2 の回数の多いものから。子どもが最初に書く範囲 (時刻・乱数・数学・文字列・ファイル)
   を先に、機器や通信の細部は後に。
3. **実装を読んで書く**: 推測で書かない。型と実装が食い違っていたら、実装を直さず報告に書く (直すかはユーザ)。
4. **内部用は載せない**: `_` で始まるもの、アプリから呼ぶ想定の無いものは sig に書かない。道具には
   「わざと載せない」の一覧を持たせ、数に入れない。
5. **書き方は 1 つに揃える**: 型紙 (S1) を `sig/README.md` に足す。`fmrb_asterism.rbs` (A6b) の書き方
   (引数の意味・失敗・順番・短い例) を、つまずく API の手本にする。素直な API は例 1 つだけ (README の今の決まり)。
6. **段ごとに AI ホストで確かめる**: 足した範囲を使う頼みを決めておき、足す前と後で返るコードを比べる (S6 の形を
   各段の終わりに小さく回す)。

## 4. スコープ

### 入れるもの

- `sig/` の追加と直し。上流由来の 15 ファイル (`string.rbs` など) も、足りないメソッド (`String#byteslice` など)
  を直接足す (決定 1)。
- 抜けを数える道具 (`rake sig:coverage`、Ruby)。
- `sig/README.md` の型紙。
- 足りない所を埋める短い見本のアプリ (要るときだけ。A6b の `talk_*` と同じ扱い)。
- 型エンジン (picoruby-ti) の上限の手当て (シグネチャの領域が足りなくなるとき。要約と同じ 24 ビット化)。

### 入れないもの

- API そのものの変更・追加 (実装は変えない)。
- 省略できる引数・キーワード引数を型エンジンが扱えるようにする改造 (今は「いちばん多い呼び方を必須引数で書く」
  決まりだったが、エンジンは既に扱えると分かった。決定 22)。
- AI ホストの側の変更 (バンドルの作り方、検索の範囲)。sig が増えた結果の確認だけをする。

## 5. 段階

| 段 | 中身 | 確認 |
|---|---|---|
| S0 (済 2026-11-24) | **抜けを数える道具** `rake sig:coverage`: (1) アプリ・雛形・見本が使うクラスとメソッドのうち sig に無いもの、(2) 実装 (mrblib の公開メソッドと C の定義) にあって sig に無いもの、(3) 例・英語・要約の長さ (112 バイト) の欠け。「わざと載せない」の一覧 (理由つき) を読んで除く。今の数を基準として記録する | 道具の出す数が 2 章の調査と大きく違わない (違う所は理由を報告)。`rake regress` には入れず、報告として出す (決定 3) |
| S1 (済 2026-11-24) | **型紙と上流由来のファイル**: `sig/README.md` に、つまずく API の書き方の型紙 (引数の意味・戻り値と失敗・使う順番・短い例) と、キーワード引数の書き方を足し、「上流由来の 15 ファイルは変更禁止」を「直接足してよい (試験の期待値も直す)」に改める。1 つ (`String#byteslice`) で通す | 型紙どおりに書いた 1 件が、エディタの補完・F1・AI ホストの検索の 3 つで読める |
| S2 (済 2026-11-25) | **最初に使う範囲**: `FmrbConst` (キーの番号・テーマの色・`BOARD`)、`Machine` (時刻・待ち)、`Math`、乱数、`GC`、`String`・`Array`・`Float` の足りない分、`File`・`Dir`・`Time` | S0 の (1) で、この範囲の「無い」が 0。AI ホストへの決まった頼み (時間で動くアニメーション、ファイルの読み書き) が調べ物なしか 1 回で正しく返る |
| S3 (済 2026-11-25) | **例の無い 92 件と長文の無い 3 件を埋め、行をまたぐ強調 42 行を直す** (道具に「強調が行をまたぐ」を足す): `fmrb_app`・`fmrb_gfx`・`fmrb_system`・`fmrb_ui` ほか (英語の無い要約は、数え直すと 0 件だった) | S0 の (3) の「例なし」「長文なし」が 0 (わざと載せない分を除く) |
| S4 (済 2026-11-25) | **部品と音**: `FmrbUI` の部品 (label・button・toggle・stepper・text_field・スクロールバー。アプリが呼ぶ分)、`FmrbApp` の残り、Kernel のメッセージ 4 件、`MmlPlayer`・`SmfPlayer`・`FmrbMidi` (送り先はアプリが呼ぶ分)・`FmrbAudio` (マイク)、`TileMap`・`P5` の残り、`FmrbI18n`・`FmrbAssoc` | S0 の (2) で、この範囲の「無い」が、わざと載せない分だけになる。決まった頼み (ボタンと入力欄のある画面、MML を鳴らす) |
| S5 (済 2026-11-25) | **データ・機器・通信の残り**: `JSON` (`Digger` も)・`MessagePack`・`FmrbToml`・`FmrbColors`・`Digest::SHA256`・`Fmrb::Fft`・`Fmrb::Raycast`・`I2C`・`FmrbNet`・`FmrbHw`・卓のゲームの部品 (`TableClient`)・Asterism の残り全部 (`Connection`・`Credentials` ほか、ROS 2 の橋、`FmrbAsterism` のモジュール関数)・storage のライブラリ (`FmrbReceiveConf`・`Unit8Encoder`) | 同上。機器の無い端末でどうなるか (失敗のしかた) が書いてある |
| S6 (済 2026-11-25) | **まとめの確認**: 全体を通して、エディタ (補完・F1)、ファームと storage の大きさ、AI ホストの通し (決まった頼み 10 個ほどを、足す前の版と比べる)。`doc/reference/` に「sig の書き方と数え方」を残す | 大きさの増分が記録されている。頼みの結果の比較が報告にある |

- **S2a 済 (2026-11-24、report/s2a.md)**: `FmrbConst` の定数 162 と `bt_mac` (`sig/fmrb_const.rbs`)、`Math` の関数 26・
  `PI`・`E`・`DomainError` (`sig/math.rbs`)。アプリが使う未定義の定数は 76 → 18。親の検収: 定数の名前 162・
  キーの番号の値 87・`Math` の関数名 26 が実装と全部一致、例の値 11 個が合う、sim (Retro) で `FmrbConst::KEY_U`・
  `Math.sq` の補完と要約、`byteslice` の Ctrl+E (1 引数と範囲は通り、3 引数だけ指摘) を確認。型データベースは
  要約 65,104 バイト・シグネチャ 33,679・名前 9,129、定数 191。定数の長文は F1 に出ない (ヘルプの生成が定数を
  扱わない) ので、定数は要約だけ。書かなかった内部用の定数: `APP_CTRL_*`・`MAX_PATH_LEN`・`LED_ERR_*`。
- **S2b 済 (2026-11-24、report/s2b.md)**: `Machine` 24 (`sig/machine.rbs`)、`GC` 20 (`sig/gc.rbs`)、`RNG` 3
  (`sig/rng.rbs`)、`Kernel#rand`、上流由来のクラスに 9 (`Object#equal?`・`freeze`・`respond_to?`、`Float#round`、
  `Integer#round`、`String#rindex`、`Array#select`、`Hash#key?`・`each_key`)。sig は 73 クラス・914 メソッド。
  親の検収: 名前は実装と全部一致、sim のシェルで `round` の 5 例・`rand` の戻り値の型・`GC.stat` のキー 10 個・
  `key?`・`rindex`・`delay_ms(300)` (sim で 335 ms) が sig の記述どおり、エディタで `Machine.board_m` の補完。
  残り: `Object.const_defined?`・`remove_method` (`sig/class.rbs` の領分。S2c で足す)。**見つかった実装の不具合の疑い**:
  ESP32 の `Machine.delay_ms` が指定の約 1/10 しか待たない (doc/reference/TODO.md に記録、ユーザの判断待ち)。
- **S2c 済 (2026-11-25、report/s2c.md)**: `File` 15 と `SEPARATOR`・`Dir` 24・`Time` 52・`IO` 7・`Module#const_defined?`・
  `remove_method`。sig は 77 クラス・1,015 メソッド。親の検収 (sim のシェルで実際に動かした): 無いファイルの
  `File.size`・`File.open` が RuntimeError (メッセージも記述どおり)、`write` の戻り値 2、終わりでの `read` が ""、
  `Dir.children` の失敗、`Time.at(0).utc.to_s`・`Time.local(...).to_s`・`Time + Time` の TypeError、
  `Object.const_defined?`、`Dir` の `exists?`・`rmdir`・`unlink`・`pwd`・`pos`・`tell` があること。sim では
  `Dir.mkdir("/tmp/d1")` は 0 を返して通る。前からある `sig/fmrb_misc.rbs` の `File` が実装と食い違っていたので、sim で確かめて直した (`File#gets` は
  NoMethodError になるので消し、`read` の長文で代わりの書き方を示した。`File.open` の mode は省略でき、ブロックの形も
  書いた。`File#read` は長さを取る。エディタの Ctrl+E が正しい `File.open(path)` を「引数が足りない」と出していた)。
  追加で確かめたこと: `a+` は追記にならず先頭から上書き、読み取りで開いて書くと RuntimeError "write failed"、
  `"w"` で開いて読むと ""、エディタで `f.rea`・`Time.no`・`Dir.chil` の補完。
  **見つかった実装の不具合の疑い** (直していない): `File.path("x")` が nil を返す (sim で確認)、`File.expand_path` の
  基準が `ENV["PWD"]`、ESP32 のファイル HAL の経路が 128 バイトで黙って切れる読み、`Dir.chdir` の後の相対パスが
  ESP32 と sim で違うかもしれない。
- **S3 済 (2026-11-25、report/s3.md)**: 例を 102 件・長文を 3 件・英語側を 16 件足し、行をまたぐ強調 122 行を直した。
  数える道具の書き方の欠けは全部 0 (対象は上流由来の 15 ファイル以外の 23 ファイル・780 メソッド。`FmrbApp#destroy`・
  `main_loop` は `start` が呼ぶ内側なので、例なしのまま載せない一覧へ)。道具に「強調が行をまたぐ」を足し、コードの印
  (`` ` ``) の中の `**` は数えない。親の検収: 宣言 1,268 個のシグネチャと要約が 1 つも変わっていないことを機械的に照合、
  sim でサブの挙げた例 (`Machine`・`rgb_to_332`・`remote_access_paired` ほか) と F1 の強調の色を確認。
  サブが見つけた型と実装の食い違いを、sim の Ctrl+E で誤検知を確かめたうえで直した: `FmrbApp#publish(topic, ?data)`、
  `FmrbUI#label`・`button`・`toggle`・`stepper`・`text_field` のキーワードと省略できる引数、`TileMap#render` と
  `TileRing.new` の必須キーワード。直した後の sim で、正しい呼び方が指摘されず、引数の足りない呼び方だけが
  指摘されることを確認。`File.open(path)` の誤検知 (S2c) も消えた。
- **S4a 済 (2026-11-25、report/s4a.md)**: 44 メソッドを足した (`FmrbUI` の `scrollbar`・`set_range`・`direction`・`bg`、
  `FmrbUI::Scrollbar`、`FmrbApp` の設定・状態・ホイール・閉じるボタンほか 16、`Kernel` のメッセージ 4、`FmrbI18n` 3、
  `FmrbAssoc` 5 と定数 6)。既にあるシグネチャの直し: `FmrbUI.new` のキーワード、`enum` の `index:`、`HitAreas#add` の data、
  `FmrbApp#send_message` の戻り値 (bool)、`FmrbI18n.truncate_to` の省略できる引数、`FmrbGfx#sync_file` の `dest:`。
  FmrbUI の内側 73 個 (各部品の `.new`・`draw_widget`・`press` ほかと、基底の受け皿) は載せない一覧へ (決定 11)。
  sig は 79 クラス・1,058 メソッド、実装にあって sig に無い Ruby は 612 → 499。親の検収: 名前を実装と照合、sim の
  エディタで正しい呼び方 11 個が指摘されず、引数の足りない 2 個だけ指摘、シェルで `FmrbAssoc.resolve`・`truncate_to`。
  **見つかった不具合の疑い**: `Kernel.send_message` などの構造体が本体と違う (doc/reference/TODO.md、ユーザの判断待ち)。
- **S4b 済 (2026-11-25、report/s4b.md)**: 55 メソッド・定数 20・クラス 2 を足した (`FmrbMidi` の関数 5、`SmfPlayer` 22、
  `MmlPlayer` 14、`ApuTransport` 8、`MIDI::Device` 2、`FmrbAudio` のマイク 4、`FmrbMic.available?`、タイル・P5・絵の残り)。
  既にあるシグネチャ 38 個を実装に合わせた (`MIDI::Device` の `channel:` と省略できる velocity、プレーヤーの戻り値、
  `TileSheet.new`・`stamp`・`render_view` のキーワード、`FmrbAudio` の省略できる引数ほか。要約は 1 つも変えていない)。
  送り先の下回りとプレーヤーの内側 49 個は載せない一覧へ (決定 12)。sig は 81 クラス・1,117 メソッド、実装にあって
  sig に無い Ruby は 499 → 395。親の検収: 変えたシグネチャを実装と照合、sim のシェルで `gm_name`・MML と SMF の
  プレーヤー (読み込み・エラーの文・`start`・`tick`・`lookahead_us`)・マイク (sim では false・0・nil)・`png_path?` が
  記述どおり、エディタで正しい呼び方 7 個が指摘されず、引数や必須キーワードの足りない 2 個だけ指摘。
  **S4 完了**。型データベースのシグネチャの領域は 44,108 バイト (上限 65,535 の 67%)。S5 (約 300 メソッド) を足すと
  約 79% の見込みなので、**S5 の前にシグネチャの位置も 24 ビットにする** (要約と同じ手当て。8 章)。
- **S5a 済 (2026-11-25、report/s5a.md)**: 23 クラス・130 メソッドを足した (`JSON` と例外・`JSON::Digger`・`MessagePack`・
  `FmrbToml`・`FmrbColors`・`Digest::SHA256`・`FmrbHw`・`FmrbNet`・`I2C`・`GPIO`・`Fmrb::Fft`・`Fmrb::Raycast`・
  `FmrbReceiveConf`・`Unit8Encoder`)。新しい sig のファイル 10 個 (サイトの道具の領域分けにも入れた)。内側 32 個は
  載せない一覧へ。sig は 104 クラス・1,244 メソッド、アプリが使う未定義のクラスは 22 → 11。親の検収: sim のシェルで
  JSON (読み取り・書き出し・2 種類の例外・Digger)、MessagePack、TOML (読み取りとエラーの文)、色の名前、`FmrbHw`、
  `FmrbNet.ip_address`、FFT とレイキャストの例外が記述どおり。`I2C`・`GPIO`・`Digest` は sim に無い (記述どおり)。
  迷ったものとして残る: `FmrbReceiveConf` の 4 つ、`BMI270`・`RMT`・`Net::HTTP`・`URI`・`MbedTLS::Digest`、例外のクラス。
- **S5b 済 (2026-11-25、report/s5b.md)**: 226 メソッドを足した (`Asterism` の関数 6、`Connection` 29 ほか核のクラス、
  `Credentials`・`Types`・`Pubs`・`Sequences`・`Status`・`Profile`、`FmrbAsterism` の関数 10、ROS 2 の橋
  (`sig/asterism_ros.rbs`、約 80)、`TableClient` (`sig/fmrb_table_client.rbs`、37))。A6b の型 13 個を決定 22 の形に直した
  (`Link.new` のキーワード、`expose` の `methods:`、`[]`・`status_of` の `timeout:`、`publish`・`subscribe` ほか)。
  内側 56 個は載せない一覧へ。sig は 125 クラス・1,470 メソッド、実装にあって sig に無い Ruby は 293 → 6。
  親の検収: 要約が変わっていないことを機械的に照合、変えたシグネチャを実装の定義と照合、sim のシェルで 9 項目
  (`tls?`・`credentials_for` の nil と例外・`Types.number`・`Pubs.place`・`encoding_id`・`Profile.png_info`・
  `ROS.resolve`・`ROS.require_type` の例外) が記述どおり。**エディタの Ctrl+E では確かめられなかった**: 型エンジンが
  入れ子のクラス (`A::B`) を受け手に解けない (doc/reference/TODO.md。sig の誤りではない)。
  **S5 完了。sig を書く段はここまで**。
- **S6 済 (2026-11-25、report/s6_ai_host.md)**:
  - 大きさ (`rake regress:size`、S3 と P4、通過): 型データベースは sig の分で約 +160 KB (メソッドの行 2,541 → 5,156、
    シグネチャ 29.5 → 58.3 KB、要約 53.2 → 111.8 KB、名前 7.3 → 15.4 KB。見積もり)。F1 のヘルプは 229 ファイル・808 KB
    (storage。前は sig の量の比で約 360 KB)。**S3 のファームの区画の空きは 288 KB (5%)**、P4 は 1.37 MB (16%)。
    S3 の storage の像 (5 MB) は作れている。
  - AI ホスト (前 c6d0fbd8 / 後 develop、既定のモデル、頼み 10 個を 1 回ずつ): 20 個とも検査を通り、後の sig に実在しない
    API は 0。良くなった: キーの名前 (`FmrbConst::KEY_*`)、`JSON.generate`・`parse`、乱数 (`RNG.random_int`。前は
    「rand は使えない」と誤った説明)、`text_field` の `max:`、端末間のやり取り (呼び出し 7 回 → 3 回)。
    使われなかった: `MmlPlayer`・`Dir`・`Machine` (前も後も別の API で作った)。軽く悪くなった: 得点の保存に `app_state`
    (再起動で消える) を選んだ。バンドルは 120,890 → 183,028 バイト (+51%)、1 回の読みは 49,423 → 76,738 トークン (+55%)、
    10 個の費用の合計は前後ほぼ同じ (0.0322 → 0.0314 ドル。調べ物が減った分で取り返した)。
    サブは LLM の呼び出しの上限 (30 回) を頼みの数と読み違え、36 回流した (費用は 0.073 ドルで上限の内)。
  - この通しで見つけて直した sig: `draw_text` (背景の色と `mixed:`)、`fit_text` (印・印の幅・フォント)、
    `app_state`・`save_app_state` の要約に「再起動で消える」。
  - 参照資料: doc/reference/sig_writing_and_coverage.md。
- **定型の作業は Sonnet のサブに任せる** (2026-11-24 ユーザ)。S2a が試しで、質に問題は無かった。指示書に
  「読んだ実装のファイルと行を報告に書く」「確かめていないことを分けて書く」を入れ、親は名前と値を実装と機械的に
  突き合わせる。道具の変更と、型紙で足りない判断が要る所は Opus。
- **S7 (2026-11-25 ユーザ決定)**: sig から、ドキュメントサイト (family-mruby.github.io) の「クラスリファレンス」を機械的に
  作って公開する。**sig の作業 (S4〜S6) が終わったら、develop の内容で公開まで行う**。下の「S7 の中身」。
- 各段は、サブが書いて親が検収する (fmrb-dev-flow 4 章)。検収では、足した型のうち数件を親が実装と
  突き合わせる。
- 段ごとの確認は `rake regress` と `rake ti:test`、sig を直した後のビルドは `rake clean` から (sig/README)。
  重い道具 (`regress:size` など) は S6 にまとめる。
- S2〜S5 の順番と切り方は、S0 の数が出てから見直す。

### S7 の中身 (クラスリファレンスをサイトに出す)

- **道具** (試作済 2026-11-25): `tools/ti/gen_site.rb`、`rake ti:site FMRB_SITE_DIR=<サイトの checkout>` (`ONLY=A,B` で
  一部だけ)。sig の全クラスについて、クラスごと・言語ごとのページ (`docs/api/reference/<クラス>.md`・`.ja.md`)、
  領域ごとの一覧 (`index.md`)、`mkdocs.yml` のナビゲーション (2 つの印の行の間) を書く。ページは定数の表、
  クラスメソッド・インスタンスメソッドごとの見出し (リンクできる)、シグネチャ (重ねた形は全部)、要約、長文と例。
  端末の F1 のヘルプ (`gen_help.rb`) と同じコメントから作り、形だけがブラウザ向け。
- **サイトでの位置**: 手で書いた話題ごとの 17 ページ (使い方の流れ・写真・表) はそのまま残し、その下に
  「Class Reference」として並べる。置き換えない。
- **試作で確かめたこと**: `FmrbUI`・`Machine`・`FmrbConst` の 3 クラスを生成し、サイトの MkDocs でビルドが通り、
  リンクの検査 (`scripts/check_links.rb`) が通り、日本語・英語のページがブラウザで読める形で出た。
- **本番 (S6 の後)**:
  1. 道具の仕上げ: 領域の分け方 (sig のファイル → 領域)、載せない範囲 (内側のクラスを出すか)、手で書いたページとの
     相互リンク、ナビゲーションの日本語 (`nav_translations`)、長いシグネチャの折り返し、道具の試験。
  2. develop の sig で全クラスを生成し、サイトの手元のビルドとリンクの検査を通す。
  3. サイトのリポジトリにコミットして push (公開は main への push で自動。push はユーザの指示で)。
  4. ページの足元に「どの版から作ったか」(fmruby-core develop の版) を出す。リリースの版との違いがあることを
     一覧のページに書く。
  5. sig を直したらサイトも作り直す手順を、リリースの手順 (family-mruby/doc/release.md) に足す。
- 決めること (本番の前): サイトに出さないクラス (Asterism の細部、内側の部品) の線、開発中の API を載せることの
  断り書きの文。

## 6. 受け入れ条件

1. `rake sig:coverage` が、アプリが使うクラス・メソッドの「sig に無い」を 0 と出す (わざと載せない一覧を除く)。
2. Family mruby の分の全メソッドに例がある (同)。
3. 要約はすべて 111 バイト以内で、英語がある。
4. `rake regress`・`rake ti:test` が通る。型データベースの各領域が上限の 8 割以下。
5. ファームと storage の増分が測ってあり、Retro の storage に収まっている。
6. AI ホストの決まった頼みで、足す前より悪くなったものが無い (返るコードが sig にある API を型どおりに使う)。

## 7. 決定 (2026-11-24 ユーザ)

1. **上流由来の 15 ファイルに足りないメソッドは、そのファイルを直接直す**。「変更禁止」の決まりをやめる
   (`sig/README.md` も S1 で直す)。型エンジンの試験が文字列の一致で見ている所は、試験の期待値も一緒に直す。
   上流 (picoruby-ti の example/rbs) の更新を取り込むときは、差分を手で合わせる。
2. ~~キーワード引数と省略できる引数は今のまま (必須引数の形で書き、長文で説明する)~~ → 決定 22 で改めた
   (シグネチャに正確に書く)。型エンジンは直さない、は変わらない。
3. **道具 (`rake sig:coverage`) は、最初は報告だけ**。抜けが 0 になった範囲から順に、増えたら落ちる検査に切り替える。
4. **内部用 (わざと載せないメソッド) は、道具の側の一覧に理由つきで持つ**。実装のソースに印は付けない。
5. **見本のアプリは最小限**: 型と例で足りない所だけに足す。
6. **英語の無い 25 件に英語を足す** (S3 で、例の無い分と一緒に)。

### S0 の結果を受けた決定 (2026-11-24 ユーザ、report/s0.md 4.2)

7. **`FmrbConst` (キーの番号・テーマの色・`BOARD`。アプリで 273 回、最大の欠け) は S2 に入れる**。
8. **扱いに迷うクラスは、クラスごとに決める** (下の 10〜16)。
9. **Asterism の gem が `@api private` と書いたメソッド (152) は、道具がその印を読んで数から除く**。一覧には
   写さない。新しく印を付けることはしない (決定 4 のまま)。
10. **PicoRabbit の部品** (`PicoRabbit::Theme` ほか 6 クラス、約 110): 載せない (スライドのアプリの内側)。
11. **FmrbUI の内側** (`Widget` の 31、各部品の `draw_widget`・`press` など) と **スクロールバー**: アプリが呼ぶ分
    だけ載せる (`scrollbar`・`set_range` など)。残りは S4 で、理由つきで載せない一覧へ。
12. **MIDI の送り先** (`SerialTransport`・`ApuTransport`・`Sam2695`・`NoteScheduler`・`MIDI::Device`): アプリが呼ぶ分
    だけ載せる (`device=`・`transport`・`defer_voices` など)。残りは S4 で載せない一覧へ。
13. **Asterism の ROS 2 の橋** (`Asterism::ROS::*`、約 70): 載せる (S5)。
14. **Asterism の印の無い公開メソッド** (`Connection` の残り 29、`Topic`・`Subscription`・`SampleInfo`・`Future` の残り、
    `Credentials` 19、`Types`・`Pubs`・`Sequences`・`Status`・`SharedSession`) と、**`FmrbAsterism` のモジュール関数 17・
    `Link` の残り 7**: 全部載せる (S5)。
15. **JSON**: `JSON::Digger` (12) は載せる (S5)。内部向けの関数 (`wasm_build?`・`use_regexp=` ほか 7) は載せない。
16. **小さなクラス**: `FmrbI18n` の残り 3・`FmrbAssoc` 5・`Digest::SHA256` 6 は載せる。`Fmrb::SpinelHello` は載せない。
17. **要約の上限は 111 バイト** (エディタの箱は 112 バイトで、終わりの印の分 1 バイトを引く)。文書と道具を直し、
    超えていた 2 件 (`Array#collect!`・`FmrbApp.set_text_cursor`) を縮めた。
18. **Kernel のメッセージ 4 件** (`send_message`・`receive_message`・`broadcast_message`・`message_queue_exists?`) を
    載せる (S4)。
19. **アプリが `_` で始まるメソッドを呼んでいる件**は sig に載せず、doc/reference/TODO.md に課題として記録した。
20. **storage にある Ruby のライブラリのクラス** (`FmrbReceiveConf`・`Unit8Encoder`) も sig に載せる (S5)。
21. ホストで動かす生成の台本 (6 ファイル) と、わざと壊してある見本 (1 ファイル) は、アプリとして数えない。

### S1 の結果を受けた決定 (2026-11-24 ユーザ、report/s1.md 2 章)

22. **省略できる引数とキーワード引数は、シグネチャに正確に書く** (決定 2 を改める)。決定 2 は「型エンジンが
    扱えない」が前提だったが、今の型エンジン (picoruby-ti) は `?` とキーワードを読み、エディタのまちがい探し
    (Ctrl+E) が引数の数を検査する。必須引数の形に丸めると、正しい別の呼び方に「引数が足りない」と出る。
    既に必須引数の形で書いたもの (A6b の通信の型、`create_canvas_gfx` など) は、その API を触る段で直す
    (通信の型は S5)。
23. **長文の強調 (`**`) が行をまたぐと F1 の画面で色がずれる**。既存の 42 行は S3 で直し、数える道具に
    「強調が行をまたぐ」を足す。

決定 6 の「英語の無い 25 件」は、道具で数え直すと 0 件だった (調査の正規表現が、節の見出しのコメントを要約として
読んでいた)。S3 でやることは、例の無い 92 件と、長文の無い 3 件。

### S4 の後の決定 (2026-11-25 ユーザ)

24. **型データベースのシグネチャの位置も 24 ビットにする** (S5 の前に)。picoruby-ti `563bb68`。要約のときと違って
    詰め物の空きが無く、メソッドの行が 18 → 20 バイト、定数の行が 10 → 12 バイトになる (flash 約 7 KB 増)。
25. **`Kernel.send_message`・`receive_message`・`broadcast_message` は、直すまで sig から外す** (決定 18 の一部を保留)。
    メッセージの構造体が本体と違い、呼ぶとメモリを壊す見込みのため (doc/reference/TODO.md)。

### S6 の後の決定 (2026-11-25 ユーザ)

26. **残りの API のうち sig に足すのは例外のクラスだけ** (`sig/exception.rbs`: `Exception` と主な 17 クラス)。
    `Net::HTTP`・`URI`・`MbedTLS::Digest`・`BMI270`・`RMT`・`FmrbGfx#draw_text_mixed` は載せない一覧へ。
27. **サイトのクラスリファレンスには sig のクラスを全部出す** (Asterism の細部・ROS 2 の橋・Ruby の基本クラスも)。
    載せないのは型エンジンの都合の名前 (`Untyped`) だけ (`tools/ti/site_exclude.yml`)。
28. **エディタの型エンジンが入れ子のクラス (`A::B`) を受け手に解けない件は、S7 の後に続けて直す**
    (picoruby-ti の fork。doc/reference/TODO.md)。
29. **S3 のファームの区画の空き (288 KB、5%) は記録だけ**。足りなくなったときに決める (doc/reference/TODO.md)。

## 8. 未確定事項

- S2〜S5 の順番と切り方 (S0 の数が出てから見直す)。
- (済) シグネチャの領域の手当ては決定 24 で行った。
