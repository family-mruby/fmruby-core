# L9c 報告: 親の窓に付いたダイアログ、メニューを絵の中に、VM での描画の自動の切り替え

> 状態: 完了 (受け入れ 6 点。VirtualBox の Kubuntu での確認と GLES は残件) | 更新: 2026-10-27 | 指示書は instruction_l9c.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

コミット: e0542880 (コードと試験)。この報告は別のコミット。基準の絵は更新していない。

## 1. 不具合の原因

### 再現

- 試験用の小さなクライアントを 2 つ作った (`test/native/wl/clients/`、試験のたびにコンテナの中で gcc でつくる)。
  - `wl_dialog` (Wayland): 親の窓 (300x150、緑) と、`xdg_toplevel.set_parent` で親を付けた窓 (青、右下に赤い閉じるボタン)、`xdg_popup` (黄、位置決めの調整 slide・flip あり / なし)。FIFO で命令を受け、受けた入力をログに書く。
  - `x11_dialog` (X11): 親の窓と、`WM_TRANSIENT_FOR` の窓、override-redirect の窓 (メニュー)。
- 出力を回して (`display.rotate = 90`、720x1280、絵は 1 倍で (147,520) の 426x240) 上下に広い黒い帯を作り、絵の外に何か出れば grim の画面で数えられるようにした。
- 直す前の program でこの一式 (`wl_dialog_check.rb`) を流した結果:

| 見たもの | 直す前 |
|---|---|
| 親つきの xdg の窓 | **代役は付いていた** (題の帯と枠あり)。置き場所は画面の真ん中から 10 ずつずらす普通の窓の決まりで、親の真ん中ではない (親の中身の真ん中 (213,131) に対し (223,141)) |
| 親を押して前に出す | 親が上に来て、ダイアログは親の後ろに隠れる (青い画素 13,437 → 0) |
| X11 の transient の窓 | 代役は付いていた。置き場所は同じく親と無関係 |
| xdg_popup を窓の右下の角で開く | **絵の外の黒い帯に 14,472 画素はみ出す**。slide・flip を許したポップアップでも同じ (位置決めの制約の箱を渡していなかった) |
| ポップアップを開いたまま Family mruby を押す | **閉じない** (popup_done が来ない、黄色の 15,000 画素がそのまま残る) |

### 原因

- 親つきの toplevel はもともと代役の対象で、「代役なしで一番上」にはならない。代役なしで、全部の窓の上に、絵の外まで出るのは**ポップアップの層に入るもの** (xdg_popup と X11 の override-redirect) だけ。Qt のメニュー (QMenu) は Wayland では xdg_popup。KDE のアプリが出した「Saved Sessions / New Session / Manage Sessions...」は、項目の並び (… 付きの項目、Saved Sessions の小見出し) からもメニューと見るのが合う。ダイアログと xdg_popup を取り違えていたのではなく、**メニューの扱いに 2 つの穴があった**:
  1. xdg_popup の最初の configure で `wlr_xdg_popup_unconstrain_from_box` を呼んでいなかった (tinywl のまま `schedule_configure` だけ)。アプリが位置決めで「はみ出たらずらす・裏返す」を頼んでいても、制約の箱が無いので、頼んだ場所 (窓の右下の角から右下へ) にそのまま出た。ポップアップの層は絵の上・出力全体なので、絵の外の黒い帯にもそのまま描かれた。
  2. Family mruby の上を押したとき、押下は seat を通らずに Family mruby へ送っていた (L9a の作り)。xdg_popup の grab を終わらせる (popup_done を送る) のは wlroots の seat の押下の処理なので、Family mruby の上をいくら押してもメニューは閉じず、一番上に残り続けた。
- ダイアログの側の穴は 2 つ: 置き場所が親と無関係 (画面の真ん中) だったことと、親を押すと親の後ろに隠れること。

## 2. ダイアログ (親を持つ窓)

### 親を知らせる

- コンポジタは窓が現れた通知 (`COMP_NOTICE_MAPPED`) に親の番号を入れる (x の欄。0 は親なし)。xdg は `toplevel->parent` の窓の番号、X11 は `xs->parent` (WM_TRANSIENT_FOR) の窓の番号 (override-redirect の親は数えない)。現れた後で親が変わったときは新しい通知 `COMP_NOTICE_PARENT`。ログの窓の行に `a dialog of window N` が付く。
- 代役は親つきでも同じ代役 (題の帯・枠・閉じるボタン・右下のつまみ)。絵の下に描いて穴で見せる形も同じ。

### 置き場所

- ブリッジ (`wl_bridge.c`) が代役を立てるとき、親の代役の窓を窓の一覧から探し、`comp_dialog_place` で「ダイアログの中身の真ん中 = 親の中身の真ん中」に置く。そのうえで Family mruby の画面 (基本の大きさ) に収まるよう最小だけずらす (上はメニューの帯の下)。画面より大きいときは左上を合わせる。
- ダイアログは親とほぼ同時に現れるので、親の代役がまだ無いときは最長 1 秒 (`PARENT_WAIT_MS`) 待つ。待っても無ければ普通の窓と同じ置き場所。
- ダイアログは普通の窓の「10 ずつずらす」の数に入れない。

### 前後とフォーカス

- 出たとき: 普通の窓と同じく kernel に「spawned」を送るので、前に出て、キーボードがダイアログに行く (L9a の仕組みのまま)。
- **親が前に出たとき**: ブリッジが 10 ms ごとに、親の代役の z がダイアログの代役の z より上なら、ダイアログの代役を `fmrb_app_bring_to_front` で前に出す (ダイアログの入れ子は 1 回に 1 段ずつ)。kernel の Ruby は変えていない。代役の並べ方 (C の z の値) だけで済み、kernel の窓の一覧は押すたびに C から読み直すので食い違わない。ログに `stand-in pid N in front of its parent's`。
- このときキーボードは window manager が決めたまま (押した親) に残る。モーダルなダイアログならアプリが親へのキーを捨てるので害は無い。ダイアログを押せばキーボードはダイアログに移る。
- Family mruby の窓 (エディタなど) を前に出すと、ダイアログはその後ろに隠れる (一番上に残らない)。ダイアログを特別な層に置いていないので、普通の窓どうしの前後のまま。
- 親が閉じたら: アプリがダイアログも消すのに任せた (試験のクライアントは親の close で終わり、両方の窓が消えて代役も終わる)。

## 3. メニューを絵の中に収める

- **xdg_popup**: 最初の commit (と `reposition` の頼み) で `wlr_xdg_popup_unconstrain_from_box` を、絵の矩形を窓の面の座標に直した箱で呼ぶ (`comp_route_picture_box`)。アプリが許した slide・flip・resize で絵の中に動く。許していない、または動かしても収まらないものは、**絵の端で切る** (ポップアップの面に `wlr_scene_subsurface_tree_set_clip`。commit ごと・窓が動くごとに付け直し、入れ子のポップアップは親の後に)。どちらの場合も絵の外の黒い帯には 1 画素も出ない。
- **X11 の override-redirect** (メニュー・ツールチップ): 現れたときと場所が変わったとき、絵の矩形に入るよう最小だけ動かして `wlr_xwayland_surface_configure` で伝える (X の座標も出力の座標なので、プログラムは動いたことを ConfigureNotify で知る)。絵より大きいものは左上を合わせて端で切る。ログに `X11 menu N WxH moved from (x,y) to (x,y), inside the picture`。
- **押したら閉じる**: ポップアップの上でない所を押すと、押した窓のもの以外のポップアップを `wlr_xdg_popup_destroy` で閉じる (popup_done が届く。入れ子は外側だけ閉じれば wlroots が中も閉じる)。押した窓自身のメニューは、その窓のアプリが自分で閉じる (grab の決まりのまま)。ログに `N menu(s) closed (a press elsewhere)`。
- メニューは今どおり絵の上の層 (Family mruby の窓の上にも出る)。

## 4. VM での描画の自動の切り替え

### 作り

- `family-mruby-session` が、設定ファイルを読んだ後で `systemd-detect-virt --vm` を 1 回呼ぶ。VM の名前 (`oracle` = VirtualBox、`vmware`、`kvm`、`qemu`、`microsoft` など) が返れば、**決まっていないものだけ** `WLR_RENDERER=pixman` と `WLR_NO_HARDWARE_CURSORS=1` にする。設定ファイルか環境で決まっていればそのまま (例: `WLR_RENDERER=gles2` を書けば GLES を試せ、矢印だけソフト描画になる)。
- 切り替えたときは 1 行: `family-mruby-session: a virtual machine (oracle): drawing without the GPU (WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1); set WLR_RENDERER in /etc/default/family-mruby-session to choose otherwise`。両方決まっていたときは `a virtual machine (kvm): kept as set: ...`。
- `none` (VM でない)、または `systemd-detect-virt` が無いときは何もしない (今までどおり wlroots が選ぶ)。

### この形にした理由

- **始める前に決められる**: 黒い画面は program の中からは見えない。コンポジタの絵は出力に commit され続け (失敗は返らない)、黒くなるのは表示の後段 (GPU のドライバ・KMS の面) の話なので、「描けたか」を program が見分ける手掛かりが無い。起動してから見分けて切り替える形は作れない。
- **wlroots の自動の選び方はもう一段の逃げ道を持っている**: wlroots 0.19 は GLES の renderer が作れないとき (ソフトウェアの GL しか無い時は `WLR_RENDERER_ALLOW_SOFTWARE` が無いと断る) pixman に落ちる (コンテナの中の自動が pixman になるのはこの道。wlroots の中のログの文言からの見立てで、VM の中では確かめていない)。黒くなったのは「GLES の renderer は作れたが画面に出ない」場合と見られ、これは「作れない」として見分けられない。
- **VM の判定は軽く確か**: systemd があるディストリビューション (Kubuntu を含む) なら必ずあり、CPU の hypervisor の印と DMI を見るだけ。VM の上の GPU (VMSVGA・QXL・virtio-gpu の 3D なし) は多くが同じ問題を持つので、VM なら pixman という決まりは外れが少なく、外れても設定ファイルで覆せる (ユーザの決定どおり)。
- **実機の PC は変わらない**: `systemd-detect-virt --vm` は `none` を返し、何も足さない。
- **台本の側に置いた**: ログイン画面の入口でだけ効く。開発の `rake native:wl_run` や試験の一式は環境をそのまま使うので変わらない。
- どちらの変数が効いたかはユーザの VM でも分けて確かめていない。両方を足すのは決定どおり。GLES で何が出ているかは、設定ファイルに `FMRB_WLR_DEBUG=1` を書くと wlroots のログ (`GL renderer: ...` など) が出る。

## 5. 受け入れ条件

確認したコマンド: `rake regress:wl` (標準と全部 mruby のそれぞれに `rake native:wl`、`wl_check.rb --renderer pixman`、`wl_apps_check.rb --renderer pixman`、新しい `wl_dialog_check.rb --renderer pixman`)、`rake native:test` (新しい `session_test.rb` を含む)、`rake regress`、`ruby test/native/deb/deb_check.rb --arch amd64|arm64`。全部 headless。

| 条件 | 結果 |
|---|---|
| 1. ダイアログ (xdg と X11) | `wl_dialog_check` の 1・2・3・7。xdg: 親 300x150 の中身の真ん中 (213,131) に、ダイアログ 160x90 の中身の真ん中 (213,131)。代役の題の帯 (222,36,82) つき、一番上、キーボードはダイアログ (5 打鍵すべて届く)、押下も届く、右下のボタンで閉じて窓と代役が消える。青い画素 13,437 (代役の丸い角の 3 画素を除いた全部)。400x200 の大きいダイアログは中身が (13,31) で画面に収まる。X11 の transient: 親の真ん中 (223,141) にダイアログの真ん中 (223,141)、キー 5 打鍵、ボタンで閉じる。どれも絵の外の画素 **0** (出力 720x1280、絵は (147,520) の 426x240) |
| 2. 前後 | ダイアログを出したままエディタを前に出すと、ダイアログの青い画素が 13,437 → 8,850 (エディタの後ろに隠れる)。親の見えている所 (247,56) を押すと、親が前に出たあとブリッジがダイアログを前に出し (`in front of its parent's`)、青い画素 13,437 (全部見える)、Linux の窓の並びはダイアログが親の上。キーボードは親 |
| 3. メニューを端で開く | 親を絵の右端に寄せ、親の右下の角 (290,140) から 150x100 のポップアップ。slide・flip を許したもの: (141,41) に動いて出力の (412,617)、絵の中、黄色 15,000 画素、外 0。許さないもの: (290,140) のまま、絵の端で切れて黄色 528 画素、外 0。Family mruby の上を押すと popup_done が届き黄色 0 (`1 menu closed`)。X11 の override-redirect 150x100 を (513,560) に頼むと (423,560) に動き、黄色 15,000、外 0。直す前は外に 14,472 画素、押しても閉じなかった (1 章) |
| 4. VM の判定 | `session_test.rb` 12/12: VM (oracle) で設定なし → `WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1` と理由の 1 行。設定ファイルに `WLR_RENDERER=gles2` → gles2 のまま、矢印だけ 1。両方設定 → そのまま (`kept as set`)。環境の `WLR_RENDERER=vulkan` → そのまま。VM でない (`none`)・`systemd-detect-virt` が無い → 何も足さず何も言わない。deb_check amd64 の session (設定は `ROTATE=90` だけ、`systemd-detect-virt` の代役が oracle を返す): 理由の 1 行が出て、renderer pixman、出力 720x1280 |
| 5. 回帰 | `rake regress:wl` 通る (wl_check 19/19、wl_apps_check 8/8、wl_dialog_check 8/8 を標準と全部 mruby で)。`rake native:test` (490 + 64 + 52 + 17 + session 12 + 50 + comp 92)。`rake regress`: host 19/19、描画の記録 205/205。`rake native:deb ARCH=amd64` (3.3 MB、入れると 12.6 MB) と `deb_check --arch amd64` **17/17**。`rake native:deb ARCH=arm64` と `deb_check --arch arm64` **18/18** (作り方は変えていない) |
| 6. 両方の形、pixman | `wl_dialog_check` 8/8 が標準と全部 mruby の両方で、pixman で通る (数値は上と同じ) |

- kernel・デスクトップの Ruby、端末・sim・ブラウザ版のコードは変えていない (変えたのは native/ と試験と rakelib だけ)。`regress:size`・`regress:screens` は流していない (端末・sim の物に触れていないため)。

## 6. VM で人が確かめる手順 (VirtualBox の Kubuntu 26.04)

1. 開発機で `.deb` を作る: `cd fmruby-core && rake native:deb ARCH=amd64` → `native/build-deb/family-mruby_<版>_amd64.deb`。VM へ写す。
2. VM の設定はそのまま (グラフィックコントローラ VMSVGA、3D なし)。
3. VM の中で入れ直す: `sudo apt install ./family-mruby_<版>_amd64.deb`。
4. **設定ファイルを消す**: `sudo rm /etc/default/family-mruby-session` (前回 pixman と矢印のソフト描画を書いたもの)。
5. ログアウトし、ログイン画面で「Family mruby」を選んでログインする。**設定ファイルが無くても画面が出て、矢印が見える**こと。
6. ログ (`~/.local/share/sddm/wayland-session.log`、無ければ `journalctl -b --user`) に次の 2 行があること:
   - `family-mruby-session: a virtual machine (oracle): drawing without the GPU (WLR_RENDERER=pixman WLR_NO_HARDWARE_CURSORS=1); ...`
   - `native: compositor: wlroots 0.19.x on WAYLAND_DISPLAY=... (backends drm..., renderer pixman, seat from the session)`
7. ダイアログとメニュー:
   - Kate (か Konsole・Dolphin) を Linux の区分から起こし、前回の「Saved Sessions / New Session / Manage Sessions...」のメニューを開く。**絵の中 (黒い帯の外) に出て**、項目を選べること。メニューの外 (Family mruby の壁紙など) を押すとメニューが閉じること。
   - 窓を画面の右端・下端に寄せてからメニューを開いても、絵の外へ出ないこと (ずれるか、端で切れる)。
   - Kate の「ファイルを開く」(Ctrl+O) か「設定」のダイアログを出す。**Family mruby の題の帯つきで、親の窓の真ん中に**出て、キーが届き、ボタンで閉じられること。
   - ダイアログを出したまま、メニューからエディタ (Family mruby の) を出すと、ダイアログがエディタの後ろに隠れること。Kate の窓 (親) の見えている所を押すと、親と一緒にダイアログも前に出て、親の上に見えること。
8. 設定が勝つこと (できれば): `/etc/default/family-mruby-session` に `WLR_RENDERER=gles2` を書いてログインし直すと、ログが `drawing without the GPU (WLR_NO_HARDWARE_CURSORS=1; kept as set: WLR_RENDERER=gles2)` になり、renderer が gles2 で起きる (VMSVGA の 3D なしでは前回のように黒くなるかもしれない。確かめたらファイルを消して戻す)。

## 7. 撤回した見立て・踏んだ罠

- **「ダイアログが代役の対象から外れている」は外れ**: 再現すると親つきの toplevel には代役が付いていた。枠なしで一番上・絵の外に出る性質はポップアップの層のものなので、見たものはメニューと結論した (1 章)。指示書の「ダイアログ」の作り (親の真ん中、前後) も別に足した。
- **Family mruby の矢印が穴をふさぐ**: 矢印 (Family mruby が絵に描く) が止まっている所に窓が現れると、矢印の画素は 0x01 でないので穴にならず、その点を押しても Family mruby に行く。試験では、窓を押す前に矢印を一度よそ (壁紙の左下) へ動かす (`wclick`)。人の手では矢印が動けば直る。L9a からある性質で、直していない (残件)。
- **閉じたダイアログの代役がしばらく残る**: 窓が消えてから代役が自分で終わるまで (100 ms ごとの見回り) は、代役の枠がそこにあって押下を取る。試験は代役の終わり (`stand-in pid N ended`) を待ってから次を押す。
- **ポップアップを閉じる繰り返し**: 外側のポップアップを閉じると wlroots が中のものも閉じ、一覧から消える。一覧を辿りながら閉じると消えた項目を踏むので、1 つ閉じるたびに最初から辿り直す (上限 64 回)。
- **試験のクライアントの罠**: `wl_seat` を版 5 で結ぶと `frame` などの呼び返しを全部書かないと libwayland が abort する (押した瞬間にクライアントが落ちた)。版 3 にした。FIFO への書き込みは、読み手が落ちていると開くところで止まるので、非ブロックで開いて「動いていない」とする。
- X11 の override-redirect のメニューは、プログラムが置いた場所が絵の中なら ConfigureNotify が来ない (動かさないので)。試験は絵の外にまたがる場所に出して、動いたことを確かめる。
- 代役の左下の丸い角の 3 画素は、窓の中身の上に枠の色を描く (L9a からの形。L9d で扱う)。試験の青い画素の数はその 3 画素を除いて数える。

## 8. 残件

- VirtualBox の Kubuntu 26.04 での確認 (6 章)。実際の Qt・GTK のメニューとダイアログ (試験は自前の小さなクライアントだけ。Qt・GTK はコンテナの像に入れていない)。GLES の描画。
- 親を押したとき、キーボードは親に残る (ダイアログに移さない)。モーダルなダイアログの親を押してキーを打っても何も起きない (アプリが捨てる)。移すなら kernel に頼む口が要る。
- X11 のメニューは、Family mruby の上を押しても閉じない (X の pointer grab の中の話で、押下が Xwayland に届かないため)。X11 のメニューを閉じるのはそのプログラムの中を押すか Esc。
- Family mruby の矢印が止まっている所に窓が現れると、その点は動かすまで穴にならない (7 章)。
- `systemd-detect-virt` が無いディストリビューションでは VM を見分けない (設定ファイルで指定する)。
- `doc/regression/usage.md` の regress:wl の節に `wl_dialog_check.rb` と時間 (約 10 分) を書き足すとよい (範囲の外なので直していない)。
