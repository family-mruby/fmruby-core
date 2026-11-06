# 回帰の道具を作業の木ごとに分ける (並行で流す)

> 状態: 完了 | 更新: 2026-11-06 | 共有 checkout と worktree から同時に `rake regress:checkpoint` を流すと、ネイティブ版の置き場・port・ルータの名前が同じで混ざって落ち、止め損ねが port 5585 を持ったまま残った。名前と port を木ごとに決まる値にし (`tools/regress/tree.rb`)、起こしたものは必ず止め、直下の道具は `FMRB_ROOT` で探すようにそろえた。使い方の正は usage.md の 13 章

## 1. 起きたこと (2026-11-06)

- 作業の木が違っても、`regress:checkpoint` のネイティブ版の run dir (`/tmp/fmrb-native-ckpt-<uid>` と `-b-`・`-c-`)、debug の port 5585〜5587、自前の Zenoh のルータのコンテナ名 `fmrb_regress_ckpt_zenohd` とその port (17457・17459) が同じだった。2 つの木から同時に流すと、片方のネイティブ版がもう片方の run dir で動き、ログが混ざって落ちた。止め損ねたネイティブ版が 5585 を持ったまま残り、どの木でも流せなくなった。
- worktree から流すと、直下の道具 (family-mruby の tools/) を木の 1 つ上で探す所が残っていた: `tools/regress/screens/drivers.rb` の `ROOT`、`sim_run.rb` の `ROOT`、`counterparts.rb` の `ROOT`・`CORE`、`test/native/` の確認の `TOOLS`。サブが毎回一時的に直していた。新しい木で `tmp/regress/screens/` が無いとネイティブ版のビルドのログを開けずに落ちた。
- 画面の比較 (web) が、もう無いブラウザを指す `wasm/build/drive_browser.pid` (10/27 の古い印) で、始まる前に止まった。

## 2. 直したこと

- **木ごとの名前** (`tools/regress/tree.rb`)。共有 checkout (`.git` がディレクトリ) は今までの名前のまま。ほかの木 (`.git` がファイルの worktree) は、実のパスの SHA-1 の先頭 6 桁 (ID) を run dir とコンテナ名に足し、port は木ごとの 8 個の組 (`20000 + 8 × slot`、slot は ID から 1〜400) から取る。対象は `regress:checkpoint` のネイティブ版 A・B・C と sim 用の置き場、ルータ (tcp・ws)、実機のルータの名前、ブラウザ版の開発サーバ、`regress:screens` のネイティブ版と開発サーバの既定の port。人のネイティブ版 (5565)、画面の比較の共有 checkout のもの (5575)、sim (5555)、コンポジタの確認 (15565〜) とは重ならない。
- **使う前に port の空きを確認する**。答えるものがいるか bind できなければ、port・持ち主 (`ss` で分かるとき)・理由を言って止まる。slot が別の木と重なったときもここで分かる。
- **止め損ねを残さない**。`regress:checkpoint` は今までの ensure (構成ごとの down、ルータの down) に加えて、at_exit で同じ木の run dir の pid の file が指す生きた fmruby-native (コマンド行で確認する) を TERM→KILL で止め、同じ木の名前のルータを消す。始めるときは今までどおり同じ木の前の回の残りを止める。`regress:screens` のネイティブ版は、同じ木の run dir で動いているものを「前の回の残り」として止めてから起こす (今までは止まっていた)。
- **`FMRB_ROOT`** を、直下の道具を探す全部の所で見る (上の 2 の所と `screens/image.rb`、`checkpoint.rb`、`test/native/` の wl・sdl・deb・net の確認、`rakelib/test.rake` の sim の煙試験、`rakelib/voice.rake`)。直下の道具が無ければ「worktree なら FMRB_ROOT を渡す」と言って止まる。
- **sim は流せないときに理由を言う**。sim の docker compose は `<ROOT>/fmruby-core` をマウントするので、それがこの木と違えば (worktree から共有の family-mruby を指したとき) 止まる。今までは黙って共有 checkout の sim を起こしていた。
- **古い印を消す**。`drive_browser.pid` の pid が生きていなければ消して進む。Windows の Chrome の印は、WSL 側の pid が消えても Chrome が動いていることがあるので、`fmrb_drive` のプロファイルの chrome.exe が無いことも powershell で確認する (聞けなければ動いているとみなす)。
- `regress:screens` の始めに `tmp/regress/screens/` を作る。

## 3. 確認

worktree (ID 3ec253、slot 180、port 21440〜21447) から、scratchpad に作った `FMRB_ROOT` (直下の tools/ の写しと、この木への symlink の fmruby-core) を渡して流した。一時的な直しは無し。

- `rake regress`: host 21 suites 通過、描画の記録 205 通過。
- `rake regress:checkpoint ONLY=standard WEB=0`: ok (3.6 分)。run dir は `/tmp/fmrb-native-ckpt-1000-3ec253` ほか、ルータは `fmrb_regress_ckpt_zenohd_3ec253` (21443・21444)。終わった後にネイティブ版もルータも残っていない。
- `rake regress:screens ONLY=native`: 7 通過 / 0 落ち (native 3/3、native-keys 4/4、1.9 分)。`tmp/regress/screens/` を消してから流した。
- 直下の道具が無いとき (`FMRB_ROOT` なしで worktree から): `regress: ... is a worktree of fmruby-core and has no root tools above it; give FMRB_ROOT=...` で止まる。`FMRB_ROOT` に共有の family-mruby を渡すと、sim は「compose がマウントするのはこの木でない」と言って止まる。
- port の空き: 木の A の port (21440) を別のプロセスが持っていると、`port 21440 is taken (ruby pid ...); another tree's port block may be this one (slot 180), or an earlier run left a program behind` で止まる。
- 共有 checkout と同じ形 (`.git` がディレクトリ) の木では、名前は `/tmp/fmrb-native-ckpt-<uid>`・5585・`fmrb_regress_ckpt_zenohd` のまま (tree.rb を写した仮の木で確認)。
- 古い印: 死んだ pid の印 (linux・windows) は消して進み、生きた pid の印では今までどおり止まる。

## 4. 並行と Ctrl+C

- **並行**: この worktree の写し (別のパス、ID f1a42d、slot 206、port 21648〜) を scratchpad に作り、その写し用の `FMRB_ROOT` を渡して、この木 (作るところから) と写し (`NO_BUILD=1`。写しの native/build の CMake はもとの木のパスを持つので作らない) から `rake regress:checkpoint ONLY=standard WEB=0` を同時に流した。両方 ok (2.5 分・1.6 分)。流れているあいだ、ルータは `fmrb_regress_ckpt_zenohd_3ec253` (21443・21444) と `_f1a42d` (21651・21652) の 2 つ、ネイティブ版はそれぞれの run dir で動いた。終わった後は何も残っていない。
- **Ctrl+C**: B が上がった後にプロセスグループへ SIGINT を 1 回送ると、ensure がネイティブ版 A・B とルータを止めて終わる (pid の file も残らない)。2 回続けて送ると ensure の途中で切れるが、at_exit がルータを消した (`checkpoint: removing the Zenoh router(s) left running`)。ネイティブ版も残らない。
- 注意: 非対話の shell の `&` で起こすと SIGINT が無視されたまま継がれ、Ctrl+C の代わりの SIGINT が届かない (試すときは SIGINT を既定に戻してから起こす)。端末の Ctrl+C では起きない。

## 5. 残したこと

- `regress:wl` ほか `test/native/` の確認 (wl・sdl・net の l2n) は固定の名前と port のまま (l2n の debug の port は共有 checkout の checkpoint と同じ 5585)。直下の道具を `FMRB_ROOT` で探すことだけそろえた。
- 直下の道具 `tools/fmrb_web.rb` の Windows の Chrome のプロファイル (`C:\Users\Public\fmrb_drive`) は木によらず 1 つなので、2 つの木から同時に `fmrb_web.rb up` (Windows の Chrome) は使えない。画面の比較のブラウザ版は印 (`wasm/build/drive_browser.pid`) が木ごとなので、ここだけ木をまたいで重なりうる。
- sim は 1 組のコンテナなので、木が違っても同時に 1 つ。
