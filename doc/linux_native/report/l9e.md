# L9e 報告: PC の全画面の見せ方と、コンポジタの小さな直し

> 状態: 完了 (受け入れ 6 点。VirtualBox の Kubuntu での確認と、代役のタスクバーのアイコンは残件) | 更新: 2026-10-28 | 指示書は instruction_l9e.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

コミット: 4842976f (コードと試験と usage.md の 11 章)。この報告は別のコミット。基準の絵は更新していない。

## 1. 全画面の大きさの決め方

### 決まり

全画面の大きさは、アプリの `fullscreen_hires` の値で 3 つに分けた。`fullscreen_hires` を bool から uint8_t にし (構造体の大きさは同じ)、値を 0 / 1 (`FMRB_FULLSCREEN_HIRES_FIXED`) / 2 (`FMRB_FULLSCREEN_HIRES_ANY`) にした。

| アプリ | 端末・sim・ブラウザ版・ネイティブ版の 426x240 の画面 | コンポジタの広い画面 (640x400、960x540 など) |
|---|---|---|
| 普通の全画面 (shooter など、0) | 今までどおり | 今までどおり 426x240 (最大の整数倍で真ん中) |
| `.app.toml` の `fullscreen_hires = true` (1: 640x360 で並べたもの。麻雀、hires_test) | 640x360 の高解像度の画面 (今までどおり) | 640x360 (画面が 640x360 より小さいときは 426x240)。最大の整数倍で真ん中 |
| エディタ (組み込みの表で 2: どの大きさでも並ぶもの) | 640x360 の高解像度の画面 (今までどおり) | 画面の大きさそのもの (640x400、960x540) |

- 1 と 2 の違いはコンポジタの広い画面の上だけ。端末・sim・ブラウザ版はどちらも今までどおり 640x360 の高解像度の画面になる。
- 大きさを決めるのは C の 1 か所 (`fmrb_app_fullscreen_size_for`、`fmrb_app.c`)。起動のとき (spawner) と、F11 で全画面に入るとき (kernel の Ruby の `fullscreen_size(pid)`) の両方がここを通る。kernel の Ruby の `fullscreen_size(pid)` は、今まで Ruby で組んでいた判断 (高解像度の画面を取るアプリならその大きさ、ほかは `_screen_width` / `_screen_height`) を、新しい口 `_fullscreen_size(pid)` (Spinel は `fmrb_spx_fullscreen_size`、mruby は kernel.c) 1 つにした。端末での答えは前と同じ (同じ判断を C に移しただけ)。
- 広い画面では高解像度の画面の切り替え (SET_SCREEN_MODE) を使わない。`fmrb_app_fullscreen_hires_size` は今までどおり 426x240 の画面のときだけ 640x360 を答えるので、広い画面では kernel の `screen_mode_for` は何も選ばず、切り替えは送られない。全画面のアプリは画面の大きさ以下の普通の canvas として出る。
- 「広い画面」は L9d で決めた、機種が `form = "output"` で画面が 426x240 より大きいとき (`fullscreen_w` が付いているとき)。

### 表示のタスクの条件 (端末と共通のファイル)

- 表示のタスクは「基本の大きさのフレームに、ちょうど 640x360 の canvas が出ていたら、高解像度に移る途中」として合成から外していた。ネイティブ版で、フレームが 640x360 以上のとき (広い画面) はこの外しをしないようにした (`#if defined(FMRB_PLATFORM_NATIVE)` の中の条件 1 行)。広い画面には高解像度の画面が無いので、640x360 の canvas は 640x360 で並べたアプリそのもの。
- 端末・sim・ブラウザ版のビルドではこの行は入らない (コンパイルの段階で消える) ので、動きは変わらない。ブラウザ版の 1280x720 のページも今までどおり外す。

### 見せ方

- 画面より小さい全画面のアプリ (426x240 か 640x360) は、L9d の「見せる所 (view)」でその矩形だけを出力に収まる最大の整数倍で真ん中に出す。1280x800 で 426x240 は 3 倍で (1,40)、640x360 は 2 倍で (0,40)。1920x1080 で 426x240 は 4 倍で (108,60)、640x360 は 3 倍でちょうど埋める。
- エディタの全画面は画面と同じ大きさなので view は無く、画面全体の 2 倍がそのまま出力を埋める。

### エディタが任意の大きさで並ぶか

- エディタは利用者の範囲から並べ方を計算する (`recompute_layout`: メニューの行は上、状態の行は下、桁数は幅 ÷ 字の幅、行数は残りの高さ ÷ 行の高さ)。640x400 と 960x540 で画面を撮って見た。メニュー (File Edit Search Run View Keys) が上の端、状態の行 (`[New] Ln 1, Col 3` と右端の `[A]`) が下の端に並び、打った字が左上に出る (`editor_fullscreen_type.png`)。決め打ちは無かった。

### 640x360 を決め打ちしたアプリ

- `fullscreen_hires = true` を書いているアプリは麻雀と hires_test の 2 つ (flash/ の .app.toml を全部見た)。
- 麻雀は `@big = 幅 >= 640 && 高さ >= 360` で 640x360 の卓 (`LAYOUT640`、部品の座標を 640x360 の絵の上の決め打ちで持つ) を選び、1280x720 以上でだけ別の並べ方 (`@xl`) になる。640x400 や 960x540 を渡すと、640x360 の卓が左上に寄り、下や右に空きが出る (崩れる)。hires_test は 640x360 の試験の絵。
- そこで、この 2 つ (`.app.toml` で `fullscreen_hires = true` と書いたもの全部) には **640x360 を渡し、2 倍 (出力が大きければ最大の整数倍) で真ん中に出す (余りは黒)** 形にした。1280x800 では 640x360 を 2 倍で (0,40)、上下 40 画素ずつ黒 (`mahjong_composite.png`)。
- エディタのように任意の大きさで並ぶアプリが自分で「画面の大きさをください」と言う書き方 (`.app.toml` で 2 を書くなど) は足していない (仕様を増やすことになるので残件)。

## 2. コンポジタの直し

### 親を押したときのキーボード (ダイアログへ)

- 原因: 親を押すと kernel の window manager が親を前に出して親にキーボードを渡す。ブリッジはその後でダイアログの代役を前に出していた (`fmrb_app_bring_to_front`) が、キーボードの持ち主は変えていなかった。
- 直し方: ブリッジがダイアログを親の前に出すとき、kernel にタスクバーと同じ `focus_app` (前に出してキーボードを渡す) を頼む。この頼みは押下を扱った後に kernel が読むので、押下で親に渡ったキーボードがその後でダイアログに移る。コンポジタは次の置き場所でキーボードの行き先を合わせる。同じダイアログへの頼みは 300 ms に 1 回まで (kernel が読むまでの間に繰り返さない)。kernel の Ruby は変えていない。
- モーダルかどうかは見ていない (指示書どおり、親を押せばダイアログに移す)。モーダルでないダイアログ (検索の窓など) で親にも打ちたいときは、ダイアログを閉じるか、Ctrl+Tab で親に切り替える形になる。

### X11 のメニューを Family mruby を押したときにも閉じる

- 原因: X11 のメニューはポインタを X の中で握り (grab)、どこを押されてもそのプログラムに押下が届いて閉じる。しかし Xwayland は自分の面の上の押下しか受け取らないので、Family mruby や Wayland の窓を押しても Xwayland に何も届かず、メニューは開いたままだった。
- 直し方: 押下が X11 の窓の上でないとき、窓の種類 (`_NET_WM_WINDOW_TYPE`) がメニュー (popup menu・dropdown menu・menu・combo) の override-redirect の窓が出ていれば、そのメニューの面を通して押下と離しを Xwayland に送る (押した場所はメニューの外の座標のまま)。X は握っているプログラムにその押下を渡し、プログラムが自分で閉じる。そのあとポインタの行き先は元に戻す (Family mruby の押下は今までどおり Family mruby にも届く)。
- メニューの種類だけにした理由: ツールチップはポインタを握らない。握っていない窓を通して押下を送ると、その場所にある X11 の窓 (Family mruby の窓の下に隠れたものでも) が押される。GTK・Qt のメニューは種類を付ける。
- ログに 1 行: `a press elsewhere passed to X11 menu N (x,y of it)`。

### 止まった矢印の下の穴

- 原因: Family mruby の矢印は表示のタスクがフレームに描き込む。矢印の先の画素は矢印の色なので、止まった矢印の下に窓が来ても、その画素は穴の色 (0x01) にならず、穴と判定されない。矢印を動かすと新しい場所の画素で判定し直すので直っていた。
- 直し方: (1) コンポジタは Family mruby の矢印の先の位置を覚える (ポインタを Family mruby に送るたびに。窓の上で矢印を隠したら「無し」)。穴の判定で、点が矢印の先と同じ画素なら、矢印の外になる左・上・左上の画素で穴かを見る (矢印は先から右と下に伸びる形)。(2) 窓の置き場所・前後が変わったときと、新しいフレームが来たときに、止まっているポインタの行き先を判定し直し、変わっていればポインタが動いたときと同じ切り替え (Family mruby の矢印を隠して窓に入る、またはその逆) をする。変わっていなければ何も送らない。ボタンを押している間と、窓を引きずっている間はしない。
- 窓の内容の左上の角の 1 画素に矢印の先が止まっているときだけは、左も上も枠なので穴にならない (矢印を 1 画素動かせば入る)。

### 全画面のアプリに切り替わる瞬間の 1 枚

- 原因: 見せる所 (view) はブリッジが 10 ms ごとに窓の一覧を見て決め、置き場所と一緒にコンポジタへ渡していた。フレームとは別の道なので、全画面のアプリが出た最初のフレームを前の見せ方 (画面全体の 2 倍) で、閉じた直後のフレームを全画面の見せ方で出すことがあった。
- 直し方: 見せる所をフレームと一緒に運ぶようにした。表示のタスクが合成した canvas の並び (下から上、それぞれの位置と大きさ) をネイティブ版の表示の口 (`display_backend_native.cpp`) で覚え、フレームを渡すときに「全画面のアプリの canvas (一番上のもの) が画面より小さければ、その矩形」をそのフレームの見せる所として一緒に置く (`native_frame_put_canvases`)。コンポジタはフレームを写すときに、そのフレームの見せる所を受け取って使う。全画面のアプリの canvas を含むフレームは必ずその見せ方で、含まないフレームは必ず画面全体で出る。
- アプリの canvas が全画面のものかは、ネイティブ版だけの関数 `fmrb_app_canvas_fullscreen` が錠を取らずに文脈の表を読んで答える (表示のタスクから毎フレーム呼ぶので、表示を待っているタスクが錠を持っている場合に止まらないように)。
- 置き場所 (`comp_places_t`) から view を消し、ブリッジの見せる所の計算もやめた。

### 終える前の待ちの間の制御の口

- 原因: Exit の後の「窓が閉じるのを待つ」(最大 5 秒) はコンポジタの中の繰り返しで、main の繰り返しを抜けた後なので、制御の口 (shot・quit・comp) を誰も読んでいなかった。また制御の口は出力を閉じる前に閉じていた。
- 直し方: 待ちの繰り返しで制御の口を読む (`native_ctl_poll`)。shot と comp はそのまま答え、quit が来たら待ちをやめてすぐ次 (残ったものに SIGTERM) へ進む。main では制御の口を閉じるのを出力を閉じた後にした。

## 3. アイコンの読み方と変換の重さ

### 読み方

- アイコンのテーマの決まり (freedesktop) どおりに探す。テーマの順: 利用者のテーマ → そのテーマが受け継ぐテーマ (Inherits の順、幅優先、同じものは 1 回) → hicolor。
- 利用者のテーマ: Plasma の設定 (`$XDG_CONFIG_HOME/kdeglobals` の `[Icons] Theme`、無ければ `~/.config`) を先に、無ければ GTK の設定 (`gtk-4.0/settings.ini`、`gtk-3.0/settings.ini` の `gtk-icon-theme-name`)。どちらにも無ければ breeze (Plasma の既定。入っていなければ hicolor だけ)。
- テーマの中: index.theme の `Directories` の各節 (Size・Type・MinSize・MaxSize・Threshold・Scale) から、ランチャーの大きさ 24 との距離を決まりどおりに計算し、近い順に並べる。Scale が 2 以上の節は見ない。名前のファイルを PNG、SVG、SVGZ の順に探し、最初に見つかったものを使う (テーマの順が先、その中で近い大きさが先)。テーマのどれにも無ければ今までどおり pixmaps。hicolor に index.theme が無いときは今までの大きさの一覧で探す。
- テーマの節の一覧は、一覧を読むたびに 1 回だけ読む (Breeze は 383 の場所、読むのに 4〜7 ms)。同じ Icon の値は 1 回だけ探す (300 個の項目が同じ Icon を持つとき、探すのは 1 回)。

### SVG の変換

- librsvg を実行時に開いて (dlopen) 使う。プログラムには結び付けないので、librsvg が無い PC でも起動し、そのときは SVG のアイコンだけが無い (ログに 1 行)。arm64 の作り方は変えていない。
- 96x96 に描いて (cairo の ARGB)、PNG と同じ縮め方 (不透明度で重みを付けた平均、半分以上覆われていれば不透明) で 24x24 の RGB332 にする。作った BMP はキャッシュに残り、同じ元のファイルは次からは変換しない (今までの PNG と同じ)。
- .deb の Depends に `librsvg2-2` を足した (amd64 だけ。dlopen なので dpkg-shlibdeps は見つけない。libsystemd0 と同じ扱い)。
- 重さ (コンテナの中、wl_apps_check): テーマの節を読むのに 1〜6 ms (Breeze の 383 の場所)。キャッシュが空の最初の一覧で、SVG 4 つを含むアイコン 7 つに 193〜199 ms (librsvg を開く時間と最初の描画の準備を含む。PNG だけだった L9d では 0〜2 ms)。2 回目の一覧 (キャッシュの BMP を使う) は 2 ms。300 個を足した 309 項目の読み直しで、アイコン 2 ms、Reload から一覧が出るまで 210 ms (標準) / 207 ms (全部 mruby) と L9b と同じ。同じ Icon の値を 1 回だけ探す形にする前は、Breeze の 383 の場所を項目ごとに探してアイコン 58 ms、Reload から 612 ms かかった。
- 一覧はアイコンを作る前に出すようにした (名前と項目を先に置き、アイコンは作れたものから同じ一覧に足す)。窓の名前 (代役の題) は一覧から決めるので、SVG の変換を待つと最初の窓の名前が付かなかった (foot が「Foot」でなく「foot」になった) ため。ランチャーが一覧を「ready」と見るのは今までどおりアイコンまで済んだ後。

### 代役 (タスクバー) のアイコン

- 付けていない。タスクバーの項目は 9x9 の色の四角に頭文字を描く形で、24x24 のアイコンを入れる場所が無い。付けるには 9x9 (か 11x11) のアイコンを別に作る口と、デスクトップの Ruby (端末と共通) のタスクバーの描き方を変える必要がある。残件。

## 4. Qt の見た目

### 調べたこと

- Qt は platform theme (見た目の部品: 字体、色、アイコンのテーマ、ダイアログ) を、`QT_QPA_PLATFORMTHEME` が無いとき `XDG_CURRENT_DESKTOP` から選ぶ。KDE なら Plasma のもの (plasma-integration の KDEPlasmaPlatformTheme)、ほかは Qt の既定。Family mruby が起こすプログラムは `XDG_CURRENT_DESKTOP=family-mruby` なので、Plasma の部品が入っていても Qt の既定の見た目になっていた。
- `XDG_CURRENT_DESKTOP` に KDE を足す (`family-mruby:KDE`) と、Qt は Plasma を選ぶが、各プログラムと Linux の一覧の `OnlyShowIn=KDE;` の判定が変わる (システム設定の一部などが一覧に出る)。指示書の「判定が今と変わらないように」に当たるので、この形は取らなかった。

### 直し方

- コンポジタが開くとき、`QT_QPA_PLATFORMTHEME` が決まっていなくて、Plasma の Qt の部品 (Qt 6 の `KDEPlasmaPlatformTheme6.so`、Qt 5 の `KDEPlasmaPlatformTheme5.so`) が入っていれば、`QT_QPA_PLATFORMTHEME=kde` を自分の環境に置く。そこから起こすもの全部 (ランチャー、autostart、Shell の `!`、Xwayland) に効く。`XDG_CURRENT_DESKTOP` は `family-mruby` のまま (一覧の判定は変わらない)。利用者が自分で決めていればそれを使う。ログに 1 行: `Qt's look: Plasma's (QT_QPA_PLATFORMTHEME=kde; <見つけた部品>)`。

### 確かめた方法と結果

- 方法: 使い捨てのイメージ (build image に plasma-integration と kdialog を足したもの。確かめた後で消した) で、コンポジタを headless で起こし、autostart で kdialog (Qt 6 の KDE のプログラム) を出した。`QT_DEBUG_PLUGINS=1` で Qt がどの platform theme を読んだかをログで見て、grim で画面を撮った。
- 結果 (今回の作り): ログに `Qt's look: Plasma's (QT_QPA_PLATFORMTHEME=kde; /usr/lib/x86_64-linux-gnu/qt6/plugins/platformthemes/KDEPlasmaPlatformTheme6.so)`。kdialog の環境に `QT_QPA_PLATFORMTHEME=kde`、Qt のログに `KDEPlasmaPlatformTheme6.so loaded library`。ボタンが Breeze の形 (選んだボタンが青く、ニーモニックの下線が出ない)。
- 比べたもの: `QT_QPA_PLATFORMTHEME=none` (Qt が Plasma を選ばない、前と同じ状態) では、部品の場所は見るが読み込まず、ボタンは Qt の既定 (Fusion。ニーモニックの下線つき)、質問のアイコンが出る。2 つの画面で見た目が違うことを目で確かめた。
- 確かめていないこと: 実際の Kubuntu (Plasma の設定・字体・色) での見た目は VM で人が見る (6 章)。Qt 5 のプログラムは Qt 5 の部品 (`KDEPlasmaPlatformTheme5.so`) が入っていれば同じ名前 `kde` で読む見込みだが、コンテナでは試していない。
- 検出の口 (`linux_apps_plasma_qt`) は host の試験で、偽の根の下に部品のファイルを置いて確かめた。

## 5. 受け入れ条件

確認したコマンド: `rake regress:wl` (標準と全部 mruby のそれぞれに `rake native:wl`、wl_check・wl_apps_check・wl_dialog_check・wl_keys_check、headless、pixman、出力 1280x800。9 分 28 秒)、`ruby test/native/wl/wl_check.rb --output 1920x1080 --renderer pixman --engine standard|mruby`、`rake native:test`、`rake regress`、`rake regress:screens ONLY=retro,modern,native`、`rake regress:screens ONLY=retro,modern FULL=1`、`rake regress:size`、`rake native:deb ARCH=amd64|arm64` と `deb_check.rb --arch amd64|arm64`。

| 条件 | 結果 |
|---|---|
| 1. 全画面の大きさ | 1280x800: F11 でエディタが 640x400 (`Runtime fullscreen: PID N -> 640x400`)、view なしで 2 倍の (0,0)、grim は 1280x800 (出力をちょうど埋める)、1 打鍵で絵が変わる、F11 で窓に戻る。1920x1080: 960x540 を 2 倍で (0,0)、同じく打鍵が通る。shooter は 1280x800 で 426x240 を 3 倍で (1,40)、1920x1080 で 4 倍で (108,60)、周りの黒以外 0 画素。麻雀・hires_test は 1280x800 で 640x360 を 2 倍で (0,40)、1920x1080 で 3 倍で (0,0)、中は絵と全画素一致 (921,600 / 921,600、2,073,600 / 2,073,600)。標準と全部 mruby の両方 |
| 2. 直しの確認 | regress:wl に足して通る。ダイアログへのキー: 親を押した後、キーボードはダイアログ (4、親は 1)、5 打鍵がダイアログに 5、親に 0。X11 のメニュー: ポインタを握るメニュー (grab 0 = 取れた、黄色 15,000 画素) が、壁紙を 1 回押すと 1 回 Xwayland に渡り、プログラムが閉じて黄色 0。止まった矢印: 矢印を画面の真ん中 (213,125) に置いたまま X11 の窓を出すと、動かさずにポインタがその窓 (`5:140.50,59.50`、cursor window) に行く (判定し直し 1 回)。切り替えの 1 枚: shooter・麻雀・hires_test の出入りで、見せ方がフレームと一緒に変わった回数が各 2 (入る・出る)、どのフレームも自分の見せる所で出る (作りの上で前の見せ方の絵は出ない)。待ちの間の制御の口: Exit の後の待ちの間に shot (640x400) と comp info が答え、quit で待ちをやめて Exit から 1.8 秒で終わる (待ちは 5 秒、止めた foot は SIGTERM・SIGKILL で終わり、残ったプロセス無し)。wl_check 26/26 (前の 23 に、エディタの全画面・麻雀・待ちの間の制御の口)、wl_dialog_check 11/11 (前の 8 に 3)、wl_apps_check 9/9 (前の 8 に Breeze の SVG)、wl_keys_check 3/3 を標準と全部 mruby で |
| 3. アイコンと Qt | Breeze にだけ SVG で入っている accessories-calculator を Icon にした項目が一覧に出る (576 画素のうち 360 画素が描かれた 24x24)。ログ `icon themes breeze > hicolor (none set: Plasma's default; 383 directories read in 4 ms), 4 SVGs converted`。Qt は 4 章 |
| 4. 回帰 | `rake regress`: host 19/19、描画の記録 205/205。`regress:screens ONLY=retro,modern,native`: 23/23 (retro 8/8、modern 10/10、native 2/2、native-keys 3/3)、全部 mruby (`FULL=1`): 22/22。基準は変わっていない。`regress:size`: NARYAv3・TAB5 とも **DIRAM・IRAM・other の内蔵 RAM +0**、Spinel の静的な内蔵 RAM も同じ。flash は NARYAv3 4,556,752 / TAB5 6,738,576 (L9d の時から -128 / -112: kernel の Ruby の判断を C に移した分)。`rake native:test` (514 + 64 + 52 + 17 + session 12 + apps 67 + comp 119)。`deb_check --arch amd64` 17/17、`--arch arm64` 18/18 (amd64 の Depends に `librsvg2-2`、arm64 の Depends は変わらない) |
| 5. 両方の形、pixman | 1・2 の確認 (wl_check、wl_dialog_check、1920x1080 の wl_check) が標準と全部 mruby の両方で、pixman で通る |
| 6. .deb | 最後に `rake native:deb ARCH=amd64` で作り直した (この報告のコミットの版。3.3 MB、入れると 12.6 MB) |

- ブラウザ版の画面 (`regress:screens` の web) は流していない。`tools/fmrb_web.rb` が開いたブラウザが開いたままで、道具が止めるため (L9a からと同じ。閉じていない)。ブラウザ版に効く変更は、kernel の Ruby の `fullscreen_size` を C に移したこと (答えは同じ) と、表示のタスクの条件 (ネイティブ版のビルドだけに入る行) で、ブラウザ版の動きは変わらない作り。
- 時間: 何もしない時 CPU 2.9〜3.1%、キーから foot の commit まで平均 1.2 ms、最長 2 ms (L9d と見分けがつかない)。

## 6. VM で人が確かめる手順 (VirtualBox の Kubuntu 26.04)

1. 開発機で `.deb` を作る: `cd fmruby-core && rake native:deb ARCH=amd64` → `native/build-deb/family-mruby_<版>_amd64.deb`。VM へ写す。
2. VM の中で入れ直す: `sudo apt install ./family-mruby_<版>_amd64.deb` (librsvg2-2 は Kubuntu に入っている)。
3. ログアウトし、ログイン画面で「Family mruby」を選んでログインする。
4. **エディタの全画面**: メニューからエディタを開き F11。画面いっぱい (1280x800 の画面なら 640x400 を 2 倍、黒い帯なし) にメニュー・状態の行が並び、打てること。F11 で窓に戻ること。
5. **全画面のゲーム**: shooter は今までどおり 426x240 を 3 倍で真ん中 (上下に黒)。麻雀 (ヒデ雀) は 640x360 を 2 倍で真ん中 (上下 40 画素ずつ黒)。起こす・閉じる瞬間に、前の見せ方の絵 (左上に寄った小さな絵や、拡大された一部) が一瞬出ないこと。
6. **ダイアログとキーボード**: Kate の「ファイルを開く」(Ctrl+O) などのダイアログを出し、親の Kate の窓の見えている所を押す。ダイアログが前に出て、そのままキーを打つとダイアログに入ること。
7. **X11 のメニュー**: Qt のプログラムを X11 で起こす (Family mruby の Shell で `!env QT_QPA_PLATFORM=xcb kate &` など)。メニューを開き、Family mruby の壁紙を押すと閉じること。ログに `a press elsewhere passed to X11 menu N`。(xterm の Ctrl+クリックのメニューのような古い作りは、メニューの種類を付けないので閉じない見込み。8 章)
8. **止まった矢印**: 矢印を壁紙の上に置いて手を離し、Linux の区分から Konsole などを起こしてその窓が矢印の下に来たとき、矢印を動かさずにクリックすると Konsole に入ること (矢印が Linux のものに変わっていること)。
9. **アイコン**: ランチャーの「Linux」の区分で、Dolphin・Konsole・Kate・システム設定などに Breeze のアイコンが出ること (前は hicolor に PNG のあるものだけ)。ログに `linux apps: icon themes breeze > hicolor (...)` と `N SVGs converted`。システム設定で別のアイコンのテーマを選んでログインし直すと、そのテーマのアイコンになること。
10. **Qt の見た目**: Kate・Dolphin・Konsole が Plasma (Breeze) の見た目 (字体・色・アイコン・ファイルのダイアログ) で出ること。ログに `Qt's look: Plasma's (QT_QPA_PLATFORMTHEME=kde; ...)`。
11. **終わりの待ち**: (開発機の入れ子の確認で十分) 止まった窓がある状態で Exit を選んだ後も、`tools/fmrb_native.rb shot` などの制御の口が答えること。

## 7. 撤回した見立て・踏んだ罠

- **一覧が SVG の変換を待っていた**: 最初の作りでは、アイコンを全部作ってから一覧を出していた。SVG の変換 (最初の 1 回は librsvg の準備で 200 ms ほど) の間に foot の窓が出ると、窓の名前を決める一覧がまだ無く、代役の題が「Foot」でなく「foot」になって wl_check が止まった。一覧を先に出し、アイコンを後から足す形にした (3 章)。
- **同じ Icon を項目ごとに探していた**: 300 個の項目が同じ Icon を持つ試験で、Breeze の 383 の場所を項目ごとに探してアイコンの時間が 2 ms から 58 ms になった。一覧を読む間は同じ値を 1 回だけ探すようにした。
- **hires_test の期待**: 最初は hires_test を shooter と同じ 426x240 の期待で試験に書いていたが、`.app.toml` で `fullscreen_hires = true` と書いたアプリなので 640x360 になるのが今回の決まりどおり。期待を直した。
- **麻雀の卓の大きさ**: 麻雀は画面の大きさで卓の並べ方を選ぶので任意の大きさで動くようにも見えるが、640x360 の卓は部品の座標を決め打ちで持っていて、640x400 や 960x540 では左上に寄る (1 章)。

## 8. 残件

- VirtualBox の Kubuntu 26.04 での確認 (6 章)。実際の Qt・GTK のメニューで X11 のメニューが閉じるか (試験は自前の X11 のクライアントだけ)。GLES の描画。
- 代役 (タスクバー) のアイコン (3 章の最後)。
- 任意の大きさで並ぶアプリが `.app.toml` で「画面の大きさの全画面」を頼む書き方 (今は組み込みのエディタだけ)。仕様を増やすのでユーザの判断。
- ダイアログのキーボードはモーダルかどうかを見ずに移す (親に打ちたいときは Ctrl+Tab)。
- 止まった矢印の先が窓の内容の左上の角の 1 画素にあるときは穴にならない (2 章)。
- ポインタを握るが種類を付けていない古い X11 のメニュー (Xaw の xterm のメニューなど) は、Family mruby を押しても閉じない (今までどおり)。
- Firefox などの二重の帯、出力の大きさの追従 (起動時に 1 回)、縦長の出力は 426x240、既に jp を選んだ人は auto にならない: ユーザの決定どおり今のまま。
- `regress:screens` の web は今回も流していない。
