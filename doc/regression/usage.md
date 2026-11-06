# 回帰の確認の使い方 (rake regress)

> 状態: 進行中 | 更新: 2026-11-06 | `rake regress` 1 つでホストの試験と描画の記録の比較を流し、短い要約だけを見る。場面の足し方、端末の足し方、基準の更新の決まり。重い方 (`rake regress:full`: S3・P4 のファームの大きさと内蔵 RAM の表、sim の VM プールの表) は 7 章。実機の起動ログの比較 (`rake regress:boot`、実機がつながっているときだけ) は 8 章。sim・ブラウザ版・ネイティブ版の画面を台本で動かして画素で比べる `rake regress:screens` は 9 章。アプリが外の相手 (ゲームホスト・歩数の送り手・MIDI のモニタ・音の探り) とやり取りする場面を、相手を道具が立てて流す `rake regress:live` は 10 章。ネイティブ版のコンポジタ (Linux の Window とアプリ) の一式 `rake regress:wl` は 11 章。ネイティブ版でアプリを止めて続ける・2 つのネイティブ版の間や sim・ブラウザ版・実機との間で移す (チェックポイント) `rake regress:checkpoint` は 12 章

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

- 端末と画面の大きさは `tools/regress/targets.rb` の 1 か所にある。1 行 1 端末: 名前・種類 (modern / retro / mini)・画面の大きさ・高解像度の全画面 (あれば)・全画面の余白・Window の既定の大きさ・日本語フォントの有無・`config/` の system_conf と storage_exclude・ファームの FMRB_HW_TARGET (`firmware`、ブラウザ版は無し) とそのビルドの設定 (`build`)・実機のつなぎ方 (`serial`、8 章)。
- `firmware` のある端末は `rake regress:size ALL=1` (7 章) の表にも入る (既定で作るのは `size_default` の付いた S3・P4 の 1 台ずつ)。ファームの設定は `FIRMWARE_ENV` (標準構成: Spinel のカーネルとエディタ、mruby のデスクトップ) に端末ごとの `build` を重ねたもので、`.env` には左右されない。Rakefile の BOARD_BUILD_DEFAULTS と食い違うと `rake regress:size` が止まる。
- **端末を 1 台足すときは、この一覧に 1 行足す**。次の `rake regress` でその端末の記録が「基準が無い」と出るので、記録を見て `rake regress:update` する。
- 実機の起動ログ (8 章) と sim の台本 (7.2) の基準と幅は、手で数回流して見比べずに calibrate で取る: `rake regress:boot:calibrate BOARD=<端末>` → 数行の要約を読む → `APPLY=1` (8.1)。
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
- これらは sim・ブラウザ版の画面の比較 (`rake regress:screens`、9 章) と実機で見る。

## 7. 重い方: RAM とフットプリント (rake regress:full)

```
rake regress:full                # rake regress + regress:size + regress:run + regress:screens + regress:live + regress:wl + regress:checkpoint SIM=1 (全部で 34 分ほどだった。2026-11-06 から checkpoint にブラウザ版の場面が入り、2 分ほど増える見込み。sim は同じ構成を 1 回だけ作り、部分の間で使い回す)
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
rake regress:run:calibrate ONLY=modern [RUNS=5]   # 1 回作って台本を RUNS 回流し、揺れと幅の案を出す (RUNS=3 で 3 分ほど)
rake regress:run:calibrate APPLY=1 ONLY=modern    # 前回の calibrate を基準と幅にする (regress:run:calibrate:apply も同じ)
ruby tools/regress/size.rb --restore   # 途中で殺したときに、退避した build/ を戻す
```

- `rake regress` (安い方) には入らない。docker でファームを作り、sim を起こす。sim がすでに動いている (誰かが使っている) と `rake regress:run` は始めない。
- **build/ は元に戻る**: 始める前の build/・sdkconfig・端末ごとの libmruby・Spinel の生成物・flash/etc の生成物を `build.regress.saved/` と `build.regress-aside.saved/` に退避し、終わったら (落ちても) 戻す。最後に `build/: TAB5 (esp32p4)` のように何が入っているかを出す。殺したときは次の実行が止まって `--restore` を案内する。
- `rake regress:full` に ONLY= は効かない (部分ごとに意味が違う)。絞るときは部分を 1 つずつ流す。
- `rake regress:full` の中では、**sim の同じ構成を 1 回だけ作る** (`tools/regress/build_tree.rb` の `sim_build!`・`keep_for_later!`)。標準の Retro は run・screens・checkpoint が、標準の Modern は run・screens・live・checkpoint が使う。部分が使い終えた build/ (と sdkconfig、libmruby、flash/etc の生成物) を `tmp/regress/build_cache/<鍵>/` へ移し、次の部分が同じ鍵なら作らずに戻す (移すだけで写さない)。鍵は作る設定と、そのときのソース (HEAD、追跡しているファイルの変更、追跡していないファイルの名前・大きさ・時刻、`.env`、`MRUBY_CHECKPOINT_DIR` の中身) なので、途中でソースを変えれば作り直す。置き場は `regress:full` の始めと終わりに消す。部分を 1 つずつ流すときは今までどおり毎回作る。作ったか使い回したかは各部分の要約に出る (`build cached`)。
- sim は種類 (Retro・Modern) を切り替えると graphics-audio を起こし直す (1 回 17 秒ほど) ので、各部分は sim が今いる種類から流す (`tmp/regress/sim_family`)。regress:checkpoint の sim の場面は種類ごとにまとめて流す (標準・全部 mruby の順)。
- ブラウザ版のページを人が開いている (`tools/fmrb_web.rb` のブラウザか、port 8016 の開発サーバ) ときは、`regress:screens` のブラウザ版だけを外して流す (要約に `web SKIPPED`。9 章)。
- 最後に部分ごとの時間が 1 行出る (`regress:full: 33.3 min (regress 0.4, size 4.1, run 4.3, screens 4.1, live 1.1, wl 9.8, checkpoint 9.4)`)。測り方と内訳は `report/rg_full_time.md`。

### 7.1 ビルドの時点の表 (rake regress:size)

- 既定は S3 と P4 で 1 台ずつ (2026-10-26 ユーザ決定)。アプリや gem の直しはどの端末にも同じように効くので、これで足りる。端末ごとに差が出る所 (ドライバ・sdkconfig・区画・storage の除外) を触ったときと、端末を足したときは `ALL=1` で全端末を作る。

- 端末ごとに `rake clean_all` → `FMRB_HW_TARGET=<端末> rake build:esp32` (設定は targets.rb で固定) → `esp_idf_size` (idf.py size・size-components・size-files の中身、json2) を map ファイルにかける。結果は `tmp/regress/size/<端末>.json` (と生の json2、ビルドのログ)。
- 表は端末ごとに 1 行: DIRAM・IRAM (S3 だけ。P4 は DIRAM に含まれる)・ほかの内蔵 (S3 の RTC、P4 の HP core RAM・LP RAM)・アプリのイメージ (.bin の大きさ)・一番小さいアプリ区画の空き。数字の後ろが基準との差。
- **内蔵 RAM の種類のどれか 1 つでも増えたら落ちる** (`FAIL internal RAM grew: DIRAM +64`)。減ったときは差が出るだけ (基準を更新すれば次から減った値が基準になる)。flash (イメージ・区画の空き) は増えても落とさない。
- その下に Spinel のプログラムごとの静的な内蔵 RAM (生成された TU の .data/.bss のうち内蔵に置かれた分。[ ] は PSRAM に置かれた分) と、差のあった部品 (size-components のアーカイブ) の上位 6 行。部品ごとの全部は結果の json にある。
- 基準は `test/regress/size_baseline.json` (git に入れる)。基準と違う端末の組み合わせ・設定で作った結果を比べないように、結果には作った設定 (env) とコミットが入る。

### 7.2 動かした時点の表 (rake regress:run)

- Retro (NARYAv3) と Modern (TAB5) の Linux ビルドを作り、3 つのコンテナを headless で起こして台本を流す: 起動 → デスクトップの「Boot complete」→ 控え (idle) → アプリ 4 つ (kamon・MML・StackChan Remote・Spinel Hello) を順に起動 → 控え (running) → 閉じる → 控え (closed)。控えは、まず debug server の gc で全アプリに完全な GC をかけ (各アプリの task が次のメッセージ待ちで自分のエンジンの GC を回す。全部が終わるまで最大 5 秒待ち、間に合わないアプリがあれば落ちる)、そのあと debug server の ps (sim_app ps と同じ): アプリごとの VM プールの使用量とタスクのスタックの空きの最小。ほかにログから mruby のアプリごとの「コンパイル直後のプール」(使用量と最大)。
- 結果は `tmp/regress/sim/<種類>.json` とコアのログ (`<種類>.log`)。基準は `test/regress/sim_baseline.json`。
- 比べ方 (幅): 起動したアプリ・デスクトップの使用量は 512 B、カーネルとサービスホストは 4 KB か 3% の大きい方、スタックの空きは 512 B、コンパイル直後は 256 B まで動いてよい。悪い向き (使用量が増える・空きが減る) に幅を超えたら落ちる。閉じた直後のカーネルの使用量も比べる。幅の根拠は `report/rg3b.md` と `report/rg_gc.md`。
- 控えの前の GC (2026-11-03 から): それまでは GC がどこまで進んでいたかで数が動き、Modern のカーネルは 5 回で 68 KB 動いた (幅を 137 KB にしていた)。閉じた直後のカーネルは 40 KB 動くので比べていなかった。GC をかけてからは、カーネルの揺れは 1 KB 未満、ほかは 0〜56 B。数は「生きているもの」の量になり、前より 25〜60 KB 小さい。
- 幅は種類ごとに基準の json の `tolerance` が既定で、種類 (retro・modern) ごとの `families.<種類>.tolerance` があればそれが勝つ (calibrate の APPLY=1 が書く)。
- **台本や構成を変えて揺れ方が変わったとき**: `rake regress:run:calibrate ONLY=<種類>` → 要約を読む → `APPLY=1`。要約の読み方は 8.1 と同じ。sim では幅の案は絶対値の部分だけで、割合の部分 (カーネル・サービスホストの 3%) は今の値のまま。比べない数があればその揺れも `not compared:` の行に出る (今は無い)。結果は `tmp/regress/sim/<種類>.calibrate.json`、各回のコアのログは `<種類>.calibrate.<n>.log`。
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
rake regress:boot:test                   # 解析部と calibrate の計算の試験 (作り物のログ、実機は要らない。rake test・rake regress に入っている)
rake regress:boot:calibrate BOARD=naryav4 [RUNS=5]   # 焼かずに RUNS 回起動し、揺れと幅の案を出す (RUNS=3 で 2 分ほど)。FLASH=1 で最初に 1 回だけ焼く
rake regress:boot:calibrate APPLY=1 BOARD=naryav4    # 前回の calibrate を基準と幅にする (regress:boot:calibrate:apply も同じ。実機は使わない)
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
- 基準があるのは今は P4-Nano (NARYAv4) だけ。ほかの端末は、つないだときに `rake regress:boot:calibrate BOARD=<端末>` で基準と幅を取る (8.1)。端末を足すときは targets.rb の行に `serial:` (VID:PID・リセットのしかた・ボーレート) を書く。
- 幅は基準の json の `tolerance` が既定で、端末ごとの `boards.<端末>.tolerance` があればそれが勝つ (calibrate の APPLY=1 が書く。`regress:boot:update` は端末の幅を残す)。
- 落ちたら全文のログ (`tmp/regress/boot/<端末>.log`) の、落ちた項目の行を見る。内蔵 RAM が減ったなら、ビルドの時点の数 (`rake regress:size`) とどちらで減ったかを比べる (静的な領域が増えたのか、起動中の確保が増えたのか)。

### 8.1 端末を足したとき (calibrate)

端末を足したとき、端末の作りが変わって揺れ方が変わったときは、幅を道具に測らせる。

1. `rake regress:boot:calibrate BOARD=<端末>`: 今入っているファームで RUNS 回 (既定 5) リセットして起動ログを取り、数ごとの揺れ (最大 - 最小) を測る。焼かない (`FLASH=1` なら最初に 1 回だけ app の区画を焼く)。
2. 出てくる要約を読む:

```
  kind         items moved  spread (where)                         now            proposal
  m1_internal     24     5  436 (M1|ble_nimble_port internal)      1,024          1,024
  m1_largest      24     2  4,096 (M1|ble_nimble_port largest)     8,192          8,192
  m1_psram        24     0  0                                      16,384         16,384
  iram_free        1     1  456 (IRAM free)                        1,024          1,024
  stack_free      11     6  144 (stack fmrb_host)                  384            384
  boot 8.9-8.9s
  candidate vs the baseline now: 7 of 84 numbers differ (most: IRAM free +36)
calibrate: naryav4 3 boots (1.8 min: capture 36-36s x3); tmp/regress/boot/naryav4.calibrate.json
```

   - 1 行が幅の種類 1 つ: 数の個数、動いた数の個数、一番大きい揺れとその数、今の幅、幅の案。案が今と違う行には `*` が付く。
   - 幅の案 = 揺れの 2 倍ほどを丸めた値 (内蔵 RAM・IRAM free は 256 B、stack は 128 B 単位)、ただし下限を持つ (internal・IRAM free 512 B、stack 256 B、psram 16 KB)。largest は 4 KB 刻みで動くので揺れそのものを 4 KB に丸め、下限は 8 KB。数回では見えない揺れ (たまにしか起きない確保の順) を下限で受ける。
   - 基準の案は数ごとの中央値 (偶数回なら小さい方。どれかの回で実際に出た値)。最後の行は今の基準と何個違うか。
   - crash・リブートのあった回があると `NOT CLEAN` と出て、APPLY は断る。揺れが大きすぎて案が意味を持たない (内蔵 RAM が数 KB 動くなど) ときは、取り込まずに揺れの元を調べる。
3. 人が要約を見て、よければ `rake regress:boot:calibrate APPLY=1 BOARD=<端末>`。基準と端末の幅が `test/regress/boot_baseline.json` に書かれる (`calibration` に回数と揺れも残る)。5 章の決まりは同じ (基準を変えるのは人が決める)。

sim の台本 (7.2) も同じ形: `rake regress:run:calibrate ONLY=<種類>` → 要約 → `APPLY=1`。

## 9. 画面の比較 (rake regress:screens)

```
rake regress:screens                    # sim の Retro・Modern (標準構成)・ブラウザ版・ネイティブ版で全場面 (9 分ほど)
rake regress:screens ONLY=modern        # 1 つだけ (retro・modern・web・native。FULL=1 のときは modern で modern-mruby も)
rake regress:screens SCENE=editor       # 名前にこの文字を含む場面だけ (カンマで複数)
rake regress:screens FULL=1             # 全 mruby の構成 (retro-mruby・modern-mruby) も足す (12.5 分ほど)
rake regress:screens:update             # 前回の結果のうち落ちた・基準の無い写しを基準にする (5 章の決まりを守る)
ruby tools/regress/screens.rb --list    # 場面と、それを流す構成の一覧、基準の大きさ
ruby tools/regress/screens.rb --attach modern   # もう動いている sim (web なら --port のページ、native なら
                                                # tools/fmrb_native.rb up で起こしたもの) に台本だけ流す
```

- `rake regress:full` に入っている。`rake regress` (安い方) には入らない。
- 構成は 6 つ: `retro`・`modern` (標準構成: Spinel のカーネルとエディタ、mruby のデスクトップ。targets.rb の `FIRMWARE_ENV`)、`retro-mruby`・`modern-mruby` (カーネル・デスクトップ・エディタを mruby に。`FULL=1` のときだけ)、`web` (ブラウザ版、標準構成)、`native` (Linux のネイティブ版、標準構成、機種の定義 `sdl`。doc/linux_native/)。
- 流れ: sim の構成ごとに clean_all → `rake build:linux` → 3 つのコンテナを headless で起こす (種類を切り替えた直後の 1 回目が上がらないときは起こし直す) → デスクトップの「Boot complete」→ 場面を順に。ブラウザ版は `rake spinel:gen`・`wasm:mruby`・`wasm:web` で作り、**自分の開発サーバ (既定 port 8016、`WEB_PORT=` で変える)** とヘッドレスのブラウザを起こし、ページの保存 (/home と設定) を消してから場面を流す。終わったらサーバもブラウザも止める。port 8006 の `rake wasm:serve` (人が使っているもの) には触らない。ネイティブ版は最後に `rake native:mruby`・`native:core` で作り (ブラウザ版を流さないときは `spinel:gen` も)、`tools/fmrb_native.rb` でヘッドレスに起こす。**自分の置き場 (run dir `/tmp/fmrb-native-regress-<uid>`、debug の port 5575、利用者と キャッシュの場所は `tmp/regress/screens/native_home/` を毎回空から)** を使うので、人が起こしているネイティブ版 (`/tmp/fmrb-native-<uid>`・5565) とは並べて動く。終わったら止める。run dir・port・ブラウザ版の port は共有 checkout のもので、worktree から流すと木ごとの名前になる (13 章)。前の回の残り (同じ木の run dir で動いているネイティブ版) は止めてから始める。
- **誰かの sim、または tools/fmrb_web.rb が起こしたブラウザが動いていたら始めない**。ブラウザは `wasm/build/drive_browser.pid` の印で見る。印の pid がもう無い (Windows の Chrome のときは、プロファイル `fmrb_drive` の chrome.exe も無い) なら古い印として消して進む (`web: removed a stale ...` と出る)。ブラウザ版の port (8016) を誰かの開発サーバが使っているときも同じ。ただし `rake regress:full` の中 (`--skip-busy-web`) では、ブラウザ版だけを外して (作らず、流さない) ほかの構成を流し、要約に `web: SKIPPED (...)` と `web SKIPPED` が出る。人が開いているページとそのサーバには触らない。ブラウザ版を確認するのは、それを閉じてから `rake regress:screens ONLY=web` で。build/ は 7 章と同じく退避して戻す。wasm/build と native/build は今の checkout から作ったものが残る (wasm・native の libmruby も新しいものが戻る)。
- 出力は落ちたものだけ。通ったときは 2 行 (`build/: TAB5 (esp32p4)` と `screens: N passed / 0 failed (...)`)。落ちたときは `FAIL <構成> <場面>: <画素数> px in (x0,y0)-(x1,y1) -> tmp/regress/screens/diff/<構成>/<場面>.png` が 6 つまで。

### 9.1 台本の書き方

`test/regress/screens/*.rb` に書く (ファイル名の順に流れる)。共通の手順は `helpers.rb`。

```ruby
Regress::Screens.scene "editor_type", on: %w[modern retro web modern-mruby retro-mruby] do |s|
  s.open_editor      # helpers.rb: デスクトップのメニューから開き、Window が出るまで待つ
  s.mark             # 今の画面を覚える
  s.key "a"
  s.wait_change      # 覚えた画面から変わるまで
  s.park             # ポインタを決まった場所へ
  s.shot             # 落ち着くのを待って基準と比べる
end
```

- 入力: `key`・`text`・`click(x, y)`・`move`・`press`・`release`・`wheel` (tools/fmrb_input.rb・fmrb_web.rb と同じ言葉)。`launch(path)` は sim とネイティブ版 (debug server の spawn)。ブラウザ版はランチャーから開く (`open_app(path, web_index:)`)。
- 待つ: 時間ではなく画面で待つ。`wait_stable(quiet: 1.0)` (その秒数だけ画面が変わらなくなるまで)、`wait_change(rect)` (直前の入力の前の画面、か `mark` した画面から変わるまで)、`wait_until("説明") { |img| ... }`・`wait_pixel(x, y, [r, g, b])`、`wait_log(/re/)` (sim のコアのログ)。どれも上限の秒数を持ち、超えたら場面が落ちる。`pause(ms)` は最後の手段。
- `shot(名前 = nil, quiet:, timeout:, rect:)`: 画面が落ち着くのを待ち、基準があればそれと同じになるまで最大 timeout 秒 (既定 8) 待ってから比べる。1 つの場面で何枚撮ってもよい (名前を付ける)。
- 隠す範囲: `mask(x, y, w, h)` はその場面のすべての比較と待ちから外す。helpers.rb の `before_scene` が全場面でメニューバーの時計 (日付と秒、右端から 90 px) と、置いたポインタの所を隠す。
- 各場面はデスクトップに何も開いていない状態から始まる。場面の後で、道具は場面が開いたアプリを閉じ (sim はプロセスの一覧で、ブラウザ版はページの読み直しで)、**デスクトップが起動直後の画面と画素で同じに戻ること**を確認する。戻らないとその場面が落ちる (ダイアログを閉じた跡が残る不具合を拾う)。デスクトップのダイアログ (メニュー・ランチャー・Config) は場面の中で自分で閉じる。
- テーマが違う: sim は Light、ブラウザ版は Cyberpunk (設定の既定)。Window の題の色・ボタンの色は helpers.rb の `title_rgb`・`button_rgb` が構成ごとに返す。ネイティブ版は sim と同じ Light だが、写しが合成済みの RGB565 そのもの (sim の写しは graphics-audio の RGB332 を通る) なので、同じ赤でも値が少し違う (`native?` で分ける)。今のネイティブ版の場面は `desktop` と `editor_type` の 2 つ。

### 9.2 基準と落ちたとき

- 基準は `test/regress/screens/baseline/<構成>/<場面>.png` (git に入れる。30 枚で 160 KB ほど)。今回の写しは `tmp/regress/screens/out/`、差分の絵は `tmp/regress/screens/diff/` (基準を灰色に、違う画素を赤紫、隠した範囲を紺で塗ったもの)、結果の一覧は `tmp/regress/screens/results.json`、sim のコアのログは `tmp/regress/screens/<構成>.log`。
- 落ちたら差分の絵と写しを見る。意図した変化なら、人が確認してから `rake regress:screens:update` (前回の結果の落ちた・新しい写しだけを基準に写す。流し直さない)。
- 場面を足したら一度流すと `NEW ... no baseline` と出る。写しを見て、思ったとおりなら `rake regress:screens:update`。
- 場面を書くときは `--attach`: sim を `--keep` を付けて流すと最後の構成の sim を起こしたまま (build/ も退避したまま) 終わるので、`ruby tools/regress/screens.rb --attach modern --scene <名前>` を繰り返す。済んだら `docker compose ... down` と `ruby tools/regress/size.rb --restore`。

### 9.3 画面の比較で分からないこと

- 音、実機の入力、表示の後段 (HDMI・NTSC)、実機だけの初期化。sim とブラウザ版の画面は実機と同じ描画の部品 (LovyanGFX) を通るが、Retro の実機は WROVER の側で描くので、Retro の実機の画素は見ていない。
- 時計の中身と日付 (隠している)。

## 10. 相手のいる通し (rake regress:live)

```
rake regress:live                     # Modern の sim で全場面 (ビルド込みで 2.5 分ほど)
rake regress:live ONLY=othello        # 名前にこの文字を含む場面だけ (カンマで複数。SCENE= も同じ)
rake regress:live:update              # 前回の結果のうち落ちた・基準の無いものを基準にする (5 章の決まりを守る)
ruby tools/regress/live.rb --list     # 場面と基準のファイルの数
ruby tools/regress/live.rb --attach modern --scene board   # もう動いている sim に台本だけ流す
```

- `rake regress:full` に入っている。`rake regress` (安い方) には入らない。
- アプリが外の相手とやり取りする場面を、**相手を道具が立てて**流し、相手の側の状態 (卓の様子・受けたバイト列・音の数値) を基準と比べる。実機・VPS は使わない。相手はすべてこの PC の上: sim の zenohd (tcp 127.0.0.1:7447)、asterism-games の作業コピー (fmruby-core の隣、`ASTERISM_GAMES_DIR` で変える) から起こすゲームホスト、ルートの `tools/fmrb_midi_monitor.rb`・`tools/fmrb_audio_probe.rb`、`tool/imu/step_send.rb`。
- 流れは 9 章と同じ (build/ を退避 → clean_all → `rake build:linux` (TAB5) → sim を headless で起こす → 場面ごとにデスクトップから始めて、終わったらアプリを閉じてデスクトップが元どおりか見る → 片付け)。構成は `modern` だけ (Asterism・卓のアプリ・walk_scroller がそろう。麻雀は 640x360)。
- 出力は落ちたものだけ。通ったときは 2 行 (`build/: TAB5 (esp32p4)` と `live: N passed / 0 failed (...)`)。落ちたときは `FAIL modern <場面>.<名前>: <違う所>` が 6 つまで。

### 10.1 相手 (tools/regress/counterparts/)

| 相手 | 台本での立て方 | 中身 |
|---|---|---|
| ゲームホスト | `g = s.counterpart :games, table: :othello` (`:board`・`:mahjong`) | asterism-games を `bundle exec bin/asterism-games` で、卓 1 つの設定と空の状態のディレクトリ (`tmp/regress/live/counterparts/<場面>/games/`) で起こす。node は `rghost`。AI の鍵・資格情報・外のルータは環境から外す。`g.table` (卓の view)、`g.call(:look, g.token)` (卓の任意のメソッド)、`g.member`・`g.token` (保存された状態の端末の札)、`g.stop` (SIGTERM: ホストは保存して網から去る)・`g.resume` (同じ状態で起こし直す) |
| 歩数の送り手 | `st = s.counterpart :steps` | `tool/imu/step_send.rb --measure` を node `rgsteps` で。`st.walk(10, cadence: 120)`、`st.state` は送った数と、walk_scroller が描いた後に置く progress (歩数・count・印) の列 |
| MIDI のモニタ | `m = s.counterpart :midi` | ルートの `fmrb_midi_monitor.rb` を起こし、出力を記録。`m.messages` (時刻・バイト列・種類)、`m.notes` (音名・鳴り始め・止め・チャンネル) |
| 音の探り | `a = s.counterpart :audio` | ルートの `fmrb_audio_probe.rb`。`a.listen(秒) { 鳴らす操作 }` が窓の数・鳴った窓の数・ピークを返す |

- 相手の 4 つの言葉: start (場面で立てたとき)、state、stop・resume (場面の途中で止めて戻す。できない相手は誤りを返す)、close (場面の後に道具が呼ぶ。落ちた場面でも)。足すときは `Regress::Counterparts::Base` を継いで `register` する。
- 卓の状態を読むのは、道具の中の CRuby の Asterism の接続 1 本 (node `rgprobe`、asterism gem)。

### 10.2 台本の書き方

`test/regress/live/*.rb` に書く。9 章の台本の言葉 (`key`・`click`・`launch`・`wait_log`・`shot` …、`test/regress/screens/helpers.rb` の助け) はそのまま使え、次が足される。共通の手順は `test/regress/live/helpers.rb`。

```ruby
Regress::Live.scene "board" do |s|
  g = s.counterpart :games, table: :board
  s.open_table_app "/app/game/board/board.app.rb", "board", g   # 一覧に卓が出るまで
  s.join_table g, "board"                                       # Enter、ホストの会員に入るまで
  s.board_stroke [[60, 80], [100, 100], [150, 120], [200, 140]]
  s.poll("the stroke at the host") { g.table["n"] == 1 }
  s.check "stroke", s.board_summary(g)                          # 要約を基準と比べる
  s.restart_host g, "board"                                     # 止めて戻す (アプリのログで去った・戻ったを待つ)
  ...
end
```

- `check(名前, 要約, variant: nil)`: 要約 (Hash) を `test/regress/live/baseline/<構成>/<場面>.<名前>[.<variant>].json` と比べる。違えば違う葉を `path: 前 -> 今` で出す。**要約に揺れる物 (名前・札・版・時刻・麻雀の牌) を入れない**のは台本の側の決まり。ホストが引く物 (オセロの色) で結果が決まった数の形のどれかになるときは `variant:` で形ごとに基準を持つ (`othello.move.black` と `.white`)。
- `poll(説明, timeout:) { 値 }`: 値が返るまで待つ (相手の状態で待つ)。`expect(説明) { 真偽 }`: 偽なら落ちる。
- 相手の誤り (ホストが起きない、モニタが死んだ、呼び出しが答えない) は、その場面の失敗になる。

### 10.3 場面

| 場面 | 相手 | 確認すること |
|---|---|---|
| `othello` | ゲームホスト (othello1、CPU 200 ms) | 一覧 → Enter で入る → s で座る → c で CPU → Enter で始める (`lobby`) → 端末の番に最初の合法手をクリック、CPU が返す (`move`) → ホストを止めて戻す → 次の手が通る (`after_host`) → Esc と y で投了 (`resigned`) → Esc で出る、会員から消える (`left`)。盤は端末の石を D、相手を O で比べる |
| `board` | ゲームホスト (board1) | 入る → 1 本描く (色・幅・始点・終点) → ホストを止めて戻す、絵を読み直す (`picture read: 1 strokes`) → 2 本目が届く → Esc で出る |
| `mahjong` | ゲームホスト (mahjong1、東風、CPU 300 ms) | 入る (名前を聞かれたら Enter) → 1 で座る → c で CPU 3 人 → 始める → 端末の番に Enter で打牌し、ホストの河に 1 枚増える → ホストを止めて戻す → もう 1 打牌 → Ctrl+Q で閉じる (席はホストに残る) |
| `walk_scroller` | 歩数の送り手 | 120 歩/分で 10 歩。描いた後の progress が 10 回、最後の count が 10、通った印。歩いた後の画面も撮る (`walked.png`) |
| `midi_serial` | MIDI のモニタ | MIDI APU で 7 (serial へ) → 1 (音階)。C4〜C5 の 8 音が ch1 で、長さ 200 ms・間 260 ms (中央値を 10 ms に丸めて比べ、1 音ずつは ±40 ms に収まること)、前に 16 チャンネル分の all off |
| `apu_sound` | 音の探り | MIDI APU (APU のまま) で、鳴らす前の 1 秒は無音、1 (音階) を押しながらの 3 秒で鳴った (ピーク > 256) こと、鳴った窓の数を秒に丸めた値 (2 秒) |

### 10.4 基準と落ちたとき

- 今回の要約は `tmp/regress/live/out/modern/`、結果の一覧は `tmp/regress/live/results.json`、相手のログ (ホスト・モニタ・送り手・探り) は `tmp/regress/live/counterparts/<場面>/`、sim のコアのログは `tmp/regress/live/modern.log`。
- 意図した変化なら、人が確認してから `rake regress:live:update`。オセロの色の形のように一方の基準が無いときは `NEW ... no baseline` と出るので、写しを見て update する。
- わざと止めた相手を戻さない、相手が別の答えを返す、のどちらも場面の失敗として出る。

### 10.5 分からないこと

- 本当の網の揺れ (遅延・切断の途中)、TLS と資格情報の付いた接続 (VPS・常設の卓)、ブラウザ版の ws の経路。Retro (卓のアプリは Retro の実機には無い)。
- HTTP の相手 (天気のアプリ): 決まった外の HTTPS に繋ぐ作りで、偽の相手に向ける口が無いので入れていない。
- 音の善し悪し (数値で鳴ったか・長さだけを見る)。MIDI の時刻は PC に届いた時刻 (FIFO) で、実機の UART の時刻ではない。

## 11. コンポジタ (rake regress:wl)

```
rake regress:wl                       # 標準と全部 mruby の 2 つを作り (rake native:wl、1 つずつ)、2 つの確認を並べて流す (11 分ほど)
WL_SERIAL=1 rake regress:wl           # 確認も 1 つずつ (20 分ほど。並べたときに落ちたものを見分けるとき)
rake native:wl_check                  # 1 つの形だけを、自動と pixman の 2 つのレンダラで (NATIVE_ENGINE=mruby で全部 mruby)
rake native:wl_check NESTED=1         # WSLg に入れ子 (確認の間だけ Window が出る。マウスを Window の上に置かない)
```

- ネイティブ版をコンポジタとして動かし (doc/linux_native の L8・L9a、`--machine linux_pc_wl`)、Linux の Window (foot) を Family mruby の Window として扱えるかを確認する。Ubuntu 26.04 のイメージ (約 1 GB) の中で作って動かす。手元の Ubuntu 22.04 には wlroots 0.19 が無いため。
- 2 つの構成の確認は並べて流す (入れ物の名前・実行の場所・結果の場所は構成ごと、全部 mruby の確認は debug の port を 100 上に出す)。出力は構成ごとに `tmp/wl_check/checks-<構成>.log` に取り、両方が終わってから続けて出す。
- `rake regress:full` に入っている。`rake regress` (安い方) には入らない。headless (出力は 1280x800 の仮のもの。Family mruby の画面はその半分の 640x400 で、2 倍で出力を埋める。`wl_check.rb --output 1920x1080` などで出力を変えられる)、レンダラは pixman (WSL2 では自動で選んでも pixman になる)。
- 基準の絵を持たない。grim で撮った合成後の画面を、同じ時に撮った Family mruby の絵 (`shot`) を倍率どおりに置いたものと比べ、Linux の Window が見えるべき所 (穴) 以外は 1 画素も違わないこと、穴には foot が出ていることを見る。foot の版や字体が変わっても揺れない。エディタの 1 打鍵だけはネイティブ版の基準 `editor_type.png` (426x240) とエディタの Window の範囲で比べる (Window の四隅は後ろの壁紙が透けるので除く)。全部 mruby のエディタはメニューに Debug が 1 つ多いので、その行は除く。
- 確認の台本は 9 つ。`test/native/wl/wl_check.rb` (L9a: Window の扱い)、`test/native/wl/wl_dialog_check.rb` (L9c: 親を持つダイアログが親の上の真ん中に枠つきで出て前後が正しいこと、メニューが絵の矩形の外に出ないこと。出力を 90 度回して黒い帯を広く取って数える。L9e: 親を押すとキーボードもダイアログに移ること、止まった矢印の下に Window が来るとその Window に矢印が行くこと、ポインタを握る X11 のメニューが Family mruby を押すと閉じること)、`test/native/wl/wl_apps_check.rb` (L9b: ランチャーの「Linux」の区分、一覧から起こす、Xwayland の遅延起動と X11 の Window、自分で大きさを決める Window に代役が合うこと、.desktop を 300 個足したときの開く時間、Exit で起こしたもの全部と Xwayland が終わること。L9e: Breeze の SVG しか無いアイコンが一覧に出ること)、`test/native/wl/wl_keys_check.rb` (L9d: 偽の `/etc/default/keyboard` を入れ物の中にかぶせ、配列を選んでいない (`auto`) ときに foot と Family mruby の Shell の両方が Linux の配列に合うこと、Config で選んだ配列が勝つこと)、`test/native/wl/wl_open_check.rb` (L11: File Manager から Linux のアプリでファイルを開く。HOME・XDG_DATA_HOME・XDG_CONFIG_HOME を実行の場所に置き、自前の `.desktop` (受け取った引数を書き出す台本)・globs2・mimeapps.list・associations.toml で、関連付けに無い種類が Linux の既定で開くこと、右クリックの「Linux のアプリで開く」と候補の 2 番目、8 個の候補の「その他...」の次の頁、表の `linux:<id>` と `linux`、.txt・.rb・種類の分からないファイルが今どおりであることを見る)。`test/native/wl/wl_clip_check.rb` (L12a: クリップボード。foot に自前の台本 (日本語の 1 行を出してから受けたものをファイルへ) を走らせ、Shell の `edit` で開いたエディタの Ctrl+A・Ctrl+C を foot の Ctrl+Shift+V で貼れること、foot の行をドラッグで選んで Ctrl+Shift+C したものをエディタの Ctrl+V で貼って保存できること、Shell のプロンプトの Ctrl+V と `!` で動く Linux のプログラムへの Ctrl+Shift+V、Xwayland の X11 のプログラム (`clients/x11_clip.c`) の CLIPBOARD との行き来、持ち主の foot が終わっても選択が残ること (コンポジタが持ち直す) を見る。数は `comp clip`)。`test/native/wl/wl_ime_check.rb` (L12b: PC の IME。全体を `dbus-run-session` の中で動かし、HOME と fcitx5 の設定 (英字の配列と mozc の 2 つ) を実行の場所に置く。コンポジタが fcitx5 を起こすこと、foot で Ctrl+Space・`nihongo`・Space 2 回 (候補の Window が foot の入力の位置の近く、絵の矩形の中に出る)・上・Enter で「日本語」が入ること、Shell の `edit` が IME の仮想キーボード経由で届くこと、エディタで変換中の文字がカーソルの位置に描かれ候補がその下に出て「日本語」が保存されること、もう 1 つのネイティブ版を `--machine sdl` で Wayland の客として起こし、その中のエディタに SDL の口で「日本語」が入ること、fcitx5 を止めるとかな入力に戻ること、IME のプログラムが無い 2 回目の起動でかな入力が働くことを見る。数は `comp ime`)。`test/native/wl/wl_winops_check.rb` (L13: PC の Window と画面ロック。確認だけの D-Bus を system bus として立てて、その上に logind の代わり (`clients/fake_logind.c`: Lock・PrepareForSleep・休止の遅らせ) を置き、ロックのプログラムも確認だけのもの (`clients/wl_lock.c`) にする。題の帯のダブルクリックでエディタと foot が作業領域いっぱいになって戻ること、wl_dialog の最大化の頼み、Super + 矢印で半分・最大化・戻すことと矢印が Window に届かないこと、logind の Lock でロックの絵が画面を覆い入力が Family mruby にも Window にも届かないこと、ロックのプログラムが落ちても開かずキーで起こし直せること、PrepareForSleep でロックしてから休止を進めること、電源のメニューのロック、本物の swaylock が覆い、落ちても開かないことを見る。数は `comp info` の `states=`・`lock=` と `comp stats` の `to_fmrb`)。`test/native/wl/wl_settings_check.rb` (L14: PC の設定と丈夫さ。確認だけの D-Bus を 2 つ立て、system bus には NetworkManager の代わり (`clients/fake_nm.c`: 6 つのネットワーク、保存済み 2 つ、秘密を代理人に聞く 1 つ) と BlueZ の代わり (`clients/fake_bluez.c`: 対になった機器 3 つと対になっていない 1 つ) を置き、session bus は通知と portal に使う。PipeWire と WirePlumber を起こし、確認だけの出力 (null sink) を 1 つ作る。プログラムはログイン画面のセッションの台本 (`native/package/family-mruby-session`) から起こす。ネットワークのダイアログの「Wi-Fi の設定...」で設定の Window が開くこと、一覧 (同じ名前は 1 つ、隠れたものは出ない)、開いたネットワークのつなぐ・切る、新しい鍵つきのネットワークに違うパスワードで失敗し (その時に作った接続を消す)、正しいものでつながること、パスワードを NetworkManager が持たない保存済みのネットワークで代理人が「無い」と答えて問い、入れたパスワードを代理人が渡してつながること、パスワードを持つ保存済みのネットワークがそのままつながること、ログにパスワードが出ないこと、探す。メニューの Bluetooth で頁が変わること、対になった機器だけが出ること、入り切り、つなぐ・切る、失敗の理由。音量のキーで PipeWire の音量とミュートが動き知らせが出ること (Linux の Window がキーボードを持っていても Window に届かない)、デスクトップのミュート。`notify-send` の知らせが右上に出て数秒で消えること、`--wait` が NotificationClosed を受けること。portal の ScreenCast が出力の流れを返すこと (`clients/portal_cast.c`、包みの family-mruby-portals.conf と xdg-desktop-portal-wlr の設定を使う)。SIGSEGV で台本が起こし直し、新しい方が知らせを出すこと、Exit (制御の口の quit、電源のメニューの Exit と同じ道) では起こし直さないこと、1 分に 4 回目の異常な終わりでセッションが終わること。数は設定の Window のログの行 (`settings: wifi ...`)、代わりのサービスのログ、`wpctl get-volume`)。wl_dialog_check.rb は出力を回すので画面は 426x240 のまま。
- wl_check.rb で確認する中身は頭の説明 (起動、画面の大きさ、枠、移動と大きさの変更、foot へのキーとポインタ、Family mruby へのキー、エディタの全画面が画面の大きさで出ること (L9e)、前後、Ctrl+Tab、全画面のアプリが最大の整数倍で真ん中に出ること (shooter は 426x240、640x360 で並べた hires_test と麻雀は 640x360。見せ方がフレームと一緒に変わる回数も)、時間、代役の四隅、閉じるボタン、終わり方、終える前の待ちの間も制御の口が答えること (L9e))。結果と画面は `tmp/wl_check/<形>-<出し方>-<レンダラ>/`、作りのログは `tmp/wl_check/build-<形>.log`。
- SDL の Window の機種 (`sdl`) のクリップボード (L12a) は別に `rake native:sdl_clip` (`test/native/sdl/sdl_clip_check.rb`)。`rake native:core` で作ったものを自前の Xvfb の上で動かし、もう 1 つの SDL のプログラム (`test/native/sdl/clip_peer.c`) をデスクトップのアプリの代わりにして、アプリの `set_clipboard` が向こうで読めること、向こうのコピーをアプリが読めることを見る (1 分ほど)。`regress:wl` には入らない。
- 分からないこと: GLES での描画 (WSL2 の GPU では動かない)、人の手のキーボードとマウスでの操作、ポップアップの多いアプリ (GTK・Qt のメニュー)、VM や実機の DRM での session。L14 では本物の NetworkManager・BlueZ (polkit の許可、実際の電波)、本物の音の出口、ブラウザの会議からの画面の共有 (portal の問いの Window は KDE・GNOME の portal が出す)、systemd の利用者の session での portal の起こされ方。

## 12. アプリを運ぶ (rake regress:checkpoint)

```
rake regress:checkpoint               # ネイティブ版を標準と全部 mruby の 2 つで作り、両方で流し、続けてブラウザ版とネイティブ版の間も (作り直しが少ないときで 6 分ほど。emsdk と Linux の Chromium が要る)
rake regress:checkpoint SIM=1         # 続けて sim (Retro・Modern) とネイティブ版の間も (作るところから 11 分ほどと、ブラウザ版の場面の 2 分ほど。regress:full はこれ)
rake regress:checkpoint SIM=retro     # sim は Retro だけ (modern も同じ)
rake regress:checkpoint SIM=noja      # 日本語のフォント (efontJA) を持たない Modern の sim (FMRB_FONT_JA=0) との間の font の場面だけ
rake regress:checkpoint ONLY=mruby    # 1 つだけ (standard・mruby)
rake regress:checkpoint NO_BUILD=1    # ネイティブ版は作らずに、今あるもので流す (sim は毎回作る)
rake regress:checkpoint WEB=0         # ブラウザ版の場面を飛ばす (ネイティブ版の場面だけ。作るところから 4 分ほど)
rake regress:checkpoint BOARD=naryav4 # 続けてつないだ実機 (P4) の中と、実機とネイティブ版の間を Asterism で (つないだときだけ。regress:full には入れていない)
rake regress:checkpoint BOARD=naryav4 BOARD_ONLY=1 ONLY=standard NO_BUILD=1  # 実機の場面だけ (相手のネイティブ版は今あるもの。1 分ほど)
```

- 動いているアプリをイメージに書き出して閉じ、そのイメージから起動し直して、止まった所から続くことを確かめる (doc/checkpoint の F1)。もう 1 つのネイティブ版へ移すこと (F2)、Window のあるアプリを運ぶこと (F3)、ビルドの違う sim とネイティブ版の間で運ぶこと (F4a)、Asterism で移すこと (F5) も確かめる。段取りはカーネル (Ruby) が持つので、カーネルの構成 2 つ (標準 = Spinel、全部 mruby) で流す。
- `rake regress:full` に入っている (最後に、`SIM=1` で流す。ブラウザ版の場面も入る)。`rake regress` (安い方) には入らない。ブラウザ版の場面は既定で流す (2026-11-06 から。公開のブラウザ版が gem を持つようになったため、doc/checkpoint/report/web_public.md)。`WEB=0` で飛ばす。`BOARD_ONLY=1` のときも流さない。実機は使わない。ネイティブ版の場面は build/ に触らない (`spinel:gen`・`native:mruby`・`native:core` だけ)。sim の場面は build/ を借りて返す (`regress:run` と同じ `build_tree.rb`)。sim は 3 つのコンテナを使うので、`regress:run`・`regress:screens` と同時に流さない。sim がもう動いている (人のもの) ときは、作る前に断って止まる。
- ネイティブ版は `tools/fmrb_native.rb` でヘッドレスに、**自分の置き場** (run dir `/tmp/fmrb-native-ckpt-<uid>`、debug の port 5585、利用者とキャッシュの場所は `tmp/regress/checkpoint/<構成>_home/` を毎回空から) で起こす。移す場面の相手 (B) は同じビルドをもう 1 つ (run dir `/tmp/fmrb-native-ckpt-b-<uid>`、port 5586、`<構成>_home_b/`)。人が起こしているネイティブ版 (5565) や画面の比較のもの (5575) と並べて動く。ここに書いた run dir・port・ルータのコンテナ名と port は共有 checkout のもの。worktree から流すと木ごとの名前になり、別の木の `regress:checkpoint` と同時に流せる (13 章)。起こしたネイティブ版とルータは、通っても落ちても Ctrl+C でも止める。始めるときは同じ木の前の回の残りを止めてから起こし、port がふさがっていれば理由を言って止まる。
- 試験のアプリは `/app/test/ckpt_counter.app.rb` (Window なし、`.app.toml` に `movable = true`)。Task 2 つ (a・b) と本体のループ (main) が数え、1 歩ずつログに出す。`t=` は `Machine.board_millis` の起動からの差。
- 最初に、運ぶのではない場面を 1 つ流す。
  - turns: `/app/test/task_turns.app.rb` の Task (1 歩ごとに `Task.pass`) が、メインループの番がタイムスライスより長くても 10 歩を進める (10 秒以内)。ネイティブ版は tick を自給するので、ここで見張る (doc/checkpoint/report/hang_task_sleep.md)。
- 運ぶ場面は 8 つ。止める頼み方は 3 通りあり、どれもカーネル → アプリの `_spin` → セーフポイントの同じ道を通る (F1b)。
  - restart: 数えさせてから、デバッグサーバの `checkpoint` (`ruby tools/fmrb_native.rb app checkpoint <pid> --restart`。答えは `{status: written, path}`・`{status: refused, reason}`・`{status: timeout}`) で止める。アプリが `/tmp/ckpt_counter.ckpt` を書いて閉じ、カーネルがそれを起動し直す。
  - wait: `--restart` なしで止め、アプリが閉じてから 2 秒待ち、`app restore` で `.ckpt` を起動する。
  - refuse: `movable` の無いアプリ (`task_sleep`) は断られ、動き続ける。
  - refuse file: `movable` でもファイルを開いたままのアプリ (`/app/test/ckpt_file.app.rb`) は、セーフポイントで gem が断り (開いた `File` の C のデータは運べない)、動き続ける。
  - window (F3): `/app/test/ckpt_window.app.rb` (Window あり、`movable = true`。クリックで点を、キーで文字を描く。日本語のフォントと自前の `GfxBlock` を使い、Task がログに数え、知らせ (`ckpt_window`) を `subscribe` する) をクリックとキーで描かせ、`--restart` で書いて起動し直す。Window の範囲 (`[60, 50, 160, 90]`) が書く前と画素ですべて同じ (アプリが `on_restore` で描き直す。比べ方は 9 章の画面の比較と同じ `Image#diff`、マウスの指す先は Window の外へ動かしてから撮る)、Task が次の 1 歩から続く、知らせを `subscribe` し直している (カーネルのログ)、起動し直した後にクリックとキーが届く。違ったときは `tmp/regress/checkpoint/window.png` と `.diff.png` を残す。
  - self: `/app/test/ckpt_self.app.rb` が `on_update` の中から自分のイメージを頼む (`FmrbApp#checkpoint(restart: true)`)。書かれて起動し直し、頼むのは 1 回だけ (戻ったアプリは頼んだ印を持っている)。
  - self refused: `movable` の無い `ckpt_self_refused` が同じことを頼み、`on_checkpoint_refused` で理由を受けて動き続ける。
  - shell: Shell の Window を開いて `checkpoint -r <pid>` を打つ。カウンタが restart と同じように運ばれ、Shell に答え (`checkpoint: PID n: wrote ...`) が来る。
- 2 つのプログラムの間で移す場面が 5 つ (F2・F3・F4)。最初のプログラム (A) の横に B を起こしてから流す。
  - move: `ruby tools/fmrb_native.rb app move <pid> --to <B の run dir>`。A でアプリを書いて止めたままにし、イメージと `.toml` を分けて B へ運び、B のカーネルが起動して「起動できた」と答えてから、A のカーネルがアプリを閉じる (D15)。答えは `{status: moved, pid}`。カウンタが B で続き、A はイメージの後に 1 歩も数えず普段どおり閉じる。A の書いたときの task tick と B の復元した task tick が同じ、基準の名前の数と digest が A と B で同じ。
  - move refused: 同じことを `--damage` (イメージを短く切って運ぶ、試験用の印) で。B が復元を断り、A は止めた所から続く (`{status: stayed, reason, here: continued}`)。B のデスクトップには失敗の Window を出さない (理由は頼んだ側が受け取る。B のログには残る。カーネルの `App '<名前>':` の行が無いことで見る)。
  - move no answer: `app checkpoint <pid> --hold 1500` で止めたまま誰も答えない。A のカーネルが 1.5 秒で続けさせる。
  - requires (F4): `/app/test/ckpt_requires.app.rb` (`.app.toml` に `requires = ["linux_settings"]`、ネイティブ版だけのメソッド) を B へ移す。同じビルドなので移り、答えに `missing` が無く、B の `/tmp/ckpt_requires.toml` に `requires` が写っている。断る形と知らせる形は、ビルドの違う sim の場面 (calls・requires) で見る。
  - window move (F3): まず window と同じ Window のアプリを `app checkpoint <pid> --hold 5000` で止め、タイトルバーに「移動中」の印 (`moving...`) が出ている (Window の範囲の違いがタイトルバーの中だけ) ことを撮って確かめ、`app finish <pid> continue` の後は Window が止める前と画素で同じ (印の画面は `tmp/regress/checkpoint/window_mark.png` に残す)。次に B へ移す。A のログに印を出して消した行が 1 回ずつ、B の Window が A で書く前の Window と画素で同じ、B で Task が続き知らせを `subscribe` し直す、B でクリックとキーが届く、B の `on_restore` の行の `font=` が `ja,12` (頼んだフォントから選び直す)、A の Window は閉じて、A はイメージの後に 1 歩も数えない。
  - window theme: 3 つ目のネイティブ版 C (同じビルド、debug port 5587、利用者の場所の `/etc/system_conf.toml` はビルドの最初の起動用の etc の写しの `[theme]` を cyberpunk に、`language` を `"ja"` に書き換えたもの) を起こす。C で `ckpt_window` を新しく起動して window と同じ操作をした Window を見本に撮り、閉じる。A の同じ Window (明るいテーマ) を C へ移し、タイトルバーと左の縁の画素が C の `menu_bg`・`border` であること、Window の範囲が見本と画素で同じことを確認する (基底クラスの枠を着いた側のテーマで作り直すこと、doc/checkpoint/report/f3_frame_theme.md)。違ったときは `tmp/regress/checkpoint/window_theme.png` を残す。枠とは別の確認として、`ckpt_window` の 2 つの `FmrbUI` の Label (`bg:` を省いたものと `bg: 0x92` のもの) の地の画素とログの値 (`ui_bg=`・`chosen_bg=`・`window_bg=`) を見る。見本・A・移した後で、省いたものはその端末の Window の地の色 (C で 0x00、A で 0xFF)、渡したものはどこでも 0x92 (doc/checkpoint/report/lang_bg_restore.md)。違ったときは `widget_ground.png` を残す。
  - language: 同じ C へ `ckpt_calls` を移す。A のログの行が `lang=en word=word-en language=en`、C の `on_restore` の行で `FmrbApp.language` が `ja`、`FmrbI18n.lang` も `ja`、アプリが `FmrbI18n.add` で足した語を引くと `word-ja` (表示の言語が着いた側になり、足した語も運ばれること)。C のログは `<構成>_c.log`。終わったら C は止める。
- sim とネイティブ版の間 (F4a)。ネイティブ版の場面の後に、構成ごと・sim の系列 (Retro・Modern) ごとに、sim をその構成で作り (`rake build:linux`、`regress:screens` と同じ端末の固定の設定)、3 つのコンテナをヘッドレスで起こし、ネイティブ版 (同じ構成、`sdl` = Modern の 426x240) を横に起こして流す。sim の debug サーバは 5555 で、道具は `FMRB_DEBUG_TCP_PORT=5555 ruby tools/fmrb_native.rb app ...` (sim から移す) と `app move PID --to 5555` (sim へ移す) を使う。場面は 10 個と、日本語のフォントの無い sim との font (`tools/regress/checkpoint_sim.rb`)。`SIM=1` (か `SIM=retro` など) のときだけ流す。種類ごとにまとめ、sim が今いる種類から流す (Modern なら 標準 Modern・全部 mruby Modern・標準 Retro・全部 mruby Retro の順)。
  - sim restart・sim hold・sim window: sim の中で F1 (起動し直し)、F2 の止めたまま誰も答えない形、F3 の Window の起動し直し (Window の範囲が画素で同じ)。sim の時分割 (tick を進める別のタスク) の上でも止める・続けるが正しいこと。
  - to native・to sim: カウンタを sim からネイティブ版へ、ネイティブ版から sim へ。数が次の 1 歩から続き、task tick が書いたときと同じ。名前の数は両方を出す (ビルドが違うので同じではない。doc/checkpoint/report/f4.md の 1)。
  - window: `ckpt_window` を sim でクリックとキーで描かせてネイティブ版へ移し、Window の範囲が RGB332 で同じ (sim とネイティブ版の画面の取り方で RGB332 から 24 bit への広げ方が違うので、RGB332 に戻して比べる。Window の角の 4 画素は角丸の外の壁紙が見えるので除く)。移った後にクリックとキーが届く。
  - wide: `/app/test/ckpt_wide.app.rb` (Window 380x100) をネイティブ版から sim へ。Retro では Window が画面 (320 幅) に収まるように縮められ、アプリが `on_restore` の前に `on_resize` を 1 回受け、利用の範囲の四隅に描く赤い四角が新しい大きさの位置にある。Modern では大きさが変わらず `on_resize` は来ない。
  - full: `/app/test/ckpt_full.app.rb` (全画面) を sim からネイティブ版へ。Retro の 320x240 から 426x240 になり `on_resize` を受ける (Modern からなら同じ大きさで来ない)。
  - calls: `/app/test/ckpt_calls.app.rb` (ネイティブ版だけの `FmrbApp.linux_settings` を `respond_to?` で備えて呼ぶ。申告なし) をネイティブ版から sim へ。移り、答えの `missing` が `["linux_settings"]`、sim のログに足りない名前の行、sim で数え続ける (doc/checkpoint/movable_apps.md の 5.2)。
  - requires: `ckpt_requires` (同じ名前を `requires` に書いたもの) をネイティブ版から sim へ。sim が断り (`requires the method 'linux_settings'`)、ネイティブ版で止めた所から続く (5.3)。
  - font (系列 `noja`、最初の構成だけ): 日本語のフォント (efontJA) を持たない Modern の sim (`FMRB_FONT_JA=0` で作る。AtomS3R・StickS3 と同じ) を別に作り、`ckpt_window` (`set_font(:ja, 12)`) をネイティブ版から sim へ移して戻す。ログの `font=` が、書く前に `ja,12`、sim で `ja,8` (misaki)、戻って `ja,12`。戻った Window が出る前と画素で同じ (基底クラスが頼んだフォントから選び直すこと、doc/checkpoint/report/font_pct.md)。`SIM=1` に入る (sim を 1 回多く作る)。
  - sim のログは `tmp/regress/checkpoint/sim_<構成>_<系列>.log`、そのときのネイティブ版のログは `..._native.log`、作ったときのログは `..._build.log`。出力は構成と系列ごとに 1 行。
- Asterism で移す場面 (F5、`tools/regress/checkpoint_asterism.rb`)。流すあいだだけ、自前の Zenoh のルータ (sim の zenohd と同じ `eclipse/zenoh` のイメージ、コンテナ名 `fmrb_regress_ckpt_zenohd`、127.0.0.1 の 17457 (tcp)・17459 (ws)) を起こす (docker が要る)。A と B には端末の設定を書いてから起こす: `/home/asterism/config.toml` (そのルータとノード ID `rg-ckpt-a`・`rg-ckpt-b`) と `/home/asterism/receive.toml` (相手のノードだけから受け取る)。カーネルが起動のときに転送のアプリを動かす。頼み方は `ruby tools/fmrb_native.rb app move <pid> --node <ノード ID>` (デバッグサーバの `move`)。
  - asterism move: カウンタを `rg-ckpt-b` へ。相手に聞き (呼ぶメソッドの名前)、書いて止め、Asterism で分けて運び、B で起動できてから A で閉じる。B で次の 1 歩から続き、A はイメージの後に数えない。運ぶ時間・止めていた時間・移す全体の時間を出す。
  - asterism shell: Shell で `move <pid> rg-ckpt-b` を打つ。移り、Shell に `move: PID n moved to rg-ckpt-b (PID m there)` が来る。
  - asterism requires: `/app/test/ckpt_requires_absent.app.rb` (`requires` にどのビルドにも無い名前) を B へ。B が聞かれた所で断り、イメージは書かれず (止まらない)、A で数え続ける。B には何も送られない。
  - asterism not allowed: カウンタを A 自身のノードへ。A の `receive.toml` は自分を許していないので、止める前に断られる。
  - asterism no answer: カウンタを誰もいないノード (`rg-ckpt-nobody`) へ。止める前に断られる。
  - asterism late: `--hold 1` で移す。運んでいる間に止めておく時間が切れて A で続き、後から B で起動した写しは B で止められる (2 つにならない)。
  - 警告つきで移す形 (相手のビルドに無い名前を呼ぶ) は、ビルドの違う相手が要るので sim の場面 (`SIM=1`) で見る: ネイティブ版から sim へ Asterism で、asterism calls (`ckpt_calls`。移り、`missing` が `["linux_settings"]`、sim に警告の行) と asterism requires (`ckpt_requires`。sim が聞かれた所で断り、止まらない)。sim の `/home/asterism` は作業の木の `flash/home/asterism` なので、流すあいだだけ設定を書き、終わったら元に戻す (あったものは書き戻し、無かったものは消す)。ルータは sim の zenohd (127.0.0.1:7447)。
- ブラウザ版との場面 (F5、`WEB=0` でなければ、`tools/regress/checkpoint_web.rb`)。ネイティブ版の場面の後に 1 回、最初の構成のネイティブ版 (A) と、公開のブラウザ版 (gem を持つ。`wasm/build/dist`) の間で流す。作るのは `spinel:gen`・`rake wasm:mruby wasm:dist` (emsdk が要る。`NO_BUILD=1` なら今ある dist)。`wasm/build` は人の開発サーバ (`rake wasm:serve`) が読むものと同じなので、流した後にページを読み直すとこの木の今のビルドになる (`regress:screens` の web と同じ)。ブラウザ版は**自分の開発サーバ** (`rake wasm:serve DIST=1`、port 8067) と**自分のヘッドレスの Chromium** (Linux のもの: `FMRB_CHROME`、`/usr/bin` のもの、なければ Playwright の `~/.cache/ms-playwright` のもの。プロファイルは `tmp/regress/checkpoint/web_profile`、ページのログは Chromium の `--enable-logging` で `web.chrome.log` へ) で開く。`tools/fmrb_web.rb` の `up` は使わない (状態の file が人の開いているページと共用なので) ので、人がブラウザ版を開いたままでも流せる。ページは `fmrb_web.rb --port 8067` の `put`・`reload`・`text`・`key` で動かす。ページの `/flash/home/asterism` (端末の `/home/asterism`) に設定 (ルータは自前のルータの ws/ の port、ノード ID `rg-ckpt-web`、`rg-ckpt-a` からだけ受け取る) を置いて読み直し、`?app=default/shell` で Shell を開いておく。
  - web move: カウンタを A からブラウザ版へ (`app move <pid> --node rg-ckpt-web`)、2 秒後にブラウザ版の Shell の `move <pid> rg-ckpt-a` で A へ戻す。どちらの向きも次の 1 歩から続き、運ぶ時間・止めていた時間・イメージの大きさを出す。
  - web calls: `ckpt_calls` をブラウザ版へ。ブラウザ版に `linux_settings` が無いので `missing` に出て、移り、ブラウザ版に警告の行。
  - web requires: `ckpt_requires` をブラウザ版へ。聞かれた所で断られ、A で止まらずに数え続ける。
  - web late: `--hold 1` で移す。A で続き、ブラウザ版で起動した写しは止められる。
  - ログは `tmp/regress/checkpoint/web.chrome.log` (ページ) と `web_native.log` (A)。
- 実機との場面 (F6、`BOARD=<端末>` のときだけ、`tools/regress/checkpoint_board.rb`)。`regress:boot` と同じく opt-in で、既定にも `regress:full` にも入らない。実機にはこの木のファーム (P4 の構成は gem を持つ) を焼いておき、fmrb の MCP の `serial_start` でシリアルを開いておく (ログは MCP の capture の file から読み、ポートは開かない。別の file は `BOARD_LOG=`)。実機は開発の遠隔操作 (`FMRB_DEV_REMOTE_CTL`、HTTP) で動かす: アプリの起動・一覧・停止、ファイル、止める頼み (`POST /app/checkpoint`・`POST /app/move`、結果は `GET /app/checkpoint?pid=`。実機のデバッグサーバは BLE なので)。実機の IP はログの `Connected, ip=` (なければ `BOARD_IP=`)、この PC の側の IP は実機への経路の送り元 (`HOST_IP=`)。自前の Zenoh のルータ (`fmrb_regress_ckpt_zenohd_board`、全部のインターフェースの 7447。Zenoh の決まりの port で、端末のためにルータを置く PC はもう通してある。WSL2 の mirrored は Hyper-V のファイアウォールが port ごとに通すので、ほかの port は実機から届かない。別の port は `BOARD_ROUTER_PORT=`。sim の zenohd と同じ port なので、sim が動いているときは断って止まる) を起こし、実機の `/home/asterism` の `config.toml`・`receive.toml` を流すあいだだけ書き換えて、終わったら元に戻す。場面が実機で起こすアプリ (`ckpt_*` と転送のアプリ) は、実機のものが無いか違うときだけこの木の `flash/` から置き、終わったら元に戻す (app だけ焼いた実機は storage が古いままなので)。転送のアプリは開発の遠隔操作で起こし、カーネルがそれを自分の転送のアプリとして引き取る (`Transfer app adopted`)。最初の構成のネイティブ版 (A、ノード `rg-ckpt-a`) と実機 (ノード `rg-ckpt-board`) の間で流す。`BOARD_ONLY=1` なら実機の場面だけ (ネイティブ版の場面は飛ばす)。fmruby-core の worktree で流すときは、`tools/fmrb_native.rb` などの直下の道具を置いたディレクトリ (その `fmruby-core` がこの木) を `FMRB_ROOT=` で渡す。
  - board self: 実機の中で `ckpt_self` が自分のイメージを頼み (`restart: true`)、書かれて起動し直し、数が次の 1 歩から続く。
  - board in・out: カウンタを A から実機へ、実機の `/app/move` で A へ戻す。どちらも次の 1 歩から続く。
  - board window: `ckpt_window` を実機へ移して戻す (`on_restore` と Task が続くことだけ。実機の画面は HDMI なので画素は比べない)。
  - board calls・requires: `ckpt_calls` は `linux_settings` が `missing` に出て移る。`ckpt_requires` は聞かれた所で断られ、A で止まらない。
  - board no answer: 実機のカウンタを誰もいないノードへ。止める前に断られ、実機で数え続ける。
  - ログは `tmp/regress/checkpoint/board.log` (実機) と `board_native.log` (A)。
- 最後に、A と B のログに強制終了 (`No response in ...ms, forcing termination`) が 1 件も無いことを確かめる。どのアプリも頼んで閉じ、アプリの Task はアプリと一緒に終わる (doc/checkpoint/report/f2.md の 3)。
- restart・wait・self・shell で確かめること (ログから): a・b・main が、書いた時の次の 1 歩から続く。`on_checkpoint` と `on_restore` の main が同じ。`t=` の書く前と後の差が 0〜800 ms (1 歩は 250 ms ほど。2 秒待っても時計が飛ばないこと = D9)。
- 出力は構成ごとに 1 行と最後の 1 行。落ちたら理由がその行に出る。ネイティブ版のログは `tmp/regress/checkpoint/<構成>.log` (B は `<構成>_b.log`)、作ったときのログは `build.log`。

```
standard: ok (turns: done 10 steps in 1201ms; restart: a 5->6, b 3->4, main 5->6, t step 447 ms, image 23039 B; wait 2.0s: t step 451 ms; refused: ...; file refused at the safe point: C data of class File (type 'IO') has no entry in the checkpoint data table (ivar; window: same 160x90 px, task 5->6, image 29672 B, click and key after; self: ...; shell: ...; move: a 5->6, b 3->4, main 5->6, t step 427 ms, tick 200, 4054 names alike, carried in 1144 ms, held 1350 ms; move refused: ...; move no answer: ...; window move: same there, task 5->6, click and key there, carried in 1388 ms; 53s)
mruby: ok (...)
checkpoint: ok (2.9 min)
```

## 13. 作業の木ごとに流す (worktree・並行)

共有 checkout (family-mruby の中の fmruby-core) と worktree、または worktree 同士で、同じ道具を同時に流せる。ネイティブ版の置き場・port・ルータの名前を、木ごとに決まる値にしてあるため (`tools/regress/tree.rb`)。

- **共有 checkout** (`.git` がディレクトリ) は今までと同じ名前: `regress:checkpoint` の run dir `/tmp/fmrb-native-ckpt-<uid>` (`-b-`・`-c-`・`-sim-` も)、debug の port 5585・5586・5587、ルータ `fmrb_regress_ckpt_zenohd` (17457・ws 17459)、実機のルータ `fmrb_regress_ckpt_zenohd_board`、ブラウザ版の開発サーバ 8067。`regress:screens` のネイティブ版 `/tmp/fmrb-native-regress-<uid>`・5575、ブラウザ版 8016。
- **ほかの木** (`.git` がファイルの worktree) は、木の実のパスの SHA-1 の先頭 6 桁 (ID) を名前に足す: `/tmp/fmrb-native-ckpt-<uid>-<ID>`、`fmrb_regress_ckpt_zenohd_<ID>` など。port は木ごとの 8 個の組 `20000 + 8 × slot` から (slot は ID から決まる 1〜400): +0〜+2 がネイティブ版 A・B・C、+3・+4 がルータ (tcp・ws)、+5 が checkpoint のブラウザ版、+6 が画面の比較のネイティブ版、+7 がそのブラウザ版。人のネイティブ版 (5565)、sim (5555)、コンポジタの確認 (15565〜15679)、共有 checkout の port とは重ならない。実機のルータの port (7447) は実機から届く port なので木によらない (同時に 2 つの木で実機の場面は流せない)。
- **使う前に port の空きを確認する**。答えるものがいるか bind できなければ、port と持ち主 (`ss` で分かれば) と理由を言って止まる (ID の slot が別の木と重なったときや、前の回の残り)。
- **止め損ねを残さない**: `regress:checkpoint` は起こしたネイティブ版とルータを、通っても落ちても Ctrl+C でも止める (ensure と、それが届かなかったときの at_exit)。始めるときは同じ木の前の回の残り (run dir の pid の file が指す生きた fmruby-native、同じ名前のルータ) を止めてから起こす。ほかの木のものには触らない。
- **直下の道具** (`tools/fmrb_native.rb`・`fmrb_png.rb` など、family-mruby の tools/) は、木の 1 つ上か、`FMRB_ROOT=<ディレクトリ>` で渡した所で探す。worktree の上には無いので、worktree から流すときは、直下の tools/ を写し、その `fmruby-core` をこの木への symlink にしたディレクトリを `FMRB_ROOT=` で渡す。無ければ「worktree なら FMRB_ROOT を渡す」と言って止まる。`regress:checkpoint`・`regress:screens`・`regress:live`・`test/native/` の確認 (wl・sdl・deb・net) が見る。`rake regress` (安い方) は直下の道具を使わないので要らない。

```
mkdir -p /path/root && cp -r ../family-mruby/tools /path/root/ && ln -s "$PWD" /path/root/fmruby-core
FMRB_ROOT=/path/root rake regress:checkpoint ONLY=standard WEB=0
FMRB_ROOT=/path/root rake regress:screens ONLY=native
```

- **sim は共有 checkout からだけ**。sim の docker compose は `<FMRB_ROOT>/fmruby-core` をマウントするので、それがこの木でなければ (worktree から共有の family-mruby を指したときなど) 理由を言って止まる (`regress:run`・`regress:screens` の sim・`regress:live`・`regress:checkpoint SIM=`)。sim のコンテナ名は 1 組なので、木が違っても sim は同時に 1 つ。
- 名前を木ごとに分けたのは `regress:checkpoint` と `regress:screens` (と、それを使う `regress:live`) のネイティブ版・ルータ・開発サーバ。`regress:wl` などの `test/native/` の確認は今も固定の名前と port (15565 から) なので、2 つの木で同時に流さない。
- 経緯は report/rg_tree_iso.md。

