# rake regress:full を短くする (確認は減らさない)

> 状態: 完了 | 更新: 2026-11-04 | 部分ごとに流すと 50.9 分 (ブラウザ版を除く) だった `rake regress:full` を、同じ確認のまま 33.3 分にした。sim の同じ構成を 1 回だけ作って部分の間で使い回す (9 回 → 4 回)、sim の種類の切り替えを減らす順、コンポジタの確認を 2 つの構成で並べる。人が開いているブラウザ版のページがあるときは、画面の比較のブラウザ版だけを外して流す

## 1. 測ったもの

同じ木 (Part 1 の後、コミット 7119ebef の上) で、部分を 1 つずつ流した時間 (前) と、`rake regress:full` 1 回 (後)。ブラウザ版の画面 (`regress:screens` の web) は、人がわざと開いているページ (port 8016 と `tools/fmrb_web.rb` のブラウザ) があるので、前も後も流していない。

### 1.1 前 (部分ごと)

| 部分 | 時間 | 内訳 |
|---|---|---|
| regress | 0.5 分 | ホストの試験 26 秒、描画の記録 1 秒 |
| size | 4.2 分 | ESP-IDF のビルド NARYAv3 117 秒・TAB5 115 秒 |
| run | 4.75 分 | sim を作る 80 秒 × 2、sim 55・60 秒 (固定の待ち `SETTLE` 3 秒 × 7 を含む) |
| screens (retro・modern・native・native-keys) | 7.2 分 | sim を作る 80 秒 × 2、場面 70・77 秒、ネイティブ版を作る 62 秒、場面 20・29 秒 |
| live | 2.5 分 | sim を作る 79 秒、場面 65 秒 |
| wl | 19.5 分 | 標準を作る 70 秒、確認 8.8 分、全部 mruby を作る 47 秒、確認 8.8 分 |
| checkpoint SIM=1 | 12.2 分 | ネイティブ版を作る 55 秒、場面 58 秒 × 2、sim を作る 81・81・62・62 秒、sim の場面 78 秒 × 4 |
| 合計 | 50.9 分 | |

### 1.2 重なっていた仕事

- **sim のビルドが 9 回**。どの部分も始めに `clean_all` して `rake build:linux` する。同じ設定 (targets.rb の固定の設定) のビルドが、標準の Retro は run・screens・checkpoint で 3 回、標準の Modern は run・screens・live・checkpoint で 4 回。違うのは checkpoint の全部 mruby の 2 つだけ。1 回 60〜80 秒。
- **sim の種類の切り替え**。graphics-audio は新しい画面の大きさを次の起動で当てるので、種類を変えた直後の 1 回目は前の大きさで上がるか上がらず、起こし直す (rg3b.md、1 回 17 秒ほど)。前の順では run (retro → modern)、screens (retro → modern)、live (modern)、checkpoint (標準 retro → 標準 modern → mruby retro → mruby modern) で 8 回切り替わる。
- **コンポジタの確認は 2 つの構成で同じものを 10 個ずつ順に**。確認は入れ物の中で動き、構成ごとに入れ物の名前・実行の場所・結果の場所が分かれている。共有するのはホストに出す debug の port (構成で同じ番号) だけだった。
- ネイティブ版のビルドは screens と checkpoint の 2 回あるが、どちらも差分のビルド (libmruby は `keep_built!` で戻る) で、checkpoint の分は 1 分弱。触っていない。
- size の ESP-IDF のビルド 2 つは、ほかに使う部分が無い。触っていない。
- 固定の待ち: run の `SETTLE` (VM プールを測る前の落ち着き) は数そのものに効くので変えない。checkpoint・screens は既にログか画面で待っている。wl の確認の中の固定の待ち (0.15〜1.5 秒、1 構成で 1〜2 分ぶん) は、合成の結果を撮る前の落ち着きで、確認ごとに見直す量に見合わないので触っていない。

### 1.3 並べられないもの

- sim の部分どうし (run・screens・live・checkpoint): 3 つのコンテナと build/ が 1 組。
- size と sim の部分: 同じ build/ と、退避する `flash/etc` の生成物・Spinel の生成物。
- wl と sim の部分: `native:wl` が `rake setup`・Spinel の生成物 (無ければ止まる)・`native:etc` を使い、sim の部分はそれらを退避・作り直す。また wl の確認は固定の待ちが多く、ビルドと並べると揺れるおそれがある。wl の中の 2 つの構成どうしだけを並べた。

## 2. 変えたこと

| 何を | どう | 確認が減らない理由 |
|---|---|---|
| sim のビルドを使い回す (`tools/regress/build_tree.rb`) | `regress:full` が `REGRESS_BUILD_CACHE` (`tmp/regress/build_cache/`) を置く。`sim_build!(env)` は clean_all の後、同じ鍵のビルドがあれば移して戻し (作らない)、無ければ作る。部分は sim を止めてから `keep_for_later!` で build/・sdkconfig・libmruby (picoruby/build)・flash/etc の生成物を置き場へ移す。鍵は作る設定とそのときのソース (HEAD、`git diff HEAD`、追跡していないファイルの名前・大きさ・時刻、`.env`、`MRUBY_CHECKPOINT_DIR` の HEAD と差分) | 使い回すのは同じ設定・同じソースから作ったものだけ。部分ごとの起動・場面・比べ方は変えていない。部分を 1 つずつ流すときは置き場が無く、毎回作る (今までどおり) |
| 種類の順 (`SimRun.current_first`) | `SimRun.up` が上がった大きさから種類を `tmp/regress/sim_family` に書き、run・screens・checkpoint はその種類から流す。checkpoint の sim の場面は種類ごとにまとめた (種類 → 構成) | 流す組は同じ。順と出力の行の順だけが変わる (checkpoint の sim の行は種類ごと) |
| wl の 2 つの構成を並べる (`rakelib/regress.rake`、`test/native/wl/*.rb`) | 2 つのビルドは順に (`native/build-wl/mruby` の libmruby を共有するので)、確認は構成ごとのスレッドで並べる。全部 mruby の確認は debug の port を 100 上にずらす。出力は `tmp/wl_check/checks-<構成>.log` に取り、両方が終わってから続けて出す。`WL_SERIAL=1` で今までどおり 1 つずつ | 確認の台本は同じ。並べても 1 つの確認の時間は変わらなかった (wl_check の headless: 1 つずつ 1:48、並べて 1:48) |
| 人が開いているブラウザ版のページ (`tools/regress/screens.rb --skip-busy-web`) | `regress:full` の中では、port 8016 か `tools/fmrb_web.rb` のブラウザが使われていれば、ブラウザ版だけを外して (作らず、流さない) ほかの構成を流し、`web: SKIPPED (...)` と要約に `web SKIPPED` を出す。部分を 1 つで流すときは今までどおり断る | 外したことが要約に出る。前は screens 全体が断られて、sim の画面も見ていなかった |
| `regress:full` の要約 | 最後に部分ごとの時間を 1 行 | — |

## 3. 後 (rake regress:full 1 回)

```
regress:full: 33.3 min (regress 0.4, size 4.1, run 4.3, screens 4.1, live 1.1, wl 9.8, checkpoint 9.4)
regress:full: ok
```

| 部分 | 前 | 後 | 差 | 効いたもの |
|---|---|---|---|---|
| regress | 0.5 | 0.4 | | |
| size | 4.2 | 4.1 | | |
| run | 4.75 | 4.3 | −0.45 | Modern から (sim 43 秒。切り替えなし) |
| screens | 7.2 | 4.1 | −3.1 | sim 2 つとも使い回し (`build cached`) |
| live | 2.5 | 1.1 | −1.4 | 使い回し |
| wl | 19.5 | 9.8 | −9.7 | 2 つの構成の確認を並べた |
| checkpoint SIM=1 | 12.2 | 9.4 | −2.8 | 標準の 2 つを使い回し、種類ごとにまとめて切り替えは 1 回 (sim の場面 61・61・78・61 秒) |
| 合計 | 50.9 | 33.3 | −17.6 (−35%) | rake を含む実時間は 34.3 分 |

- sim のビルドは 9 回から 4 回 (run の 2 つと checkpoint の全部 mruby の 2 つ)。置き場は最大 2 つ (1 つ 0.9 GB ほど) で、終わりに消える。
- 結果: 全部の部分が通った (screens 27/27、live 16/16、wl 両構成の 10 個ずつ、checkpoint はネイティブ版 2 つと sim 4 つ)。最後の build/ は TAB5 (RISC-V)。
- ブラウザ版の画面 (前に 1 分ほどのビルドと 1〜2 分の場面) は、ページが開いている間は外れる。それを閉じたあとに `rake regress:screens ONLY=web` で流す。

## 4. 気づき

- 「部分を 1 つずつでも流せる」ことと「通しで重ねない」ことは、置き場を通しのときだけ置く形で両立した。置き場の鍵にソースを入れたので、通しの途中でファイルを直しても古いビルドは使われない (作り直す)。
- 前の測り方の 1 回目で、wl の標準の apps の「終わり」(Exit で Linux の Window が 5 秒で閉じず 1 つ残る) が 1 回落ちた。並べた後の通しと、それまでの wl では出ていない。この段の変更 (port・並べ方) の前の 1 つずつの回で出たので、並べたことによるものではない。続けて出るなら別に追う。
- checkpoint の F3 の「移動中」の札が消えずに残ることが `SIM=1` の通しで 1 回あった (札の画面を消しても合成し直していなかった)。Part 1 で直した (checkpoint の report/f4.md の 11)。

## 5. 残したもの (やらなかった)

- sim のコンテナを部分の間で起こしたままにする: 部分ごとに sim を新しく起こすこと自体を確認の前提にしている (起動直後のデスクトップと画素で比べる、VM プールの数) ので、使い回すと確認が変わる。
- wl と sim の部分を並べる: 1.3 のとおり共有するものがある。
- run の固定の待ち: 数の揺れに効く (rg3b.md・rg_gc.md)。
