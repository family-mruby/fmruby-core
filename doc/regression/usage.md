# 回帰の確認の使い方 (rake regress)

> 状態: 進行中 | 更新: 2026-10-25 | `rake regress` 1 つでホストの試験と描画の記録の比較を流し、短い要約だけを見る。場面の足し方、端末の足し方、基準の更新の決まり

## 1. 流し方

```
rake regress                 # ホストの試験 (rake test の中身) + 描画の記録の比較
rake regress:draw            # 描画の記録の比較だけ (CI の test-host はこれを rake test の後に流す)
rake regress:draw ONLY=board # 名前に board を含む場面だけ
rake regress:update          # 今の記録を基準にする (下の決まりを守る)
ruby tools/regress/run.rb --list                              # 場面と端末の組の一覧
ruby tools/regress/run.rb --debug board/list@modern-426x240   # 1 つをこのプロセスで流す (例外がそのまま見える)
```

- 出力は数行の要約。通ったときは 2 行 (`host: 17 suites, ...` と `regress: 205 runs ...`)。落ちたときは落ちた場面のうち 3 つの名前と差分の最初の数行、残りの名前、全文の場所 (`tmp/regress/diff.txt`) が出る。ホストの試験の出力は `tmp/regress/host/<suite>.log`。
- sim・ブラウザ版・実機は使わない。docker も要らない。描画の記録の比較は 1 秒ほど、全体で 20 秒ほど。

## 2. 仕組み

- アプリの .app.rb (起動の行 `begin ... app.start` を除いたもの) と gem の Ruby (`lib/add/picoruby-fmrb-app/mrblib/` ほか) を CRuby に読み込み、FmrbGfx の C の側だけを記録係に差し替えて (`tools/regress/recorder.rb`)、場面の台本どおりに on_create・on_update・キー・クリックを流す。描いたものを 1 行ずつ、呼ばれた順にテキストで書き出す (座標・大きさ・色・文字と、そのときのフォントと文字の大きさ)。present で 1 枚の絵の区切り。
- 揺れるものは固定する: 時計 (`Machine.board_millis`・`Time.now`) は台本が進めたときだけ進む、乱数は場面ごとに決まった種、ファイルは git に入っている `flash/` と場面が置いたものだけ (`tools/regress/fake_fs.rb`)、網は偽物 (`tools/regress/fake_asterism.rb`。Link・TableClient・fmrb-asterism.rb の設定は本物を読み、その下の Asterism を偽物にする)。
- 1 つの場面 × 1 つの端末ごとに fork した別のプロセスで流す (前の場面の状態が残らない)。
- 基準は `test/regress/baseline/<アプリ>/<場面>@<端末の組>.txt`。

## 3. 端末の一覧 (端末を足すとき)

- 端末と画面の大きさは `tools/regress/targets.rb` の 1 か所にある。1 行 1 端末: 名前・種類 (modern / retro / mini)・画面の大きさ・高解像度の全画面 (あれば)・全画面の余白・窓の既定の大きさ・日本語フォントの有無・`config/` の system_conf と storage_exclude。
- **端末を 1 台足すときは、この一覧に 1 行足す**。次の `rake regress` でその端末の記録が「基準が無い」と出るので、記録を見て `rake regress:update` する。
- 画面が同じ端末 (Tab5・NARYAv4・ブラウザ版) は同じ絵になるので、1 組の記録を共有する (端末の組、`modern-426x240` のような名前)。新しい大きさの端末だけが記録を増やす。端末が増えても確認の手間は大きさの種類の分しか増えない。
- どの場面をどの端末で描くか: その端末の storage_exclude がアプリのファイルを外していれば描かない (Retro は walk_scroller と麻雀、StickS3・AtomS3R は app/game・app/demo を外す)。それに加えて場面ごとに条件を書ける (`on:`)。

## 4. 場面の足し方

`tools/regress/scenes/*.rb` に書く。1 ファイルに 1〜数アプリ。

```ruby
Regress.app "board", "flash/app/game/board/board.app.rb" do
  on { |t| t.family != "mini" }            # 端末の条件 (省略可)
  scene "list" do |s|
    s.host.tables["games-host-a1b2c3/games/board1"] = { "st" => "open", "mb" => 3 }
    s.start                                # new と on_create
    s.update(2)                            # on_update を 2 回 (時計は返り値の分だけ進む)
    s.net.put_states                       # ホストが卓の状態を出す
    s.update
    s.key :down                            # キー (名前か 1 文字)
    s.update
  end
  scene "board_ja", lang: "ja" do |s| ... end
end
```

- 台本で使えるもの: `start`・`update(n)`・`run(ms)` (時計が ms 進むまで回す)・`wait(ms)`・`key`・`type("文字")`・`click(x, y)`・`press`・`move`・`release`・`wheel`・`tap(:widget_id)` (FmrbUI の部品の真ん中か `at: :left/:right` を押す)・`control(msg)` (file_selected など)・`ivar(:@x)`・`seed(path, text)` (ファイルを置く)・`env.extra[...]` (ログの行・タスクの表・HTTP の答え)・`net` (網: `host.answer(:view) { ... }`、`put(key, value)`、`call_exposed`、`refuse`、`reconnecting`)。
- アプリにしか要らない偽物 (センサ、MIDI のポート) は `prelude { ... }` に、複数のアプリで使うものは `tools/regress/scene_helpers.rb` に書く。
- アプリと gem のコードには試験用の分岐を足さない。読めない作りは道具の側で受ける。
- 足したら `rake regress` で「基準が無い」と出る。`tmp/regress/out/` の記録を読んで、思ったとおりの画面なら `rake regress:update`。

## 5. 基準の更新の決まり

- **落ちたときに基準を更新してよいのは、変化が意図したものだと人が確認したときだけ**。差分 (`tmp/regress/diff.txt`) を読み、その直しで変わるはずの所だけが変わっていることを確かめてから `rake regress:update` する。更新した場面の一覧が出るので、コミットの本文にその理由を書く。
- 「振る舞いを変えない」直しで落ちたら、基準ではなく直しの方を疑う。
- 結果が揺れる場面を見つけたら、直すか外す (揺れる試験は信用されなくなる)。

## 6. 描画の記録で分からないこと

- 本当の画素 (フォントの字形、LovyanGFX の角丸の描き方、合成、透過色)。記録は「何をどこに描くよう頼んだか」まで。
- 音、実機の入力 (タッチは相対移動)、表示の後段 (HDMI・NTSC)、Spinel のプログラム (デスクトップ・エディタ・シェル。CRuby では読めない)。
- これらは sim・ブラウザ版の画面の比較 (RG2) と実機で見る。
