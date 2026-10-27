# L9a 報告: Linux の窓を Family mruby の窓として扱う

> 状態: 完了 (受け入れ 7 点。GLES と人の手での操作は残件) | 更新: 2026-10-27 | 指示書は instruction_l9a.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

コミット: 2a38f7d7 (コードと試験)、d8ce8de2 (`rake regress:wl` を `regress:full` に載せた所と usage.md)。
基準の絵は更新していない (sim・ネイティブ版とも)。

## 1. 形の要点: Linux の窓ごとに代役のアプリを 1 つ

作りの方針は「kernel の window manager が通知を汲み、中身を描かない窓として扱う」だったが、
kernel の Ruby には手を入れず、**Linux の窓 1 つに、代役の小さな mruby のアプリ 1 つ**を
立てる形にした。

- 代役 (`main/prebuild_scripts/kernel/linux_window.app.rb`) は普通の窓のアプリで、
  Family mruby の題の帯 (Linux の窓の題)・枠・閉じるボタン・右下の大きさ変えのつまみを描き、
  中身を RGB332 の 0x01 で塗るだけ。
- 窓の移動・大きさの変更・前後・フォーカス・閉じるボタン・Ctrl+Tab での切り替えは、
  window manager と input_router が今の窓と同じ道で行う。代役の窓の置き場所を見て、
  コンポジタが Linux の窓をそこに置く。
- 理由: (1) 前後・フォーカス・題の帯の操作・メニュー・ダイアログとの重なりが、今の仕組みの
  まま全部そろう。kernel に「中身を描かない窓」を足すと、`_get_window_list`・
  `bring_to_front`・z の並べ直し・入力の行き先がアプリの文脈 (`g_ctx_pool`) を前提に
  しているので、C と Ruby (Spinel と mruby の両方) にまたがる変更になる。(2) kernel の
  Ruby を触らないので、Spinel の罠 (型が絞れない呼び出しの 0、mixin の素の定数) にも
  当たらず、端末・sim・ブラウザ版の kernel は 1 バイトも変わらない。(3) 代役は mruby の
  VM 1 つぶんのメモリを使うが、ネイティブ版 (PC) だけの話で、アプリの上限は 32。
- 代役はネイティブ版だけが持つ。`main/CMakeLists.txt` で端末・sim の bytecode から外し
  (keys desktop と同じ扱い)、代役が使う `FmrbApp.wl_window`・`FmrbApp.wl_close` は
  `FMRB_PLATFORM_NATIVE` の中だけに定義した。

## 2. 合成の形 (透過の表し方)

- **透過は 0x01 のカラーキーで表す** (別の面は作らない)。コンポジタは
  「黒 → Linux の窓 → Family mruby の絵 → Linux の窓のポップアップ」の順に重ねる。
  絵は ARGB で、**代役の中身の矩形の中にあって、RGB332 0x01 (合成後の RGB565 で 0x000A)
  のままの画素だけ**を透明にする (穴)。代役の上に Family mruby の窓・メニュー・ダイアログが
  重なれば、そこは 0x01 でなくなるので穴にならない。前後関係は絵そのものが決める。
- 0x01 を選んだ理由: 絵の受け渡しは表示タスクが合成し終えた RGB565 の 1 枚 (`native_frame`)
  で、別の面 (どの画素がどの窓か) を足すには表示タスク (`display_local_task.cpp`、端末と共通)
  の合成に手を入れることになる。0x01 は「透過のキーにだけ使い、見える色には使わない」と
  決まっている色で、キーつきの canvas の 0x01 は合成で透けて絵には残らない。しかも
  コンポジタは代役の中身の矩形の中しか見ないので、矩形の外で 0x01 が出ても穴にならない
  (Linux の窓が無い時は絵は L8 と同じに出る)。
- **代役の canvas はキーを付けずに作る** (`rounded_corners = false` で spawn)。普通の窓の
  canvas は角丸のために 0x01 をキーにしていて、P4・ネイティブ版の合成は canvas 全体を
  キーで比べる (`set_composite_regions` はこの合成では何もしない) ので、キーつきだと中身の
  0x01 が透けて壁紙が見えるだけになる。そのかわり代役の四隅の外側の 3 画素ずつは透けない
  (壁紙の地の色で塗った)。
- Linux の窓は中身の矩形 (フレーム座標) × 倍率の所に置き、`wlr_scene_subsurface_tree_set_clip`
  でその大きさに切る。窓が矩形より大きくても、別の Linux の窓の穴にはみ出さない。
  ポップアップは絵の上の層に置く (Family mruby の窓の上にも出る)。
- 窓が見えないとき: 代役の置き場所がまだ無い (現れた直後の数百 ms)、代役が suspend された
  (全画面のアプリが上にいる)、絵が高解像度の 640x360 のとき。どれも Linux の窓を出さない
  (全画面のアプリは Linux の窓も含めて画面を持つ)。
- 合成後の画面は、穴以外が Family mruby の絵と 1 画素も違わない (受け入れ 2 の表)。

## 3. 置き場所・前後・フォーカスの受け渡し

| 場所 | 中身 |
|---|---|
| `native/wlr/wl_bridge.c` | FreeRTOS のタスク (10 ms ごと)。`input.backend = "wlroots"` のときだけ入力の源の 1 つとして起こす。通知を汲み、代役を spawn し、窓の一覧と HID の行き先から置き場所を作って渡す |
| `native/wlr/comp_link.{h,c}` | 受け渡しの口 (FreeRTOS も wlroots も使わない)。通知 (コンポジタ → Family mruby、L8 の `comp_route` の環を汲む)、置き場所 (Family mruby → コンポジタ、最新が勝つ、変わったときだけ eventfd で起こす)、頼み (閉じる、32 個の環)、倍率と絵の基本の大きさ |
| `native/wlr/comp_route.{h,c}` | 置き場所を受けて窓を置く、穴の判定 (ポインタと絵の透明)、前後、フォーカス。host 試験の対象 |
| `native/wlr/output_wlr.c` | 上を wlroots につなぐ |

- **置き場所**: 代役の窓の中身の矩形 (窓の位置 + (1, 11)、大きさ − (2, 12))、z の順 (下から)、
  suspend かどうか、キーボードの持ち主 (HID の行き先が代役ならその Linux の窓、ほかは
  Family mruby)。ブリッジは 10 ms ごとに `fmrb_app_get_window_list` と
  `fmrb_kernel_get_hid_routing` を読み、変わったときだけ渡す。`fmrb_app.c` などに
  呼び返しの口は足していない (端末のコードに触れないため)。
- **大きさ**: コンポジタは中身の矩形 × 倍率を `wlr_xdg_toplevel_set_size` で頼む。foot は
  文字の格子に合わせて少し小さく描く (504x384 を頼んで 504x377)。余りの行は黒く見える。
- **現れたとき**: 通知 (大きさと app id) を受けて、Linux の窓の大きさ ÷ 倍率 (切り上げ) +
  枠の大きさで代役を spawn する。画面の真ん中に、2 つ目からは 10 ずつずらす。名前は
  app id (foot なら "foot")。kernel には「kernel の外で spawn した」の通知 (`spawned`、
  debugd と同じ口) を送るので、kernel はキーボードを渡し、一覧に入れる。
- **題**: `set_title` を通知で送り、代役が 100 ms ごとに読んで題の帯を描き直す。
- **前に出すとき (押したとき)**: 穴の上で押されると、コンポジタは (a) その窓に押下を送り、
  (b) その場でキーボードをその窓に移し、(c) Family mruby にも同じ点の押下と離しを送る。
  (c) で input_router が代役を前に出してフォーカスを与え、少し後の置き場所で (b) と同じ
  結論が来る。題の帯と右下のつまみは穴ではないので、そこを押すと Family mruby の
  移動・大きさの変更になる。
- ブリッジは最初の絵が出るまで窓の一覧を読まない。入力の源はアプリの表より先に起きるので、
  先に読むと空の錠で落ちた (下の罠)。

## 4. 入力

- **キー**: Family mruby のフォーカスが代役なら、キーはコンポジタからそのまま Linux の窓へ
  (Family mruby の入力タスクを通らない)。L8 の「Family mruby が忙しくても打鍵は遅れない」が
  残る (受け入れ 3・時間の表)。ただし **Ctrl+Tab と Ctrl+, (窓の切り替え) はいつも
  Family mruby へ**。修飾キーの状態は行き先にかかわらず追うので、Ctrl を押したまま foot から
  Family mruby へ切り替えても Ctrl が付いて届く。キーは押した所に離す (L8 のまま)。
- **ポインタ**: 穴の上なら Linux の窓へ、窓の中の座標は出力の画素のまま (1 画素ずつ動かすと
  foot に 1 ずつ違う座標が届く)。そのとき Family mruby のカーソルを隠し (入力の環に
  `NATIVE_INPUT_CURSOR` を足して `fmrb_host_set_cursor_visible`)、wlroots のカーソルを出す。
  絵の上に戻れば逆。ボタンを押している間は押した所の物に送り続ける (L8 のまま)。
- **キーボードの配列**: Linux の窓の keymap を xkb の `jp` / `us` で作り、Family mruby の
  `keyboard_layout` を毎回の pump で見て、変われば作り直す (設定の画面で変えても追う)。
- **アプリからの頼み**: `request_move` と `request_resize` は、そのボタンを押している間の
  移動・大きさの変更にして、代役の窓に対して input_router と同じ C の呼び出し
  (`fmrb_app_update_window_position` / `_size`) をする。移動は動かすたびに、大きさは離した
  ときに 1 回 (Family mruby の窓と同じ)。**最大化と全画面は断る** (xdg-shell 5 で
  wm_capabilities を空にし、頼まれても何も変えずに configure だけ返す)。
- **xdg-decoration**: マネージャを作り、どの窓にも「サーバ側で描く」と答える。foot は自分の
  題の帯を描かない (起動のログに `decoration: server side`)。

## 5. 終わり方 (頼む → 待つ → 止める)

- 閉じるボタン: 代役は `stop` を上書きしていて、閉じるボタン・Ctrl+Q・kernel の stop の
  どれも「Linux の窓に閉じるよう頼む」(`FmrbApp.wl_close` → xdg close) になる。
  アプリが窓を閉じたら、代役はそれを見て自分で終わる。代役が先に消えた (kill された) ときは、
  ブリッジが窓に閉じるよう頼み、2 秒たっても窓が残っていれば代役を立て直す。
- Family mruby の Exit (や制御の口の quit): main の繰り返しが終わったあと、出力を閉じる前に
  コンポジタが (1) 全部の窓に閉じるよう頼む、(2) 窓が全部消えるか 5 秒たつまで、
  Family mruby の絵を出し続けたまま回る (アプリが保存の確認を出せば答えられる)、
  (3) 窓が消えてもまだつながっているクライアントには 1 秒まで自分で抜けるのを待つ、
  (4) 残りに SIGTERM、0.5 秒後に SIGKILL、(5) autostart のセッションを L8 どおり止める。
  Shell の `!` で起こしたものは L10 の仕組みでこの後に終わる。
- 確認: foot 2 つ (1 つは SIGSTOP で止めて、閉じる頼みに答えないもの) で Exit を選ぶと、
  6.2 秒でプログラムが終わり、コンテナの中に残ったプロセスは無い (受け入れ 4)。

## 6. 受け入れ条件

確認したコマンド: `rake regress:wl` (中で `rake native:wl` と `wl_check.rb --renderer pixman`
を標準と全部 mruby の 2 つ)、`ruby test/native/wl/wl_check.rb --engine standard|mruby`
(自動と pixman)。全部 headless、出力は 1280x720。標準・全部 mruby とも 38/38 (2 レンダラ ×
19 項目)。`regress:wl` は 4 分 20 秒。

| 条件 | 結果 |
|---|---|
| 1. 題の帯と枠、foot の帯は無い、移動・大きさ | 起動 4.1 秒で foot (696x494) の代役が出て、中身 232x165 が (97,49)。ログに `decoration: server side`。代役の題の帯の色が foot の上にある。題の帯を (+154,+10) 引きずると中身が (97,49) → (251,59)、foot は出力の (754,177) = 絵の位置 + 中身 × 3。右下のつまみで中身 232x165 → 168x128、foot は 504x384 を頼まれて 504x377 で描き直した |
| 2. 前後 | エディタを foot に半分重ねた状態で、合成後の画面は穴以外が絵と **0 画素差**、穴は 9,391 (全体は 21,420)。foot の見えている所を押すと foot が前に出て穴が 21,420、0 画素差。デスクトップのメニューを foot に重ねると穴 17,208、終了の確認のダイアログでは 13,740、どちらも 0 画素差。穴の中に 0x01 のまま残った画素は 0 |
| 3. フォーカス、細かいポインタ | foot を押して `hello`・Enter と、jp 配列で Shift+2・P の右のキー・Shift+7 → foot のファイルが `"hello\n\"@'\n"`。Family mruby の絵は foot の外で変わらない。同じフレームの画素の中で出力の 1 画素ずつ動かすと、foot に x = 252.5, 253.5, 254.5 で届いた (1 フレーム画素 = 3 出力画素)。foot の上で Family mruby のカーソルは隠れる。メニューを押すとキーボードが Family mruby に戻り、エディタの 1 打鍵がエディタの窓の範囲で native の基準 `editor_type.png` と **0 画素差**、foot には何も届かない。foot にフォーカスがある時の Ctrl+Tab は Family mruby の切り替えになり、foot には何も届かない |
| 4. 閉じるボタンと終わり | 2 つ目の foot の閉じるボタンで、窓が消え、代役が終わり、その foot のプロセスは残らない。3 つ目の foot を止めた (SIGSTOP) 上で電源のメニューの Exit → はい: `asked 2 windows to close` → 動く方の foot が自分で閉じる → `windows still open after 5000 ms` → SIGTERM → SIGKILL → 6.2 秒で終わり、exit=0、残ったプロセス無し |
| 5. keyboard_layout = jp | 3 の `"@'` (us なら `@['` になるキー)。`comp info` の `keymap=jp` |
| 6. 回帰の道具 | `rake regress:full` に `regress:wl` を入れた (`rake regress` には入れない)。`rake regress:wl` 通る。`rake regress` (host 19/19、描画の記録 205/205)、`rake regress:size` (内蔵 RAM **+0**、flash は L10 の時と同じ値: NARYAv3 4,552,304 / TAB5 6,734,160、基準 6f7b5848 との差 +39,680 / +45,232 は以前からのもの)、`rake regress:screens ONLY=retro,modern,native` (retro 8/8、modern 10/10、native 2/2、native-keys 3/3)、`rake native:test` (490 + 64 + 52 + 17 + 81)、`rake native:core` (wlroots=0 で通る)。`rake regress:run` は基準が古く両方とも落ちるので HEAD と比べた: 端末・sim に効く 3 ファイル (app.c・main/CMakeLists.txt・代役の rb) を外した木と今の木で、差は最大 ±216 B (retro closed の kernel)、ほかは ±168 B 以内で向きもまちまち (揺れの幅の中) |
| 7. 標準と全部 mruby、pixman | 1-4 を含む全項目が両方で通る。WSL2 では自動のレンダラも pixman になる (L8 と同じ)。GLES は試せていない |

- ブラウザ版の画面 (`regress:screens` の web) は流していない。`tools/fmrb_web.rb` が
  14:11 に開いたブラウザ (wasm/build/drive_browser.pid) が開いたままで、道具が
  「閉じてから」と止めたため。ユーザのものかもしれないので閉じていない。ブラウザ版の
  動きに効く変更は無い (代役の bytecode は keys desktop と同じく wasm にも入るが、
  どこからも呼ばれない)。
- kernel の Ruby は変えていないので、sim の全部 mruby の構成の画面
  (`FULL=1`) は流していない。

### 時間 (L8 の 5 と同じ測り方)

foot がキーボードを持つ状態で 6 秒に 30 キー。CPU はプログラム全体の 1 コアに対する割合。
「動き続けるアプリ」は L8 の shooter から stackchan (窓のアプリ、1 秒に約 20 枚) に替えた。
shooter は全画面で、Linux の窓ごと画面を取るため (それが正しい動き)。

| 形 / レンダラ | 負荷 | CPU | 絵の数 (約 6.5 秒) | 絵の間隔 平均 / 最長 | キー → foot の commit 平均 / 最長 |
|---|---|---|---|---|---|
| 標準 / pixman | 何もしない | 3.5% | 6 | 1001 / 1002 ms | 1.5 / 3 ms |
| 標準 / pixman | stackchan | 7.3% | 134 | 48.8 / 53 ms | 1.4 / 2 ms |
| 標準 / pixman | spin_hog | 102.0% | 0 | - | 1.2 / 2 ms |
| 全部 mruby / pixman | 何もしない | 3.5% | 6 | 1001 / 1002 ms | 1.4 / 2 ms |
| 全部 mruby / pixman | stackchan | 7.9% | 135 | 48.4 / 53 ms | 1.4 / 2 ms |
| 全部 mruby / pixman | spin_hog | 102.2% | 0 | - | 1.2 / 2 ms |

絵を ARGB にして穴を開ける分の重さは、何もしない時の CPU で L8 (3.3%) と見分けがつかない。

## 7. 人が WSLg で触る手順

```
cd fmruby-core
rake native:wl                 # 初回はイメージ (Ubuntu 26.04) とビルド。全部 mruby は NATIVE_ENGINE=mruby
rake native:wl_run             # WSLg に窓が出て、Family mruby の中に foot の窓が出る
```

- foot の窓は Family mruby の題の帯つき。題の帯で動かし、右下のつまみで大きさを変え、
  丸で閉じる。エディタなどを開いて重ね、押した方が前に出ることを見る。
- 終わりは電源のメニューの「Exit」(foot に閉じるよう頼み、数秒で答えなければ止める) か、
  入れ子の窓を閉じる。
- 2 つ目の Linux のアプリは Shell の `!foot` などで起こせる (L8 で確認した口)。窓は誰が起こしても
  代役が付く作りだが、Shell から起こした窓は L9a では自動では見ていない (確認の一式は
  コンテナの中で直に起こした foot)。

## 8. 撤回した見立て・踏んだ罠

- **「kernel が通知を汲む」は C のタスクにした**。kernel の Ruby に汲む口を足すと Spinel と
  mruby の両方の kernel に C の呼び出しを足すことになり、窓の扱いもアプリの文脈が前提なので、
  代役のアプリに置き換えた (1 章)。L8 の見立ての「錠 + eventfd」の受け渡しはそのまま使った。
- **0x01 で塗った中身が透けて穴にならなかった**: 窓の canvas は角丸のために 0x01 をキーに
  していて、P4・ネイティブ版の合成は canvas 全体をキーで比べる。代役を `rounded_corners =
  false` で spawn して、キーの無い canvas にした。
- **ブリッジが起動直後に落ちた (SIGSEGV)**: 入力の源は `fmrb_app_init` より先に起きるので、
  `fmrb_app_get_window_list` が空の錠を取った。最初の絵が出るまで待つようにした。
- **xdg-shell 3 で `wlr_xdg_toplevel_set_wm_capabilities` が assert で落ちた**: 版 5 で作り、
  呼ぶ前に版を見るようにした。
- **点の下の面を scene の根から探すと絵に当たる**: 絵を窓の上に置いたので、
  `wlr_scene_node_at` は窓自身の木の中で探すようにした。
- **終わりで、閉じた foot が SIGTERM を受けていた**: 窓が消えた直後はまだつながっているので、
  窓が消えてから 1 秒はクライアントが自分で抜けるのを待つようにした。
- **確認の道具の罠**: spin_hog は 20 秒ずつ回り続けて終わらないので、確認の後に kill
  する (debugd は 1 秒の休みの間しか答えないので、25 秒待つ)。shooter は全画面で Linux の
  窓を隠すので、時間の負荷は窓のアプリ (stackchan) にした。全部 mruby のエディタはメニューに
  Debug が 1 つ多いので、基準と比べるときはその行を除く。
- **入れ子 (WSLg) での引きずりは当てにならない**: `--nested` で走らせたら、大きさの変更の
  途中で WSLg 側の本物のマウスの動きが同じカーソルを動かして、違う大きさになった (起動と枠の
  確認は通った)。入れ子で走らせる時はマウスを窓の上に置かない (wl_check.rb と usage.md に
  書いた)。確認の一式は headless。

## 9. L9b への見立て

- **Linux のアプリの起動の口**: 窓は誰が起こしても代役が付くので、起動の口は「`WAYLAND_DISPLAY`
  を自分の口にしてプロセスを起こし、覚える」だけでよい (L10 の `linux_pty` の外側の、端末を
  持たない版)。`.desktop` の `StartupWMClass` (無ければ実行ファイル名) と xdg の app id を
  突き合わせれば、代役の名前とランチャーの項目・アイコンを結べる。Exit の時の終わり方は今の
  「窓に頼む → 待つ → クライアントを止める」に、窓を出さないで残ったプロセス (起こした記録で
  分かる) を足す。
- **Xwayland**: X11 の窓も `comp_route` には同じ番号の窓として入れられ、代役もそのまま
  使える。置き場所は `wlr_xwayland_surface_configure` で伝える。X11 のアプリにはサーバ側の
  飾りを頼む決まった口が無いので、自分の題の帯を描くアプリは二重になる (`_MOTIF_WM_HINTS`
  を見て、飾りを持たないものだけ代役の枠にする、などを決める)。
- **ログイン画面の入口**: session (cage) をこのコンポジタに切り替えるときは、終わり方の
  5 秒の待ちと、ログアウト (logind) の順番を合わせる必要がある。
- ポップアップは絵の上の層に出るだけで、ポップアップの多いアプリ (GTK のメニュー) では
  まだ見ていない。

## 10. 残件

- GLES での描画 (GPU のある Linux PC で `rake native:wl_run`)。人の手のキーボードと
  マウスでの操作 (WSLg の入れ子で)。
- アプリが自分から大きさを変えたとき (`RESIZED` の通知) は代役を合わせていない。foot が
  文字の格子に合わせて少し小さく描いた余りは黒く見える。
- xdg の move・resize の頼みはコードだけで、自動では見ていない (foot はサーバ側の飾りの
  時には頼まない)。大きさの変更の頼みは離した時に 1 回で、途中の枠は出ない。
- 最大化・全画面は断っている。クリップボードの受け渡し (Family mruby との間)、
  primary selection、text-input (Linux の窓へのかな入力) は無い。Family mruby のかなの
  ローマ字入力は Linux の窓には効かない (キーはそのまま届く)。
- 代役の名前 (タスクバーの名前) は spawn の時の app id のままで、題の変化は題の帯にだけ出る。
  代役の四隅の外側 3 画素は透けない。
- 代役の中身の矩形の上に、キーの無い canvas が 0x01 を描くと、そこに Linux の窓が透ける
  (0x01 は見える色に使わない決まりなので起きないはず)。
- 終わりの待ち (5 秒) の間は制御の口が答えない (shot・quit は届かない)。
- 出力が 2 つ以上のときは今も最初の 1 つだけ。
