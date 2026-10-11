# ヘッドレス S3 H1 指示書: 新しい構成 ATOMS3R でビルドして起動する

> 状態: 指示 | 更新: 2026-10-12 | `FMRB_HW_TARGET=ATOMS3R` の構成を作り、AtomS3R で起動する。描画は端末の中で描く経路 (CPU 版) で 128x128 の画面に出す。単一アプリの起動 (`boot_mode = "app"`) を全端末で使える設定として作る。H1a-H1d の順に進め、各段で測る

前提: doc/headless_s3/plan.md (決定事項 2 つ、段階)、report/h0.md (ピンの表、4 章の `fmrb_pin_assign.h` の案、5 章の H1 の設計の案、8 章の決めること)、fmruby-core/CLAUDE.md (ビルド、sdkconfig、submodule、ATOM_DISPLAY の扱い)、doc/wasm/ (CPU 版の描画の経路)、doc/archive/idf_seam/、親の CLAUDE.md (シリアルと flash の MCP ツール)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 決まっていること (2026-10-12 ユーザ、全部おすすめ)

- 区画は A 案 (factory 5M + storage 3008K)。保存領域に入れないファイルの一覧を端末ごとに持てる一般の仕組みを足す。
- BLE は外す (全端末で使える一般のビルドの条件として)。
- 言語を外す選択肢は H1a の大きさを見てから決める (H1 では作らない。数字を出して止まる候補)。
- system_conf: `boot_mode = "desktop" | "app"`、`startup_app_retries = 3`、既存の `startup_app`。
- フラグ: `FMRB_HAS_LOCAL_DISPLAY`、`FMRB_HAS_PPA` を `FMRB_HW_MODERN` から切り分ける。`FMRB_HW_FAMILY` は `mini`。
- 画面のピンは `FMRB_PIN_LCD_SPI_*`。画面の制御 IC は GC9107 と ST7735S を ID で見分ける。
- sdkconfig の defaults (`config/sdkconfig.defaults.atoms3r`) は report/h0.md 5 章の案で作ってよい (承認済み)。**それ以外の既存の sdkconfig / defaults は変えない**。
- 「ヘッドレス専用のコードはなるべく書かない」。ボード固有はピンの分岐・画面のドライバの設定 (`lgfx_atoms3r.hpp` など)・設定ファイル 3 つ・ボードの表の行・HW の define だけ。

## 2. 段

| 段 | 内容 | 測る・確かめる |
|---|---|---|
| H1a | 構成の骨組み: HW の define、ピンの分岐 (h0 の 4 章)、区画の表、sdkconfig の defaults、system_conf、ボードの表、`FMRB_HAS_LOCAL_DISPLAY` / `FMRB_HAS_PPA` の切り分け (h0 の約 10 か所)、BLE を外す条件。画面への出力はまだ無し (描画の経路は動くが送り先が無い、でよい) | 起動のログ、起動の時間、`M1|` の行 (内蔵 RAM)、`idf.py size-components`、flash の残り。**Retro (NARYAv3) と Tab5 のビルドが通り、Linux の sim の標準構成が起動すること** (共通の部分を触るため) |
| H1b | 画面: `lgfx_atoms3r.hpp` (GC9107 / ST7735S を ID で選ぶ)、RGB332 → RGB565 の送り出し、背光 (LP5562 の W、I2C1 は描画の側が持ち、アプリの I2C は描画の側を通す既存の P4 の形) | 最初の画素までの時間、present の時間。**画面の見た目はユーザが目で確かめる** (Claude からは見えない。焼いた後に依頼する) |
| H1c | 単一アプリ: `boot_mode = "app"` で `startup_app` を全画面で起動し、デスクトップを出さない。落ちたら `startup_app_retries` 回まで起動し直し、それでも落ちたら止めて、理由を画面とシリアルに出す。全端末で使える設定として作り、sim でも確かめる | わざと落ちるアプリで 3 回の起動し直しと停止を見る。sim の既定 (`desktop`) が変わらない |
| H1d | 外すもの: 保存領域に入れないファイルの一覧 (端末ごと、一般の仕組み)、既存の選択肢で外せるもの (`FMRB_FFT_SPINEL=0` など)。言語を外す選択肢が要るかを数字で示す | flash と内蔵 RAM の基準値 (H2・H3 の比べる元)。言語ごとの大きさ |

## 3. 範囲・決まり

- **触ってよいもの**: fmruby-core の main/・components/ (submodule 以外)・config/ (新しいファイルと、system_conf の新しいキーの既定値)・lib/add・lib/patch・Rakefile・rakelib、`flash/app/test/` の試しのアプリ、doc/headless_s3/。ブランチは develop から `feature/headless-h1`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` にユーザの手元の変更がある。`.env` は変えない。ターゲットは環境変数 `FMRB_HW_TARGET=ATOMS3R` で渡す)。
- **触らないもの**: 既存の sdkconfig / sdkconfig.defaults の中身、submodule、fmruby-graphics-audio、ほかの端末の振る舞い (共通の部分を変えるときは、既定の値で今と同じ振る舞いになるようにする)。
- **内蔵 RAM**: Retro と Tab5 の内蔵 RAM を増やさない (M1 の行か size-components で比べる)。
- **端末**: AtomS3R は `/dev/ttyACM1` (USB の ID が Tab5 と同じなのでポートを明示する。ポートを開くとリセットがかかる)。焼いてよい。出荷時 (前の中身) の保存は `/home/kishima/fmrb/device_backup/atoms3r/` にある。P4-Nano (`/dev/ttyACM0`) は触らない。
- **終わったら**: `build/` を Linux の標準構成 (x86-64) に戻す (`rake clean_all` で wasm の libmruby が消える罠に注意。doc/wasm/)。AtomS3R には H1 の最後のファームを残してよい。
- **止まる条件**:
  - ここに無い仕様の選択 (使う人から見える名前・既定の振る舞いの変更) が要るとき。
  - Retro か Tab5 の内蔵 RAM が増える、またはビルドが通らなくなり、すぐ直せないとき。
  - 既存の sdkconfig を変える必要が出たとき (案を書いて返す)。
  - H1b の画面の見た目の確かめ (ユーザ) が要るところまで来たら、焼いた状態で返してよい (H1c・H1d を先に進めてから返すのが望ましい)。
- **report**: `doc/headless_s3/report/h1.md`、Japanese plain style (常体)。段ごとの作り、実測、変えた共通の部分の一覧 (他の端末への影響と確かめ)、ユーザに確かめてほしいこと、H2 への申し送り。資料の名前・URL は書かない。端末の MAC はリポジトリに書かない。
