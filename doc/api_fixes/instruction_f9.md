# F9 指示書: ESP32 のビルドの選択が前のビルドと違うときに止まる守りを、全部の選択に広げる

> 2026-11-26 | 親セッションからサブへの指示。計画は plan.md (決定 24)。今ある守りは F8 で `FMRB_EDITOR` に付けた物
> (report/f8.md。`rakelib/build.rake` の `build/fmrb_editor_option`)。

## 背景

ESP32 のファームは、rake が作る mruby のライブラリ (libmruby。gembox と build_config が環境変数を読む) と、CMake が
作る本体 (同じ環境変数を読んで、どの port・どのソースを入れるかを決める) の 2 つからできている。CMake は環境変数が
変わっただけでは構成し直さないので、同じ `build/` で選択を変えると 2 つが食い違う (F8 の報告: gem はあるのに port が
無くてリンクに失敗、の形で実際に出た。気付かないまま古い設定のファームができることもある)。今は `FMRB_EDITOR` に
だけ「前のビルドと値が違うと `rake clean_all` を求めて止まる」守りがある。

## やること

1. **結果を変える環境変数を全部挙げる**: Rakefile (`ENGINE_ENV_OPTS`・`BOARD_BUILD_DEFAULTS`・`.env` の読み込み)、
   `rakelib/`、`lib/upstream/` の gembox と build_config、`CMakeLists.txt` (直下・`main/`・`components/`) で `ENV`・
   `$ENV{...}` を読んでいる所を grep し、ESP32 のファームの中身を変える物 (`FMRB_HW_TARGET`・`FMRB_KERNEL_ENGINE`・
   `FMRB_APP_ENGINE_*`・`FMRB_ASTERISM`・`FMRB_FONT_JA`・`FMRB_FFT_SPINEL`・`FMRB_IMU`・`FMRB_EDITOR` ほか) と、
   変えない物 (書き込みの速さ・ポート・表示だけの物) を表にする。**既定の値が効いた後の値** (ボードの既定・`.env` を
   足した後) で比べること。
2. **守りを 1 つにまとめる**: `rake build:esp32` (と、同じ `build/` を使う ESP32 のタスク) が、上の表の全部の値を
   `build/` の中の 1 つのファイルに記録し、次のビルドで違いがあれば、**何が前と違うか** (名前・前の値・今の値) と
   **直すコマンド** (`rake clean_all`。sim の `build-linux/` は消えないことも) を出して止まる。`FMRB_EDITOR` だけの
   今の守り (`build/fmrb_editor_option`) はこれに置き換える。
   - 記録が無い `build/` (この守りより前に作った物) は、今の `build/` の中身から分かる範囲 (sdkconfig のターゲットなど)
     で判断し、分からなければ止めずに記録を書いて続ける (既にある build を壊さない)。**今の `build/` は P4-Nano
     (NARYAv4) 用**で、この守りを入れた後も `FMRB_HW_TARGET=NARYAv4 rake build:esp32` がそのまま通ること。
   - 意図して続けたいとき用の抜け道 (今ある `STALE_OK=1` の流儀に合わせた環境変数) を 1 つ用意する。
   - `rake regress:size` (端末ごとに自分でビルドして元に戻す。`tools/regress/`) がこの守りで止まらないこと
     (あの道具はターゲットごとに build を切り替える。今どうやっているかを読んで、守りと両立させる)。
   - `rake flash`・`flash:app`・`flash:app_sync` が、`build/` の記録と今の環境変数のターゲットが違うときに
     焼く前に止まる (違う端末用のファームを焼く事故を防ぐ)。今そういう確認があるかを先に調べる。
3. **試験**: ホストで動く試験 (道具の関数を Ruby で直接呼ぶ形。`rake test` の並びに足す): 同じ値なら通る・
   1 つ違うと止まって名前が出る・記録が無いと通って書く・抜け道。
4. **文書**: `CLAUDE.md` (fmruby-core) の「ビルド方法」の注意と、`doc/regression/usage.md` の 2.1 (ビルドの置き場と
   印) に、この守りを 2〜3 行で書く。

## 確かめる

- `rake regress`、足した試験。`FMRB_HW_TARGET=NARYAv4 rake build:esp32` が今の `build/` で通る (増分のビルドで
  数分)。続けて `FMRB_HW_TARGET=NARYAv4 FMRB_ASTERISM=0 rake build:esp32` が**止まって**理由を出す (ビルドは
  始まらない)。`rake regress:size ONLY=narya_v3` が通り、終わった後 `build/` が元 (NARYAv4) の記録に戻っている。
- sim・ネイティブ版・ブラウザ版のビルドには触らない (あちらは別の印の仕組みがある。doc/regression/usage.md 2.1)。

## 報告 (doc/api_fixes/report/f9.md)

環境変数の表 (変える / 変えない、読んでいる場所) / 守りの作り (記録の場所と形、止まるときの文) / 記録の無い build の
扱い / `regress:size`・flash との両立 / 試験と確認の結果 / 確かめていないこと / コミット。

## 範囲と止まる条件

- 触ってよい: Rakefile・`rakelib/`・`tools/regress/` (両立に要る分)・試験・fmruby-core の `CLAUDE.md` の該当の
  2〜3 行・`doc/regression/usage.md`・`doc/api_fixes/report/f9.md`・`doc/README.md`。
- 変えない: ビルドの既定の値、sdkconfig、CMake がソースを選ぶ条件そのもの、sim・ネイティブ版・ブラウザ版のビルド。
- 利用者 (開発者) から見た動きが「選択が前と違うときに止まる」以外に変わる直し方が要るときは止まって報告する。
- **実機には焼かない** (`rake flash*` を実際には流さない。止まる所の確認は、ポートに触る前に止まることをコードと
  ドライランで確かめる)。シリアルのポートと tab5_* / serial_* / flash のツールには触らない。
- push しない・マージしない (ブランチ `api-fixes-f9`)。ユーザに質問しない。裏の処理が残っていないことを確かめて返す。
  コメントとコミットは英語、報告は日本語 (常体)。
