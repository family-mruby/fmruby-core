# 回帰の確認の使い方 (rake regress)

> 状態: 進行中 | 更新: 2026-10-26 | `rake regress` 1 つでホストの試験と描画の記録の比較を流し、短い要約だけを見る。場面の足し方、端末の足し方、基準の更新の決まり。重い方 (`rake regress:full`: S3・P4 のファームの大きさと内蔵 RAM の表、sim の VM プールの表) は 7 章。実機の起動ログの比較 (`rake regress:boot`、実機がつながっているときだけ) は 8 章

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

- 端末と画面の大きさは `tools/regress/targets.rb` の 1 か所にある。1 行 1 端末: 名前・種類 (modern / retro / mini)・画面の大きさ・高解像度の全画面 (あれば)・全画面の余白・窓の既定の大きさ・日本語フォントの有無・`config/` の system_conf と storage_exclude・ファームの FMRB_HW_TARGET (`firmware`、ブラウザ版は無し) とそのビルドの設定 (`build`)・実機のつなぎ方 (`serial`、8 章)。
- `firmware` のある端末は `rake regress:size ALL=1` (7 章) の表にも入る (既定で作るのは `size_default` の付いた S3・P4 の 1 台ずつ)。ファームの設定は `FIRMWARE_ENV` (標準構成: Spinel のカーネルとエディタ、mruby のデスクトップ) に端末ごとの `build` を重ねたもので、`.env` には左右されない。Rakefile の BOARD_BUILD_DEFAULTS と食い違うと `rake regress:size` が止まる。
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

## 7. 重い方: RAM とフットプリント (rake regress:full)

```
rake regress:full                # rake regress + regress:size + regress:run (全部で 9 分ほど)
rake regress:size                # S3・P4 の 1 台ずつ (NARYAv3・TAB5) のファームを作り、idf.py size の数字を基準と比べる (4 分ほど)
rake regress:size ALL=1          # firmware のある全端末 (10 分ほど)。regress:size:all も同じ
rake regress:size ONLY=sticks3   # 1 端末だけ (名前か FMRB_HW_TARGET、カンマで複数)
rake regress:size TABLE=1        # 作らずに、前回の結果から表だけ出す
rake regress:size NO_RESTORE=1   # 最後の端末のビルドを build/ に残す (元の build/ は退避されたまま)
rake regress:size:update         # 前回の結果を基準にする (5 章の決まりを守る)
rake regress:run                 # sim (Retro・Modern) で決まった台本を流し、VM プールを基準と比べる (4.5 分ほど)
rake regress:run ONLY=modern     # 片方だけ
rake regress:run TABLE=1         # sim を起こさずに、前回の結果から表だけ出す
rake regress:run:update          # 前回の結果を基準にする
ruby tools/regress/size.rb --restore   # 途中で殺したときに、退避した build/ を戻す
```

- `rake regress` (安い方) には入らない。docker でファームを作り、sim を起こす。sim がすでに動いている (誰かが使っている) と `rake regress:run` は始めない。
- **build/ は元に戻る**: 始める前の build/・sdkconfig・端末ごとの libmruby・Spinel の生成物・flash/etc の生成物を `build.regress.saved/` と `build.regress-aside.saved/` に退避し、終わったら (落ちても) 戻す。最後に `build/: TAB5 (esp32p4)` のように何が入っているかを出す。殺したときは次の実行が止まって `--restore` を案内する。
- `rake regress:full` に ONLY= は効かない (部分ごとに意味が違う)。絞るときは部分を 1 つずつ流す。

### 7.1 ビルドの時点の表 (rake regress:size)

- 既定は S3 と P4 で 1 台ずつ (2026-10-26 ユーザ決定)。アプリや gem の直しはどの端末にも同じように効くので、これで足りる。端末ごとに差が出る所 (ドライバ・sdkconfig・区画・storage の除外) を触ったときと、端末を足したときは `ALL=1` で全端末を作る。

- 端末ごとに `rake clean_all` → `FMRB_HW_TARGET=<端末> rake build:esp32` (設定は targets.rb で固定) → `esp_idf_size` (idf.py size・size-components・size-files の中身、json2) を map ファイルにかける。結果は `tmp/regress/size/<端末>.json` (と生の json2、ビルドのログ)。
- 表は端末ごとに 1 行: DIRAM・IRAM (S3 だけ。P4 は DIRAM に含まれる)・ほかの内蔵 (S3 の RTC、P4 の HP core RAM・LP RAM)・アプリのイメージ (.bin の大きさ)・一番小さいアプリ区画の空き。数字の後ろが基準との差。
- **内蔵 RAM の種類のどれか 1 つでも増えたら落ちる** (`FAIL internal RAM grew: DIRAM +64`)。減ったときは差が出るだけ (基準を更新すれば次から減った値が基準になる)。flash (イメージ・区画の空き) は増えても落とさない。
- その下に Spinel のプログラムごとの静的な内蔵 RAM (生成された TU の .data/.bss のうち内蔵に置かれた分。[ ] は PSRAM に置かれた分) と、差のあった部品 (size-components のアーカイブ) の上位 6 行。部品ごとの全部は結果の json にある。
- 基準は `test/regress/size_baseline.json` (git に入れる)。基準と違う端末の組み合わせ・設定で作った結果を比べないように、結果には作った設定 (env) とコミットが入る。

### 7.2 動かした時点の表 (rake regress:run)

- Retro (NARYAv3) と Modern (TAB5) の Linux ビルドを作り、3 つのコンテナを headless で起こして台本を流す: 起動 → デスクトップの「Boot complete」→ 控え (idle) → アプリ 4 つ (kamon・MML・StackChan Remote・Spinel Hello) を順に起動 → 控え (running) → 閉じる → 控え (closed)。控えは debug server の ps (sim_app ps と同じ): アプリごとの VM プールの使用量とタスクのスタックの空きの最小。ほかにログから mruby のアプリごとの「コンパイル直後のプール」(使用量と最大)。
- 結果は `tmp/regress/sim/<種類>.json` とコアのログ (`<種類>.log`)。基準は `test/regress/sim_baseline.json`。
- 比べ方 (幅): 起動したアプリ・デスクトップの使用量は 512 B、カーネルとサービスホスト (自分の GC の都合で動く) は 4 KB か 3% の大きい方、スタックの空きは 512 B、コンパイル直後は 256 B まで動いてよい。悪い向き (使用量が増える・空きが減る) に幅を超えたら落ちる。閉じた直後のカーネルの使用量は比べない (ごみを集めたかどうかで 40 KB 動く)。幅の根拠は `report/rg3b.md`。
- sim で取れないもの: 起動ログの M1| (Linux のビルドは出さない) と周期ダンプの IRAM free (sim では周期ダンプが出ない)。これらは実機の起動の確認 (RG4) で見る。

### 7.3 落ちたとき

- 内蔵 RAM が増えた: 表の行と「差のあった部品」で、どのアーカイブが増えたかが分かる。シンボルまで見るなら、その端末を `ONLY=<端末> NO_RESTORE=1` で作り直して build/ に残し、`idf.py size-files` / `size-symbols` を当てる (済んだら `ruby tools/regress/size.rb --restore`)。
- 増えるのが意図したもの (機能を足した) なら、人が確認してから `rake regress:size:update` し、コミットの本文に増えた量と理由を書く。内蔵 RAM は増やさない方針 (doc/reference/internal_ram_budget.md) なので、増やす前に PSRAM へ置けないかを先に考える。

## 8. 実機の起動ログ (rake regress:boot)

```
rake regress:boot BOARD=naryav4          # ファームを作り、app の区画だけ焼き、リセットして起動ログを基準と比べる (5 分ほど)
rake regress:boot BOARD=naryav4 FLASH=0  # 焼かずに、今入っているファームの起動だけ見る (40 秒ほど)
rake regress:boot BOARD=naryav4 TABLE=1  # 実機を使わずに、前回の結果から表だけ出す
rake regress:boot:update BOARD=naryav4   # 前回の結果を基準にする (5 章の決まりを守る)
rake regress:boot:test                   # 解析部の試験 (作り物のログ、実機は要らない。rake test・rake regress に入っている)
```

- **実機がつながっていて、使ってよいときだけ流す**。`rake regress`・`rake regress:full` には入らない。`BOARD=` は targets.rb の名前か FMRB_HW_TARGET (省略すると環境変数の FMRB_HW_TARGET)。ポートは targets.rb の USB の VID:PID で探す。同じ VID:PID の端末が 2 台あるとき (Tab5・StickS3・AtomS3R は 303a:1001 で同じ) は `PORT=/dev/ttyACMx` で指定する。
- **焼くのは app の区画だけ** (`rake flash:app`)。storage と /home は消えない。全体の書き込み (`rake flash`) はしない。焼く速さは targets.rb の `flash_baud` (落ちたら 115200 で 1 回やり直す)。ビルドの設定は `rake regress:size` と同じ固定のもの (`firmware_env`) で、`.env` には左右されない。build/ は退避して戻す (7 章と同じ)。
- **ポートを使っている物がいたら始めない**: MCP サーバの serial_start・flash (ポートごとの錠 `~/.fmrb_mcp/<port>.lock`) か、ポートを開いているほかのプロセス (ユーザのモニタなど) が見つかると、ポートに触らずに止まる (`port busy: ...`)。MCP の capture なら serial_stop してから流す。流している間は同じ錠を取るので、MCP の serial_* と flash は断られる。
- 起動ログは、リセットしてから周期ダンプ (10 秒ごと) が 3 回出るまで取る (`DUMPS=n` で変えられる)。リセットは端末ごと (targets.rb の `reset`): 外付けの USB-UART (NARYAv3・P4-Nano) は RTS で EN を引く。チップの USB-Serial-JTAG (Tab5・StickS3・AtomS3R) はポートを開くだけでリブートするので、何も送らない。
- 集めるもの: crash マーカー (`Guru Meditation`・`abort()`・`Backtrace:`・`panic'ed`・`assert failed`・stack overflow・`CORRUPT HEAP` など)、リセット原因 (`rst:0x..`) と取っている間に起動した回数、`M1|` の各段 (internal・largest・psram)、最後の周期ダンプの `IRAM free:`・タスクごとの stack の空き・VM プール (used・peak・ExcHW)、`Boot complete` までの秒数 (行が届いた時刻から。0.5 秒刻み)。
- 結果は `tmp/regress/boot/<端末>.json`、全文のログは `<端末>.log` (各行に届いた時刻。mDNS の名前・IP・MAC・アクセスポイントの名前は伏せてある)、ビルドと焼いたときのログは `.build.log`・`.flash.log`。基準は `test/regress/boot_baseline.json`。

```
board    reset              boot    crash M1 steps  M1 last internal    IRAM free           least stack free             dumps
NARYAv4  POWERON            8.9s    0     24        212,636 +0          208,592 -24         status_led 1,508 +0          3
  pools (used/peak): fmrb_kernel 100,824/126,208  system_desktop 649,096/708,888  Services 391,040/475,648
boot: naryav4 vs baseline 2.1.0-847-g802b16a3-dirty, ok (0.6 min: capture 36s; not flashed; log tmp/regress/boot/naryav4.log)
```

- **落ちるもの**: crash マーカーが 1 つでもある、取っている間に 2 回以上起動した (勝手にリブートした)、`Boot complete`・周期ダンプ・`M1|` が無い、基準にある `M1|` の段やタスクが無い、基準より悪い向き (空きが減る) に幅を超えた数。幅は M1| の internal 1 KB・largest 8 KB・psram 16 KB、IRAM free 1 KB、stack の空き 384 B (同じファームで 5 回起動した揺れの 2 倍ほど。根拠は `report/rg4.md`)。基準の json の `tolerance` で変えられる。
- よい向きに幅を超えた数と、基準に無い新しい `M1|` の段は `moved` の行に出るだけ (落ちない)。VM プールは表に出すだけで比べない (カーネルは自分の GC の都合で動く)。
- 基準があるのは今は P4-Nano (NARYAv4) だけ。ほかの端末は、つないだときに 1 回流し、ログと表を人が見てから `rake regress:boot:update BOARD=<端末>` する。端末を足すときは targets.rb の行に `serial:` (VID:PID・リセットのしかた・ボーレート) を書く。
- 落ちたら全文のログ (`tmp/regress/boot/<端末>.log`) の、落ちた項目の行を見る。内蔵 RAM が減ったなら、ビルドの時点の数 (`rake regress:size`) とどちらで減ったかを比べる (静的な領域が増えたのか、起動中の確保が増えたのか)。

