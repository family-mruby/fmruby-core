# 全部 Spinel の構成の生成を手元で (rake regress:spinel)

> 状態: 完了 | 更新: 2026-11-07 | CI の spinel-gen でしか分からなかった「デスクトップを Spinel で作るとコンパイラが拒否する」を、`rake regress:full` の部分 `rake regress:spinel` で手元で拾う。生成だけで 18 秒ほど。ビルドの生成物 (main/prebuild_scripts/spinel/gen) には触らない。e3ab8127 の Spinel 側を外すと 8 件の拒否で落ちることを確認した

## 1. 何をするか

- CI の spinel-gen は `FMRB_KERNEL_ENGINE=spinel FMRB_APP_ENGINE_DESKTOP=spinel FMRB_APP_ENGINE_EDITOR=spinel rake spinel:gen` を流す。手元ではデスクトップを mruby で作るので、デスクトップが呼ぶ FmrbApp のメソッドが mruby の binding にだけあっても気づかない (ci_develop_20261107.md)。
- `rake regress:spinel` はこれと同じ生成 (カーネル・エディタ・デスクトップ、gem 3 つ、platform linux) を `tmp/regress/spinel/` に書く。C のコンパイルはしない (それは CI の build-linux (spinel) の受け持ちのまま)。
- `rake regress:full` の 2 番目 (`regress` の次) に入れた。`rake regress` には入れていない。

## 2. 作り

- rakelib/spinel.rake: `spinel:gen` の中身を `spinel_compiler_dir` (コンパイラの在りか、無ければ取って作る) と `spinel_gen_programs(bin, gen_dir:, stage_dir:, kernel:, editor:, desktop:, fft:, platform:)` (プログラムごとのコマンドの列を返す。gem の写しは stage_dir へ) に分けた。`spinel:gen` は gen_dir = SPINEL_GEN_DIR、stage_dir = SPINEL_SRC_DIR で今までどおり。
- tool/spinel/gen_kernel_combined.rb・gen_app_combined.rb: `fmrb_const_generated.rb` を書く先を固定の gen/ から「出力と同じディレクトリ」にした。普段の呼び出しは出力が gen/ なので変わらない。カーネルの方は毎回この定数のファイルを書き直すので、これを分けないと gen/ の時刻が動く。
- rakelib/regress.rake の `regress:spinel`: 書く先も gem の写しも `tmp/regress/spinel/` (毎回空から)。つなぎ合わせ (速い) を順に流してから、コンパイラを 6 つ同時に流す (つなぎ合わせは定数のファイルを共有するので並べない)。プログラムごとの log をそこに残し、落ちたら拒否の行を 20 行まで出す。
- gem の写しの `cp` の行は出さないようにした (`spinel:gen` の出力から 7 行減るだけ)。

## 3. 確認

- 時間: 通るとき 18.1〜19.6 秒 (要約の行は 17.9 秒)、落ちるとき 18.8 秒。コンパイラを並べたので、順に流す CI より短い (user 時間は 32 秒)。
- ビルドの生成物に触らないこと: `main/prebuild_scripts/spinel/` の下 40 ファイルの mtime と sha256 を、流す前と `regress:spinel` を 2 回流したあとで比べ、全部同じ。3 回目 (下の落ちる確認) のあとの差は、確認のために手で戻した fmrb_app_base_spinel.rb の mtime だけ (中身は同じ)。
- 生成物の中身: tmp/regress/spinel の C は gen/ の C と、`#line` のパスと、gen/ が esp32 の構成で作られていた所 (PLATFORM・BOARD) だけが違う。
- `spinel:gen` が変わっていないこと: HEAD の worktree で、全部 Spinel の構成の `spinel:gen` を直す前と後で流し、gen/ の中身の差は BUILD_DATE (作った時刻) だけ、gem の写しも同じ、流れたコマンドも `cp` の行を除いて同じ。
- 2026-11-07 の失敗を拾うこと: e3ab8127 の fmrb_app_base_spinel.rb の差分を逆にあてて流すと、`FAIL spinel system_desktop_combined` と次の行で落ちた (battery_level・power・linux_settings 2 か所・wifi_networks・notices_gen・notices・bt_devices の 8 件と POWER_ITEMS の警告、`8 refusals, nothing written`)。CI の 9 件との差は linux_keyboard で、e3ab8127 は呼び出し側を `#:spinel-strip-begin/end` で外したので、Spinel 側だけを戻しても出ない。確認のあと `git checkout` で戻した。
- `rake regress`: host 21 suites 全通過、描画の記録 213 件全通過。
- `rake regress:full` を通しては流していない (34 分ほどかかり、足したのは独立した部分 1 つなので)。
