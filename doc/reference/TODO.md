# TODO

## Path長チェック

メッセージ構造体で送れるサイズで、パスの長さが決まる。
最大長を検討するとともに、フレームワーク側で、長すぎるパスを弾けるようにする。

## submodule ↔ lib/upstream/patch 整合性 CI チェック

fmruby-core は upstream PicoRuby を submodule として保持し、差分は `lib/upstream/patch/` (部分上書き) と `lib/upstream/replace/` (mrbgem 丸ごと差し替え) で管理している。`rake setup` が `cp -f` で適用する方式のため、submodule を直接編集しても成功し、`lib/upstream/patch/` に反映忘れがあっても検出されない (silent drift)。

実際に 2026-04-19 の監査で 2 件の漏れが見つかった (コミット `b5bebd3`, `b0ac57b` で収容):
- `picoruby-i2c/include/i2c.h` と `picoruby-i2c/src/mruby/i2c.c` (I2C#close 追加)
- `picoruby-mruby/lib/mruby/mrbgems/mruby-task/mrbgem.rake` (HAL auto-load 無効化)

### 実装方針

CI (GitHub Actions) で以下を実行:

1. `git submodule update --init --recursive` で submodule を pristine 状態に
2. `rake setup` を実行 (パッチ適用)
3. 各 submodule (ネストも含む) 内で `git diff --name-only` を取得
4. 変更ファイル一覧を lib/upstream/patch/ と lib/upstream/replace/ の対応ファイルと `diff -q` で突合
5. どれかに不一致があれば失敗

### マッピングルール

- `mrbgems/<gem>/<path>` → `lib/upstream/patch/<gem>/<path>` または `lib/upstream/replace/<gem>/<path>`
- `mrbgems/picoruby-mruby/lib/mruby/mrbgems/<gem>/<path>` → `lib/upstream/patch/picoruby-mruby/lib/mruby/mrbgems/<gem>/<path>`
- 例外: `mrbgems/picoruby-mruby/lib/mruby/mrbgems/mruby-dir/mrbgem.rake` は `lib/upstream/patch/mruby-dir/mrbgem.rake` に置いている (Rakefile L152 参照)。Rakefile の cp 行をパースするか、例外許可リスト化する
- `mrbgems/mruby-compiler2/{include,src}/<file>` → `lib/upstream/patch/compiler/<file>` (命名規約が異なる。Rakefile L133-137 参照)

### 推奨実装

`rake check:patches` として Rakefile に追加。ローカル検証にも CI にも使える。`.github/workflows/` で `rake check:patches` を実行するジョブを追加する。

余裕があれば pre-commit フックにも組み込む (ローカルでの早期検出)。

### 関連

- 監査手順の原型: [../../tmp/picoruby_customization_summary.md](../../tmp/picoruby_customization_summary.md) §6.3
- ランチャー再走査で system_desktop が落ちる (NARYA v4、断続、未解決): [launcher_rescan_desktop_stop.md](launcher_rescan_desktop_stop.md)

## VM の命令の境目の判定を整理して速くする (2026-11-03 記録、今は深追いしない)

mruby の `vm.c` の `RETURN_IF_TASK_STOPPED` (命令の境目ごとのタスク切り替えの判定) を
「2 回の読み込みと 1 回の分岐」に縮め、遅い側を 1 か所の関数に追い出すと、x86-64 で
実行した命令の数が 1.3〜4.7% 減った (valgrind で数えた値。別の実験の副産物。測り方と
数字は `vm_speed_measurement.md`)。`mrb_vm_exec` の中のレジスタの割り当てが楽になり、
`mrb` の読み直しと `regs` の退避が消えたため。

Family mruby の `lib/upstream/patch/picoruby-mruby/lib/mruby/src/vm.c` は同じマクロに自前の
時分割 (`TASK_SELF_TICK`、境目ごとに数を 1 つ減らす) を足しているので、ESP32 では
上流より効く可能性がある。ただし Xtensa・RISC-V で同じだけ減るかは未確認。

着手するときの順:

1. ホストで、Family mruby の `vm.c` (自前の patch 込み) に同じ整理を当てた版と元の版の
   命令数を valgrind で比べる (端末不要)。
2. 端末 (S3・P4) で時間を測る (実機を繋いだとき。数回測る)。
3. 効果があれば、上流の mruby への PR の候補にする (判定は上流の `d2a1c43a5` の所)。

## 使い勝手: 設定の画面 2 つ (2026-11-07 記録、NARYAv3 の Wi-Fi を設定し直したときにユーザが指摘)

- **Console の接続の設定 (tools/web の settings) が端末の状態に追いつかない**: 端末で「BLE Setup」を選んで受け付けを開いても、すでに BLE でつないでいたページは「The board is not accepting settings」のままで、「Read from board」が押せない。ページを読み込み直すと書けた。端末のログでは受け付けを開いた (`settings window open for 300 s`) あとにページがつながり、そのあと読み書きの要求が 1 つも来ていない。ページがつないだときの状態を持ったままで、受け付けが開いたことを知る手段 (通知か、押したときの問い合わせ) が無いと見られる。
- **デスクトップの設定の画面 (config_dialog) が Retro の画面に収まらない**: 項目が多く、下のボタンが画面の外に出て押せない (NARYAv3、320x240)。ページの切り替え (タブか、次へ・前へ) が要る。

## 実機の /home を残したまま焼く仕組み (2026-11-15 記録、ユーザの希望)

今は /home を残すために、storage (8 MB) を読み出し (P4-Nano の CH343 で 460800 bps・約 3.5 分) → PC で littlefs を
開いて /home を新しい storage に重ねる → 全体を 115200 bps で書く (約 9 分)、を手でやっている。案:

- **開発のビルドでは鍵ごと WiFi で退避できる** (2026-11-15 ユーザ: 開発のビルドは読み書き自由。doc/remote_guard の G0)。
- **storage を書かない**: app の区画だけ書き (app_only)、storage の /home 以外の差分 (ビルドの storage_root と
  端末の中身の差) を WiFi の `/fs/put` (fmrb_rd_fs.rb の push) で送る。/home にも鍵にも触らない。
- **書き込みの速さ**: CH34x (CH340・CH343) の端末は最初から 115200 bps にする (ユーザ: 115200 が安定、ほかは時々失敗する)。
- 一式を `rake flash:keep_home` (と MCP の flash の選択肢) にまとめ、退避の像も念のため残す。

## エラーの Window を Return キーでも閉じる (2026-11-15 記録、ユーザの希望)

アプリのエラーの Window (Ruby・Lua・Python・wasm のアプリが落ちたときに出るもの) を、閉じるボタンだけでなく
Return キーでも閉じられるようにする。

**済 (2026-11-16、wt/small)**: Window が出ている間はデスクトップがキーボードを預かり (overlay_state の take_keys)、
Return・テンキーの Enter・Esc で閉じ、閉じたら前の持ち主へ返す。ほかのキーは Window の裏へ通さない。

## WAMR の様子を見えるようにする (2026-11-16 記録、ユーザの希望、夜の作業に入れる)

今は System Monitor (`main/ruby/default_app/monitor.app.rb`) にも Shell の `ps` にも WAMR の様子が出ない (Spinel の
カーネルの写しは wasm を native と表示する)。案:
- C に `fmrb_wasm_stats` (ランタイム: 動いているか、共通のプールの使用・最大・全体、境界の関数の数、モジュールの数) と、
  アプリごとの値 (wasm のメモリのページの今と上限、バイナリの大きさ、直近のフックの時間・命令数、トラップの回数) を
  足し、読むだけのメソッド (決定 18 の形) で Ruby に出す。
- System Monitor に WAMR の欄 (ページか、タスクの一覧の列)。Shell の `ps` のエンジンの列に wasm。
- 10 秒ごとのシリアルのダンプに `fmrb_wasm:` の行。

**済 (2026-11-16、wt/small)**: C の `fmrb_wasm_stats`・`fmrb_wasm_app_stats` (components/fmrb_wamr)、境界の
`FmrbApp.wasm_stats`・`FmrbApp.wasm_app_stats(pid)` (読むだけ、決定 18)、System Monitor の 3 ページ目「WASM」、
Shell の `ps` の ENGINE 列、両カーネルの vm_type の `:wasm`、ダンプの `fmrb_wasm:` 行。直近の命令数は WAMR が
残りの数を書き戻さないので出していない (WAMR に手を入れれば出せる)。トラップの回数はアプリが止まるので
ランタイム全体の数 (起動から) にした。

## set_font の mruby と Spinel の違い (2026-11-16 ユーザ決定、doc/app_boundary/plan.md)

**済 (2026-11-16、wt/small)**: Spinel の `FmrbGfx#set_font` を mruby に揃えた (`:ja_bold` を選ぶ、近い字への
置き換え、返り値は選ばれた `[family, size]`、大きさを省くと 8、`current_font` と幅の測り方も mruby と同じ)。

## 回帰の道具を壊れにくくする (2026-11-16 記録、ユーザの指摘)

今夜だけで、作りの変化と関係ない所で道具が落ち、調べて直して流し直す手間が何度も出た: ブラウザ版はランチャーの
何番目かでアプリを開くので、アプリが増えると別のアプリが開く (kamon が IMU に)。ランチャーと `/app` の一覧の画面は、
アプリを 1 つ足すたびに基準が変わる。ブラウザ版の port が並行の作業と重なる。Services の GC の時間切れの揺れ。案:
- ブラウザ版でもパスでアプリを開く口を作る (`fmrb_web.rb` の命令の経路に launch を足す)。`web_index` をなくす。
- 一覧を写す場面は、回帰専用の決まったアプリの組 (またはランチャーの対象を絞る設定) で見せ、本物の `flash/app` の
  増減で基準が変わらないようにする。時刻・空きメモリなど変わる所は比べる範囲から外す。
- port は道具が空きを探して取り、使っている印を残す (並行の作業と重ならない)。
- 既知の揺れ (GC の時間切れなど) は 1 回だけ自動でやり直し、やり直したことを要約に出す。
- 見た目そのものを比べる必要の無い場面は、画素の一致でなく「Window が開いた・題名が出た」などの判定に。

→ 2026-11-16 対応 (doc/regression/report/hardening.md)。ブラウザ版の `launch`、ランチャーとファイルマネージャの決まった組、
`Tree.port` の空きと印、regress:run の GC の 1 回のやり直し。画素から「開いた」に変える場面は見直して無し。残りは report の 5 章。
- (2026-11-16 ユーザ) regress:run で測るアプリは代表的なものでよい (決まった組のまま、組み合わせを意図して選ぶ)。MCP の web_* にパスで開く口、ネイティブ版の試験の port も空きを取る形に。
  → 2026-11-16 対応: regress:run は代表の組 (usage.md 7.2、sim の基準の作り直しが残り)、MCP の `web_app`、`test/native/` の port は `Tree.free_port` (usage.md 13 章)。

## アプリを何回も開いて閉じてメモリが漏れないかを見る (2026-11-16 記録、ユーザの希望、急がない)

regress:run を代表の組 (9 本) にしたら、全部閉じた後のカーネルの VM プールが前の基準より 17 KB 多かった (Modern)。
アプリの出入りが増えた分の表の広がりなどと見ているが、漏れと区別していない (カーネルの VM プールは PSRAM)。案:
- 同じ組を N 回 (例えば 10 回) 続けて開いて閉じ、回ごとに各 VM プール・内蔵 RAM・PSRAM の空きを記録する道具
  (`regress:soak` など)。回を重ねても値が落ち着けば漏れではなく、増え続けるなら漏れ。
- 実機 (P4・S3) でも同じ台本を流せる形 (WiFi の devctl の起動・終了と、シリアルの `IRAM free` / `fmrb_app:` の行)。

## 試験の GC で Spinel の文字列も掃除する・devctl に GC (2026-11-16 ユーザ、soak の結果から)

doc/regression/report/soak.md: Spinel のカーネルの文字列は自分の閾値や予定でしか掃除されず、debug server の gc では
掃除されないので、regress:run と soak の控えが 20〜60 KB の周期で揺れる (漏れではない)。決めたこと:
- debug server の gc で Spinel の文字列の掃除も行う (試験の口だけ。ふだんの動きは変えない)。直した後に regress:run の
  基準を取り直す。
- 開発のビルドの devctl に「GC をかける」口を足し、実機の soak も sim と同じ測り方にする。
- 2026-11-16 両方入れた (`fmrb_spinel_gc_full` が文字列も若い・古いとも掃除、devctl `POST /sys/gc`、soak の実機は
  それを使う)。sim の soak の振れは数 KB に縮み、regress:run の基準も取り直した (report/soak.md 6 章)。残りは実機の soak。

## Config のダイアログを画面の高さに収める (2026-11-16、ユーザ)

- 設定 13 行 + リリースのビルドの遠隔操作の 1 行で高さ 238 ドットになり、240 ドットの画面 (Retro・Modern とも)
  にメニューバーへかぶせてやっと入る。Retro でも以前から出ていた。
- 決定: **ページ (タブ) に分ける**。上にタブ (例: 表示 / 入力 / 音と時刻 / システム)、各ページ 6〜8 行で、ダイアログは
  画面の中に余裕を持って収める。設定が増えたらページに足す。Save・Cancel は全ページ共通 (どのページの変更も 1 回の
  Save で書く)。

**済 (2026-11-16)**: 4 ページ (表示: テーマ・壁紙・余白X/Y、入力: キーボード・マウスX/Y、音と時刻: 音量・タイムゾーン、
システム: 言語・デバッグログ・BLE/WiFi 自動起動・遠隔操作)。高さは 6 行分で固定の 140 ドット、メニューバーの下の
中央に置き、メニューバーにかぶせる逃げ道は消した。Save・Cancel は全ページ共通 (Cancel は音量の試聴も戻す)。
タブはクリックと Tab / Shift+Tab、上下で タブの行と各行の間を動き、左右はタブの行ではページ、行では値。Enter で
Save、Esc で Cancel (以前はキーを読んでいなかった。開いている間はキーを預かり、閉じたら元のアプリへ返す)。
ブラウザ版で隠す行 (CFG_WEB_HIDDEN) は従来どおりで、行の無くなったページはタブごと消える。遠隔操作の行を sim で
見るには、sim の `flash/etc/system_conf.toml` に `sim_remote_guard = true` を足して起こし直す (main/kernel/
fmrb_remote_guard_sim.c。メモリの中だけの代役で、ESP32・ブラウザ版・ネイティブ版のパッケージでは空の関数。
`rake build:linux` がこのファイルを作り直すので、ビルドの後に足す)。

**済 (2026-11-17)**: Cyberpunk で、選んだタブと選んだ行の字 (緑 0x1C) が地の強調色 (マゼンタ 0xE3) の上で読みにくかった
(コントラスト比 約 2.3:1)。選んだ所の字は、テーマの字の色が強調色の上で 3:1 に届けばそのまま、届かなければテーマの色
(text_light・window_bg・menu_bg・border・button、0x01 は除く) のうち一番離れるものにした (config_dialog.rb の
`cfg_sel_text`)。Cyberpunk は黒 (window_bg、約 6.7:1)。Light (黒/ピンク 約 8.5:1)・Dark (白/灰 約 4.6:1) は今までどおり。
ブラウザ版も同じテーマの値 (config/system_conf_wasm.toml) なので同じく黒になる。メニューのドロップダウンの選んだ項目も
同じ組 (緑/マゼンタ) だが、これは別 (system_desktop.app.rb の DROPDOWN_TEXT)。

## sim のビルドを ESP32 と分け、古いビルドで起動しない (2026-11-16、ユーザ。今夜の作業の後に入れる) -- 済 (2026-11-17)

- **済 (2026-11-17)**: sim は `build-linux/` (sdkconfig も中、コンポーネント管理は使わない)、ESP32 の storage の
  `/etc` の生成物は `build/fmrb_etc/`。`clean_all` は ESP32 の分だけ (`clean:linux`・`clean:web` を足した)。印は
  `tools/build_stamp.rb` (回帰の道具の sim のビルドの使い回しの鍵も同じもの)、断るのは sim_up・native_up・web_up・
  `tools/dev_run_check.sh`・`tools/fmrb_native.rb up`・`rake wasm:serve` (`allow_stale: true` / `STALE_OK=1`)。
  使い方は doc/regression/usage.md 2.1。以下はもとの要件。

- 今は sim が `build/` を ESP32 と共用し、実機用に作る (`build:esp32`・`clean_all`) たびに sim のビルドが消えて 3〜5 分
  作り直している。また `clean_all` がブラウザ版の wasm の libmruby まで消す。
- やること:
  1. sim のビルドを別の場所 (例: `build-linux/`) にする。sim のコンテナ・回帰の道具 (build_tree.rb)・sim_up の検査を追従。
  2. `clean_all` でブラウザ版の部品を消さない (ブラウザ版を消すのは別の名前のタスクにする)。
  3. **古いビルドで起動しない仕組み** (ユーザの要件): ビルドのときに「どのソースから作ったか」の印 (HEAD と、ファームに
     入るソース — main/・components/・sig/・lib/・config/ など、`flash/app` は入らない — の追跡・未追跡の変更から作る
     ハッシュ) をビルドの場所に書き、sim_up・native_up・web_up (と CLI) は起動の前に今の木と比べる。違えば既定で止め、
     理由 (どのファイルが新しいか) と作り直すコマンドを出す (作り直して起動するか、そのまま使うかを選ぶ口も)。
     `flash/app` のアプリはビルドに入らず直接読まれるので、印の対象外 (開き直すだけで足りる)。
     回帰の道具が使い回す sim のビルド (REGRESS_BUILD_CACHE) の鍵と同じ考え方なので、作りを共通にする。

## app_sync の後にランチャーが古い一覧のまま (2026-11-16)

- app_sync は「ファームを焼く → 起動 → storage の差分を WiFi で送る」の順。ランチャーの一覧は起動時に作るので、送った
  .app.toml・アイコンが反映されない (P4-Nano で「C hello」が隠れず、「C tour」が仮の W のアイコンだった。再起動で直った)。
- 直し: 差分を送った後に、ランチャーに読み直させる (devctl から一覧の再スキャンを頼む口) か、送った物があれば再起動する。

**済 (2026-11-17)**: devctl に `POST /app/rescan` を足した (main/drivers/devctl/devctl_http.c)。カーネルへ転送のアプリと同じ
`apps_installed` (name は "remote sync") を送り、デスクトップがランチャーを読み直す (開いていれば今、閉じていれば次に
開いたとき。ログは `Launcher: /app changed (remote sync); ...`)。リリースのビルドではほかの口と同じく登録した PC だけ。
sync (`fmrb_rd_fs.rb sync`・`fmrb_flash_keep.rb sync`、つまり `rake flash:app_sync` と MCP の flash の app_sync) は 1 つでも
送ったら最後にこれを頼み、結果を 1 行出す。口の無い古いファームでは「再起動まで起動時の一覧のまま」と
`fmrb_rd_fs.rb <IP> reboot` を案内する (sync は失敗にしない)。手で頼むのは `fmrb_rd_fs.rb <IP> rescan`。
内蔵 RAM の静的な増分は 0 (口の表と送る中身は const で .rodata、処理はコード)。httpd の口の数が 1 つ増え
(Retro 20→21、checkpoint 入り 23→24、Modern は 30 のまま 29 を使う)、起動時のヒープが数十バイト増える。

## 漢字の辞書は 4 MB があれば優先する (2026-11-17、ユーザ) -- 済 (2026-11-17)

- **済 (2026-11-17)**: 辞書を開くたびに選ぶ (`fmrb_kanji_pick_default`)。変更の口は 2 つの名前のどちらにも反応し、置けば
  4 MB に切り替わり、消せば 1 MB に戻る。開いている間に持つ表は 1 MB 15,545 バイト・4 MB 24,196 バイトで、どちらも
  SYSTEM プール (PSRAM)。内蔵 RAM の静的な増分は 0。P4-Nano と sim で確認。(同日追記) 4 MB があっても開けなければ理由を
  ログに出して 1 MB を開く (`fmrb_kanji_open_default`)。経緯は doc/kanji_input/plan.md の
  「4 MB の辞書を優先する」。以下はもとの要件。

- `kanji_dict` を設定していないときは `/sd/dict/fmrb_kanji_4m.dic` があればそれを、無ければ `/sd/dict/fmrb_kanji.dic`
  を使う。設定したときはその 1 つだけ。案内 (/usr/share/doc/kanji_ja.md・kanji.md)・system_conf の説明・doc/kanji_input も合わせる。

## 画面の回帰のブラウザ版で、アプリの一覧とコアのログを見る口 (2026-11-17)

- `rake regress:screens` (doc/regression/report/rg7.md) は、ブラウザ版では「アプリが動いている・閉じた」
  (プロセスの一覧) と「コアのログのクラッシュの印」を見られない (ページが答える・Window が出る、まで)。
  場面の片付けもページの読み直しで 1 回 30 秒ほどかかる。
- ページに「動いているアプリの一覧」「pid で閉じる」「コアのログを読む」口を足せば見られるが、ファームと
  wasm/web の変更になる。**今は足さない** (2026-11-17 ユーザ)。sim とネイティブ版が同じコードを見ている。

## ルータに届かない間、Asterism の Link の connect がアプリを約 4 秒止める (2026-11-18、ユーザ決定「後回し」)

- `FmrbAsterism::Link` (`lib/gems/picoruby-fmrb-asterism/mrblib/link.rb`) は、つなぎ直しのたびに
  `connect` を同期で呼ぶ。ルータに届かない間は `cannot open a session ... (-128)` が返るまで約 4 秒
  (sim で測った値) アプリの on_update が止まる。その間のキーは失われず、後でまとめて入る。
- AI ヘルパー (doc/ai_coding/report/a2.md 3.1・8 章) で見えた。再試行の間を 8 秒に延ばして (Link の既定は
  3 秒) 止まる時間の割合を下げただけで、止まること自体は残っている。Link を使うほかのアプリ
  (Asterism を使うゲーム・デモ) も同じ形で止まる。
- 直す向き: connect を止まらない形 (裏のタスク・非同期の Future) にするか、届かないと分かっている間は
  試す間隔を伸ばす。実機 (WiFi 越し・TLS のルータ) で何秒止まるかもまだ測っていない。
- **今は直さない** (2026-11-18 ユーザ。課題として記録だけ)。

## 表示をさらに速くする: 変わった所だけ合成・SRM・PSRAM の写し (2026-11-18、ユーザ決定「今は調べない、課題に残す」)

- 今の値と内訳は doc/reference/display_frame_time.md。合成の手当て 3 つの後、640x360 のデスクトップは
  1 フレーム約 25 ms で、そのうち拡大して出す PPA SRM が約 17 ms (66%)、壁紙の blend が約 8 ms。
- 残っている手 (どれも作っていない。見立ては doc/modern_resolution/report/r0b.md の 4 章と 8 章):
  - 変わった所だけ合成して出す (SRM と壁紙の blend の両方に効く。作りは大きい)。
  - SRM を待たずに次のフレームの準備をする (フレーム/秒は上がる。tear の扱いが先)。
  - 大きな写し (遠隔の画面の写し、全面の commit) を DMA に任せる。HDMI の走査中の memcpy は約 31 MB/s。
  - PSRAM の速度の設定 (sdkconfig の提案。編集はユーザの判断)。
  - 全画面のエディタの clear (約 8.5 ms。色キー付きの canvas なので今は飛ばせない)。
- 描画速度は重要 (ユーザ)。再開するときは Fable 5.2 のサブに調査させてよい。
- あわせて未調査: 遠隔の入力の接続が閉じた直後などに出た Interrupt WDT の panic (lwIP の tcpip タスク。
  r0.md 5.5、r0b.md 6.3。手当ての後のファームでは出ていない)。

## 解像度に合わせた Window の最初の大きさと位置 (2026-11-18 ユーザ)

- 640x360 の画面 (P4-Nano の既定、doc/modern_resolution R2) で、Window のアプリの最初の大きさと位置は
  426x240 のときと同じ画素のまま。エディタが 240x200 で左上に出てデスクトップのアイコンに重なる、
  ランチャーが上寄りに出て下に約 150 画素空く (doc/modern_resolution/report/r1a.md 6 章・r2.md)。
- 解像度に合わせて大きさと位置を決める形を、ブラウザ版 (640x360・852x480 のページ)・ネイティブ版
  (広い画面) も併せて検討する。
- **R2 では直さない** (2026-11-18 ユーザ。今のまま)。

## デスクトップの VM プール: Config の「画面の解像度」の分 約 3 KB (2026-11-18、ユーザ決定「受け入れて取り込む」)

- doc/modern_resolution の R1a で、system_desktop の VM プールが約 3.1 KB 増えた (811 KB → 814 KB。Retro と
  Modern の両方。主に `system_desktop/config_dialog.rb` の項目と、変えた行だけを保存する作り)。
- 削る余地: Retro では項目のコードを読み込まない、保存の作りを C 側に寄せる、など。調べていない。

- ブラウザ版の AI ヘルパーが、つながるまで約 20 秒「Wi-Fi につながるのを待っています」と出る
  (Asterism Link の wifi 待ちが原因の見立て。doc/ai_tutor_vps/report/v1.md。2026-11-19 ユーザ: 課題に残す)。
  **済 (2026-11-20、doc/ai_coding/report/a3e.md)**: ブラウザ版はネットワークを「つながっている」と報告する (アドレス・SSID なし)。

- インターネットにつながっているとき、グローバル IP を表示する (2026-11-21 ユーザ: あとでよい)。
  端末は自分のグローバル IP を知らないので、外の相手に「どこから来たか」を聞く形になる (HTTP の 1 回。
  App Store と同じ `FmrbNet.request`)。聞く先の案: 自分の VPS に 1 行の応答を置く / 公開の確認用のサービス。
  **決定 (2026-11-21 ユーザ)**: 聞く先は自分の VPS に置く口 (相手の IP を 1 行で返すだけ)。端末の設定に聞く先の URL を
  書く形にし、設定が無ければこの機能は出ない (作者のサーバを既定に焼き込まない)。出す所はネットワークのダイアログに 1 行
  (開いたときに聞く)。対象はネットワークのある端末全部 (Modern・WiFi のある Retro・ブラウザ版・ネイティブ版。
  ブラウザ版のために、口は別のページからの問い合わせを許す応答ヘッダを付ける)。未着手 (チャットの段の合間に)。

- 描画側の PNG の読み込みでも、透明 (α・tRNS) を透明色 `0x01` にする (2026-11-21 ユーザ決定。doc/asterism_chat/report/c3b.md の Q1)。
  今は VM の中の読み手 (`png332.c`、`BMP332.parse`) だけが透明を `0x01` にし、絵を直接読む側 (`SpriteImage#load_bmp`・
  `create_image`: sim・Retro は graphics-audio、P4・ブラウザ版・ネイティブ版は `display_local_task.cpp`) は透明を捨てて黒く出す。
  **読み方を共通化して、アプリごとに避ける実装が増えないようにする** (ユーザ)。graphics-audio の直しを含む。未着手。

- ホストの gem (asterism-chat・asterism-tutor) の `status` が、**自分の公開が外から見えているか**まで確かめる
  (2026-11-21。VPS で台帳の行に役割を付け忘れ、`up, joined` と出るのに console の図にもほかのノードからも見えなかった。
  今の `up, joined` は証明書で接続できたことまで)。案: 自分のオブジェクトの meta を自分で問い合わせて答えが返るかを見る。未着手。

- アプリが内部用の印 (`_` で始まる) のメソッドを呼んでいる (2026-11-24、doc/archive/sig_coverage/report/s0.md 4.2 の 10)。
  AI ヘルパーの `FmrbApp._get_last_error`、midi_bench の `_clear`・`_stats`・`_audio_note_bytes`。`_` で始まるものは
  sig に書かない決まりなので、補完にも AI ホストの知識にも出ない。アプリから使うものなら公開の名前を用意する
  (API の変更になるのでユーザの判断)。sig には載せていない。未着手。

- **ESP32 の `Machine.delay_ms` が、指定の約 1/10 しか待たない見込み** (2026-11-24、doc/archive/sig_coverage/report/s2b.md。
  コードの読みだけで、実機では未計測)。`lib/upstream/replace/picoruby-machine/ports/esp32/machine.c` の
  `Machine_delay_ms` は `vTaskDelay(ms / ESP32_MSEC_PER_TICK)` で、`ESP32_MSEC_PER_TICK` は 10 (tick が 10 ms の前提)。
  ビルドの `CONFIG_FREERTOS_HZ` は 1000 (tick = 1 ms) なので、`delay_ms(300)` は 30 tick = 30 ms、`delay_ms(5)` は
  0 tick (待たずに譲るだけ) になる。sim (posix の実装) では指定どおり待つ (300 → 335 ms を確認)。使っている所:
  BMI270 の初期化の待ち (`delay_ms(1)`〜`(5)`)、カーネル、ランチャー、試験のアプリ。直すなら
  `vTaskDelay(pdMS_TO_TICKS(ms))`。待ちが 10 倍に伸びる (本来の長さになる) ので、実機で前後を測ってから。
  **決定 (2026-11-25 ユーザ): 実機で測ってから直す。** sig の `Machine.delay_ms` の長文には「約 1/10 しか待たない
  可能性がある」と書いてある (直したら sig も直す)。

  机上で見た影響の範囲 (2026-11-25。ESP32 のビルドだけ。sim・ネイティブ版・ブラウザ版は posix の実装で、指定どおり待つ):

  | 呼ぶ所 | 指定 | 今 (読みどおりなら) | 直した後 | 見立て |
  |---|---|---|---|---|
  | Asterism の待ちの輪 (`Asterism.pause(WAIT_STEP_MS)`、2 ms。同期の呼び出し `wait_until`・二重起動の確認 `already_there?`・ROS の `wait_for`) | 2 ms | 0 tick = 眠らずに譲るだけ。**答えを待つ間、アプリのタスクが回り続ける** (打ち切りは時刻で見ているので、待つ長さ自体は正しい) | 1 回 2 ms 眠る | 一番効く。CPU を空けるので、待ちの間のほかのタスク (同じか低い優先度) が動けるようになる。応答は最大 2 ms 遅れる |
  | ランチャー (mruby のデスクトップ。アイコン 1 つ・フォルダの項目 1 つごと) | 1 ms | 0 tick。コメントの狙い (優先度の低い status_led に回す) は**果たせていない** (譲るだけでは低い優先度に回らない) | 1 項目 1 ms | アイコン 30 個で +30 ms ほど。狙いどおりになる |
  | BMI270 (6 軸センサ) の初期化 (P4 は常に入る。S3 は `FMRB_IMU=1` のとき) | 1〜5 ms | 0 tick。リセットの後の待ちが無く、I2C のやり取りの時間と再試行 (20 回) で通っている見込み | 指定どおり | 初期化が数十 ms 伸びるだけで、確実になる方向 |
  | カーネルの起動時の再試行の間 | 500 ms | **標準の構成 (Spinel のカーネル) は別の実装** (`board_millis` を見て回る) なので影響なし。mruby のカーネルなら 50 ms | 同左 / 500 ms | 標準の構成では変わらない |
  | Spinel のアプリ (エディタなど) の `delay_ms` | - | 別の実装 (同上) | 変わらない | 影響なし |
  | 試験のアプリ (`busy_input` 700 ms・`dbg_e1_test` 100 ms) | - | 70 ms・10 ms | 指定どおり | `busy_input` は「入力をためる」試験で、今は狙いの 1/10 しかためていない |
  | 利用者のアプリ (アプリの書き方の案内が `sleep_ms` の代わりに `Machine.delay_ms` を勧めている) | 任意 | 指定の 1/10。10 ms 未満は待たない | 指定どおり | **実機で合わせて作ったアプリは 10 倍遅くなる**。同梱のアプリは (試験を除いて) 使っていない |

  `ESP32_MSEC_PER_TICK` を使うのは `Machine_delay_ms` だけ。`sleep`・`sleep_ms` (mruby のタスクの tick) と、アプリの
  `on_update` の戻り値での待ちは別の経路で、この件の影響を受けない。

  実機で測ること (つないだとき): (1) `t = Machine.board_millis; Machine.delay_ms(300); Machine.board_millis - t` と
  `delay_ms(5)` を直す前後で (シェルの irb)、(2) 直した後の起動ログ (BMI270 の初期化が通る・起動の時間)、
  (3) Asterism の同期の呼び出しの間の CPU (10 秒周期のタスクの表) と応答の時間、(4) ランチャーを開く時間。

- ファイルまわりで見つかった実装の不具合の疑い (2026-11-25、doc/archive/sig_coverage/report/s2c.md。sig を書くために実装を
  読んで見つけたもの。どれも直していない):
  - `File.path("x")` (クラスメソッド) が nil を返す (**sim で確認**)。`file.rb` の `self.instance_of?(self.class)` の判定。
  - mode `"a+"` が追記にならず、先頭から上書きする (**sim で確認**: "abc" に `a+` で "Z" を書くと "Zbc")。`"rb+"` は
    読み取り専用になる (`+` を 2 文字目でしか見ない。読みだけ)。sig には今の動きを書いてある。
  - `File.expand_path` の相対パスの基準が `ENV["PWD"]` で、`Dir.getwd` でない (読みだけ)。
  - ESP32 のファイル HAL の経路の表が 128 バイトで、長い経路が黙って切れる見込み (読みだけ)。
  - `Dir.chdir` の後の相対パスの意味が、ESP32 (今いる場所を見ない) と sim で食い違うかもしれない (読みだけ)。
  - `IO#puts` が配列を展開しない (**sim で確認**: `$stdout.puts([1,2])` は `[1, 2]` の 1 行。仕様かもしれない)。
  直すかどうかはユーザの判断。直したら sig の該当の長文も直す。

- **`Kernel.send_message`・`receive_message`・`broadcast_message` (C の gem `picoruby-fmrb-kernel`) のメッセージの構造体が、
  本体のものと形が違う** (2 つの定義を読み比べて確認、2026-11-25、doc/archive/sig_coverage/report/s4a.md)。gem の中の自前の型は
  `{type, size, data[64]}` (72 バイト)、本体 (`components/fmrb_msg/fmrb_msg.h`) は `{type, src_pid, size, data[176]}`。
  `receive_message` は queue の 1 件ぶん (本体の大きさ) を 72 バイトの局所変数に受けるので、呼ぶとスタックを壊す見込み。
  `send_message` は足りない分を読み過ぎ、受け手の `size`・`data` が 4 バイトずれる。sim で `Kernel.respond_to?` は true
  (呼べる状態)。**同梱のアプリとカーネルは誰も呼んでいない** (アプリは `FmrbApp#send_message` を使う) ので今は潜在。
  動かして確かめてはいない (壊す操作なので)。直し方: 本体のヘッダを include する (自前の型をやめる) か、この 3 つを
  公開の API から外す。**決定 (2026-11-25 ユーザ): 直すまでの間、sig から外す** (AI ホストが型を見て使わないように)。
  `sig/kernel.rbs` から 3 つの宣言を外し、数える道具の載せない一覧に理由つきで入れた。`message_queue_exists?` は
  構造体を使わないので残した。直したら sig に戻す。
- S4a で見つかったほかの疑い (読みだけ。直していない): `FmrbUI#bg=` は既にある部品の色を変えない (部品が作るときに色を
  写して持つ)、`FmrbI18n.add` の長文「Existing keys are left alone」は実装 (上書きする) と合わない、Spinel の基底に
  `app_settings` 系 3 つが無い、`Kernel.*` の task_id の検査 (0〜15) と本体の上限 (9) が合わない。

- 音のまわりで見つかった実装の不具合の疑い (2026-11-25、doc/archive/sig_coverage/report/s4b.md。読みだけ、直していない):
  `SmfPlayer#transpose` を鳴らしている間に変えると、前に始めた音の note off が別の高さに行って音が止まらなくなる疑い。
  `MIDI::Device#pitch_bend` が `Integer#clamp` を使う (この mruby に無いというコメントが別の所にある。アプリは呼んでいない)。
  `MmlPlayer#load_text` で空のパートの後に鳴るパートが来ると `error` が残る。`SmfPlayer#drop_queued` は共有の待ち行列を
  空にするので、プレーヤーを 2 つ同時に鳴らすと互いの予約が消える。

- データと機器のまわりで見つかった実装の不具合の疑い (2026-11-25、doc/archive/sig_coverage/report/s5a.md。直していない):
  - `JSON::Digger#dig` に配列の範囲の外の位置を渡しても例外が出ず、別の要素が返る (**sim で確認**:
    `JSON::Digger.new('[1]').dig(5).parse` が 1)。冒頭のコメントは例外が出ると書いている。
  - `JSON.parse` の `\uXXXX` は 1 バイトに丸められる読み (`[code].pack('C*')`)。ASCII の範囲 (`\u0041`) は sim で "A" に
    なった。日本語の範囲は未確認。`Array#pack` の無いビルドでは NoMethodError になる可能性 (読みだけ)。
  - `GPIO.new` はピンの台帳への取得の失敗を見ない (システム専用のピンにも書けてしまう読み)。`GPIO#read` の失敗は -1 で、
    `high?` が true になる。`I2C.new` の失敗の文が 1 種類に畳まれる。`JSON.generate` は NaN をそのまま入れる。
    `gpio_viewer.app.rb` の `PIN_COUNT = 49` は直書きで、P4 (55) と合わない。いずれも読みだけ (sim に GPIO・I2C は無い)。

- (済 2026-11-25、doc/archive/sig_coverage/report/s8.md。残りは `rescue Foo => e` の `e` に型が付かないこと) **エディタの型支援が、入れ子のクラス (`A::B`) を受け手にした呼び出しを解けない** (2026-11-25、doc/archive/sig_coverage の S5b の
  検収で確認。sim のエディタ)。`m = FmrbMidi::MmlPlayer.new(d)` や `Asterism::Types.number(...)`、
  `FmrbAsterism::Link.new(...)` の結果に型が付かず (Ctrl+T は "No type here")、その先の補完・Ctrl+E の検査が働かない。
  最上位のクラス (`FmrbUI.new`・`TileSheet.new`)、モジュールの関数の戻り値 (`FmrbMidi.device(self)` → `MIDI::Device`)、
  `Klass::CONST` の定数は解ける。原因: 型エンジン (picoruby-ti の `ti_handle_constant_path`) が「`Klass::CONST` の形だけ」を
  扱い、`A::B` をクラスとして引かない。sig の誤りではない (AI ホストは文字で読むので影響なし)。sig に入れ子のクラスが
  増えた (Asterism・ROS 2 の橋・MIDI のプレーヤー・`JSON::Digger`・`FmrbUI::*`) ので、直すと効く範囲が広い。直し方の案:
  定数のパスの全体 (`A::B::C`) をクラスの名前として引き、当たればそのクラス (静的) を返す。ユーザの判断待ち。
- 通信のまわりで見つかった疑い (2026-11-25、report/s5b.md。読みだけ): `ROS::Subscription#handler=` は `node.subscribe` で
  作った物だけ `Node#poll` に拾われ、`node.subscription` に後から付けても呼ばれない。`TableClient#leave` の戻り値が false。
- AI ホスト (asterism-tutor 0.3.1) の検査 `enumerator_next` が、`conn.each(pattern)` をブロックなしで書いた正しい形
  (sig の例・見本どおり) を落とす (2026-11-25、doc/archive/sig_coverage/report/s6_ai_host.md)。答え直しが 2 回起きた。直していない。
- `FmrbGfx#draw_text_mixed` は同梱のアプリが 9 か所で使うが sig に無い (`draw_text(..., mixed: true)` の、Hash を作らない形)。
  アプリを書く人に見せるかは未決定。
- 見本 2 つ (`flash/app/tool/sprite_editor.app.rb`・`flash/app/demo/stackchan.app.rb`) が、枠の閉じるボタンの内側の数字
  `CLOSE_BTN_CX_OFFSET`・`CLOSE_BTN_CY`・`CLOSE_BTN_HIT_R` を読んで自分でボタンの当たりを見ている。ユーザ決定 (2026-11-25):
  sig には載せない (載せない一覧に入れた、doc/api_fixes F3)。見本の読み方をどうするか (公開の口を作る・見本を変える) は課題。
- **S3 (NARYAv3) のファームの区画の空きが 288 KB (5%)** (2026-11-25、`rake regress:size`。区画 6 MB、像 6,003,680 バイト)。
  P4 は 1.37 MB (16%)。今は収まっている。ユーザ決定: 記録だけ、足りなくなったときに決める。減らす案 (未調査):
  S3 だけ型データベースの要約の領域 (約 112 KB) やシグネチャ (約 58 KB) を外す・S3 に要らないクラス
  (Asterism の ROS 2 の橋など) を db から外す、区画表でファームを広げ storage を縮める (storage は F1 のヘルプが
  約 0.8 MB)、ほかの大きな部品の見直し (`idf.py size-components`)。

## 転送のアプリのタスクのスタックの余りが 644 バイト (2026-11-26 記録、P4-Nano で実測。直し方は未決定)

- P4-Nano (リモートシェルのブランチのファーム) で、転送のアプリ (`/usr/share/transfer/transfer.app.rb`) のタスクのスタック
  (16,384 バイト) の余りが、起動の直後から 644 バイト (周期ダンプ `fmrb_task: Transfer ... 16384 644`)。
  `task_stack_kb = 24` にして測ると余り 8,836 で、**山は 15,740 バイト**。
- 分かっていること: 山は Asterism につなぐ前 (最初の周期ダンプの時点) に出る。リモートシェルを入れても切でも、
  台本を分ける前の版 (65 KB) に差し替えても同じ 644 なので、リモートシェルの変更によるものではない。
  余りが 6.7〜8 KB あった記録は 2.1.0 の頃のファームの物で、それ以後のどこかで深くなった。
- 分かっていないこと: 何が山を作っているか (候補は、checkpoint の基準を作る所 = `movable` のアプリの VM の準備、
  台本のコンパイル)。ほかの `movable` のアプリも同じ深さまで行くか。
- 今は溢れていないが、余裕が無い。直し方 (山を作っている所を浅くする / 転送のアプリだけ `task_stack_kb` を上げる =
  動いている間の内蔵 RAM +8 KB) は、原因を特定してからユーザが決める。
