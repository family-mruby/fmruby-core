# R0 指示書: 640x360 のデスクトップの調査と計測 (P4-Nano)

> 2026-11-18 | 親セッションからサブ (Opus) への指示。計画の正は plan.md (先に全部読む)。
> あわせて doc/reference/fullscreen_hires.md、doc/fullscreen_hires/plan.md、
> doc/editor_serious_mode/plan.md の 4 章、doc/regression/usage.md を読む。

## 1. この段でやること

**利用者から見た動きを変えない**。入れるのは計装 (測る仕組み) だけで、解像度の既定・設定の項目・
決め打ちの直しは R1・R2。この段の成果は report (`doc/modern_resolution/report/r0.md`) と計装。

### 1.1 決め打ちの一覧
- 426x240 (と 426・240・432・3 倍) を決め打ちしている所を、fmruby-core 全体 (main/・components/・
  lib/gems/・flash/app/ の組み込みと見本・tools/・wasm/・test/) と fmruby-graphics-audio (読むだけ) で洗い、
  表にする: 場所、何を決めているか、640x360 のデスクトップで困るか、R1 での直し方の見立て。
- Window の canvas が Window の大きさか画面の大きさか、合成の作り (変わった所だけか全面か)、
  `DISPLAY_LOCAL_MAX_CANVAS` と PSRAM の関係を読んで書く。

### 1.2 1 フレームの時間の計装
- `main/drivers/display_local/` の 1 フレーム (render_frame 相当) の時間を、**合成・出力 (拡大して
  パネルへ)・キャッシュの同期など、分けられる単位で** 測り、今ある周期のログ (`GFX STATS`) に、
  区間ごとの平均と最悪 (とフレームの数) を足す。遠隔の画面の写しが乗るならそれも分ける。
- 計装のコストは小さく (時刻を読むだけ。ログは今の周期のまま)。**内蔵 RAM を増やさない**
  (足す静的な変数は PSRAM の属性か、数十バイトまで)。Tab5・sim・ブラウザ版・ネイティブ版のビルドを
  壊さない (ESP32 だけの計装でよいが、共有コードは fmrb_* 経由。fmruby-core/CLAUDE.md)。
- この計装は残す (コミットする)。ログの読み方を report と、直下の CLAUDE.md が指す所ではなく
  `doc/modern_resolution/report/r0.md` に書く (CLAUDE.md は親が直す)。

### 1.3 sim で先に見る (安い確認)
- sim の Modern (`config/system_conf_linux_p4.toml` を**手元だけで** 640x360 にしたもの。コミットしない) で
  起動し、デスクトップ・ランチャー・メニューバー・Window のエディタ・全画面のエディタ・全画面の
  ゲームかデモ (高解像度の属性なし) を 1 つ・遠隔の画面に当たる道 (sim に無ければ飛ばす) を見て、
  崩れる所を写しと一緒に一覧にする。直さない (一覧にするだけ)。

### 1.4 P4-Nano で測る
- P4-Nano (NARYAv4、`/dev/ttyACM0`、WiFi は 192.0.2.20、host fmruby-aaaaaa) に、計装入りのファームを
  焼く。**焼くのは `app_sync` だけ** (`/home` を残す):
  `FMRB_HW_TARGET=NARYAv4 FLASH_BAUD=115200 HOST=192.0.2.20 rake flash:app_sync`
  (ビルドは `FMRB_HW_TARGET=NARYAv4 rake build:esp32`。MCP の flash は `.env` の NARYAv3 に従うので使わない)。
  **ユーザから、作業ブランチのファームを P4-Nano に焼いてよいと言われている** (R0 のため)。
- 解像度の切り替え: 端末の `/etc/system_conf.toml` は `app_sync` では端末のものが残る。まず今のものを
  スクラッチの場所に退避し (`ruby tools/fmrb_rd_fs.rb 192.0.2.20 get ...`)、
  `display_width`・`display_height` だけを変えたものを put して再起動する。**権限の判定でこの put が
  断られたら、回り道をせず**、代わりに「試しのビルドだけで解像度を上書きする」形 (コミットしない
  手元の変更) で測る。どちらで測ったかを report に書く。
  **終わったら端末の `/etc/system_conf.toml` を元 (426x240) に戻す** (戻せなければ report の先頭に書く)。
- **端末の `/home` (特に `/home/asterism/`、鍵の `.tar`) に触らない**。今 `/home/asterism/config.toml` は
  ユーザの確認用にローカルのルータ向きになっている。そのままにする。
- 操作は `tools/fmrb_rd_input.rb`・`fmrb_rd_snap.rb`・`fmrb_rd_launch.rb`・`curl http://192.0.2.20/app/...`
  (直下の CLAUDE.md の「Tab5 遠隔」。MCP の tab5_* は Tab5 を探すので、**IP を明示する CLI を使う**。
  エディタは `curl -X POST "http://192.0.2.20/app/launch?path=default/editor"`)。
  ログはシリアル (`tools/fmrb_serial_capture.py` か MCP の serial_*。CH343 は `--no-reset` で横から開ける)。
  `/dev/ttyACM0` が消えたら fmruby-core で `rake attach`。
- plan 6 章の場面 1〜6 を、**同じコミットのビルドで 426x240 と 640x360 の両方**で測る。取る値は
  plan 6 章のとおり (1 フレームの時間の内訳・平均・最悪、present/s、`spx: hid_lat`、PSRAM と内蔵 RAM の
  空き、タスクのスタックの底、VM プール)。場面 3 のドラッグは `fmrb_rd_input.rb` の `drag` を続けて送る
  (遠隔の入力が乗るので、遠隔の画面の流れを見ていないときと見ているとき = 場面 6 を分ける)。
  揺れを見るため、主な場面は 2 回ずつ。
- 640x360 で起動しない・固まるなら、そこで止めず、原因 (ログ・どの決め打ちか) を調べて report に書く。
  測るために最小限の手元の直しが要るなら入れてよい (**コミットしない**。何を直したかを report に書く。
  それが R1 の作業の一覧になる)。

### 1.5 全画面のアプリを 426x240 へ切り替える道 (plan 4.3)
- 640x360 のデスクトップの上で、高解像度の属性の無い全画面のアプリの間だけ画面を 426x240 (3 倍) に
  する形が、今の `SET_SCREEN_MODE` の作りで成り立つかを**コードを読んで**判断する (作らない)。
  足りないもの・直す所・難しい所を書く。

## 2. 受け入れ条件

1. report に、決め打ちの表、sim で崩れた所の一覧 (写しつき)、場面 1〜6 の表 (426x240 と 640x360)、
   メモリの表、1.5 の判断がある。数字には、どのログのどの行から取ったかが分かる根拠を付ける。
2. 計装が入り、P4-Nano のログに区間ごとの時間が出る。計装を入れる前後で `rake regress:size` の
   内蔵 RAM が増えていない (`regress:size` は最後に 1 回)。
3. `rake regress` が通る。sim の標準構成でエディタが開いて 1 打鍵できる (計装が共有コードに触れた場合)。
4. コミットされているのは計装と文書だけ。`config/`・解像度に関わる動きの変更は入っていない。
   `git status` に取りこぼしが無い (前からある `m components/esp_littlefs`・
   `m components/picoruby-esp32/picoruby`・`?? wasm/vendor/` は除く)。
5. 終わったとき: P4-Nano は 426x240 の設定に戻り、計装入りのファームで起動している。sim は止まっている。
   シリアルの capture は止めてある。`build-linux/` は Retro の標準構成に戻してある。

## 3. 範囲と止まる条件

- 触ってよい所 (fmruby-core): `main/drivers/display_local/` の計装、要れば `main/drivers/remote_desktop/` の
  計装、`doc/modern_resolution/report/r0.md`、`rake docs:index` の結果。
- **触らない**: 解像度の既定・`config/`・Config アプリ・デスクトップ・アプリ (R1・R2)、sdkconfig、
  submodule の中、graphics-audio (読むだけ)、直下のリポジトリ、asterism-tutor、`wasm/vendor/`、
  回帰の道具の本体と基準。plan.md は親が直す。
- 本体の checkout、ブランチ `modern-resolution` (もう切ってあり、計画書とこの指示書はコミット済み)。
  コミットの書き方は fmruby-core/CLAUDE.md。**push しない。develop へマージしない。** `git add -A` を使わない。
- 止まる条件: ユーザの作業が要る (sudo・初めての機器の bind・HDMI の目視が判断に要る)。P4-Nano が
  起動しなくなった (**焼き直して 426x240 で起動する所までは自分で戻す**。戻せなければすぐ返す)。
  範囲の外の変更が要る。見立てと大きく食い違う事実 (例: 640x360 のデスクトップは PSRAM が足りない)。
- HDMI の実際の映りは見えない。画面は遠隔の写し (`fmrb_rd_snap.rb`) で見る。目視が要る点は
  「ユーザに見てほしいこと」として report に書く。

## 4. report に書くこと

- 上の 1.1〜1.5 の結果。計装の作り (どこで何を測り、ログにどう出るか、コスト)。
- 既定にする線 (plan 7 章の 2: 1 フレーム 33 ms 以内、入力の遅れ 1.5 倍以内) に対して、実測がどうか。
  遅い場面があれば、時間がどの区間に乗っているかと、手当ての案 (見立てだけ)。
- R1 の作業の一覧 (決め打ちの直し、640x360 で起動するために要った手元の直し) を、そのまま指示書に
  できる粒度で。
- 見立て (plan 3 章) と違った点、踏んだ罠、確認していないこと、ユーザに決めてほしいこと、コミットの一覧。
