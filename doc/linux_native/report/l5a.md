# L5a 報告: (1) の形の WSL2 でできる部分と L4 の残件

> 状態: 完了 (受け入れ 6 点) | 更新: 2026-10-26 | 指示書は instruction_l5a.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

## 1. 一覧・File Manager での Esc の長押し

**長押しをデスクトップに渡し、デスクトップが決める形にした。**

- host_task は「キーボードがどこにあるか」だけを見る。アプリにあれば今どおり kernel に
  system_interrupt (アプリを閉じる)。デスクトップにあれば、デスクトップへ
  `{"cmd":"keys_esc_long","quit":<機種が esc_long なら true>}` を送る。
  キーボードの行き先が無い (デスクトップがまだ無い・消えた) ときだけ、今までどおり
  host が自分で終える。
- デスクトップ (system_desktop_keys.app.rb の `esc_held`) は、開いているものを閉じて
  メニューへ戻す: エラーの窓 → 閉じる、切り替えの一覧 → 閉じてメニュー (`keys_menu`)、
  ファイル選択 → 「選ばなかった」で閉じる (短押しの Esc と同じ。頼んだアプリに戻る)、
  File Manager → 閉じてメニュー。何も開いていないのにキーボードがある (前にアプリがいる)
  ときはそのアプリへ戻す。メニューにいて `quit` が true のときだけ、host に
  `{"cmd":"keys_quit"}` を送り、host が `fmrb_native_request_quit()` で終える。
- host は `keys_quit` を、送り手がデスクトップ (app_name が system_desktop) のときだけ聞く。
  host へ来る APP_CONTROL は今まで無かったので、native だけの分岐を
  `host_task_process_message` に足した (それ以外の型は今までどおり)。
- **選んだ理由**: どの画面が出ているかはデスクトップの変数 (@switcher_open・
  @file_manager_open・@file_selector_open・エラーの窓) にしか無い。デスクトップが kernel
  や host に今の画面を知らせる形は、開く・閉じるのたびに知らせる所が 4 つの部品に散り、
  知らせ損ないがあると長押しで終わってしまう (取り返しが付かない向きに壊れる)。
  長押しを渡す形なら、決めるのは押した瞬間のデスクトップ自身で、状態の写しを持たない。
  終える口 (`keys_quit`) は L6 のメニューの「終了」でも使える。
- 切り替えの一覧での長押しは「メニューへ」にした (短押しは前にいたアプリへ戻る)。
  受け入れ条件 1 の「メニューに戻り」に合わせた。

## 2. 一覧から終えた後に前へ出すもの

- **使った順の記録は kernel の keys の状態 (`kernel/fmrb_kernel/keys_desktop.rb`) の
  `@keys_recent`** に置いた。前に出たアプリの pid の並び (新しいものが後ろ) で、
  `keys_show` のたびに後ろへ回し、アプリが終わったら (`keys_after_exit`) 外す。
  前に出す・終えるを決めているのが kernel なので、そこに置けば記録と動きがずれない。
- アプリが終わったとき (`keys_after_exit`): 終わったのが前のアプリでなければ何もしない
  (前のアプリはそのまま)。前のアプリなら、今までどおり「キーボードが渡った先」
  (F5 で走らせた元のエディタなど。デスクトップは除く) を先に見て、無ければ
  `@keys_recent` の一番新しい生きているアプリ、それも無ければメニュー。
  自分で終わったアプリ (Esc の長押しで閉じたものを含む) も同じ規則を通る。
- 一覧から後ろのアプリを終えるとき (`keys_close`): 止める要求を読ませるため一度前に出す
  のは今までどおり (止まっている (suspend した) アプリは要求を読めない)。そのとき、
  前にいたアプリを記録の一番後ろに付け直す。終わった後は記録の規則でそのアプリが前に戻る。
  後ろのアプリは一瞬前に出るが、終わればすぐ元に戻る。

## 3. Zenoh のルータが空のとき

`FmrbAsterism.settings` (lib/add/picoruby-fmrb-asterism/mrblib/fmrb-asterism.rb) を変えた。

- 探す順 (config.toml → system_conf の zenoh_router → 資格情報) は同じ。どれでも決まらず、
  **system_conf に `zenoh_router` の行があって値が空**で、`FmrbConst::BOARD` が `"linux"`
  (sim と native) のときは、sim の名前 (`tcp/zenohd:7447`) に落とさず、ルータを空のままにし、
  設定の誤り (`Settings#error`) を
  `no Zenoh router is set (router in /home/asterism/config.toml, or zenoh_router in /etc/system_conf.toml)`
  にする。
- 「行があって空」は native のパッケージ版の system_conf だけが書く形 (L3c の配布用の値。
  コメントにも「何も仮定しない」と書いてある)。sim の system_conf には行が無く、
  ブラウザ版のページは空のときに行を消し、端末は BOARD が違うので、どれも今の既定のまま。
  C の側 (FmrbConst に native の印を足す) は触らずに済んだ。
- Link (Asterism.connect を包む部品) は設定の誤りを接続の理由として画面に出す。
  ゲーム・asterism_demo など Link を使うアプリは「no Zenoh router is set ...」を出し、
  再試行の間隔で同じ理由を出し続ける (固まらない)。
- 試験: test/asterism/run.rb に 8 項目 (空の行で誤りになる、config.toml・資格情報の
  ルータは使う、行が無ければ sim の名前、端末 (BOARD が tab5) は落ちる先のまま)。
  `rake asterism:config_test` と `rake regress` の host に入っている。

## 4. APPLaunch の 320x170 用の入口

| 入口 | 起動用の実行ファイル | `.desktop` | 機種 |
|---|---|---|---|
| Family mruby | `/usr/share/APPLaunch/bin/family-mruby-lcd` | `family-mruby-lcd.desktop` (`Name=Family mruby`) | `cardputer_zero` |
| Family mruby (HDMI) | `/usr/share/APPLaunch/bin/family-mruby-hdmi` | `family-mruby-hdmi.desktop` (`Name=Family mruby (HDMI)`) | `cardputer_zero_hdmi` |

- `rakelib/native.rake` の `NATIVE_DEB_LAYOUT` の arm64 に 1 行足しただけ (起動用の
  実行ファイルは L3a の launcher.c をそのまま、機種の名前だけ違う)。本体の画面が主なので
  `Name` は飾りなしの「Family mruby」、HDMI 側に「(HDMI)」。アイコンは両方とも同じ。
- deb_check.rb の手順 2 を 2 つの入口にした: それぞれの起動用を実行し、終了コード 3 と
  `native: refused: machine <機種>: display: ...` を確かめ、`.desktop` の Name と Exec を確かめる。
- 結果 (arm64、qemu): 下の受け入れ 4。

## 5. `.app.toml` の 2 つの欄の手引き

- 書いた場所: `doc/app-distribution/spec.md` の 3.1 節 (「実機が実際に読む鍵」の表) に
  `screens` と `needs_pointer` の 2 行。もう 1 か所、ルートの skill の資料
  (`.claude/skills/fmrb-app-new/SKILL.md` の `.app.toml` の節) にも同じ要点を足した。
  ただしルートの `.claude/` は git の追跡外 (`.gitignore`) なので、この変更はコミットに
  入らない (手元の資料だけが変わっている)。
- 宣言は足していない (指示書どおり)。

### 宣言の候補 (L5b で実機を見て決める)

native の `--machine sdl_keys` で、各アプリをパスで起動し (帯の下の 320x160 の全画面に
置かれる)、最初の画面を撮って見た (候補の見え方は WSL2 の SDL のもの。Cardputer の LCD での読みやすさは L5b)。キーの操作の有無は .app.rb を読んだ。

| アプリ | 320x160 での見え方 | キーだけで | 見立て |
|---|---|---|---|
| BASIC (`basic/basic.app.bas`) | 全面に出る | はい | 候補 (計画の「使い道の大きいもの」) |
| BASIC のサンプル (`sample_12_maze` ほか) | 全面に出る | はい | 候補 |
| raycaster | 全面に出る | はい | 候補 |
| shooter | 全面に出る (下が少し切れる) | はい | 候補 |
| walk_scroller | 全面に出る | はい (StickS3 の歩数でも動く) | 候補 |
| flappy | 全面に出る | はい (Up) | 候補 |
| picorabbit (スライド) | 全面に出る、一覧と操作の行が収まる | はい | 候補 |
| nsf_player・smf_player | 一覧は読める、下の操作の行が切れる | 一部 | 要確認 (操作の行) |
| mml | 7 項目の一覧の下が切れる | はい (1-7 のキー) | 要確認 |
| othello (卓) | 文字の画面は読める | はい | 要確認 (網が要る) |
| lua・python・picoruby (デモ) | 窓の大きさのまま (枠つき) で、右が空く | 一部 (python はクリック) | 手直しが要る |
| robo_pilot・net_probe | 窓の大きさのまま | はい | 手直しが要る |
| rpg_demo・blockgame・breakout | (下の不具合で起動直後に native が落ちて見られない) | はい | 不具合を直してから |

- **見つけた不具合 (範囲外なので直していない)**: FMSQ の曲を読むアプリ (rpg_demo・
  blockgame・breakout) を起動すると、native のプログラムが
  `double free or corruption (out)` で落ちる。mouse のデスクトップ (`--machine sdl`) でも同じで、
  L5a の変更とは関係ない。原因は `components/apu_emu/src/apu_if_p4.c` の
  `apuemu_free()` が `free()` を呼んでいること: hosted の `heap_caps_malloc()`
  (hosted/stub/heap_hosted.c) は前に 32 バイトの見出しを付けた番地を返すので、
  `free()` には違う番地が渡る。端末では heap_caps の領域を `free()` で返してよいので
  表に出ない。`heap_caps_free()` にすれば直る見込み (1 行)。ブラウザ版も同じ stub を
  使うので、壊れ方が見えないだけで同じことが起きている可能性がある。

## 受け入れ条件

確かめた構成: 標準 (`rake native:core`) と全 mruby (`rake native:core NATIVE_ENGINE=mruby`、
`fmrb_native.rb up --engine mruby`)。kernel の Ruby を変えたので先に `rake spinel:gen`。

| 条件 | 結果 |
|---|---|
| 1. 一覧・File Manager での長押し | `fmrb_native.rb up --machine sdl_keys` → File Manager を開いて `keydown esc sleep 1500 keyup esc` → メニューに戻り `status: running`。エディタを開いて Ctrl+Tab の一覧で長押し → メニューに戻り running (エディタは suspend で残る)。メニューでの長押しはログに `Esc held on the desktop: the desktop decides (quit allowed)` → `keys desktop: Esc held on the menu, ending the program` → `native: quit asked for by the machine` で終わる。`--set system.exit_key=none` ではメニューでの長押しで終わらない (`quit not allowed`)。標準・全 mruby とも |
| 2. 一覧から終えた後 | エディタ → Shell の順に起動し、一覧で Shell (前) を Delete → ps に FM-Editor (state 2 = 動いている) だけ、`Brought 'FM-Editor' (PID 5) to front`、キーはエディタに入る。もう一度 Shell を起動し、一覧でエディタを前に出してから Shell (後ろ) を Delete → エディタが前のまま。Shell の中で Esc を長押しして閉じた (自分で終わる経路) ときもエディタが前に出る。標準・全 mruby とも |
| 3. ルータが空 | 開発用の native の利用者の system_conf を `zenoh_router = ""` にして起動 → `/app/test/asterism_demo.app.rb` が画面に `linux failed: no Zenoh router is set (router in /home/...` を出し、キーも通り、プログラムは動き続ける。Link を使わずルータの文字をそのまま使うテスト用のアプリ (zenoh_nodes) は `cannot open a session to  (-91)` になる (分かりにくいが固まらない。flash/ のアプリは触らない決まり)。sim: `rake regress:live` は **16/16** (modern、2.4 分) で今どおり |
| 4. `.deb` の入口 2 つ | `rake native:deb ARCH=arm64` (3.1 MB、Depends は L3c と同じ) → `ruby test/native/deb/deb_check.rb --arch arm64` で **16/16**。`family-mruby-lcd` は `native: refused: machine cardputer_zero: display: no /proc/fb (No such file or directory): this system has no framebuffer` で終了コード 3、`family-mruby-hdmi` は `... cardputer_zero_hdmi: display: no DRM device` で終了コード 3。2 つの `.desktop` の Name は `Family mruby` と `Family mruby (HDMI)`。ほかの手順 (起動 4.5 s、デスクトップ・エディタの画素 0 差、配布物の木が変わらない、/etc の更新、debugd) も通った |
| 5. 両方の構成 | 1・2 を標準と全 mruby の両方で確かめた (上)。確認の手順は `fmrb_native.rb` の up・input・app ps・status・log だけで流した |
| 6. 回帰 | `rake regress`: host 19/19、描画の記録 205/205。`rake regress:screens`: **29/29** (retro 8/8、modern 10/10、web 6/6、native 2/2、native-keys 3/3。keys の基準 3 枚は変わらず、基準の更新なし)。`rake regress:size`: **内蔵 RAM は NARYAv3・TAB5 とも DIRAM +0・IRAM +0・other +0**、Spinel の kernel の静的な内蔵 RAM は 128 B のまま。flash は基準 6f7b5848 比 NARYAv3 +20,000 B、TAB5 +23,280 B (L4 の報告の時点から +1.8 KB / +2.8 KB。kernel の使った順の記録の分が主)。`rake regress:live`: 16/16。build/ は TAB5 (esp32p4) に戻った |

## 見立てと違った点・撤回した仮説・踏んだ罠

- 長押しを host で決めるには、デスクトップの画面を host が知る必要がある、と最初は考えた。
  デスクトップに決めさせれば知らせる仕組みが要らず、終える口も 1 つで済んだ。
- 候補の調べで native が落ちたのを keys のデスクトップの不具合と疑ったが、mouse の
  デスクトップでも同じに落ち、曲を読んだ直後の `free()` だった (上)。
- 道具の `fmrb_native.rb input keydown esc sleep 1500 keyup esc` は、メニューで終わると
  keyup を送る前にプログラムが消えるので、送れなかったと例外を出す。終わったこと自体の
  証拠なので、手順の側で `status` を見て判断した。

## 残件

- FMSQ の曲で native が落ちる不具合 (上。`components/apu_emu` は範囲外)。
- Link を使わない Asterism のアプリ (テスト用) では、ルータが空のときの理由が
  分かりにくい (Session.open の失敗の数字)。
- 宣言の候補の決定と `.app.toml` への宣言は L5b。
- 一覧から後ろのアプリを終えると、そのアプリが一瞬前に出る (止める要求を読ませるため)。
