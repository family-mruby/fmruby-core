# RG1 報告: 描画の記録の比較を、麻雀からほかのアプリと FmrbUI に広げた

> 状態: 完了 | 更新: 2026-10-25 | `rake regress` (ホストの試験 + 描画の記録の比較) と `rake regress:update` を作り、19 アプリ・73 場面 × 4 つの画面の大きさ (205 本) の基準を R6 の前の develop で取った。CI の test-host で回る。sim・ブラウザ版・実機は使っていない

## 1. 道具の作り (tools/regress/)

| ファイル | 役目 |
|---|---|
| `targets.rb` | 端末の一覧 (1 行 1 端末): 名前・種類・画面・高解像度の全画面・余白・窓の既定・日本語フォントの有無・system_conf・storage_exclude |
| `run.rb` | 入口。場面 × 端末の組を 1 本ずつ fork した子で流し、記録を `tmp/regress/out/` に書いて基準と比べる。`--update`・`--only`・`--list`・`--debug`・`-j` |
| `scene.rb` | 台本の書き方 (`Regress.app` / `scene`) と、1 本の実行 (`Session`: start・update・run・key・click・tap・control ほか、窓の大きさの変更 (全画面の要求) を app.c と同じ手順で返す) |
| `recorder.rb` | FmrbGfx の C の側 (gfx.c が定義する口) を記録係にしたもの。Ruby の側 (`fmrb-gfx.rb` の draw_text・set_font・text_width と FONT_METRICS) は本物を読む。GfxBlock (窓の枠) は最初の 1 回だけ中身を記録し、同じ値の描き直しは 1 行 |
| `stand_ins.rb` | FmrbConst (端末ごと、テーマは端末の system_conf から)、Machine・Time (偽の時計)、Log (ログの行は場面が与える)、FmrbApp の C の口 (_init は .app.toml の窓、タスクの表、heap_info)、MessagePack、FmrbNet (HTTP の答えは場面が与える)、`require "/app/..."` |
| `fake_fs.rb` | 端末のファイル: git に入っている `flash/` (読むだけ) + 1 本ごとの上書き層 (書き込みと場面が置いたもの)。`/etc/system_conf.toml` は端末の config を写す |
| `fake_asterism.rb` | Asterism の芯 (connect・poll・session の subscribe/put・each・proxy・on_join/on_leave・expose・current_call・ROS 2 の Node) の偽物と、場面が操るゲームホスト。FmrbAsterism の Link・TableClient・設定 (fmrb-asterism.rb) は本物を読む |
| `mini_toml.rb` | FmrbToml の C の側 (tomlc99) の代わり。`fmrb-toml.rb` は本物を読む |
| `scene_helpers.rb` | 複数のアプリの偽物 (MIDI のシリアルポート) |
| `scenes/*.rb` | 場面の台本 (8 ファイル) |

- 記録は 1 行 1 呼び出しのテキスト (`text 5 29 "..." #60 ja12x1`、`fill_round_rect 5 197 90 16 3 #EE`、`present`)。文字の行にはそのときのフォントと大きさを付け、set_font・set_text_size の呼び出しそのものは記録しない (同じフォントを選び直すだけの直しでは落ちない)。空の文字は描かれないので記録しない。続く present は 1 つにまとめる。アプリが送るメッセージ (publish・file_select など) は `msg` 行で残す (音の命令は残さない)。
- 揺れの元は全部固定した: 時計は台本が進めたときだけ進む、乱数は場面の名前から決まる種、ファイルの一覧は名前順、ファイルの時刻は固定、id は 101 から。同じ版で 3 回と、並列 (24) と 1 本ずつで記録が一致することを確かめた (下の 4)。
- 麻雀は載せ替えていない。`check.rb` ははみ出し・重なりの検査 (部分の枠で区切る作り) で、`rake mahjong:overflow` の出力を一字一句保つには今の代役と出力の形をそのまま残すしかなく、共通の道具に寄せる利点が小さい。`rake test` に入っているので `rake regress` の中で今までどおり回る。

## 2. 端末の一覧と、端末の足し方

- 一覧は `tools/regress/targets.rb` だけ。今は narya_v3 (Retro 320x240)・tab5・naryav4・wasm (426x240、高解像度 640x360)・sticks3 (240x135)・atoms3r (128x128)。
- 画面が同じ端末は同じ絵になるので 1 組の記録を共有する (端末の組: `retro-320x240`・`modern-426x240`・`mini-240x135`・`mini-128x128`)。Tab5・NARYAv4・ブラウザ版は 1 組。記録の頭には端末の名前を入れていないので、同じ画面の端末を足しても記録は変わらない。
- どのアプリをどの端末で描くかは、端末の storage_exclude が決める (外したファイルのアプリは描かない)。Retro は walk_scroller (と麻雀)、StickS3・AtomS3R は app/game と app/demo を外すので、卓のアプリ・walk_scroller・MML などは自動で外れる。場面ごとの条件 (`on:`) はその上に書ける (例: オセロ (直接) は 320 未満で観戦の画面だけ)。
- **端末を足すときは一覧に 1 行足す**。新しい大きさなら次の `rake regress` で「基準が無い」と出るので、記録を見て `rake regress:update`。同じ大きさなら何も増えない。config の画面の大きさと一覧が食い違うと、その組の全場面が落ちる (書き写しの誤りを止める)。
- 高解像度の全画面 (fullscreen_hires) の大きさも一覧にあり、その属性のアプリは自動でその大きさで描く。ただし今の場面に該当するアプリは無い (麻雀と Spinel のエディタだけ)。

## 3. 対象にした場面と外したもの

| アプリ | 場面 (端末の組) |
|---|---|
| オセロの卓 | 一覧・探している・席に着いた (長い名前の「…」、案内 2 行、Jev[A])・観戦・対局中 (カーソル、置けないマスのクリック、残り秒)・投了の確認・終局・AI が考え中 (ja)。Modern は窓、Retro は全画面 (8 場面 × 2) |
| ボード | 一覧・卓 (絵を 2 ページで読む)・線を描く (色と太さを選んでマウスで 1 本)・人の線と取り消しが届く・消す確認 (5 × 2) |
| walk_scroller | 始め・歩いている (歩数が届く)・最後の文字 (花火)・調整の画面 (T、センサの答えと生の値)・状態行を消す (5 × Modern) |
| step_sensor | WiFi ok/-- × AST ok/../-- の 6 通り・IMU が無い (7 × 4 組。小さい端末は全画面) |
| オセロ (直接) | 一覧 (en/ja)・誘われた・誘っている・対局 (相手の手、カーソル、クリック)・投了の確認・終局・名前の入力 (長い名前)・小さい画面の観戦 (9 場面、組ごとに 320 以上/未満で分ける) |
| MML | 始め・曲 (演奏、ループ、出力の切り替え、止める)・各ボタンのクリック・ファイルを開く (長いファイル名、選ばない) (4 × 2) |
| StackChan Remote | 始め・ボタン 5 つ (publish の中身も記録) (2 × 2) |
| 動画の再生 | ファイルが無い・再生中 (各ボタン、右クリックで速さを戻す、Open、長いファイル名) (2 × 2、小さい端末は外す) |
| rover_brain | つないでいる・/odom 待ち・drive・go の途中 (進み具合の棒)・つなげない (5 × 4) |
| logviewer | 末尾 (色分け、長い行)・折り返しとレベル切り替えとホイール (2 × 4) |
| monitor | 3 ページを巡る・タスクの [stop] を 2 回押す (長いタスク名) (2 × 4) |
| appstore | 一覧 (サムネイル)・長いアプリ名と説明 (ja)・「Installed」と取れなかった一覧 (3 × 4) |
| NSF プレイヤ | 一覧・長いファイル名 (en/ja) で再生とトラックの + (2 × 4) |
| SMF プレイヤ | 一覧・長い曲名で再生、テンポ・出力・楽器の切り替え (ja) (2 × 4) |
| midi_apu | モードを順に押す・SMF (2 × 2) |
| kamon | 始め (ja)・紋様・中央・数・大きさ・反転を変える (2 × 2) |
| PicoRabbit+ | メニュー・長いデッキ名 (en/ja)・発表 (次、次のスライド、目次) (3 × 4) |
| PicoRuby デモ | ページ送り (en/ja) (2 × 2) |
| FmrbUI の全部品 | 場面のファイルに書いた 1 枚 (ラベル (左寄せ・中央・大きさ 2)・ボタン (アクセント色、押せない)・トグル (グループ)・数値・選択・入力欄・スクロールバー)。普通の文字 en/ja、**わざと長い文字** en/ja、操作 (各部品を押す、入力欄に打つ) en と長い ja (6 × 4) |

- FmrbUI の入力欄 (TextField) を使う mruby のアプリは無い (デスクトップのファイル選択だけ) ので、部品の 1 枚 (`scenes/fmrbui_sheet.rb`) を場面のファイルの中に書いた (アプリのコードではない)。
- 外したもの (理由):
  - Spinel のプログラム (デスクトップとその確認の画面・ファイル選択・ランチャー、エディタ、シェル): CRuby では Spinel のコンパイルの制約も RAM も見られない。デスクトップには全 mruby の構成もあるが、背景のキャンバス・メニュー・ランチャーを持つ大きな kernel アプリで、今回は手を付けていない (残件)。
  - 麻雀: 1 の理由で今の道具のまま。
- アプリ・gem・ファームのコードは 1 行も変えていない。読めずに止まったアプリは無い (止まる条件に当たらず)。読むために要った偽物は道具の側にある (センサ・I2C・GPIO、MIDI のシリアルポート、ROS 2 の Node、HTTP、ログの行、タスクの表)。

## 4. 基準・時間・出力の長さ・揺れ

- 基準: `test/regress/baseline/` に 205 ファイル、30,778 行、1.4 MB。大きいのは walk_scroller (3,717 行)・ボード (3,230 行)・FmrbUI の 1 枚 (2,916 行)。
- 時間 (この PC、24 コア): 描画の記録の比較 0.9 秒 (並列)、1 本ずつ (`-j 1`) で 7.9 秒。`rake regress` 全体で約 17 秒 (ホストの試験 17 本が約 16 秒)。
- 出力: 全部通ると 2 行 (`host: 17 suites, 17 passed, 0 failed (16.1 s)` / `regress: 205 runs (73 scenes on 4 profiles), 205 passed, 0 failed (0.9 s)`)。落ちたときは落ちた場面のうち 3 つに差分の最初の 4 行ずつ、残りの名前 (8 つまで)、全文の場所で、下の例で 18 行。ホストの試験の全文は `tmp/regress/host/`、描画の差分の全文は `tmp/regress/diff.txt`。今の `rake test` の出力は 1,313 行。
- 揺れ: `rake regress` を 3 回流して要約は時間の数字以外一致。記録そのものを 3 回と `-j 1` で比べて全ファイル一致。きれいな checkout (git worktree) でも全部通る (追跡されていない手元のファイルに頼っていない)。

## 5. わざと壊した確認 (どちらも戻した)

- オセロの卓の 2 行目の文字を 1 px 下げた (`y + 15` → `y + 16`): 16 本が落ち、差分は `-text 5 29 "つないでいます: ..."` / `+text 5 30 ...` の形で出た。戻すと 205 本とも通った。
- FmrbUI のボタンの地の色を変えた (`C_BUTTON` → `0x6D`): FmrbUI を使うアプリと部品の 1 枚の 68 本が落ち、`-fill_rect 5 41 70 16 #60` / `+fill_rect 5 41 70 16 #6D`。戻すと通った。
- `rake mahjong:overflow` の出力 (`-v`) と `--dump` の 210 ファイルは、この作業の前後で一致。

## 6. CI

- `.github/workflows/build.yml` の test-host に、`rake test` の後で `rake regress:draw` を足した。落ちたときは `tmp/regress/diff.txt` を artifact (regress-diff) に残す。`rake test` はそのまま (ホストの試験を 2 回流さない)。

## 7. 記録を取って気づいたこと (直していない)

- step_sensor の下の行 (`128 steps/min`) は 128x128 で x = -14 から描く (画面の左にはみ出す)。
- 窓の幅を超える文字: 動画の再生の状態行 (長いファイル名)、MML の状態行、NSF・SMF プレイヤの長い曲名、PicoRabbit+ のデッキのパス (小さい画面)、midi_apu の状態行。FmrbUI の部品の長い文字も部品からはみ出す (R6 で詰めるところ)。

## 8. R6 で `rake regress` が受け持てる確認、受け持てない確認

受け持てる (R6 の指示書の 3 章のうち):
- **5 アプリの前後の画面**: オセロ (直接)・MML・StackChan Remote・動画の再生・rover_brain の主な画面とボタンが Retro と Modern (と小さい画面) で記録にある。「見た目は変えない」なら全部通るはずで、落ちたら変わった行がそのまま出る。描く口を FmrbGfx に移す (`gfx.draw_button`) のも、記録は基本の図形 (fill_round_rect と文字) の単位なので、画素が同じなら記録も同じ。
- **押せる範囲 (FmrbButtons → FmrbUI::HitAreas)**: 範囲そのものは記録しないが、オセロの卓・ボード・オセロ (直接)・MML・動画の再生・StackChan Remote の場面はボタンやマスを座標でクリックし、その結果の描き直しが記録に出るので、範囲がずれれば落ちる。道具は gem の `mrblib/*.rb` を全部読むので、`fmrb-buttons.rb` を消して `fmrb-ui.rb` に移しても道具の直しは要らない。
- **FmrbUI のふだんの文字の画面が前と同じ**: FmrbUI のアプリ 9 つと部品の 1 枚 (普通の文字 en/ja) が全端末の組で記録にある。
- **わざと長い文字が詰まること**: 部品の 1 枚の長い文字 (en/ja、大きさ 2)、appstore・NSF・SMF・PicoRabbit+ の長い名前の場面。R6 の後はここが「意図した変化」で落ちるので、差分を読んで詰め方 (印の有無と種類、幅) を確認し、`rake regress:update` で基準を更新する。普通の文字の場面が落ちないことが「収まる文字の見た目は 1 画素も変えない」の確認になる。
- `rake test` は `rake regress` の中に入っている。

受け持てない:
- **Spinel**: FmrbText を Spinel で型の決まる形にすること、デスクトップ・エディタ・シェル (標準構成でエディタを起動して 1 打鍵)、全 mruby の構成での起動。sim で見る (R6 の指示書どおり)。
- **内蔵 RAM** (Spinel のプログラムごとの増え、DIRAM の前後): RG3b の道具ができるまで手で。
- **本当の画素**: 角丸の描き方、字形、合成。記録が同じなら頼んだ描画は同じだが、描く側の変化は見えない。描く側を触らない R6 では問題にならない見込み。
- **麻雀**: R6 は麻雀の FmrbButtons も載せ替える。`tools/mahjong_overflow/check.rb` は `fmrb-buttons.rb` を名指しで読んでいるので、R6 でファイルを消すなら check.rb の読み込みを直す必要がある (前後の比較は今までどおり `--dump` で)。
- ブラウザ版・sim の画面そのもの (RG2)。

見込み: R6 の確認のうち、5 アプリと FmrbUI の mruby 側の画面 (前後の画素比較として指示書が求めている部分の大半) は `rake regress` の 2 行で済む。sim を起こすのは Spinel の起動と 1 打鍵、全 mruby の構成、長い文字の見た目の最終確認 (1〜2 枚) に絞れる。

## 9. 残件

- デスクトップ (全 mruby の構成) の確認の画面・ファイル選択・ランチャーを場面に入れる。
- 麻雀の記録も同じ形の基準に入れるか (今は `rake mahjong:overflow` の検査だけで、記録の前後比較は手で `--dump`)。
- 高解像度 (640x360) で描く場面 (該当する mruby のアプリができたら)。
- 記録に押せる範囲 (HitAreas の登録) を出すか: 今はクリックの結果で見ている。R6 の後に名前が落ち着いたら考える。
- `doc/README.md` の索引の更新 (`rake docs:index`) は doc/regression の文書をコミットするときに親が行う。

## 10. コミット (fmruby-core develop、push していない)

- 31dae6fd tools: rake regress, the apps' draw records under host Ruby (RG1)
- 8d9ecc08 test: draw-record baselines, taken before app_refactor R6
- 0463de1b ci: compare the draw records in test-host
- da94fba0 doc: how to use rake regress (scenes, boards, updating the baseline)
- 4b31f0cd tools: the app store's fixture names a placeholder author

指示書 (instruction_rg1.md) とこの report はコミットしていない。
