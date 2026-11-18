# R1a 指示書: Modern の解像度を選べるようにする (426x240 / 640x360)

> 2026-11-18 | 親セッションからサブ (Opus) への指示。計画の正は plan.md (2 章の決定を含めて全部)、
> 材料は report/r0.md (3 章 = sim で崩れた所、6 章 = 決め打ちの一覧、7 章、11 章 = R1 の作業の一覧) と
> report/r0b.md。先に全部読む。

## 1. この段で作るもの

同じファームで、Modern (P4-Nano・Tab5・Modern の sim) の画面を 426x240 と 640x360 から選べるようにする。
**既定はどの端末も 426x240 のまま** (P4-Nano の既定を変えるのは R2)。
**属性の無い全画面のアプリを 426x240 に切り替える仕組み (plan 4.3) は次の段 R1b**。この段では作らない
(640x360 のとき、そういうアプリが小さく・左上に出るのは、この段の終わりでは今のまま残ってよい)。

### 1.1 解像度の値と決め打ち (r0.md 6 章・11 章の 1〜3 の前半・5〜7)
- Modern の解像度は 426x240 と 640x360 の 2 つだけ。`/etc/system_conf.toml` の `display_width`・
  `display_height` がそれ以外 (Modern で) なら、426x240 として扱い、ログに 1 行出す。Retro・ネイティブ版・
  ブラウザ版の今の動きは変えない (ブラウザ版はページで大きさを選ぶ今の作りのまま)。
- 「426x240 なら」の決め打ちを、今の画面の大きさを見る形に直す:
  - `fmrb_app_fullscreen_hires_size` (`main/app/fmrb_app.c`): 画面がもう 640x360 のとき、高解像度の
    全画面のアプリは**モードを切り替えずに** 640x360 の全画面になる (r0.md 3 章で、今もそうなっている
    ことを確認済み。壊さないこと)。426x240 のときは今のとおり切り替える。
  - 遠隔の画面 (`main/drivers/remote_desktop/` の JPEG の初期化・流れの既定、遠隔のページ
    `tools/web/remote/remote.js`)、タッチ (`main/drivers/touch/touch_task.c`。Tab5 で 640x360 を選んだ
    ときに座標がずれないこと)、appstore の端末の種類の表、`desktop_common.rb` の `screens`。
  - sim: `components/fmrb_wire/include/shm_display.h` の上限 (640x360 が入るように)。graphics-audio と
    sdl2-display のイメージの作り直しが要る (直下の CLAUDE.md の「仕組みと注意」)。sim を起こす道具
    (`tools/` の sim の起動と回帰の `tools/regress/sim_run.rb`、直下の `tools/mcp/lib/sim.rb`) が
    Modern の大きさを決め打ちしている所は、設定の値から読むようにする。
- **マウスの進み方**: 640x360 のとき、同じ手の動きでポインタが画面の上で進む距離が 426x240 のときと
  同じになるようにする (`mouse_scale` を解像度に合わせて掛ける。設定の値そのものは変えない)。
- `default_user_app_*` (Window の既定の大きさ・位置) が画面の大きさに依るなら、640x360 でも
  おかしくない値になるようにする (既定の Window を大きくするかどうかは R2。ここでは、はみ出さない・
  重なりすぎないことだけ)。

### 1.2 Config の項目
- Config アプリに項目を足す。名前は仮に **「画面の解像度」/ "Resolution"**、値は
  「標準 (426x240)」「高精細 (640x360)」(言葉の細部は任せる。report に書く)。Modern だけに出す
  (P4-Nano・Tab5・Modern の sim)。Retro・ブラウザ版・ネイティブ版には出さない。
- 選ぶと、今の Config のほかの「再起動が要る項目」と同じ流儀で確認を出し、`/etc/system_conf.toml` の
  2 行だけを書き換えて再起動する (ほかの行・コメントを壊さない。今ある設定の書き換えの口を使う)。
  sim では再起動の代わりに何が起きるかを確認し、sim でも切り替えを試せる形にする
  (できなければ、sim での切り替えの手順を report に書く)。
- Config のダイアログが 426x240 と 640x360 の両方で崩れないこと。

### 1.3 戻り道
- **決まったキーを押しながら起動すると、設定を見ずに 426x240 で上がる** (ユーザの決定)。その回だけ
  426x240 で上がり、設定は書き換えない (Config で選び直せば直る)。上がったことが画面かログで分かる。
- どのキーか・いつ見るか (USB のキーボードが認識されるのは起動の途中) は、今の起動の流れを読んで
  決める。**起動の速さを落とさない** (待ちを足さない。黒い画面で待たせない)。キーボードが認識される
  より前に画面の大きさが決まってしまい、待たずには作れないと分かったら、**作らずに止まり**、
  できる形の案 (例: 画面が出た後の数秒の間にキーが押されたら 426x240 で再起動する) を report に書く。
- あわせて、PC からの戻し方 (WiFi の遠隔操作で `/etc/system_conf.toml` を書き戻す・シリアル) を
  案内の文書に 1 行書く (道具は今あるものでよい。新しく作らない)。

### 1.4 文書
- 端末の案内 (`flash/usr/share/doc/` の該当する所) と `doc/reference/fullscreen_hires.md` を動きに合わせる。

## 2. 受け入れ条件

1. sim の Modern (標準構成) で、Config から 640x360 に切り替えて起動し直すと 640x360 のデスクトップになり、
   426x240 に戻せる。両方で: デスクトップ・ランチャー・メニュー・ファイルマネージャ・Window のエディタ
   (1 打鍵)・全画面のエディタ (640x360 で出る。出入りできる)・Config・マウスのクリックの位置が正しい。
   全部 mruby の構成でも 640x360 で 1 回 (起動・エディタの 1 打鍵・Config)。
2. P4-Nano (`app_sync` で焼く。ユーザの許可あり) で、Config から 640x360 に切り替えて再起動し、
   426x240 に戻せる。遠隔の画面 (写しの大きさと入力の座標) が両方で正しい。マウスの倍率は
   コードと、遠隔の入力で確認できる範囲で (実物のマウスの進み方はユーザが見る)。
   **終わったら P4-Nano は 426x240 に戻す**。端末の `/home` に触らない。
3. 戻り道: 作れたなら、sim か P4-Nano で、640x360 の設定のままそのキーで 426x240 で上がることを確認
   (P4-Nano で実物のキーが要るなら、コードの経路の確認までにして「ユーザに見てほしいこと」に書く)。
4. Retro (sim)・ネイティブ版・ブラウザ版・Tab5 のビルドの動きが変わっていない: `rake regress`、
   最後に 1 回 `rake regress:screens` と `rake regress:size` (内蔵 RAM が増えていない)。sim の VM プール
   (`regress:run`) は流さない (親が最後に流す)。
5. `git status` に取りこぼしが無い (各リポジトリ。fmruby-core の前からある 3 つ、graphics-audio の
   `m components/LovyanGFX`・`m components/esp_littlefs`、直下の `?? asterism-tutor/` は除く)。
   sim・ネイティブ版・シリアルの capture は止めてある。`build-linux/` は両リポジトリとも Retro の標準構成。
   sim の flash に置いた試験の設定は元に戻してある。

## 3. 範囲と止まる条件

- 触ってよい所:
  - fmruby-core: 1 章に挙げた所 (C・Ruby・Config アプリ・`tools/`・案内の文書)、
    `doc/modern_resolution/report/r1a.md`、`rake docs:index` の結果。
  - fmruby-graphics-audio: sim で 640x360 を出すために要る所だけ。ブランチ `modern-resolution` を
    `develop` から切ってコミットする。
  - 直下 (family-mruby): `tools/mcp/lib/sim.rb` と、その自己試験のうち要る所だけ。ブランチ
    `modern-resolution` を `develop` から切ってコミットする。**動いている MCP サーバは古いコードのまま**
    (起こし直しはユーザの作業) なので、この段の sim の確認は CLI (`tools/dev_run_check.sh`・
    `fmrb_screenshot`・`fmrb_input` など) で行い、MCP の sim_* には頼らない。
- **触らない**: `config/` の解像度の既定 (R2)、sdkconfig、リンクのプロトコルの形 (足す必要が出たら止まる。
  上限の定数を上げるのはよい)、`main/drivers/display_local/` の合成の手当ての作り (R0b。不具合を
  見つけたら再現の手順を report に書く)、属性の無い全画面のアプリの切り替え (R1b)、submodule の中、
  回帰の基準、asterism-tutor。plan.md は親が直す。
- fmruby-core はブランチ `modern-resolution` に続けてコミットする (この指示書はコミット済み)。
  コミットの書き方は fmruby-core/CLAUDE.md。**どのリポジトリも push しない。develop へマージしない。**
  `git add -A` を使わない。
- P4-Nano: 焼くのは `app_sync` だけ (`FMRB_HW_TARGET=NARYAv4 FLASH_BAUD=115200 HOST=192.0.2.20 rake flash:app_sync`)。
  MCP の flash・tab5_* は使わず、IP (192.0.2.20) を明示する CLI を使う。シリアルは要るときだけ
  (R0 の罠: detach・attach の繰り返しは 2 回まで)。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る。P4-Nano が起動しなくなった (426x240 で
  起動する所までは自分で戻す)。戻り道が待ちなしでは作れない (1.3)。**利用者から見た動きを、plan・
  この指示書に無い形で変える必要が出たとき** (選ばずに止まる)。
- Spinel の罠 (doc/editor_multi_file/instruction_m1.md 4 章)。標準構成でエディタが開いて 1 打鍵できること。
  内蔵 RAM を増やさない。

## 4. report (doc/modern_resolution/report/r1a.md)

- 直した決め打ちの一覧 (r0.md 6 章の表のどれを、どう直したか。直さなかったものと理由)。
- Config の項目の言葉と流れ、戻り道の作り (キー、いつ見るか、起動の時間への影響)。マウスの倍率の決め方。
- 受け入れ条件 1〜5 の結果と根拠 (写しの場所)。
- 640x360 で見つけた崩れ (R1b・R2 の材料): 属性の無い全画面のアプリ、デスクトップの並び、
  組み込みのアプリの見え方。直さずに一覧にする。
- 自分で決めた細部、見立てと違った点、確認していないこと、ユーザに見てほしいこと (HDMI・実物のマウスと
  キー)、ユーザに決めてほしいこと、コミットの一覧 (リポジトリごと)。
