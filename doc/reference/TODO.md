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

## 漢字の辞書は 4 MB があれば優先する (2026-11-17、ユーザ)

- `kanji_dict` を設定していないときは `/sd/dict/fmrb_kanji_4m.dic` があればそれを、無ければ `/sd/dict/fmrb_kanji.dic`
  を使う。設定したときはその 1 つだけ。案内 (/usr/share/doc/kanji_ja.md・kanji.md)・system_conf の説明・doc/kanji_input も合わせる。
