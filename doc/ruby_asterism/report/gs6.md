# GS6 報告: 共有のお絵かきボード (線の列、ボードごとの保存、console の MCAP で記録と再生)

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | instruction_gs6.md の結果。asterism-games 0.11.0 にお絵かきボード (ゲーム `board`、卓 `board1`) を足し、枠組みに「状態の外に配るもの」(`Game#tell`) と保存の間引き (`SAVE_HOLD_MS`) を足した。画面は fmruby-core の「Board / お絵かき」(`flash/app/game/board/`)。console は作りを変えずに、ボードの鍵を記録して再生でき、ブラウザ版に描いていく様子が流れることを deploy/local で確かめた (docs だけ足した。版は 0.3.4 のまま)。止まる条件には当たっていない

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| asterism-games | main `41c63a7` | games 0.11.0: `lib/asterism_games/board.rb`・`board/table.rb`・`board/remote.rb`、枠組みの `Game#tell` / `take_told` / `SAVE_HOLD_MS`・`save(force:)`、ホストの `put_told` と @status の `strokes`、`config/example.yml`・`rooms.yml` に `board1`、試験 `test_board.rb` (と `test_rooms.rb`・`test_framework.rb` の追従)、README「The drawing board」、docs/install.md |
| asterism-console | main `de6ac42` | docs だけ: `docs/v4.md`「Recording a drawing board」、`docs/rooms.md` (部屋で開けるアプリにボードを足す例、`games` の役割の開くページ)。コードと VERSION (0.3.4) は変えていない |
| fmruby-core | feature/gs6 (develop から) | ボードのアプリ (`flash/app/game/board/board.app.rb`・`.app.toml`・`board.icon`・`board.bmp`)、この報告と画面 (`report/gs6/*.png`) |

push・タグはしていない。asterism は読んだだけ。コミットの前に差分を鍵 (`BEGIN`・`PRIVATE`・`sk-`・pass の形)・IP の形・メールアドレスで検査した (9 章)。

## 2. ボードの形

### 2.1 呼び出し (`AsterismGames::Board::Remote`、meta に args / doc つき)

| 呼び出し | 答え |
|---|---|
| `join(name)` | `{"token", "owner"}`。最初に入った人が主 |
| `line(token, c, w, pts)` | 新しい線。c 色 0-15、w 太さの段 0-3、pts `[x, y, ...]` 1-256 点。`{"id" => id}` か理由 (`"colour"`・`"width"`・`"points"`) |
| `more(token, id, pts)` | 自分のいちばん新しい線に点を足す。`"ok"`、`"not yours"`、`"long"` (1 本 1024 点を越える) |
| `undo(token)` | 自分の最後の線 (ボードに残っているもの) を取り消す。`"ok"` / `"nothing"` |
| `clear(token)` | 主だけ全部消す。`"ok"` / `"not owner"` |
| `picture(id, at)` | 今の絵の 1 ページ: 線 id (0: 最初) のその点 at から、400 点まで。`{"v", "n", "p" => [[id, c, w, pts, k], ...], "nx" => [id, at] / nil}`。k 1 はページをまたいだ線の続き (先頭が at の 1 つ前の点) |
| `view(token)` / `table` | ボードの見え方 (`n` 線の数、`np` 点の数、`last`、`mb` [名前, 1 途切れ・2 主]、`lim`)、view は `own`・`mine` つき |
| `mine` / `leave` | 枠組みのまま |

主は最初に `join` した会員。主が去ると (leave か 60 秒の不在で忘れられたとき) 入った順で次の会員に移る。取り消しは「自分の最後の線」だけ。

### 2.2 鍵と 1 通の形

受け付けた変更は `asterism/<host>/games/<board>/strokes` に put で配る (枠組みの `Game#tell`。ホストは次の step で、その step の状態の put より先に、言われた順に put する)。

| 1 通 (MessagePack) | |
|---|---|
| `{"o" => "a", "id", "c", "w", "p" => [x, y, ...], "k", "v"}` | 線 id の点。k 0 が始まり、k 1 は続き (p の先頭はその線のそれまでの最後の点。どの 1 通もそれだけで 1 本の折れ線として描ける) |
| `{"o" => "u", "id", "v"}` | 線 id の取り消し |
| `{"o" => "c", "v"}` | 全消し |

v はその時のボードの版。古い線が上限で捨てられるときは何も流さない (端末も同じ上限で捨てる)。

### 2.3 端末の描き方

- 購読した 1 通をそのまま描く (受け取った点の分だけ)。線は端末の中に 1 点 4 バイトの文字列で持つ (取り消しのときにその場所を描き直すため)。線ごとに囲む四角を持つ。
- 取り消しは、消えた線の四角 (太さの分広げる) だけ紙を塗り、その四角に掛かる線を、四角で切って (Cohen-Sutherland) 古い順に描き直す。全消しは紙を塗るだけ。画面全体を描き直すのは、入ったとき・大きさが変わったときだけ。
- 入ったとき、つなぎ直したとき、ホストが戻ったとき (on_join)、R (「読」) で `picture` をページごとに読む。読む間に来た 1 通は取っておき、最初のページの版より新しいものだけあとで当てる。
- 描く側は、押して動かして離すと 1 本。描いている間に 48 点たまるか 200 ms ごとに送る (最初は `line`、id が返ったら `more`) ので、ほかの人には線が伸びていくのが見える。自分の線はその場で描き、ホストからの同じ 1 通でもう一度描く (同じ画素)。

## 3. 決めた大きさ・上限とその理由

| | 値 | 理由 |
|---|---|---|
| ボード座標 | 640x360 | ブラウザ版の高解像度と同じ 16:9。窓の中では 0.47 倍ほど (302x170) に縮めて描く。座標は MessagePack で 3 バイト |
| 色 | 16 (RGB332 の固定のパレット。白は消しゴムの代わり) | 色の指定は番号だけにして、画面ごとの色の差をホストが持たない |
| 太さ | 4 段 (ボード座標で 1・2・4・8 画素) | 縮めた窓でも見分けられる段 |
| 1 通の点 | 256 点まで | 最大 1,623 B (鍵込み、id と版が 2^31 の悪い場合。試験で出す)。端末が受け取れる 4096 B の半分以下。端末から送る呼び出しの引数も同じ大きさ |
| 1 本の点 | 1024 点まで | 越えたら端末が続きを新しい線として送る (取り消しはその部分だけになる) |
| 線の数・点の数 | 1000 本・40,000 点 | 越えたら**古い線から捨てる** (断らない)。描き続けられることを優先。点の上限は端末が持てる量で決めた: 最初は 50,000 点にしたが、sim で 31,200 点のとき VM 841 KB (54 %) だったので、上限で 6 割に収まる 40,000 点に下げた (実測 934-950 KB、60-61 %) |
| `picture` の 1 ページ | 400 点まで | 最大 2,492 B (試験)。網の実測では 2,193 B |
| 保存 | 1 秒に 1 回まで (`SAVE_HOLD_MS`)、止めるときは必ず | 線の 1 通ごとに 300 KB の JSON を fsync で書かないため |

## 4. 記録と再生の確かめ

console の今の記録・再生 (V4) をそのまま使った。足りないものは無かった (コードは変えていない)。

- 記録: Recordings の「Asterism keys」に `asterism/games/games/board1/strokes`。記録は msgpack のチャンネル 1 つになる。
- 再生: 「To the network」で、記録の鍵に同じ put が流れる (`Bridge::Player` は Asterism の鍵を録ったまま put する)。端末は本物の put と同じに描く。
- 再生の 1 通は版 v がボードの今の版より古いので、端末は「再生中」と出す (状態の put を受けた版より古い線の 1 通が来たとき。状態の put はその版の線の 1 通のあとに来るので、本物の線では出ない)。R で今の絵に戻る。
- 部屋のボードは記録できない: console の証明書は部屋では「誰がいるか」を見るだけで、購読も put もできない (console の作り。rooms.md「Seats」)。記録するボードは登録側 (常設の卓) に置く。docs に書いた。

## 5. 受け入れ条件ごとの結果

### 5.1 試験 (条件 1)

| | 結果 |
|---|---|
| asterism-games `MJ_GAMES=8 bundle exec rake test` | 193 runs、42,437 assertions、0 failures、skip 2 (ルータなし) |
| 同、`ASTERISM_TEST_ROUTER=tcp/127.0.0.1:17667` で test_network.rb・test_app_presence.rb | 4 回中 3 回合格。1 回は test_network.rb の麻雀の打牌が `"not offered"` (2 秒の待ちの中で打牌の番が来なかった時間の揺れ。麻雀の卓とこの Step の変更は関わらない。8 章) |
| asterism-console | docs だけなので試験と rubocop は回していない |

足した試験 (`test_board.rb`、13 件): 線と続きが strokes の鍵に出る (続きが最後の点から始まる)、断るもの、1 本の点の上限、自分の最後の線だけ取り消す、主だけが全部消せる・主が移る、線と点の上限で古い線から捨てる (新しい線は残る)、1 通の最大とページの最大、ページごとの絵が全部の点を 1 回ずつ返す、保存と再開 (主・id・トークン)、保存の間引き、ホストが線を状態より先に put し @status に `strokes` を数える、ホストを起こし直すと絵が戻る。`test_rooms.rb` に「ボードを開く部屋には board1 だけ」を足した。

### 5.2 家の LAN の網で、ブラウザ版 2 つと sim (条件 2)

構成: この Step の zenohd `gs6-zenohd` (tcp 17667・ws 17669、sim の網に入れて sim の zenohd へ `-e`)、ゲームホスト (作業コピー、`board1`・`othello1`)、sim (426x240、標準構成)、ブラウザ版 1280x720 (web_up、ポート 8006) と 640x360 (2 つ目の headless Chrome をポート 8007 の開発サーバに向けた)、線を描く CRuby の台本。

| 確かめ | 結果 | 画面 |
|---|---|---|
| 3 つが同じボードに描き、互いの線が出る | 合格。sim が青、1280 のページが緑、640 のページがマゼンタの太線を描き、3 つの画面に同じ 16 本が出た | `report/gs6/sim_three.png`・`web1280_three.png`・`web640_three.png` |
| 取り消しが全員に効く | 合格。640 のページの U でマゼンタの線が 3 つの画面から消えた (16 → 15 本) | `sim_undo.png`・`web1280_undo.png`・`web640_undo.png` |
| 主の全消し | 合格。主でない 640 のページの C は何も起きない (ボタンも出ない)。主の sim の「消」→ y で 3 つとも白紙 | `web640_not_owner_clear.png`・`web640_cleared.png`・`web1280_cleared.png` |
| ゲームホストを起こし直すと絵が戻る | 合格。TERM で止めると保存して終わり (`stopped (state saved)`、board1.json 3,414 B)、起こし直すと sim は on_join で絵を読み直し (15 本・382 点を 43 ms)、同じ絵に戻った | `sim_after_host_restart.png` |
| 区域だけの描き直し (取り消し) | 合格。1280 のページで 666 本の上に太い黒線を描いて取り消すと、その周りだけ描き直された | `web1280_full_drawn.png`・`web1280_full_undone.png` |

窓の大きさはどの画面でも同じ (372x222)。1280x720 では画面の左上に小さく出る (GS2 と同じ)。sim (明るいテーマ) とブラウザ版 (暗いテーマ) で、ボードの紙は白、パネルはテーマの色。

### 5.3 console の deploy/local (条件 3)

構成: console の作業コピー (0.3.4) を `rehearse prepare → build → bootstrap → up`。`.env` だけ `ASTERISM_ROOM_APPS` に Board を足した。役割 `games-host`・`games` (ブラウザも持つ、開くページはボードの画面) と行 `games` を console の container の `bin/rails runner` で作り、`Relay::Issuer#issue_local` で `games.tar` を作った。もう一度 bootstrap してルータの設定を書き、zenohd を起こし直した。ゲームホストは作業コピーから作った image (`asterism-games:gs6`) を asterism-games の deploy/vps (project `asterism-games-rehearsal`) で上げた (部屋の監督と常設の卓 `mahjong1, mahjong2, othello1, board1`)。ブラウザは docker の Playwright 1.56 (Chromium) を rehearsal の web の網に置き、自前の台本 (標準入力から命令を読む) で動かした。pass は `BrowserPass.issue!` で作り、リンクの `app=` でボードの画面を直に開いた。記録と再生は、管理画面と同じ行 (`Recording`・`Playback`) を runner で作った。

- **記録して、白紙にして、再生**: 合格。pass のページ (`wasm-admin3`、主) で 7 本 (家・屋根・戸・太陽・草・雲・山) を描く間、`asterism/games/games/board1/strokes` を記録した: 24 通、4,153 B、22.5 s (描いている間に線を分けて送るので、7 本が 24 通)。主が全消しして白紙にし、1x で再生: 24 通を 13.8 s で送り終え (`skipped {}`)、ページに 1 本ずつ描かれていった。途中の画面では緑の草が描きかけで、「再生中」と出ている。終わりは描いたときと同じ絵。R でボード自身の絵 (白紙) に戻った。

  | 画面 | ファイル |
  |---|---|
  | 記録した絵 | `report/gs6/dl_recorded_drawing.png` |
  | 全消しのあと | `dl_cleared.png` |
  | 再生の途中 (5 本目、草を描いている途中) | `dl_playback_mid.png` |
  | 再生の終わり | `dl_playback_end.png` |
  | R のあと (ボードの絵は白紙のまま) | `dl_after_reread.png` |

- **部屋のボード**: 合格。部屋 (スロット 01、「Opens: Board」) を作ると 1 秒でホストが上がり (`room 01: ... opens /app/game/board/board.app.rb: host pid 178 (its own certificate games.tar, 16 seats; board1)`)、部屋のリンクで開いた 2 つのページが `games/games/board1` に入り、それぞれ赤と青の線を描いて、両方の画面に 2 本が出た (`dl_room_page1.png`・`dl_room_page2.png`)。

### 5.4 数字 (条件 4)

| | 値 |
|---|---|
| 線 1 本の 1 通の最大 | **1,623 B** (256 点、座標・色・太さ・id・版が最大のとき、鍵込み。試験)。網で見た最大の 1 通は `picture` の答えの 2,193 B (試験の悪い場合 2,492 B) |
| ゲームホストの RSS (VmHWM、家の LAN、board1 と othello1) | 起こした直後 58.1 MB → 線 500 本 (各 60 点、計 30,000 点) で **61.3 MB** → 点の上限 (666 本・39,960 点) で 64.6 MB |
| 保存の大きさ (board1.json) | 500 本・30,000 点で **239,026 B**、上限で 318,441 B |
| ボードのアプリの VM (sim、普通のプール 1.5 MB) | コンパイル直後 使用 518 KB (ピーク 1,089 KB)、ボードに入って白紙 542-548 KB (35 %)、500 本・30,000 点 **746 KB (48 %)**、上限 934-950 KB (60-61 %) |
| ボードのアプリの VM (ブラウザ版、白紙) | 397-417 KB (25-27 %) |
| 描く処理の時間 (受け取ってから present まで、sim) | 60 点の線 1 本 **平均 3.5 ms、最大 9 ms** (間を空けて 20 本・160 本)。500 本を 1.5 s で一度に描かせたときは 83 回にまとまり 1 回平均 24.8 ms・最大 43 ms |
| 取り消しの描き直し (sim) | 500 本の上で小さな線を取り消す 193 ms。666 本の上で横に長い太線を取り消す 925 ms |
| 絵を読む (入ったとき、sim) | 500 本・30,000 点 (75 ページ) で 3.4-3.5 s、587 本で 4.2 s |

### 5.5 エディタ (条件 5)

sim の標準構成 (Spinel のカーネル、Spinel のエディタ) でメニューから Editor を開き、`x` を 1 打鍵: 「Ln 1, Col 2 *」と出た (`report/gs6/sim_editor_keystroke.png`)。

## 6. 見立てと違った点

- **画面全体の描き直しは使えないほど遅い**。最初は取り消しのたびに全部の線を描き直していた。500 本・30,000 点で **13.6 s** かかり、その間アプリが止まって、ホストの印が一度切れたと見なされた (on_join で読み直しが走り、さらに 46 s)。sim の描画の道 (`fmrb_transport`) は良いときで毎秒 8,000-8,900 命令、詰まると毎秒 1,000 命令ほどに落ちる (同じ時に動かした計測用のアプリも 1 秒の窓が作れなかった)。線の囲む四角を持って、取り消しはその四角だけ描き直す形に変えた (193 ms)。あわせて、縮めた画面で同じ画素に落ちる点は描かない。入ったときの読み込みはページの往復の間に命令がはけるので 3.5 s で済む。
- **パレットのクリックが次の線に効いていた**。クリックの列を描く処理のあとで回していたので、色を選んですぐ描くと (Playwright の操作) 前の色で描かれた。マウスの出来事を来た順に処理する形に直した。
- **入った直後に紙が塗られないことがあった** (暗いテーマで黒い紙)。入ったあとの全体の描き直しが、絵を読んでいる間は紙を塗らない作りだった。全体の描き直しはいつも紙を塗り、それまでに読んだ線を描く形にした。
- **「再生中」の誤報**。状態の put を線の 1 通より先に処理していたので、CRuby の台本が速く描くと本物の線を再生と見なした。線の購読を先に読み、版は状態の put からだけ取る (view の答えは線の put より先に着くことがあるので使わない) 形にした。
- **アイコンは .icon だけでは出ない**。ランチャーは .icon の横の .bmp を読む (`tool/gen_icon_bmp.rb`)。`ruby tool/gen_icon_bmp.rb flash/app/game/board` で作って一緒に入れた。
- deploy/local の nginx は `PLAY_DIST` を bind で持つので、`rake wasm:dist` (dist を作り直す) のあとは 404 になる。nginx を作り直して戻した。
- 1 つの Chrome の profile では headless の 2 ページ目が上がらない (web_up のポート違いでも)。2 つ目は別の profile (Windows の一時ディレクトリ) で起こし、`fmrb_web.rb --port 8007` で動かした。

## 7. GS7 (gem と image) に向けて分かったこと

- 枠組みに足したのは `Game#tell` / `take_told` / `SAVE_HOLD_MS` / `save(force:)` と、ホストの `put_told`・@status の `strokes` だけ。どれもゲームに依らない (`test_framework.rb` の「枠組みはゲームを読まない」はそのまま通る)。gem に切り出すときの API の候補に入る。
- image は何も足さずにボードを出せた (`config/rooms.yml` に `board1` を足しただけ。部屋の監督・常設の卓とも)。ボードのホストのプロセスは 48 MB ほど (部屋 1 つ分)。
- console は手を入れずに記録と再生ができた。部屋の中は記録できないことを docs に書いた (console の守りの作りなので、変えるなら console の側の判断)。

## 8. 残ったこと

- **実機での確かめ**: Tab5・Retro では動かしていない。描画の道が sim より遅いので、取り消しの描き直しと、入ったときの読み込み (500 本で sim 3.5 s) がどこまで延びるかを測る。
- 太い線は `draw_thick_line` (1 画素の線をずらして重ねる) なので、斜めの線に縞が出る (画面の `dl_*` の太陽・青い線)。直すなら描く側に太い線の命令が要る (graphics-audio。この Step の範囲外)。
- 上限で捨てた古い線は、その場所が描き直されるまで端末の画面に残る。
- 再生は記録したときの線の id を使うので、白紙にしないで再生すると、ボードの本物の線と id が重なることがある (画面には描けるが、端末が持つ線に点が足される)。docs では「白紙にしてから再生」と書いた。
- test_network.rb の麻雀の打牌の待ち (2 秒) は、ルータ越しで時々足りない (今回 4 回中 1 回)。前からの揺れと見ているが、続くなら待ちを延ばす。

## 9. 後片付け

zenohd `gs6-zenohd`、家の LAN のゲームホストと台本、web_* の開発サーバとブラウザ (2 つ目の Chrome も)、Playwright の container、deploy/local (`GAMES=1 VOLUMES=1 rehearse down`、`.env` と `certs/` を消した)、asterism-games の rehearsal の project (`down -v`、残った volume `asterism-games-rehearsal_state` を消した)、deploy/vps の `.env` と `credentials/games.tar` を止めて消した。作った image `asterism-games:gs6`・`asterism-console-rehearsal:0.3.4` を消した。sim は音を戻して sim_down。scratchpad の鍵 (.tar)・pass のリンク・管理者のパスワード・台本・画面を消した。前からある image (`asterism-console-rehearsal:0.3.2`、Playwright、`eclipse/zenoh:1.10.1` ほか) は残した。docker の prune はしていない。fmruby-core の build/ は x86-64 のまま (`file build/fmruby-core.elf`)、ブラウザ版はこの branch で作り直した (`rake spinel:gen wasm:mruby wasm:mrb wasm:web`、`wasm:dist`)。
