# ヘッドレス S3 H0 指示書: AtomS3R の確認と、H1 の設計の案

> 状態: 指示 | 更新: 2026-10-12 | AtomS3R の出荷時の flash を保存し、回路図と実物でピンの割り当てを照合する。そのうえで H1 (新しい構成 ATOMS3R でビルドして起動する) の設計の案を作る。sdkconfig の defaults と system_conf のキーは案だけ作り、ユーザの承認を待つ

前提: doc/headless_s3/plan.md (決定事項・段階)、fmruby-core/CLAUDE.md (ハードウェア構成、ATOM_DISPLAY の扱い、sdkconfig の決まり)、`components/fmrb_common/include/fmrb_pin_assign.h`、config/ の区画の表と sdkconfig の defaults と system_conf、doc/wasm/ (機体の外に描画を出さない描画の経路 `display_backend.cpp` など)、doc/archive/idf_seam/、親の CLAUDE.md (シリアルと flash の MCP ツール)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. やること

1. **出荷時の flash を保存する** (書き込みの前に必ず):
   - esptool (ビルド用の docker の像にある) で 8 MB 全体を読み出し、scratchpad と、gitignore された場所 (例: `flash_local/` 以外の、リポジトリの外) に置く。sha256 を report に書く。コミットしない。
   - 戻し方 (esptool の write_flash) を report に書く。
2. **ピンの割り当ての照合**:
   - 公式の資料 (M5Stack の AtomS3R の文書・回路図、ESP32-S3-PICO-1 のデータシート) を読み、画面 (ST7735 または資料にある型番、SPI のピン、背光)、RGB LED (LP5562 か資料のもの)、ボタン、システムの I2C (BMI270・BMM150)、赤外線の送信、Grove (HY2.0-4P)、底面の GPIO の割り当てを表にする。
   - 資料どうしで食い違うところ (plan.md に「読んだ表に重複があった」とある) を洗い出す。
   - 実物で確かめられるもの (I2C の機器の番地の走査、ボタンの GPIO の読み、LED) は確かめる。確かめるために小さな試しのファーム (ESP-IDF の最小の例でよい、Family mruby ではなくてよい) を焼いてよい。焼いたら、試しの後に出荷時の flash に戻すかはそのままでよい (保存があるので)。
   - 結果から `fmrb_pin_assign.h` の ATOMS3R の分岐の**案**を作る (まだ組み込まない。report に載せる)。
3. **H1 の設計の案** (作らずに案を書く):
   - **描画**: AtomS3R には描画の子マイコンが無い。S3 の上で描く経路として、ブラウザ版や P4 の描画の経路 (display_backend.cpp、display_p4 など) が S3 で使えるかを調べる。必要な内蔵 RAM・PSRAM、128x128 の画面への出し方 (ST7735 の SPI の送り)、色の形 (RGB332 から画面の形へ)。
   - **区画の表**: 8 MB に、アプリの区画と storage を入れる案。今の n16r8 / n8r8 の表との違い。ファームの大きさの見込み (今の S3 のファームと、外せるもの)。
   - **sdkconfig の defaults** (`sdkconfig.defaults.atoms3r` の案): 既存の n8r8 / n16r8 との差分の形で書く。flash の線の数 (今日 esptool で読んだ値: 内蔵 8 MB、quad、3.3 V)、PSRAM の形 (Octal か Quad かを資料と実物で確かめる。esptool は AP_3v3 と表示した)、USB-Serial/JTAG をコンソールに使う、など。**ファイルは作らない。案を report に書く**。
   - **system_conf の単一アプリの設定**: キーの名前と値の案 (全端末で使える設定として。既存の system_conf の流儀に合わせる)。落ちたら 3 回まで起動し直す。
   - **外すもの**: ヘッドレスで要らないもの (言語、資源、ランチャーの絵など) を、既存のビルドの選択肢でどこまで外せるか。外すための専用の分岐を増やさない。
   - **ボード固有として書くもの**: ピンと周辺 (画面・LED・ボタン・センサ) の初期化だけ、になるかを確かめる。
   - H1 の段の分け方 (例: まず描画なしでシリアルにログが出るところまで → 画面) と、各段で測るもの。

## 2. 受け入れ条件

1. 出荷時の flash の保存があり、sha256 と戻し方が report にある。
2. ピンの割り当ての表 (資料の出典の種類と、実物で確かめた印つき) と、`fmrb_pin_assign.h` の分岐の案がある。
3. H1 の設計の案 (描画、区画、sdkconfig の defaults の差分、system_conf のキー、外すもの、段の分け方) がある。ユーザが決めることを一覧にしてある。

## 3. 範囲・決まり

- **触ってよいもの**: fmruby-core の `doc/headless_s3/report/h0.md` (develop にコミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` にユーザの手元の変更がある))。試しのファーム・読み出した flash はリポジトリの外 (scratchpad) に置く。
- **触らないもの**: `.env`、sdkconfig と sdkconfig.defaults (案だけ)、`fmrb_pin_assign.h` などのソース (案だけ)、ほかの端末 (P4-Nano は `/dev/ttyACM0`。触らない)。
- **端末**: AtomS3R は `/dev/ttyACM1` (USB の ID が Tab5 と同じ `303a:1001` なので、ポートは必ず明示する。MCP の serial_* / flash ツールがポートを指定できない場合は、ビルド用の docker の像の esptool と、ポートを明示した読み取りで行う)。ポートを開くとリセットがかかる。
- **止まる条件**: 資料で決まらず、実物でも確かめられない割り当てが、H1 の作りを左右するとき。案を書いて返す。
- **report**: Japanese plain style (常体)。資料の名前・URL・ページ番号は文書に書かない (出典の種類だけ。「公式の回路図」など)。
