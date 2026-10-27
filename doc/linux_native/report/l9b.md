# L9b 報告: Linux のアプリの起動の口、Xwayland、ログイン画面の入口

> 状態: 完了 (受け入れ 8 点。GLES・VirtualBox の Kubuntu・人の手での操作は残件) | 更新: 2026-10-27 | 指示書は instruction_l9b.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

コミット: 123db70f (コードと試験)。この報告は別のコミット。基準の絵は更新していない。

## 1. Linux のアプリを起こす口

### 形と置き場所

| 場所 | 中身 |
|---|---|
| `native/sys/linux_apps.{h,c}` | 新しい口。`.desktop` を読む、アイコンを作る、起こす、覚える、窓の名前を決める、終わりに止める。POSIX と libpng だけで書き、FreeRTOS と fmrb の allocator を使わない (host の試験がそのまま動かすため。L10 の `linux_pty` と同じ扱い) |
| `main/kernel/fmrb_platform_sys.{h,c}` | L6 の関数の表に `apps_list`・`apps_launch` の 2 項目と、呼ぶ側の短い関数を足した。既定の表は NULL のままで、端末・sim・ブラウザ版は「無い」(-1 / `FMRB_ERR_NOT_SUPPORTED`) を答える。表は `static const` なので端末の内蔵 RAM は増えない |
| `native/sys/platform_sys_linux.c` | Linux の表に 2 項目をつないだ (`NATIVE_HAVE_WLROOTS` のビルドだけ) |
| `native/wlr/output_wlr.c` | 開くときに `linux_apps_start`、主スレッドの繰り返しで `linux_apps_pump`、終わりに `linux_apps_end_all` |
| `lib/add/picoruby-fmrb-app/ports/esp32/app.c` | `FmrbApp.linux_apps(mode, lang)` (一覧の文字列、無い所では "")、`FmrbApp.linux_launch(id)` (0 / -1) |
| `main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb` | Spinel の FmrbApp には同じ名前で「無い」を答えるだけのものを置いた (デスクトップは標準でも全部 mruby でも mruby で動くので、使われるのは mruby の方。Spinel のデスクトップを作る形でもコンパイルが通るように) |

- 使えるのはコンポジタの機種 (`linux_pc_wl`) だけ。コンポジタが開いたときに `linux_apps_start` が呼ばれ、それまでと、ほかのどの機種でも `linux_apps_available()` は false。
- **起こすのはコンポジタの主スレッド**。デスクトップ (FreeRTOS のタスク) の `linux_launch` は、起こす頼みを 8 個までの列に積んで直ちに戻る。主スレッドが 5 ms ごとの `pump` で列を取り、fork して exec する。主スレッドはプログラムが終わるまで生きているので、子の `PR_SET_PDEATHSIG` (SIGTERM) は「プログラムが落ちたら一緒に終わる」の意味になる (起こしたタスクのスレッドに結ぶと、デスクトップが入れ替わったときに巻き込まれる)。
- 子: シグナルの遮断と扱いを既定に戻す → `setsid()` (自分のセッション) → 標準入力を `/dev/null` → 3 以上の fd を閉じる (Wayland の口などを渡さない) → 作業の場所 (`Path=`、無ければ `$HOME`) → `execve`。シェルは通さない。環境は fork の前に親で作る: プログラムの環境 (`WAYLAND_DISPLAY` はコンポジタ自身の口、`DISPLAY` は Xwayland の口) に `XDG_CURRENT_DESKTOP=family-mruby`。
- **待たない**: 終わりは `pump` ごとの `waitpid(WNOHANG)` で知り、ゾンビを残さない。起こした記録は 64 個まで (主スレッドだけが触るので錠なし)。記録は、プログラムが終わってもセッションに誰かが残る間は残し、空いたものから使い直す。

### `.desktop` の読み方と外す決まり

- 読む場所: `$XDG_DATA_HOME` (無ければ `~/.local/share`)、`$XDG_DATA_DIRS` (無ければ `/usr/local/share:/usr/share`) の順に `applications/` を 3 段まで。デスクトップファイル id (下の段は `sub-name.desktop`) が同じなら先に見つけたものが勝つ (利用者の物が `/usr` の物を隠す。隠す側が `Hidden=true` なら両方出ない)。
- `[Desktop Entry]` の組だけを読み、文字列の `\s \n \t \r \\` を戻す。
- 外すもの: `Type` が `Application` でない、`NoDisplay=true`、`Hidden=true`、`OnlyShowIn` に自分の名前が無い、`NotShowIn` に自分の名前がある、`TryExec` の物が無い、`Exec` が無いか空、`Exec` の最初の物が `PATH` に無い (最後のものは決まりには無いが、起こせない物を並べないため)。
- 自分の名前は `family-mruby` (ログイン画面の入口の `DesktopNames` と同じ。SDDM はこれを `XDG_CURRENT_DESKTOP` にする)。Kubuntu では `OnlyShowIn=KDE;` の物 (システム設定の一部など) は出ない。
- 名前: `Name[<lang>]` (lang は Family mruby の `language`。`ja` なら `Name[ja]`、無ければ `Name[ja_JP]` など)、無ければ `Name`。language が変われば読み直す。
- `Exec` の分け方 (決まりどおり): 空白で区切り、`"..."` の中は `\"` `` \` `` `\$` `\\` を戻す。項目の符号: `%f %F %u %U` (ファイルと URL は渡さない) と古い `%d %D %n %N %v %m` は消す、`%i` は `--icon <Icon>`、`%c` は名前、`%k` はファイルの場所、`%%` は `%`。引数の途中の符号は空になる。
- **`Terminal=true` の扱い: 端末の中で起こす**。`foot` があれば `foot -- <argv>`、無ければ `x-terminal-emulator -e <argv>` (Debian・Ubuntu の決まった口。Kubuntu では konsole)、どちらも無ければ一覧から外す。理由: Shell の `!` は行単位の端末で、全画面を使う端末のアプリ (htop など) には向かない。foot は試験の像にも入っていて L8 から確かめている。
- 並びは名前の順 (大文字小文字を区別しない)。一覧は 1024 個まで、1 ファイル 64 KB まで。

### 読む時と重さ

- 読むのは別のスレッド (1 本、終われば消える)。コンポジタが開いた時に 1 回、ランチャーの Reload (F5) と language の変化でもう 1 回。デスクトップのタスクは錠の中で文字列を写すだけで、読み終わるのを待たない (読んでいる間は「loading」の 1 行を返し、ランチャーが 50 ms ごとに聞き直す)。
- 重さ (コンテナの中、ログの値): 像の `/usr/share/applications` と試験の物で 11 ファイル 8 項目、読むのに 0-1 ms。試験の 300 個を足した 311 ファイル 308 項目で 2-5 ms。手元で 100 ファイルを読むのに 23 ms。

## 2. ランチャーの「Linux」の区分

### 作り

- 組み込みのアプリ (シェル・エディタ・アプリの店) の後ろに「Linux」の項目を 1 つ足し、**押すと (ダブルクリックか Enter) ランチャーの格子が Linux のアプリの一覧に入れ替わる**。先頭は「..」(押すと Family mruby のアプリに戻る)。題の帯は「ランチャー / Linux」。ランチャーを閉じると Family mruby のアプリに戻る。Family mruby のアプリと混ぜることはない。
- この形にした理由: ランチャーの格子・選択・キー操作・スクロール・アイコンの sprite はそのまま使える (項目の `app` に `linux:` の印を付けて見分けるだけ)。別の窓を開く形は「ランチャーの中に区分を作る」という決定から外れ、ファイルマネージャのような木にすると今のランチャーの作りを大きく変える。
- Linux の一覧では、右クリックと Tab のメニューは Reload だけ (デスクトップに置く項目は出さない。Linux のアプリはデスクトップのアイコンの対象にしていない)。Reload は `.desktop` を読み直す。
- 区分が出るのは `FmrbApp.linux_apps` が空でない時だけ = コンポジタの機種だけ。ほかの機種 (端末・sim・ブラウザ版・`sdl` と `sdl_keys` のネイティブ版) では空を返し、項目は足されない。keys のデスクトップはランチャーそのものを持たない。区分を出した時はログに `Launcher: the Linux group is offered` が出る (sim・ネイティブ版 sdl の回帰のログ、deb_check の sdl の起動のログに、この行が無いことを確かめた)。
- **数百のアプリでも開く速さが落ちない**: ランチャーを開く時には Linux の一覧を読まない (区分の項目は起動時に見出しの 1 行だけ聞く)。区分に入った時に一覧の文字列を 1 回もらって項目にし、**アイコンの sprite は見えている行の分だけ**作る (スクロールで見えた所から足す)。Family mruby のアプリは今までどおり起動時に全部の sprite を作る。

### アイコン

- 探す所: `Icon=` が絶対の場所の PNG ならそれ。名前なら各データの場所の `icons/hicolor/<大きさ>/apps/<名前>.png` を 24x24、32x32、22x22、48x48、64x64、16x16、96x96、128x128、256x256、512x512 の順に、無ければ `pixmaps/<名前>.png`。SVG・XPM・hicolor 以外のテーマ (Breeze など) は見ない。
- 変換: libpng で RGBA に読み、縦横比を保って 24x24 に収め (真ん中に置く)、各画素は下にある元の画素の不透明度で重みを付けた平均。不透明度が半分以上なら不透明。色は RGB332。黒 (0x00) はスプライトの透明の色なので 0x25 に、0x01 はコンポジタの穴の色なので 0x02 に寄せる。8 ビットの BMP (画素の値が RGB332、パレットは見るためだけ。`tool/gen_icon_bmp.rb` と同じ形) に書く。
- 置き場所: Family mruby の `/var/cache/linux/` (実体はネイティブ版のキャッシュの場所、`$XDG_CACHE_HOME/family-mruby/var/cache/linux`)。名前は元の場所・大きさ・時刻から作った hash なので、同じ元の物は 1 回だけ変換する。1 つ 1,654 B。
- 見つからない・変えられない物はアイコン無しで、ランチャーは「L」の字を描く (今の「アイコンファイルが無いアプリ」と同じ道)。区分そのものの印 (端末の小さな窓の絵) は起動時に C が同じ場所に描いて書く (`linux.bmp`)。`flash/` には何も足していない。
- 重さ (手元、`-O2`): 256x256 の PNG 50 個と 48x48 の PNG 50 個の変換が合わせて 63 ms (平均 0.6 ms)。2 回目からは変換済みを使うので 1 ms。

## 3. 起こしたプロセスと窓を結ぶ (代役の名前)

- 窓が現れた時、コンポジタが代役の名前を決めて通知に入れる (L9a は app id をそのまま使っていた)。順番: (1) 窓のプロセス (xdg はクライアントの資格情報の pid、X11 は `_NET_WM_PID`) のセッションが、起こした記録のどれかなら、その項目の名前。(2) 起こした記録の `StartupWMClass` (無ければ実行ファイルの名前) かデスクトップファイル id (`.desktop` を除く) が、窓の app id (X11 は WM_CLASS の class、次に instance) と同じ (大文字小文字は区別しない)。(3) 一覧の項目で同じ比べ方。(4) 無ければ app id。
- 名前は 31 バイトまで、UTF-8 の字の途中では切らない (アプリの名前の上限 32)。タスクバーと題の帯の最初の文字はこの名前。題が来れば題の帯は題に変わる (L9a のまま)。
- 例: 一覧の foot は app id `foot` に対して名前「Foot」。Shell の `!foot` や autostart の foot も (2)(3) で「Foot」になる。debian-xterm.desktop がある所で一覧の別の項目から xterm を起こすと、(1) でその項目の名前 (試験の「X Typer」) になる。
- アイコンは代役には付けていない (タスクバーは名前の最初の文字の四角で、窓の題の帯にもアイコンの場所が無いため)。

## 4. Xwayland

### 遅延起動

- `wlr_xwayland_create(display, compositor, lazy = true)`: 口 (`DISPLAY=:0` など) だけを作り、**最初の X11 のプログラムがつながった時に wlroots が Xwayland を起こす**。プログラムの環境の `DISPLAY` をこの口にするので、一覧・Shell の `!`・autostart の子はどれもこの Xwayland に出る (入れ子で動かす時の親の X サーバには出ない)。
- 起きた時 (`ready`) に seat と矢印の絵 (xcursor の default) を渡し、ログに `Xwayland started on DISPLAY=:0 (an X11 program connected)`。`comp info` の `xwayland=lazy|up|off`。
- Xwayland が無い PC: 実行ファイル (`WLR_XWAYLAND`、無ければ `/usr/bin/Xwayland`) が無ければ作らず、`DISPLAY` を消し、`X11 programs cannot run here: Xwayland is not installed (...; sudo apt install xwayland). Wayland programs run as before` の 1 行を出す。

### X11 の窓

- X11 の窓も L9a の代役で扱う: `comp_route` に xdg と同じ番号で入れ、置き場所は `wlr_xwayland_surface_configure` で伝える。**X の座標も出力の座標そのまま**に置く (X11 のメニューは親の窓の X の座標から自分の場所を決めるので、ずれない)。
- 前に出す・フォーカス: `wlr_xwayland_surface_activate` と `restack` (一番上)、キーボードは xdg と同じく seat から。閉じるボタン・Exit の「閉じるよう頼む」は `wlr_xwayland_surface_close` (WM_DELETE_WINDOW、受けないプログラムは X の kill)。`_NET_WM_MOVERESIZE` の移動・大きさの頼みは xdg の move・resize と同じ道 (コードだけで、自動では見ていない)。最大化・全画面の頼みは無視する。
- **override-redirect の窓 (メニュー・ツールチップ)**: 代役を付けず、絵の上の層 (L9a のポップアップの層) の一番上に、プログラムの置いた場所に出す。ポインタは一番上のメニューから先に当たりを見る。

### 飾りの扱い: 全部に代役の枠を付ける (二重の題の帯を許す)

- X11 の窓は `_MOTIF_WM_HINTS` にかかわらず全部に代役の枠を付ける。自分で題の帯を描く窓 (飾りを「持たない」と言う窓) は二重の題の帯になる。起動のログに `decorations the window manager's` / `its own (two title bars)` を出す。
- 理由: (1) 枠を省くと、その窓を Family mruby のやり方で動かす・閉じる・大きさを変える所が無くなる (自前の題の帯からの `_NET_WM_MOVERESIZE` に頼ることになり、出さないプログラムは動かせない)。L9a の「Linux の窓は Family mruby の窓」の約束がそのまま保てるのはこの形。(2) Wayland の口がある時、GTK・Qt のアプリは Wayland で出てくるので (xdg-decoration でサーバ側の飾り)、二重になるのは X11 しか話さない自前の飾りのアプリ (古い Electron・Chrome の X11 の形など) に限られる。(3) 利用者から見た動きは「どの Linux の窓にも Family mruby の題の帯がある」で、L9a と変わらない。
- 二重の帯を消す形 (枠だけ省く) は、代役に「題の帯の無い窓」を足す変更になるので、要るなら別に決める (残件)。

## 5. アプリが自分で大きさを決めた時 (L9a の残件)

- コンポジタは「窓が自分の大きさに落ち着いた」ことを Family mruby に知らせる (新しい通知 `COMP_NOTICE_FIT`、大きさは出力の画素)。
  - xdg: こちらが頼んだ大きさの configure に答えた commit (`configure_serial` が最後に頼んだもの) の大きさが頼んだものと違う時。大きさごとに 1 回。
  - X11: 窓に付いた後でプログラムが自分から大きさを頼んだ時 (ConfigureRequest)。その大きさをそのまま許し、代役が合わせ終わるまで (最長 0.5 秒) その大きさを保つ (その間に古い大きさで頼み直して揺れないため)。
- ブリッジ (`wl_bridge.c`) は代役の窓を「中身 = 窓の大きさ ÷ 倍率 (切り上げ)」になるよう `fmrb_app_update_window_size` で変える。切り上げなので、頼み直すと窓は今の大きさのまま (foot は格子の大きさに戻る、X11 は倍率の倍数に少し広がる) で、行き来しない。
- それでも残る 1 フレーム画素より小さい余り (倍率 3 なら 0-2 出力画素) は、**各 Linux の窓の下に敷いた矩形 (代役の枠の色)** が見える。色は絵の中の代役の左の枠 (中身の高さの半分) の画素。黒には見えない。
- foot の例: 枠を引いて中身 168x128 (504x384 を頼む) → foot は 504x377 → 代役は中身 168x126 (504x378) に合わせる → 下の 1 行 (504 出力画素) が枠の色 (107,0,0)。

## 6. ログイン画面の入口

- `family-mruby-session` は cage を使わず `family-mruby --machine linux_pc_wl --set display.rotate=$ROTATE $FMRB_ARGS` を直接起こす。DRM/KMS と座席は wlroots が libseat (logind か seatd) で取る (`wlr_backend_autocreate` に session を渡すようにした。L8 では NULL だった)。座席を持つ時は Ctrl+Alt+F1-F12 で仮想端末を切り替える。
- `ROTATE` はコンポジタ自身が出力を回す (`display.rotate` を wlroots の出力の transform にする。90 は `WL_OUTPUT_TRANSFORM_90`、L7 の cage の形で `wlr-randr --transform 90` と同じ向き)。ポインタは wlroots が回った出力の座標で動かすので、絵と一緒に回る。
- 設定ファイル (`/etc/default/family-mruby-session`) で設定した変数は全部プログラムの環境に渡る (`set -a`)。`WLR_RENDERER=pixman`、`WLR_NO_HARDWARE_CURSORS=1` などを書ける。`FMRB_ARGS` もそのまま。
- `WLR_BACKENDS` を指定していない時は、`WAYLAND_DISPLAY` と `DISPLAY` を消してから起こす (表示管理が渡していても、親に入れ子にならず必ず画面を取るため)。
- 終わり方: Exit → Linux の窓に閉じるよう頼み最長 5 秒待つ → 残りのクライアントと X11 のプログラムに SIGTERM、0.5 秒後に SIGKILL → 一覧から起こして残ったもの (窓の無いものも) に SIGTERM、0.5 秒後に SIGKILL → Xwayland を止める → プログラムが終わる → session の台本 (exec しているので同じプロセス) が終わり、表示管理 (SDDM) のログイン画面に戻る。電源の項目 (logind) は今のまま (`linux_pc_wl` の `power = ["logind", "exit"]` は L8 から変えていない)。
- `.deb` (amd64): Recommends を `cage` から `xwayland` にした (cage は要らない)。Depends は dpkg-shlibdeps が出すもので、`libpng16-16t64` が加わった。入口の `.desktop` の Comment を直した。arm64 の作り方は変えていない (コンポジタも linux_apps も入らない)。
- `linux_pc` (cage の機種の定義) はそのまま残っていて、`--machine linux_pc` で今までどおり使える。入口には出さない。

## 7. VirtualBox の Kubuntu 26.04 で人が確かめる手順

1. 開発機で `.deb` を作る: `cd fmruby-core && rake native:deb ARCH=amd64` → `native/build-deb/family-mruby_<版>_amd64.deb`。VM へ写す (共有フォルダなど)。
2. VM の設定: グラフィックコントローラは VMSVGA。3D アクセラレーションは入れても切ってもよい (下の 5 で切り替える)。
3. VM の中で入れる: `sudo apt install ./family-mruby_<版>_amd64.deb` (Recommends の xwayland は Kubuntu には初めから入っている)。
4. ログアウトし、ログイン画面 (SDDM) の左下のセッションの選択で「Family mruby」を選んでログインする。画面いっぱいに Family mruby が出る (426x240 の整数倍、余りは黒)。
5. 何も出ない・黒いまま・すぐログイン画面に戻る時: ログイン画面で Ctrl+Alt+F3 (VirtualBox ではホストキー + F3) で文字の画面に入ってログインし、`/etc/default/family-mruby-session` を作って次を書き、ログインし直す。
   ```
   WLR_RENDERER=pixman          # GPU を使わずに描く
   WLR_NO_HARDWARE_CURSORS=1    # マウスの矢印が見えない時
   ```
6. ログの場所: `~/.local/share/sddm/wayland-session.log` (無ければ `journalctl -b --user` か `journalctl -b`)。`native: compositor:` で始まる行に出力の大きさ・レンダラ・座席 (`seat from the session`)、`native: linux apps:` で始まる行に読んだ `.desktop` の数が出る。
7. 確かめること:
   - キーボード・マウスで操作できる。マウスの矢印が見える (見えなければ 5)。
   - メニューのランチャーに「Linux」があり、押すと Kubuntu のアプリ (Dolphin、Konsole、Kate、Firefox など) が名前の順に並ぶ。アイコンは hicolor に PNG のあるものだけ (無いものは「L」)。`OnlyShowIn=KDE;` のもの (システム設定の一部) は出ない。
   - Konsole や Dolphin を起こすと Family mruby の題の帯つきの窓で出る。題の帯で動かし、右下で大きさを変え、丸で閉じる。タスクバーの名前が一覧の名前。
   - X11 のアプリ (Konsole から `xeyes` や `xterm`、入っていなければ `sudo apt install x11-apps xterm`) を起こすと、そのとき初めて Xwayland が起き (ログの `Xwayland started`)、同じく代役つきの窓で出て、キーとマウスが届く。メニューのある X11 のアプリでメニューが正しい場所に出る。
   - Ctrl+Alt+F3 で文字の画面へ、Ctrl+Alt+F1 (SDDM の割り当てによっては F2) で戻れる。
   - 電源のメニューの「Exit」で、Linux の窓が閉じ (保存を聞くアプリは聞いてくる。5 秒で答えなければ止める)、ログイン画面に戻る。「電源を切る」「再起動」「休止」が働く。
   - 縦向きの液晶の時は 5 のファイルに `ROTATE=90` (か 270) を書く。マウスの向きが絵と合うこと。
8. Qt のアプリは `XDG_CURRENT_DESKTOP=family-mruby` で起きるので、Plasma のテーマ (Breeze) ではなく Qt の既定の見た目になることがある (動きは同じ)。

## 8. 受け入れ条件

確認したコマンド: `rake regress:wl` (中で標準と全部 mruby のそれぞれに `rake native:wl`、`wl_check.rb --renderer pixman`、新しい `test/native/wl/wl_apps_check.rb --renderer pixman`)、`rake native:test`、`ruby test/native/deb/deb_check.rb --arch amd64|arm64`、`rake regress`、`rake regress:size`、`rake regress:screens ONLY=retro,modern,native`、`rake native:core`。全部 headless、出力は 1280x720。

| 条件 | 結果 |
|---|---|
| 1. 区分・決まり・速さ | ログに `Launcher: the Linux group is offered`。一時の `XDG_DATA_HOME` (実行の場所の `data/`) に置いた `NoDisplay=true` の項目は出ない、`Name[ja]=目玉` の項目は language = ja で「目玉」、像の foot は「Foot」でアイコン付き (`/var/cache/linux/lx….bmp`)。像の `/usr/share/applications` の全部と試験の物で 11 ファイル 8 項目、読むのに 0-1 ms。300 個を足した 308 項目を読み直す (Reload) と、読むのに 2-5 ms、アイコン 2 ms (同じ元の PNG は 1 回)、Reload から一覧が出るまで 210 ms (標準) / 411 ms (全部 mruby)。**ランチャーを開く時間** (メニューの項目の押下から、ランチャーが写った最初の絵まで、3 回の中央値): 標準 8 ms (8 項目) / 7 ms (308 項目)、全部 mruby 8 ms / 8 ms。区分に入って一覧が出るまで: 標準 98 ms / 242 ms、全部 mruby 91 ms / 246 ms (308 項目の文字列を項目にする分。アイコンは見えている 12 個分だけ) |
| 2. foot を一覧から | キーで区分に入り foot を選んで Enter → `linux apps: started "Foot" (pid 27): /usr/bin/foot`、代役の名前が「Foot」(foot の app id は `foot`)、窓が代役の中に出る。foot 696x494 が中身 232x165 (倍率 3) に入り、間の 696 出力画素は全部枠の色 (107,0,0)、黒は 0 |
| 3. X11 | X11 のプログラムを起こす前: コンテナのプロセスの一覧に Xwayland が無い、`xwayland=lazy`。一覧から xeyes → `Xwayland started`、Xwayland のプロセスがある、`xwayland=up`、xeyes 150x100 が代役つきで出て名前は「目玉」(`X11 xeyes/XEyes, decorations the window manager's`)。一覧から xterm (`X Typer`) → 押して `hello` と Enter → xterm の中の `cat` のファイルが `"hello\n"`、キーボードはその窓、出力の 1 画素ずつの動きが 153.5, 154.5, 155.5 で届く。代役の名前は起こした項目の「X Typer」(debian-xterm の「XTerm」ではない) |
| 4. 自分で大きさを変える | foot (L9a の一式の 3): 枠で 168x128 (504x384) にすると foot は 504x377、代役は 168x126 に合わせた。xterm (X11): 486x318 (中身 162x106) で起き、ウィンドウ操作の並び (12 行 50 桁) で 304x160 を頼む → 代役の中身 102x54、窓は 306x162 に configure、間の画素 0、黒 0。foot は上の 2 のとおり黒 0 |
| 5. Exit | foot (Wayland)・xeyes (X11、SIGSTOP で止めて閉じる頼みに答えないもの)・xterm (X11)・一覧から起こした `sleep 1000` (窓無し) がある所で電源のメニューの Exit → はい: `asked 3 windows to close` → `windows still open after 5003 ms` → `SIGTERM to 1 client` → `SIGKILL to 1 client` → `ended 1 process started from the launcher` → 6.1-6.2 秒でプログラムが終わり exit=0、コンテナに残ったプロセス無し (Xwayland も sleep も)。L9a の一式の「終わり」(止めた foot) も通る |
| 6. session と .deb | `rake native:deb ARCH=amd64` (3.3 MB、入れると 12.6 MB、Recommends: xwayland)。`deb_check.rb --arch amd64` **17/17**: Ubuntu 26.04.1 に入れて 91 個、Recommends が xwayland で Depends に cage が無い、台本が `--machine linux_pc_wl` を exec、`ROTATE=90` と `WLR_RENDERER=pixman` を書いた設定ファイルで `family-mruby-session` を headless で起こすと、`starting /usr/bin/family-mruby --machine linux_pc_wl (rotate 90)`、レンダラ pixman、出力 720x1280 (rotate 90)、絵は 1 倍で (147,520)、grim の画面も 720x1280。Exit → はいで session が 0.6 秒で終わり exit=0、残ったプロセス無し。arm64: `rake native:deb ARCH=arm64` (trixie のイメージは作り直し無し、Depends は今までどおりで libpng も入らない) と `deb_check.rb --arch arm64` **18/18** (コンポジタも session の物も入っていない) |
| 7. 区分が出ない所、回帰 | ネイティブ版 `sdl` (deb_check の起動)、sim (Retro・Modern)、ネイティブ版 (regress:screens の native・native-keys) のログに `Linux group is offered` が無い。`rake regress`: host 19/19、描画の記録 205/205。`rake regress:screens ONLY=retro,modern,native`: 23/23 (retro 8/8、modern 10/10、native 2/2、native-keys 3/3)。`rake regress:size`: NARYAv3・TAB5 とも **DIRAM・IRAM・other の内蔵 RAM +0**、Spinel の静的な内蔵 RAM も同じ。flash は NARYAv3 4,556,640 / TAB5 6,738,512 (L9a の時から +4,336 / +4,352。ランチャーの Ruby とアプリの gem の 2 つの口と platform_sys の 2 項目)、基準 6f7b5848 との差は +44,016 / +49,584。`rake regress:wl` 通る (約 8 分)。`rake native:test` (490 + 64 + 52 + 17 + 新しい 50 + 81)、`rake native:core` (wlroots=0 png=0 で通る) |
| 8. 両方の形、pixman | 1・2・3・5 を含む `wl_apps_check` 8/8 と `wl_check` 19/19 が、標準と全部 mruby の両方で pixman で通る (WSL2 では wlroots の自動の選択も pixman。GLES は試せていない) |

- ブラウザ版の画面 (`regress:screens` の web) は流していない。`tools/fmrb_web.rb` が 14:11 に開いたブラウザ (wasm/build/drive_browser.pid) が開いたままで、道具が止めたため (L9a と同じ。ユーザのものかもしれないので閉じていない)。ブラウザ版はランチャーの Ruby が同じで、`FmrbApp.linux_apps` が "" を返す (既定の表に項目が無い) ので区分は足されない。
- kernel の Ruby は変えていない。デスクトップの Ruby (ランチャー) は標準でも全部 mruby でも mruby で動き、両方の形の一式で確かめた。

## 9. 撤回した見立て・踏んだ罠

- **起こすのはデスクトップのタスクでなくコンポジタの主スレッドにした**。L10 の pty は呼んだタスクのスレッドで fork するが、GUI のアプリは呼んだアプリ (デスクトップ) より長く生きる。主スレッドで起こせば、`PR_SET_PDEATHSIG` が「プログラムが落ちた時」だけを意味し、`DISPLAY` などの環境も主スレッドが持つ物と揃う。
- **`wlr_xwayland_destroy` が assert で落ちた** (exit=134): 自分の `new_surface`・`ready` の listener を外す前に呼んでいた。外してから呼ぶようにした。
- **Xwayland のクライアントは pid で止められない**: wlroots が socketpair で作る接続なので、資格情報の pid は自分自身になり、終わりの SIGTERM の対象にならず、待ち (0.5 秒 × 2) が空回りしていた。終わりの数と待ちからは Xwayland を外し (`wlr_xwayland_destroy` で終わる)、代わりに X11 の窓の `_NET_WM_PID` を SIGTERM・SIGKILL の対象と待ちに入れた (止めた xeyes が残らないのはこのため)。
- **ランチャーの開く時間を画面で測ると 50 ms と 300 ms に分かれた**: 画面を撮る間隔に引きずられていた。絵の番号 (`info` の seq) を細かく見て、変わった最初の絵を撮って確かめる形にした (数 ms の差まで見える)。
- L9a の一式の「枠で大きさを変える」の期待 (中身 168x128) は、代役が foot に合わせるようになって 168x126 になる。期待を「中身 = foot の大きさ ÷ 倍率 (切り上げ)」に直した。代役の名前が「Foot」になったので、ログを見る正規表現も直した。
- grim の画面は、回した出力でも論理の向き (720x1280 の縦長に、絵が正立) で撮れる。実際の液晶は横に付いているので、人が見ると正立する。

## 10. 残件

- GLES での描画、VirtualBox の Kubuntu 26.04 での確認 (7 章の手順)、人の手のキーボードとマウス。
- 自前の題の帯を描く X11 の窓は二重の帯になる (4 章)。消すなら代役に「題の帯の無い枠」を足す (仕様の判断が要る)。
- X11 の `_NET_WM_MOVERESIZE`、最大化・全画面、`_NET_ACTIVE_WINDOW` (プログラムが自分を前に出す頼み) は自動では見ていない・無視している。
- アイコンは hicolor と pixmaps の PNG だけ (SVG・XPM・Breeze などのテーマは見ない)。代役にはアイコンを付けていない。
- `Exec` の最初の物が無い項目も外している (決まりには無い追加の判断)。
- Linux のアプリはデスクトップのアイコンにできない。起動時の通知 (startup notification、`DESKTOP_STARTUP_ID`) は扱わない。
- Qt のアプリの見た目が Plasma のものにならないことがある (7 章の 8)。
- `doc/regression/usage.md` の 11 章 (regress:wl) は範囲の外なので直していない。`wl_apps_check.rb` が加わったこと、時間が約 8 分になったことを書き足すとよい。
