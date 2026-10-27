# L9d 報告: 画面を 2 倍で埋める、Linux の窓の角を透かす、Linux の印、配列を Linux に合わせる

> 状態: 完了 (受け入れ 8 点。高解像度の全画面 640x360 は表示のタスクの都合で出さず、426x240 で出る (2 章、ユーザの判断待ち)。VirtualBox の Kubuntu での確認は残件) | 更新: 2026-10-28 | 指示書は instruction_l9d.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

コミット: 51982c6d (コードと試験と usage.md の 11 章)。この報告は別のコミット。基準の絵は更新していない (端末・sim・ネイティブ版の画面の基準は全部そのまま通った)。

## 1. 画面の大きさの決め方

### 機種の定義

- `[screen] form` に `"output"` を足した。画面は「出力 ÷ `display.scale`」。`linux_pc_wl` を `form = "output"`、`scale = 2` にした。1280x800 の出力なら 640x400、1920x1080 なら 960x540、1280x720 なら 640x360。割り切れない余り (奇数の画素) は黒。
- `form = "output"` は `display.backend = "wlroots"` と `desktop.variant = "mouse"` の機種でだけ許す (ほかは理由つきで断る)。`scale` が `auto` のときは 2。ほかの機種 (sdl・keys・cardputer・`linux_pc`) と端末・sim・ブラウザ版は今のまま。
- 大きさは出力が開いた直後に 1 回だけ決める。今までは機種の定義を読んだ時点で kernel に画面の大きさを渡していたが、`form = "output"` では出力を開いた後に、出力の口 (`native_output_t` に足した `size`) から大きさを聞いて渡す。ログに `native: screen 640x400 (output 1280x800 over 2)` の 1 行。
- 決まりの例外:
  - **縦長の出力** (高さ > 幅、`ROTATE=90` など) は今までの 426x240 にする (最大の整数倍で置き、余りは黒)。デスクトップが縦長の並べ方を持っていないため、崩れない形を選んだ。L9c の `wl_dialog_check` はこの形 (720x1280、426x240 を 1 倍) で今までどおり通る。
  - その倍率で 426x240 に届かない出力 (800x480 なら 400x240) も 426x240。
  - 1920x1080 を超える分は 1920x1080 で止める (プログラムの絵の写しの大きさ。4K を倍率 1 にしたときだけ当たる)。
- `display.mode` (`"WxH"`) を wlroots でも効くようにした。出力がその大きさを持っていなければ自前の大きさにする (headless と入れ子の出力はどの大きさでも取れる)。試験はこれで 1280x800・1920x1080 の出力を作る。

### 出力の大きさが後で変わったとき

- 画面の大きさは変えない (表示のタスク・デスクトップ・開いている窓は起動時の大きさで作られている)。入れ子の窓を広げても狭めても、今の画面をその出力に収まる最大の整数倍で真ん中に置く (余りは黒、出力が画面より小さければ 1 倍で切れる)。ログ・画面とも L8 からの置き方のまま。
- 入れ子の窓の既定は 1280x720 なので、`rake native:wl_run` は 640x360 の画面で出る。

### 直した決め打ち

画面が 426x240 でも 640x360 でもない大きさで、表示のタスク・kernel・デスクトップ・アプリを読み、動かして確かめた。

- **表示のタスク・kernel**: 大きさは INIT_DISPLAY で受けるので、640x400 や 960x540 のフレームでもそのまま動いた (変えていない)。
- **デスクトップ**: メニューバー (左の名前、右の時計・音量・電源)、デスクトップのアイコン (縦に並べ、画面の高さで折り返す)、タスクバー (メニューバーの中)、ランチャー (真ん中)、電源のメニュー (右端から)、確認のダイアログ (真ん中) は画面の大きさから並べていて、決め打ちは無かった (画面は 4 章の表)。
- **壁紙**: 画面と同じ大きさの絵が無いと 426x240 の絵を左上に置いていた。デバイスのどれよりも広い画面では、出荷している絵 (426x240・640x360・852x480・1280x720) のうち画面を覆う最小のものを真ん中に置くようにした (640x400 なら 852x480、960x540 なら 1280x720、それより広ければ一番大きいものを真ん中に置き、周りはデスクトップの色)。端末・sim・ブラウザ版は同じ大きさの絵があるか、Retro (320x240) のように今までの道 (426x240 を左上) のままで、見た目は変わらない。
- **全画面のアプリの大きさ** (下の 2 章)。
- **試験の決め打ち**: 電源ボタン・Exit・確認のダイアログの「はい」「いいえ」の座標、置いた矢印の隠し、エディタの基準との比べ方 (基準は 426x240 なので左上を切り出し、窓の四隅は後ろの壁紙が透けるので除く)、foot の大きさの変更の期待 (504x384 決め打ちを倍率から)。

## 2. 全画面のアプリ

- **426x240 のまま**: 全画面のアプリが受け取る大きさは、広い画面でも Modern の 426x240 にした (`fmrb_kernel_fullscreen_size`。起動のとき (spawner)、Spinel と mruby の kernel の `_screen_width` / `_screen_height`、そこから kernel の Ruby の `fullscreen_size` が使う)。shooter などは端末と同じ遊び場の大きさで動く。kernel の Ruby の `fullscreen_size` は、デスクトップの窓の大きさを読む道をやめて `_screen_width` / `_screen_height` だけにした (端末ではどちらも「画面 − 余白」で同じ値)。
- **見せ方 (view)**: ブリッジ (`wl_bridge.c`) が 10 ms ごとに窓の一覧を見て、一番上の動いている窓が画面より小さい全画面のアプリなら、その矩形を「見せる所」としてコンポジタに渡す (`comp_places_t` に view を足した)。コンポジタは絵のうちその矩形だけを (scene の buffer の source box で) 出力に収まる最大の整数倍で真ん中に置き、余りは黒、ポインタもその矩形を通して Family mruby の座標にする。Linux の窓はその間出さない (全画面のアプリの下に隠れるのと同じ)。1280x800 では 426x240 を 3 倍で (1,40) に、1920x1080 では 4 倍で (108,60) に。表示のタスク (端末と共通) は触っていない。
- アプリが閉じる・窓に戻る (エディタの F11) と view が消え、画面全体の 2 倍に戻る。切り替わりの瞬間 (10 ms の見回りと絵の 1 枚) は、前の見せ方の絵が 1 枚出ることがある。
- **高解像度の全画面 (640x360) は出していない**。表示のタスクは「基本の大きさのフレームに、ちょうど 640x360 の canvas が出ていたら、それは高解像度に移る途中の全画面のアプリ」として合成から外す (フレームそのものが 640x360 のブラウザ版だけ例外)。640x400 のフレームでは 640x360 の canvas が出ない。また画面の幅が 640 以上だと高解像度への切り替えそのものを断る。これを通すには表示のタスクの条件を 1 か所変える必要があり (例: 「フレームが 640x360 より小さいとき」だけ外す。端末の動きは変わらない書き方はできる)、指示書の止まる条件 (端末と共通の表示のタスクを変えない) に当たるので変えていない。代わりに広い画面では高解像度を「無い」と答え (`fmrb_app_fullscreen_hires_size` は 426x240 の画面のときだけ 640x360 を返す今のまま)、高解像度の全画面のアプリは Retro や古い sim と同じく普通の全画面 (426x240) で、出力に収まる最大の整数倍で真ん中に出る (エディタの F11 は 1280x800 で 426x240 を 3 倍)。640x360 を 2 倍で出すかどうかはユーザの判断を待つ (10 章)。

## 3. 代役の四隅を透かす

- **作り**: 代役の canvas を普通の窓と同じくキーつき (角丸) で作り、そのキーを 0x01 でなく代役だけの色にした。中身の 0x01 (穴) はキーでないので不透明のまま絵に残り、四隅の外側の 3 画素ずつ (普通の窓の corner clear と同じ所) を代役のキーで塗るので、そこは表示の合成で透け、後ろの物 (壁紙、後ろの窓、後ろの Linux の窓の穴) が見える。前後関係は今までどおり絵が決める (代役の上に Family mruby の窓・メニュー・ダイアログが来れば穴は消える)。
- **キーの色**: `native_wl_canvas_key` (wl_bridge.c) が、代役の bytecode で動いているアプリにだけ、0x02・0x03・0x05・0x06・0x07 のうちテーマの題の帯・文字・枠の色でも 0x01 でもない最初の色を返す (既定のテーマでは 0x02)。canvas を作るとき (`fmrb_app_canvas.c`、ネイティブ版の中だけ) と代役自身 (`FmrbApp.wl_key`) が同じ答えを使う。代役の canvas のキーは代役の canvas の中だけの話なので、ほかのアプリがその色を使っても穴にも透けにもならない。0x01 を「見える色に使わない」決まりもそのまま。
- **選んだ理由**: 表示の受け渡しは表示のタスクが合成し終えた RGB565 の 1 枚で、canvas ごとのキーは表示のタスクが前から持っている (作るときに色を指定できる)。キーを代役だけ変えれば、表示のタスク・端末・sim・ブラウザ版・内蔵 RAM に何も足さずにできる。ほかの候補は、中身を別の色で塗る形 (その色を他の窓が使えば穴が開く。どの色も絵や画像に出得る)、合成で中身だけキーを外す形 (表示のタスクを変える) で、どちらも外した。
- 窓の構造体には足していない (代役は bytecode の場所で見分ける)。端末の構造体と内蔵 RAM は変わらない。
- L9c の `wl_dialog_check` の CORNER = 3 (代役の左下で中身の上に枠の線が描かれる 3 画素) はそのまま正しい。今回変えたのは窓の外側の 3 画素で、中身の内側の線は普通の窓と同じに残っている。

## 4. 印

- 「Linux」の区分の印は小さなペンギン、アイコンの無い Linux のアプリの共通の印は題の帯と枠のある窓の絵。ランチャーのほかの印と同じ形式 (`.icon`: 12x12 の 1 色の点の絵、`tool/gen_icon_bmp.rb` で 2 倍の 24x24 の BMP) で描き、`flash/usr/share/icon/linux.icon`・`linux_app.icon` と BMP を置いた。色はペンギン 0x4A (青みの灰)、窓 0x1B (水色)。Tux の写しではなく、頭・目・胴の白い所・ひれ・足を 12x12 で描いたもの。
- `flash/` に置いた理由: 配ってよい絵で、ほかの印と同じ場所・同じ作り方・同じ読み方 (ランチャーの `icon_file`) で済む。L9b の「起動時に C が描いて書く」形 (`linux.bmp` をキャッシュに書いていた) と、一覧の頭の行の印の欄はやめた。BMP の 2 つは開発 (リポジトリの flash/ をそのまま読む)、コンテナの試験 (リポジトリを差し込む)、`.deb` (`native:root` が git 追跡分の flash/ を入れる。`/usr/share/family-mruby/root/usr/share/icon/linux*.bmp` があることを確かめた) のどれにも入る。
- 一覧の Linux のアプリでアイコンがあるもの (hicolor の PNG から作るもの) は今までどおりそのアイコン。

## 5. キーボードの配列

- **読み方** (`sys_linux_keyboard_layout`): Plasma の利用者ごとの設定 (`$XDG_CONFIG_HOME/kxkbrc`、無ければ `~/.config/kxkbrc` の `[Layout]` で `Use=true` のときの `LayoutList` の最初) を先に、無ければシステムの設定 (`/etc/default/keyboard` の `XKBLAYOUT` の最初。`localectl` と `dpkg-reconfigure keyboard-configuration` が書くもの)。jp なら jp、それ以外 (どこにも書いていないときも) は us。
- **kxkbrc を読む理由**: Kubuntu で利用者が「システム設定 → キーボード」で配列を足すと、Plasma は kxkbrc に書き、`/etc/default/keyboard` (ログイン画面と仮想端末の配列) は変えない。利用者から見た「Kubuntu の設定」は Plasma の設定なので、そちらを先にした。`Use=false` (Plasma で配列を設定していない) なら読まない。
- **「選んでいない」の表し方**: `keyboard_layout = "auto"`。パッケージの system_conf (`system_conf_native_release.toml`) の既定を `auto` にした。開発の `system_conf_native.toml` は `jp` のまま (開発に使う WSL の `/etc/default/keyboard` は us で、手元の物理キーボードは JIS のため)。`rake native:root` の「開発とリリースで違ってよい項目」に `keyboard_layout` を足した。
- **Config**: ネイティブ版だけ選択肢が `auto` / `jp` / `us` (ネイティブ版だけが持つ `FmrbApp.linux_keyboard` で見分ける)。`auto` の行は今 Linux が何かを添えて `auto (jp)` と出す。端末・sim・ブラウザ版の選択肢 (`jp` / `us`) と既定は変えていない。Config で `jp` か `us` を選べばそれが勝つ (今までどおり起動し直したときに効く)。
- **効く所**: kernel が起動時に `auto` を解き、Family mruby のキーの表 (`fmrb_keymap_set_layout`) に入れる。Linux の窓の keymap は L9a からそれに付いて来る (`follow_layout`) ので両方に効く。ログに 1 行: `Keyboard layout: auto -> jp (Linux's: XKBLAYOUT=jp in /etc/default/keyboard)`。
- 前に入れた人の `/etc/system_conf.toml` (利用者のもの) には `jp` が書かれているので、そのままでは `auto` にならない (利用者のファイルの値は更新で変えない決まり)。Config で `auto` を選べば切り替わる (7 章の手順)。

## 6. 受け入れ条件

確認したコマンド: `rake regress:wl` (標準と全部 mruby のそれぞれに `rake native:wl`、`wl_check.rb`・`wl_apps_check.rb`・`wl_dialog_check.rb`・新しい `wl_keys_check.rb`、headless、pixman、出力 1280x800)、`ruby test/native/wl/wl_check.rb --output 1920x1080`、`rake native:test`、`rake native:core`、`rake regress`、`rake regress:screens ONLY=retro,modern,native`、`rake regress:screens ONLY=retro,modern FULL=1`、`rake regress:size`、`rake native:deb ARCH=amd64|arm64` と `deb_check.rb --arch amd64|arm64`。

| 条件 | 結果 |
|---|---|
| 1. 画面を出力 ÷ 2、帯なし | 1280x800: `screen 640x400 (output 1280x800 over 2)`、`comp info` は frame 640x400 を 2 倍で (0,0)。grim の 1280x800 と絵の 2 倍の差 **0 画素** (黒い帯なし)。1920x1080: 960x540 を 2 倍で (0,0)、`wl_check --output 1920x1080` が 23/23。メニューバー・アイコン・時計・電源ボタン・ランチャー・ダイアログが広い画面の端と真ん中に並ぶ (画面: `desktop_640x400_shot.png`、`desktop_1280x800_grim.png`、`desktop_960x540_shot.png`) |
| 2. エディタ・Shell・File Manager・ランチャー、全画面 | 640x400 でエディタ (240x200 を (5,15))・Shell・ランチャー (真ん中)・File Manager (真ん中、`file_manager_640x400.png`) が開く (エディタは wl_check の中、ほかは手で開いて画面を見た)。エディタの 1 打鍵は基準 `editor_type.png` とエディタの窓の範囲で **0 画素差** (標準・全部 mruby)。全画面: hires_test (高解像度の全画面のアプリ、ここでは 426x240) と shooter が 1280x800 で 426x240 を **3 倍で (1,40)**、1920x1080 で **4 倍で (108,60)**、周りの黒以外 **0 画素**、hires_test は中が絵と全画素一致 (920,160 / 920,160)、shooter は動いているので 98% 以上の一致 (903,411 / 920,160)。その間 foot は隠れ、閉じると戻る。エディタの F11 も 426x240 を 3 倍 (`editor_f11_grim.png`) |
| 3. L9a・L9b・L9c の確認 | `rake regress:wl` 通る (9 分 15 秒)。wl_check 23/23 (前の 19 に、画面・全画面 2 つ・代役の四隅を足した)、wl_apps_check 8/8、wl_dialog_check 8/8、wl_keys_check 3/3 を標準と全部 mruby で。foot は 696x494 で中身 348x247 に入り、1 画素ずつの動きが foot に 168.5, 169.5, 170.5 で届く (本来の画素の細かさ)。枠で 168x128 にすると foot は 336x247、代役は 168x124 に合わせる |
| 4. 四隅と前後 | 2 つ目の foot の代役の四隅の外側 12 画素が全部、出る前の絵の同じ所 (後ろの foot の代役と壁紙) と同じ、題の帯は描かれている。合成後は穴以外が絵と 0 画素差。前後: エディタを重ねて穴 9,095、foot を押して 20,748、メニューを重ねて 16,536、終了の確認を重ねて (確認は真ん中なので foot をその下に動かして) 穴が減り、どれも穴以外 0 画素差 (`corners_zoom.png`: 左上は後ろの foot の穴、ほかは壁紙) |
| 5. 印 | ランチャーの「Linux」にペンギン (`windows_640x400.png`)、一覧でアイコンの無い Sleeper・UXTerm・X Typer・XTerm に窓の絵 (`linux_list_marks.png`) |
| 6. 回帰 | `rake regress`: host 19/19、描画の記録 205/205。`rake regress:screens ONLY=retro,modern,native`: 23/23 (retro 8/8、modern 10/10、native 2/2、native-keys 3/3)。全部 mruby の sim (`FULL=1`): 22/22。基準は変わっていない。`rake regress:size`: NARYAv3・TAB5 とも **DIRAM・IRAM・other の内蔵 RAM +0**、Spinel の静的な内蔵 RAM も同じ。flash は NARYAv3 4,556,880 / TAB5 6,738,688 (L9b の時から +240 / +176: fullscreen の大きさを 1 つの関数にした分とデスクトップの Ruby)、基準 6f7b5848 との差は +44,256 / +49,760。`rake native:test` (508 + 64 + 52 + 17 + session 12 + 50 + comp 109)。`rake native:core` (wlroots=0 png=0) 通る。`deb_check --arch amd64` **17/17**、`--arch arm64` **18/18** |
| 7. 標準と全部 mruby、pixman | 1・2・4 を含む wl_check 23/23 が両方で、pixman で通る |
| 8. 配列 | 偽の `/etc/default/keyboard` (実行の場所のファイルを入れ物の `/etc/default/keyboard` に読み取り専用でかぶせる) で: `XKBLAYOUT="jp"`・`auto` → foot に `"@[^\n"`、Family mruby の Shell (`!echo` で書かせる) に `"@[^\n"`、窓の keymap jp、ログ `auto -> jp (Linux's: XKBLAYOUT=jp in /etc/default/keyboard)`。`us`・`auto` → 両方 `"[]=\n"`、keymap us。`jp`・Config で `us` → 両方 `"[]=\n"` (`Keyboard layout set to: us`)。標準と全部 mruby の両方。kxkbrc の読み方は host の試験 (Use=true の LayoutList が先、Use=false は読まない、`us,jp` は us、`de` は us、どこにも無ければ us)。端末・sim の Config の選択肢は変えていない (sim の画面の基準が通る) |

- ブラウザ版の画面 (`regress:screens` の web) は流していない。`tools/fmrb_web.rb` が開いたブラウザ (wasm/build/drive_browser.pid) が開いたままで、道具が止めたため (L9a からと同じ。閉じていない)。ブラウザ版に効く変更はデスクトップの Ruby (壁紙は同じ大きさの絵があるので同じ道、Config は `jp` / `us` のまま、ランチャーの Linux の区分は出ない) と、全画面の大きさを 1 つの関数にしたこと (値は同じ)。
- 時間: wl_check の時間の表 (何もしない時 CPU 3.5%、stackchan 8.0%、キーから foot の commit まで平均 1.2-1.4 ms、最長 2 ms) は L9a と見分けがつかない。

## 7. VM で人が確かめる手順 (VirtualBox の Kubuntu 26.04)

1. 開発機で `.deb` を作る: `cd fmruby-core && rake native:deb ARCH=amd64` → `native/build-deb/family-mruby_<版>_amd64.deb`。VM へ写す。
2. VM の中で入れ直す: `sudo apt install ./family-mruby_<版>_amd64.deb`。
3. ログアウトし、ログイン画面で「Family mruby」を選んでログインする。
4. **画面**: 1280x800 の画面いっぱいに Family mruby が出て、上下の黒い帯が無いこと。ログ (`~/.local/share/sddm/wayland-session.log` か `journalctl -b --user`) に `native: screen 640x400 (output 1280x800 over 2)`。メニューバーの右端に時計と電源ボタン、左にアイコンの列、壁紙が画面を覆うこと。
5. **全画面のアプリ**: ランチャーから shooter (シューター) を起こすと、426x240 が 3 倍で真ん中に出て (上下に 40 画素ずつ黒)、終えると広い画面に戻ること。エディタの F11 も同じ (426x240 の 3 倍)。
6. **四隅**: Konsole などを Linux の区分から起こし、窓の四隅の角丸の外に壁紙 (や後ろの窓) が見えること。Family mruby のエディタやメニューを重ねると今までどおり Linux の窓が隠れること。
7. **印**: ランチャーの「Linux」がペンギン、一覧でアイコンの無いアプリが窓の絵。
8. **配列**: 前に入れたときの設定は `jp` のままなので、メニューの Config で keyboard_layout を `auto (jp)` (または `auto (us)`。括弧の中が Kubuntu の配列) にして保存し、ログアウトして入り直す。ログに `Keyboard layout: auto -> jp (Linux's: ...)` が出て、どこから読んだか (`LayoutList=... in .../kxkbrc` か `XKBLAYOUT=... in /etc/default/keyboard`) が分かる。Family mruby のエディタと Konsole の両方で `@` `[` `:` などの記号がキーボードの刻印どおりに入ること。Config で `us` を選ぶと Kubuntu が jp でも us になること。
9. 入れ子で開発機から触るとき (`rake native:wl_run`) は WSLg の窓の大きさ (既定 1280x720) の半分、640x360 の画面になる。

## 8. 撤回した見立て・踏んだ罠

- **「高解像度の全画面は 640x360 を 2 倍で真ん中に」はそのままではできなかった**: 表示のタスクが 640x360 の canvas を基本のフレームでは出さず、幅 640 以上の画面では高解像度への切り替えも断る (2 章)。
- **終わりの待ちで落ちた (SIGSEGV)**: Exit の後の「窓が閉じるのを待つ」間、コンポジタは main の繰り返しが解放した絵の写しを指したまま、窓の下に敷く矩形の色をそこから読んでいた。426x240 の写しは heap に戻るだけで読めてしまっていたが、640x400 の写しは大きいので OS に返され、読むと落ちた (L9a からの潜在的な不具合。gdb を別の入れ物から当てて見つけた)。待つ間の写しを、フレームの大きさで確保し、待ち始めにすぐ写すようにした。待つ間の写しも 640x360 決め打ちで、広い画面のフレームが入らなかった。
- **窓の下に敷く矩形の色が穴の色になった**: 窓を動かした直後、敷く矩形の色を前のフレーム (そこがまだ穴だったもの) から読んでいた。新しいフレームが来るたびに読み直すようにした (L9b からの潜在的な不具合。広い画面で foot の下の余りの 1 行に出た)。
- **代役の角**: 最初は canvas をキーで塗ってから角丸の形を描いたので、外側の透ける画素が 5 個ずつになり、普通の窓 (3 個ずつ) と違った。枠の色で塗ってから、普通の窓の corner clear と同じ 3 画素をキーで塗るようにした。
- 確認の道具: 確認のダイアログは画面の真ん中に出るので、foot をその下に動かしてから数える形にした。エディタの基準 (426x240) と比べるときは窓の四隅が後ろの壁紙を見せるので、壁紙の違う広い画面ではその 4 か所を除く。

## 9. 撤回・変えたもの (前の段の作りから)

- L9b の「区分の印は起動時に C が描いて `/var/cache/linux/linux.bmp` に書く」をやめ、一覧の頭の行から印の欄を消した (`ready\n` / `loading\n`)。
- kernel の Ruby の `fullscreen_size` はデスクトップの窓の大きさを読まなくなった。

## 10. 残件

- **ユーザの判断**: 高解像度の全画面のアプリ (エディタの F11、mahjong など) を広い画面で 640x360 の 2 倍で出すか。出すなら表示のタスク (端末と共通) の条件を 1 か所変える (640x360 の canvas を外すのを「フレームが 640x360 より小さいとき」だけにする、幅 640 以上の画面での高解像度の切り替えは要らない) のと、`fmrb_app_fullscreen_hires_size` をネイティブ版の広い画面でも 640x360 と答え、SET_SCREEN_MODE を送らずに済ませる形になる。端末の画面はどれも 640x360 より小さいので端末の動きは変わらない見込み。今は 426x240 で出る。
- VirtualBox の Kubuntu 26.04 での確認 (7 章)。GLES の描画。
- 全画面のアプリに切り替わる瞬間、前の見せ方の絵が 1 枚出ることがある (view は 10 ms ごとの見回りで決まり、絵と同じ時刻ではない)。
- 出力の大きさは起動時に 1 回だけ決める (入れ子の窓を広げても画面は広がらない)。
- 縦長の出力は 426x240 のまま (縦長のデスクトップの並べ方は無い)。
- 既に入っている人の `keyboard_layout = "jp"` は `auto` にならない (Config で選び直す)。`auto` は起動時に 1 回読む (Kubuntu 側で配列を変えたら入り直す)。
- `regress:screens` の web は今回も流していない。
