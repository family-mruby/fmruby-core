# L7d 報告: File Manager と /mnt の小さな直し

> 状態: 完了 (受け入れ 4 点。ブラウザ版の画面の比較と VirtualBox の Kubuntu での確認は残件) | 更新: 2026-10-28 | 指示書は instruction_l7d.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

コミット: 9713c082 (コードと試験)。この報告は別のコミット。基準の絵は更新していない。

## 1. 直したもの

### 1.1 削除に失敗したら画面に出す

- 原因: `fmgr_delete_confirmed` は `File.unlink` の例外をログに書くだけだった。
- 直し方: File Manager から出す知らせの口 `fmgr_notice(文)` を足した。中身はエラーのダイアログ
  (`open_error_dialog`、両方のデスクトップにある) で、題は「Error: File Manager」。削除に失敗すると
  「Could not delete /mnt/host/adir.」が File Manager の上に出る。File Manager は後ろで開いたまま。
- エラーのダイアログは英字の字体だけで描くので、知らせの文は英語にした (起動の失敗などの
  ほかの知らせと同じ)。題も i18n の「ファイル」でなく英語の固定。
- mouse のデスクトップでは、File Manager の上の知らせはどのキーでも閉じるようにした (keys の
  デスクトップは前からそう)。前はキーが後ろの File Manager に渡り、Enter で同じ知らせが出直し、
  Esc で File Manager だけが閉じて知らせが残った。
- 確かめ方: `/mnt/host` のディレクトリ (adir) を消す → はい → 知らせが出る。外を押すと知らせだけ
  閉じる。

### 1.2 確認の外を押すと File Manager まで閉じる

- 原因 (kernel の経路を読んで確かめた): kernel は押下のときに、デスクトップが知らせた重なりの
  矩形 (このときは確認のダイアログ) の外なら、デスクトップに「外を押した」合図 ((0,0) の離し) を
  送り、重なりを無しにしてから押下を窓の並びに渡す (`find_window_at`)。デスクトップの外の何も無い
  所なら、押下はそのままデスクトップに来る。デスクトップは合図で確認を閉じ、続いて来た本物の押下を
  「File Manager の外の押下」として File Manager も閉じていた。
- 直し方: 合図が File Manager の上の確認か知らせを閉じたときは、その後 300 ms 以内に来た押下と
  その離しを 1 組だけ捨てる (`@swallow_click_until`)。kernel は合図と押下を続けて送るので、間は
  デスクトップの受け取りの順だけ。押下がほかのアプリの窓へ行ったときはデスクトップに来ないので、
  300 ms で捨てるのをやめる (後の押下を食べない)。
- 対象は確認のダイアログ (削除の問いと kernel からの問い) と、File Manager の知らせ (エラーの
  ダイアログ)。File Manager の上のファイルの選択 (アプリが頼むもの) は今どおり (残件)。
- kernel は変えていない。
- 確かめ方: aaa.txt に Delete → 確認 → File Manager の外のデスクトップ (400,150) を押す →
  確認だけ閉じて File Manager は開いたまま、aaa.txt は残る。続けて一覧の行を押すと選べる。

### 1.3 ダイアログを閉じたら kernel の重なりを File Manager に戻す

- 原因: kernel の重なりの矩形は 1 つで、ダイアログが開くとその矩形に置き換わり、閉じると
  「無し」になる。L7c は削除の確認だけを File Manager に戻していた。
- 直し方: `notify_overlay_after_dialog` (file_manager.rb) を足し、File Manager が開いていれば
  その矩形を、閉じていれば今までどおり「無し」を知らせる。ダイアログの閉じ方 10 か所 (確認、エラー、
  ファイルの選択、記憶装置、ネットワーク、時計、設定、About、ショートカット、未実装の知らせ) の
  「無し」をこれに替えた。File Manager が閉じているときの動きは前と同じ。ランチャーとメニューと
  File Manager 自身の閉じ方は変えていない。
- 確かめ方: File Manager を開いたまま、ファイルの選択を頼む試しのアプリ (一時の HOME に置き、
  debug の spawn で起こしたもの) にファイルの選択を出させ、Esc で閉じる。ログが
  `Desktop overlay: active=true` (File Manager の矩形) になり、一覧の行を押すと選べる。

### 1.4 keys の確認の行が一覧に重ならない

- 原因: 下の確認の行 (高さ 26、下から 4) は、一覧の下の 2 行ほどの上に描いていた。
- 直し方: 確認の行が出ている間は、その行の上 2 画素までで一覧を終える (`CONFIRM_H = 32`)。
  File Manager は `fmgr_h` を短くし、問われている行が見えるように巻き直す。電源の問いのメニューも
  同じにした (`menu_rows`)。電源の項目はメニューの最後に並ぶので、項目の多い機械では問われている
  項目そのものが行の下に隠れるため。問いが閉じたら一覧は元の高さに戻り、末尾に空いた行が出ない
  ように巻きを戻す。
- 確かめ方: sdl_keys で `/mnt/host` の最後のファイル (30 個のうち最後) に Delete → 一覧は
  zz_file_23〜30 が問いの行の上に並び、問われている zz_file_30 が見える。Esc で元の高さ
  (zz_file_21〜30)。電源の問い (`--set system.power=logind,exit` の Suspend) も行の上に一覧が
  収まる。

### 1.5 `/mnt/root` の削除は確認の前に「読むだけ」と出す

- 新しい口: `FmrbApp.path_read_only?(path)` (mruby: lib/add の app.c、Spinel: `fmrb_spx_app.c`・
  `fmrb_app_ffi.rb`・`fmrb_app_base_spinel.rb`、型は sig/fmrb_app.rbs、回帰の代役も)。
  中身は file HAL の新しい関数 `fmrb_hal_file_entry_is_read_only()`。
- 答えは「その項目を消す・名前を変えることが断られるか」。PC の配布物の木と `/mnt/root` で true。
  既にある `fmrb_hal_file_path_is_read_only()` は書き込み (リンクの先) で判断するので、ホームの
  外を指すリンク自体の削除 (L7 で許している) まで「読むだけ」になる。そこで項目で判断する方を
  別に足した (`check_write(path, false)`)。
- 端末 (ESP32 の file HAL) は常に false。sim とブラウザ版は振り分けの表が 1 行 (flat) なので、
  今の判断どおり false。
- File Manager: 削除の前に聞き、読むだけなら確認を出さずに知らせ
  「Cannot delete .pwd.lock: /mnt/root/etc is read-only.」を出す。
- 確かめ方: `/mnt/root/etc` の最初のファイルに Delete → 確認でなく知らせ。ログに
  `File manager: /mnt/root/etc/.pwd.lock is read-only`。host 試験に 6 項目
  (/mnt/root・配布物は true、ホーム・/home は false、外へのリンクは項目は false で書き込みは true、
  ホームそのものは true)。

### 1.6 判定から開くまでのリンクの差し替えの隙

- 原因: `check_write` は場所を実際の位置 (realpath) で判断するが、その後の変更は名前で
  (`fopen`・`unlink`・`rename`・`mkdir`・`rmdir`・`utime`・`chmod`) 行っていた。間にディレクトリを
  外へのリンクに差し替えられると、外に書けた。
- 直し方: `/mnt/host` の下の変更を、ホームの実際のディレクトリから 1 段ずつ辿る形にした
  (`host_walk`、fmrb_hal_file_posix.c)。
  - 途中のディレクトリは前のディレクトリから `openat(..., O_PATH | O_DIRECTORY | O_NOFOLLOW)` で
    開く。
  - 途中で会ったシンボリックリンクは読んで (readlinkat)、ホームの中にとどまる限り辿る。`..` は
    辿った段を戻るだけで、ホームより上には出ない。絶対の先はホーム (設定の名前か実際の位置) の
    下でなければ断る。辿るリンクは 40 まで、深さは 64 段まで。
  - 最後の名前の変更は、着いたディレクトリへの `*at` の呼び出し (`openat(... O_NOFOLLOW)`・
    `unlinkat`・`renameat`・`mkdirat`・`utimensat(AT_SYMLINK_NOFOLLOW)`・
    `fchmodat(AT_SYMLINK_NOFOLLOW)`) で行う。書くときに最後の名前がリンクに差し替わっていれば
    `O_NOFOLLOW` で開けずに断る。
  - 作るときの足りない親は、辿りながら `mkdirat` で作る (前の `mkdir_recursive` の代わり)。
  - `check_write` は理由をログに出すために前に残した。守るのは辿る方。
- Ruby の `Dir.mkdir`・`Dir.rmdir` は file HAL を通らず lib/add の dir_hal.c が libc を呼ぶので、
  ネイティブ版 (`FMRB_PLATFORM_HOSTED`) では `/mnt/host` の下だけ file HAL の新しい関数
  `fmrb_hal_file_host_dir_change()` に任せた (mkdir(2)・rmdir(2) と同じ答え。作るのは 1 段だけ、
  有れば EEXIST)。ほかの場所と端末は今までどおり。
- `/mnt/host` 以外 (sim・ブラウザ版の flat の表、配布物、/home など) の経路は変えていない
  (rename も `rename()` のまま)。
- 確かめ方 (host 試験、`test/native/file_hal.c`): file HAL に試験だけの差し込み口
  (`FMRB_HAL_FILE_TEST_HOOK` のときだけ、判定の後・変更の前に呼ばれる) を足し、そこでホームの
  ディレクトリ (やファイル) を外へのリンクに差し替える。作る・親を作る・消す・mkdir・rmdir・
  中へ名前を変える・外へ名前を変える・utime・chmod・ファイルを書く・追記の 11 通りがすべて断られ、
  外に何もできないこと、`../outside` を指す相対のリンクを断ること、ホームの中にとどまる相対・
  絶対のリンクは今までどおり辿れること、`/mnt/host` と `/home` の間の名前の変え方、`Dir.mkdir`・
  `Dir.rmdir` の口を確かめる。直す前の形 (同じ差し込み口で、辿らずに名前で変える) で流すと
  13 項目が落ちる (外に書けてしまう) ことも確かめた。

### 1.7 大きなファイルの大きさを桁に収める

- `format_size` を B・KB・MB・GB・TB・PB の 1024 ごとに変えた。数字は 4 桁まで、単位を足して
  6 文字までで、表の桁 (60 画素から巻きの棒を引いた幅) に収まる。1 GB 未満の表示は前と同じ
  (割り算を 1 段ずつにしただけで結果は同じ)。
- 割り算を 1 段ずつにしたのは、`1024 ** 4` のような定数を作らないため (32 ビットの整数の端末)。
- 確かめ方: 一時の HOME に `/proc/kcore` へのリンクを置いて `/mnt/host` を見る:
  「127TB」(140,737,471,590,400 B)。

### 1.8 特殊なファイルに印を付け、開こうとしたら「開けない」と出す

- 一覧の作り方: ディレクトリとして開けず、`File.size` も断られた項目を特殊な項目とした。
  file HAL は `/mnt` の下の装置・FIFO・ソケットを名前の解決で断る (L7) ので `stat` も通らない。
  どこも指さないリンクも同じになる (開けないのは同じなので、同じ印にした)。C の変更は要らない。
- 印: 大きさの桁に `special` と出す (前は 0B)。名前の色は普通のファイルと同じ。
- 開こうとしたとき (ダブルクリック・Enter・右クリックの Run・Edit・Copy、E・C キー): 開かずに
  知らせ「Cannot open fifo: not a regular file (a device, a FIFO, a socket or a link to nothing).」。
- 削除は前のまま試す。`/mnt/host` の FIFO などは、今回の辿り方 (1.6) で名前の解決を通らなく
  なったので消せるようになった (前は名前の解決で断られて消せなかった)。消すのは項目だけで、
  開かないので止まらない。`/mnt/root` の下は読むだけ (1.5) で消せない。
- 確かめ方: 一時の HOME に FIFO・どこも指さないリンク・`/dev/null` へのリンクを置く。3 つとも
  `special` と出て、FIFO に Enter で知らせ。sdl_keys でも同じ。host 試験は L7 の「FIFO は stat も
  通らない」をそのまま使う。

## 2. 受け入れ条件

確認は `HOME` を一時ディレクトリ (scratchpad の下) にし、`--user-dir`・`--cache-dir` もその下、
`--set system.power_log_only=true`。一時の HOME に aaa.txt・bbb.txt・big.bin (3 MB)・
ディレクトリ 2 つ・FIFO・どこも指さないリンク・`/dev/null` へのリンク・`/proc/kcore` へのリンク・
zz_file_01〜30.txt を置き、`tools/fmrb_native.rb up/input/shot/log` で台本を流した。結果は
画面とログとファイルの有無で見た。

| 条件 | 結果 |
|---|---|
| 1. 1〜8 を native で | `--machine sdl`: 8 (fifo に Enter で知らせ、3 つに `special`)、7 (kcore_link が 127TB)、2 (確認の外を押して確認だけ閉じ aaa.txt は残る)、3 (その後の行の押下で選べる。ファイルの選択を閉じた後も `active=true` で行を選べる)、1 (adir の削除で知らせ。知らせの外を押して知らせだけ閉じる)、5 (`/mnt/root/etc` で確認の前に知らせ)。`--machine sdl_keys`: 4 (File Manager の問いと電源の問いが一覧に重ならない、Esc で戻る)、削除は Enter で消える、8 の知らせ。6 は host 試験 (下)。エディタで `/mnt/host/edit_me.txt` を開いて 1 字打ち Ctrl+S: `Saved file: /mnt/host/edit_me.txt (6 bytes)`、中身が「Xhello」(辿る書き方で保存できる) |
| 1. host 試験 | `test/native/file_hal.c` が **97 項目すべて通過** (64 から 33 増: 差し替えたリンク 11 通り、相対のリンク、中にとどまるリンク、/home との名前の変え方、項目の読むだけ 6、Dir.mkdir・rmdir の口 8 ほか)。`rake native:test` に入っている |
| 2. sim の画面 | `FULL=1 ONLY=retro,modern,native,native-keys,retro-mruby,modern-mruby rake regress:screens`: **27/27** (retro 8/8、modern 10/10、retro-mruby 2/2、modern-mruby 2/2、native 2/2、native-keys 3/3。エディタの 1 打鍵の場面を含む)。基準の絵は変わらず、更新していない。web は流せなかった (5 章)。sim の Modern (標準) を `--keep` で残して手で: `/` の [app] に Delete → 確認 → File Manager の外を押す → 確認だけ閉じる → 行を押すと選べる。[boot] に Delete → はい → 「Could not delete /boot.」(sim でディレクトリは消せない) → 外を押して知らせだけ閉じる。sim では読むだけの場所が無いので削除はどれも確認が出る (今どおり) |
| 3. 回帰 | `rake regress`: host 19/19、描画の記録 205/205。`rake native:test`: 全部通過。`rake regress:size`: NARYAv3・TAB5 とも DIRAM・IRAM・other の内蔵 RAM **+0**、Spinel の静的な内蔵 RAM も同じ (下の表) |
| 4. 両エンジン | 1 の mouse の台本 (知らせ・確認の外・行の選択・削除の失敗・読むだけ) と keys の台本 (問い・取り消し・削除・知らせ・電源の問い) と エディタの 1 打鍵を、`rake native:core` (標準) と `NATIVE_ENGINE=mruby` の両方で同じ台本で流し、同じ結果。sim は `regress:screens` の modern・retro (標準) と modern-mruby・retro-mruby (全部 mruby) で通過、手での確認は標準の Modern だけ |

### 内蔵 RAM と flash

`rake regress:size` を今回のコミットの前 (f10a51a3、同じ手順で作り直した) と後で流した差。

| 端末 | 内蔵 RAM (DIRAM・IRAM・other) | app image (flash) |
|---|---|---|
| NARYAv3 (S3) | +0 (126,035 / 16,384 / 104) | 4,556,752 → 4,558,624 (+1,872) |
| TAB5 | +0 (129,698 / - / 948) | 6,738,576 → 6,740,496 (+1,920) |

flash の増えは主にデスクトップの Ruby (mruby のバイトコード) と、Spinel・mruby の
`path_read_only?` の口。基準 6f7b5848 比では +46,000 B / +51,568 B (前の段階の分を含む)。
build/ は道具が退避して戻した (終わった後も NARYAv4)。

## 3. 判断と理由

- **知らせはエラーのダイアログにした**: 両方のデスクトップにあり、keys の 320x170 にも収まる
  (幅 310)。確認のダイアログは はい・いいえ のボタンが要り、知らせには合わない。
- **知らせは英語**: エラーのダイアログは英字の字体で描く (漢字を出すには描き方と折り返しを
  変えることになり、ほかの知らせの見た目まで変わる)。File Manager の表 (Name・Size・Run・Edit) も
  英語なので、そろえた。
- **外の押下を捨てるのはデスクトップの側で**: kernel の合図の作りは、ドロップダウンなどほかの
  重なりも使っていて、変えると広く動きが変わる。デスクトップは「File Manager の上の確認・知らせを
  合図で閉じた直後の 1 組」だけを捨てる。
- **読むだけかは項目で判断する口にした** (1.5)。リンクの先で判断すると、L7 で許している
  「外を指すリンクを消す」が確認の前に断られてしまう。
- **辿り方は自前で書いた**: Linux の `openat2(RESOLVE_BENEATH)` は絶対の先のリンクを
  ホームの中を指していても断る (今は辿れている) うえ、5.6 より古いカーネルに無い。
- **電源の問いのメニューも 4 と同じにした**: 指示書の「keys の確認の行」は同じ行を使う電源の
  問いにも当たり、問われている項目が隠れるのは同じ問題なので。

## 4. 見立てと違った点・気づき

- L7c の報告で「外のクリックは合図で問いを閉じた後、同じクリックがデスクトップにも届く」と
  見ていたのは、kernel の経路 (`find_window_at` → 窓が無ければデスクトップへ) を読んで
  そのとおりだった。
- 削除の失敗の知らせを出したら、mouse のデスクトップではキーが後ろの File Manager に渡り、
  Enter で同じ知らせが出直すことに気づいた (1.1 で直した)。
- `/mnt/host` の FIFO などが消せるようになった (1.8)。名前の解決を通らずに辿るため。開かないので
  止まらない。
- ディレクトリの削除は今までどおりできない (File Manager は `File.unlink` を使う)。今は
  「Could not delete ...」と出る。

## 5. 残件

- **ブラウザ版の画面の比較**: tools/fmrb_web.rb が起こしたブラウザ (Windows 側) が開いていたので、
  `rake regress:screens` は web を外して流した (道具はそのブラウザがあると始めない。閉じない決まり)。
  1〜4 はデスクトップの共通の Ruby なので、ブラウザ版の画面の比較はブラウザを閉じられるときに
  `rake regress:screens ONLY=web` で流す。
- File Manager の上のファイルの選択 (アプリが頼むもの) を外の押下で閉じると、今までどおり
  File Manager も閉じる (1.2 は確認と知らせだけ)。同じにするかは仕様の判断。
- 受け取りのファイル転送 (main/kernel/host/host_file_local.c) は、読むだけかを聞いてから
  名前で `fopen` する。今回の触ってよい範囲の外なので、判定と書き込みの間の隙はそこに残る
  (ネイティブ版で debug の受け取りを使うときだけ)。
- `fchmodat(AT_SYMLINK_NOFOLLOW)` は glibc 2.32 より古いと使えない (Ubuntu 22.04 は 2.35、
  Debian 12 は 2.36)。
- 知らせの文は英語のまま (3 章)。
- 実機 (Tab5・S3) の File Manager の目視は無し。端末では `FmrbApp.path_read_only?` が false なので
  削除の流れは前と同じ。知らせは削除に失敗したとき・開けない項目のときだけ出る。

## 6. VirtualBox の Kubuntu で人が確かめる手順

1. `.deb` を入れ直す: `sudo apt install ./family-mruby_<版>_amd64.deb`。
2. ログイン画面で「Family mruby」を選んでログインし、メニューから File Manager を開く。
3. `/mnt/host` (ホーム) で:
   - 要らないファイルを作っておき (Konsole で `touch ~/del_me.txt`)、Delete → 確認が出たら
     File Manager の外のデスクトップを押す → 確認だけ閉じ、File Manager は開いたまま。
     もう一度 Delete → はい で消える。
   - ディレクトリに Delete → はい → 「Could not delete ...」の知らせ。どのキーか外の押下で閉じる。
   - Konsole で `mkfifo ~/a_fifo` を作って File Manager を開き直す → 大きさの桁が `special`。
     ダブルクリックで「Cannot open ...」。
4. `/mnt/root/proc` で `kcore` の大きさが「128TB」のように桁に収まる (機械で値は違う)。
5. `/mnt/root/etc` のファイルに Delete → 確認でなく「... is read-only.」の知らせ。
6. エディタで `/mnt/host` のファイルを開いて 1 字足して保存できる (Ctrl+S)。
7. 確かめたら `~/del_me.txt`・`~/a_fifo` は自分で片付ける。
