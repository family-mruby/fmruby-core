# フォントは頼んだ値から選び直す・ESP32 の問い合わせの `%xx` を戻す (F4-6・F6-9 の直し)

> 状態: 検収済 | 更新: 2026-11-06 | 2026-11-06 にユーザが「直す」と決めたサブの仮決め 2 つ。(1) `FmrbGfx` が頼まれたフォントと選んだフォントを分けて持ち、移したアプリは着いた側で頼まれたフォントから選び直す (日本語のフォントの無い端末を通っても戻る)。(2) ESP32 の開発の遠隔操作の HTTP は、どの口でも問い合わせの値の `%xx` と `+` を戻す (`/app/launch`・`/app/move` も `/fs/*` とそろう)。日本語のフォントの無い sim を作って移して戻す場面を `regress:checkpoint` に足し、直す前に落ち、直した後に通ることを確認した

## 1. フォント (F4-6、report/f4.md の 8 章の 6)

### 1.1 症状

`FmrbGfx#set_font` は着いた端末の表 (`FONT_AVAILABLE`、`FmrbConst::FONT_JA`) で一番近いフォントを選び、選んだ値だけを `@current_font` に持っていた。基底クラスは復元のときに `set_font(*current_font)` を送り直すので、日本語のフォント (efontJA) を持たない端末 (`FMRB_FONT_JA=0` のビルド、AtomS3R・StickS3) を一度通ると `[:ja, 12]` が `[:ja, 8]` (misaki) になり、その後 efontJA のある端末へ移っても 8 のままだった。

### 1.2 直し

- `FmrbGfx` (`lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb`) が `@asked_font` (頼まれた値、`[family, size]` に整えたもの) と `@current_font` (選んだ値) を分けて持つ。`set_font` が両方を書く。
- `_asked_font` は頼まれた値を返す。この直しより前に書いたイメージには `@asked_font` が無いので、そのときは `current_font`。
- `_checkpoint_font` は結び直した `FmrbGfx` のフォントを頼まれた値から選び直し、字の倍率を送り直す。`fmrb-app.rb` の `_checkpoint_restored` は、これまで自分で `set_font(*current_font)` していた所でこれを呼ぶ。
- 基底クラスの `draw_window_frame` と P5 の `text` は、枠や字を描く間フォントを既定に替えて戻す。戻すのに `current_font` を使うと、選んだ値が頼んだ値に上書きされる (efontJA の無い端末で枠を 1 回描くと頼んだ値も `[:ja, 8]` になる)。どちらも `_asked_font` で覚えて戻すようにした。同じ端末の上では頼んだ値から選び直した結果は選んだ値と同じなので、普段の描画は変わらない。
- `current_font` の戻り値、`set_font` の戻り値はこれまでと同じ (選んだ値)。アプリから見える振る舞いは、移さないときは変わらない。
- Spinel の側の `FmrbGfx` (`main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb`) はソースを共有していない。フォントを選び直さず (頼んだ値をそのまま C へ渡す)、Spinel のアプリは運ばない (movable_apps.md の 3.1) ので変えていない。

### 1.3 試験

- ホスト (`rake text:test`、`test/fmrb_text/run.rb`): `set_font(:ja, 12)` → `FONT_JA` を false にして `_checkpoint_font` → `[:ja, 8]`、頼んだ値は `[:ja, 12]` のまま、枠の保存と戻しを挟んでも `[:ja, 12]`、`FONT_JA` を true に戻して `_checkpoint_font` → `[:ja, 12]`。`@asked_font` の無い古いイメージは `current_font`。
- `regress:checkpoint` に sim の系列 `noja` を足した (`SIM=noja`、`SIM=1` にも入る。最初の構成だけ)。Modern の sim を `FMRB_FONT_JA=0` で作り (`rake build:linux` が `FMRB_FONT_JA` を IDF のビルドへ渡し、`FMRB_NO_FONT_JA` で `FmrbConst::FONT_JA` が false になる)、`ckpt_window` (`on_create` で `set_font(:ja, 12)`) をネイティブ版から sim へ移して戻す。`ckpt_window` は書いたときと `on_restore` のログに `font=` (`current_font`) を出す。ネイティブ版で `ja,12`、sim で `ja,8`、戻って `ja,12`、戻った Window が出る前と画素で同じ。
- ネイティブ版の window move の場面も、B の `on_restore` の行の `font=ja,12` を見る。
- ネイティブ版には `FMRB_FONT_JA=0` で作る口が無い (`lib/add/family_mruby_native.rb` も native の CMake も `FMRB_NO_FONT_JA` を持たない)。口を足すと libmruby とネイティブ版の作り先を分ける要があるので、口のある sim を使った。

### 1.4 確認

- 直す前 (lib の直しを外し、試験の側だけ入れたビルド): `checkpoint.rb --only standard --sim noja` が `font back: font "ja,8" where ja,12 was expected` で落ちた (sim では `ja,8` で通り、戻ったネイティブ版で 8 のまま)。
- 直した後: `rake regress:checkpoint SIM=1` が全部通った (標準・全部 mruby のネイティブ版、sim の Modern・Retro × 2 構成、`standard sim noja: ok (font: ja,12 here, ja,8 there, ja,12 back, the window the same)`、16.3 分)。
- `rake regress`: host 21 suites・描画の記録 205 runs すべて通った。
- 作業の木のまっさらな状態で sim の場面を流すと `tmp/regress/sim` が無く、`SimRun.screen_size` が画面を書けずに「the sim came up at ?」で落ちた。`screen_size` で場所を作るようにした (`tools/regress/sim_run.rb`)。

## 2. 問い合わせの `%xx` (F6-9、report/f6.md の 10 章の 9)

### 2.1 症状

`main/drivers/devctl/devctl_http.c` の `/fs/*` は `fs_url_decode` で path の `%xx` を戻していたが、`/app/launch` (`path`)・`/app/move` (`node`) は戻さなかった。普通に URL を書く道具 (`/` を `%2F` に、空白を `+` か `%20` に) から launch すると `%2Fapp%2F...` のままの path を起こそうとして失敗する。試験の道具 (`tools/regress/checkpoint_board.rb` の `board_query`) は `/` をそのまま送って合わせていた。

### 2.2 直し

- 値を取り出す `ctl_query_value` が、取り出した値を必ず戻す (`ctl_url_decode`)。すべての口 (`path`・`node`・`pid`・`hold`・`restart`・`on`・`level`) が同じ書き方を受ける。`fs_req_path` の `fs_url_decode` は二重に戻さないよう外した (`%2541` が `A` にならない)。
- 戻すのは `%XX` と `+` (空白)。問い合わせの文字列の書き方 (`application/x-www-form-urlencoded`) どおりで、Ruby の `URI.encode_www_form_component` (`tools/fmrb_rd_fs.rb` が使う) は空白を `+`、`+` を `%2B` に書く。`%` の後が 16 進 2 桁でなければそのまま残す。`%00` は値を途中で切るので受けない (その値が無いのと同じ扱いで 400)。
- 問い合わせの文字列の受け皿を 192 から 512 B に広げた (httpd の URI の上限 512)。日本語の名前は 1 文字 9 B に書かれるので、192 では短い path しか通らなかった。値は 512 B の一時の場所に取り出して戻し、戻した後の長さで受け手の大きさ (launch の path は 128) に収まるかを見る。どちらも httpd のタスクのスタック (8 KB) の上で、内蔵 RAM の静的な領域は増えない。
- 道具: `board_query` は `/` 以外を `%XX` で送る (前のまま)。今のファームは `/` も `%2F` も受けるが、この直しより前のファームを焼いたままの端末では `/app/launch` が `%2F` を戻さないので、`/` はそのまま送る形を残した。注釈だけ直した。
- family-mruby の側の道具: `tools/fmrb_rd_fs.rb` の `launch` は `URI.encode_www_form_component` で送る (`/` が `%2F`) ので、前のファームでは launch できず、直したファームで通るようになる。`tools/fmrb_rd_launch.rb` と MCP の `tab5_app` (`tools/mcp/lib/tab5.rb`) は path を書かずにそのまま送る。`/` だけの path は前も今も通るが、空白・`%`・`+`・`&`・日本語を含む path は書いて送る要がある。これらは family-mruby の側 (共有の checkout) にあるので、この直しでは触っていない (4 章の 6)。

### 2.3 確認

- `FMRB_HW_TARGET=NARYAv4 rake build:esp32` (作業の木の build/、`clean_all` の後) が通った。devctl_http.c に警告なし。
- devctl にホストの試験は無い。`ctl_url_decode` を切り出してホストで動かし、`%2Fapp%2Fx%20y.rb` → `/app/x y.rb`、日本語の `%E6%97%A5%E6%9C%AC` → `日本`、`a+b%2Bc` → `a b+c`、`50%`・`%zz%4` はそのまま、`a%00b` は受けない、を確認した (試験は木に入れていない)。
- 実機には焼いていない (3 章)。

## 3. 実機で確認すること (親が焼くとき)

- `FMRB_HW_TARGET=NARYAv4` のファームを焼き、`rake regress:checkpoint BOARD=naryav4` が通る (`board_query` は前と同じ書き方)。
- 空白と日本語を含む path で launch する: `ruby tools/fmrb_rd_fs.rb <IP> put <手元の .app.rb> "/home/テスト 1.app.rb"` (名前は例) の後、`curl -X POST "http://<IP>/app/launch?path=%2Fhome%2F%E3%83%86%E3%82%B9%E3%83%88%201.app.rb"` と `...path=/home/%E3%83%86%E3%82%B9%E3%83%88+1.app.rb` が両方 `{"ok":true,"pid":..}` で起動する。シリアルのログに `dev ctl: launched /home/テスト 1.app.rb`。
- 前のままの書き方 (`curl -X POST "http://<IP>/app/launch?path=/app/test/ckpt_counter.app.rb"`) も起動する。
- `tools/fmrb_rd_fs.rb <IP>` の対話の `launch /app/...` が起動する (前のファームでは `%2F` のまま失敗していた)。
- `/fs/list?path=%2Fhome` と `/fs/list?path=/home` が同じ一覧。
- `/app/launch?path=%2Fapp%00x` が 400 (`path required`)。

## 4. サブの仮決め (ユーザが見直す)

1. 頼んだフォントは `_asked_font` (下線で始まる、基底クラスの内向きの名前) とし、sig には足さない。アプリが使う口は増やさない。
2. 枠と P5 の字の前後でも、頼んだ値で覚えて戻す (選んだ値で戻すと、efontJA の無い端末で頼んだ値が消えるため)。
3. 日本語のフォントの無い端末の場面は、ネイティブ版に口を作らず、口のある sim (`FMRB_FONT_JA=0`) で作った。`SIM=1` に入れたので、`regress:full` は sim を 1 回多く作る。
4. 問い合わせの値は `+` も空白に戻す (問い合わせの文字列の決まり。`/fs/*` で `+` を名前に含むファイルを書かずに送っていた場合は振る舞いが変わる)。`%00` は受けない。
5. 道具 (`board_query`) は `/` をそのまま送る形を残した (前のファームでも通るように)。
6. family-mruby の側の `tools/fmrb_rd_launch.rb`・MCP の `tab5_app` は path を書かずに送るまま (共有の checkout なので触らない)。空白や日本語を含む path を渡すなら、そちらで `URI.encode_www_form_component` を通すのがよい。
