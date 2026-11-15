# 回帰の道具を壊れにくくする

> 状態: 完了 | 更新: 2026-11-16 | 作りの変化と関係ない所で道具が落ちていた 4 つ (ブラウザ版のランチャーの何番目かでアプリを開く、一覧の画面と VM プールがアプリの数で動く、port の重なり、Services の GC の時間切れ) を直した。ブラウザ版にもパスでアプリを開く口、一覧を写す場面に回帰専用の決まった組、port は空きを取って印を残す、既知の揺れは 1 回だけやり直す。使い方の正は usage.md の 7.2・9.4・13 章。元の課題は doc/reference/TODO.md の「回帰の道具を壊れにくくする」

## 1. 壊れやすかった所 (2026-11-15〜16 の夜)

| 何が | 起きたこと | 落ちた道具 |
|---|---|---|
| ブラウザ版はランチャーの何番目かでアプリを開く (`open_app(path, web_index: 13)`) | アプリを足すと並びがずれ、kamon の場面で IMU が開いた | screens (web kamon) |
| ランチャーとファイルマネージャの場面が本物の `flash/app` を写す | アプリを 1 本 (分類を 1 つ) 足すたびに 7 枚の基準が変わった (launcher ×3、fmgr_remember ×3、keys_fmgr_remember) | screens |
| デスクトップの VM プールがランチャーの項目の数で動く | 6 項目で 1.3 KB 増え、`sim_baseline.json` の更新が要った | run |
| 開発サーバの port 8016 が固定 | 並行の作業と重なり、ブラウザ版が流せない・止まる | screens (web) |
| 控えの前の GC にサービスホストが間に合わない | `gc: Services timeout` で regress:run が落ちる (流し直すと通る) | run |

ほかに見つけたもの: sim の `flash/tmp/file_manager.state.toml` は sim の外 (checkout) に残るので、ファイルマネージャの場面は前に誰かが閉じた場所の影響を受けうる作りだった (起動時に /tmp が片付くので実際には効いていなかった)。決まった組の状態を毎回置くようにしたので、これも気にしなくてよくなった。

`tmp/regress` に残っていたログ (screens・live・checkpoint・sim) で、上の GC のほかに揺れの跡は見つからなかった。

## 2. 変えたこと

### 2.1 ブラウザ版でもパスでアプリを開く

- `wasm/backend/launch_wasm.c`: 共有メモリの口 (パス・依頼の番号・答えの番号・pid・誤り)。ページが書き、入力の task (`input_wasm_task`、10 ms ごと) が `fmrb_app_spawn_app` を呼んで答える (debugd・devctl と同じ関数)。
- `wasm/web/main.js`: drive の命令 `launch` (口に書いて答えを待つ)。
- `tools/fmrb_web.rb launch PATH` (`{"pid": N}` を出す)。
- 回帰: `WebDriver#launch`、`Session#launch` と `helpers.rb` の `open_app(path)` はどの構成でもパスで開く。`web_index` は無くなった。

### 2.2 一覧を写す場面は決まった組 (`tools/regress/fixtures.rb`)

- **ランチャー**: `test/regress/fixtures/launcher_index` (本物のアプリ 12 本の索引キャッシュ)。デスクトップは起動時にキャッシュがあれば /app を歩かずにそれを使う (launcher.rb `load_launcher_cache`)。ファームにもアプリにも試験用の分岐は足していない。置く所:
  - sim: checkout の `flash/var/cache/launcher_index` (起動の前に置き、構成の後に消す。次に誰かが起こす sim は /app を歩き直す)。regress:run も同じ。
  - ネイティブ版: その回のキャッシュの場所。
  - ブラウザ版: ページの seed (下)。`/flash/var/cache` は読み直しで消えるので、page の preRun でそのタブの起動のたびに書く。
- **ブラウザ版の seed**: drive の命令 `seed` がファイルをタブの sessionStorage に持ち、`?drive=1` のページは preRun (バンドルの展開の前、`/flash/home` を置くのと同じ時点) でそれを書く。`fmrb_web.rb seed LOCAL PATH...` / `seed --clear`。/home と /app/usr は IndexedDB から読み戻されるので対象外。
- **ファイルマネージャ**: 場面の頭で `s.seed_fm` が `/tmp/regress_fm` の木 (alpha・beta・gamma.txt、alpha の下に hello.app.rb・notes.txt・picture.bmp・sub) と状態 (`/tmp/file_manager.state.toml`、`dir = "/tmp/regress_fm"`) を置く。場面は alpha に入って閉じ、開き直して alpha が出ることを撮る (「覚えている」の確認はそのまま)。sim は構成の後に木を消し、状態を前のものに戻す。
- 空きメモリの欄は sim・ネイティブ版では常に `---KB`、ブラウザ版には無いので隠していない。時計は前から全場面で隠している。

### 2.3 port は空きを取り、印を残す (`Tree.port`)

- 既定の port (共有 checkout の固定値、worktree は木の組) が空いていればそのまま。答えるものがいる・bind できない・ほかのプロセスの印がある、のどれかなら 24000〜24999 の空きを取り、`regress: port 8016 (screens_web) is taken by ...; using 24000` と stderr に言う。
- 取った port には印 `/tmp/fmrb-regress-ports-<uid>/<port>.lock` (O_EXCL で作る。中身は pid・用途・木)。そのプロセスが終わると消える (fork した子の終わりでは消さない)。pid の無い印は古いものとして取り直す。
- 対象は `Tree.port` を通るもの全部: screens のブラウザ版 (8016)・ネイティブ版 (5575)、checkpoint のネイティブ版 3 つ・ルータ 2 つ・ブラウザ版、transfer の同じ組。
- 確認: 8016 を別のプロセスで塞いで 2 つ同時に取ると、24000 と 24001 に分かれ、終わると印が消えた。

### 2.4 既知の揺れは 1 回だけやり直す

- regress:run の控えの前の GC で間に合わないアプリがあれば、もう 1 回 gc を頼む。通ればそのまま進み、要約の最後に `1 retried: gc at idle: Services timeout, retried ok` のように出る。2 回目も間に合わなければ今までどおり落ちる。今回の通しでは起きなかった (要約に retried の文字なし)。

### 2.5 画素の一致でなく「開いた」で見る場面

- 見直した: 今の screens の場面はどれも見た目そのものが確認の中身 (デスクトップ・メニュー・Config・ランチャー・エディタの 1 打鍵・picoruby の図形と日本語・kamon・walk_scroller・ファイルマネージャ)。一覧の中身で揺れていた 2 つは決まった組で揺れなくなったので、画素のまま残した。checkpoint の画面の比較は同じ回の前後の比較で、基準の絵を持たない。変えた場面は無い。

## 3. 前と後

| 変化 | 前 | 後 |
|---|---|---|
| `flash/app` にアプリを 1 本足す | screens 7 枚 + sim_baseline の更新、ブラウザ版の `web_index` の直し。落ちたものを調べて流し直す | どれも動かない (決まった組。索引に載っているアプリの見た目を変えたときだけ変わる) |
| ブラウザ版でアプリを開く | ランチャーで右キー N 回 | `launch PATH` (1 往復) |
| 8016 が塞がっている | 止まる (`port 8016 is in use`)、または `--skip-busy-web` でブラウザ版を外す | 空いている port で流す |
| Services の GC の揺れ | regress:run が落ちる → 流し直し | 1 回やり直し、要約に出る |
| デスクトップの VM プール (idle) | 815 KB (45 項目) | 802 KB (12 項目、固定) |

今回の通し (2026-11-16、共有 checkout、WSL2):

| 道具 | 結果 | 時間 |
|---|---|---|
| `rake regress` | 29 suites・229 runs 通過 | 37 秒 |
| `rake regress:screens` | 26 通過 / 7 落ち (全部 2.2 の決まった組に変えた場面の基準。絵を見て update) | 11.3 分 |
| `rake regress:run` | ok (desktop が 13 KB 小さく動いた。基準を update) | 5.4 分 |
| `rake regress:live` | 16 / 16 | 2.8 分 |
| `rake regress:checkpoint` | standard・mruby・web とも ok | 7.3 分 |

更新した基準 (別のコミット):

- `test/regress/screens/baseline/{modern,retro,web}/launcher.png`: 一覧が決まった組になった (組込み 3 つ + Avatar から NSF Player までが見え、残りはスクロールの下)。
- `test/regress/screens/baseline/{modern,retro,native}/fmgr_remember.png`、`native-keys/keys_fmgr_remember.png`: 開き直した先が `/app` から `/tmp/regress_fm/alpha` になった。
- `test/regress/sim_baseline.json`: デスクトップの VM プールが 13 KB ほど小さい (項目が 45 から 12 に)。ほかの数は幅の中の揺れ。

## 4. 省ける時間の見込み

- アプリを足す変更 1 回ごと: 落ちた screens の差分を読む (5〜10 分) + 基準の更新とコミット (5 分) + ブラウザ版の添字の直しと流し直し (screens ONLY=web 3.5 分、全部なら 8〜11 分)。**1 回 15〜25 分**。この夜は SDK のアプリ (C・Rust・Zig・tour) で 2 回あった。
- port の重なり: 気づいて止めて流し直す **5〜10 分/回**。
- Services の GC の揺れ: regress:run の流し直し **5 分/回**。
- 道具を足した分の時間の増え: ブラウザ版の seed は reset の読み直しに乗るので 0、`launch` はランチャーを開くより速い。ファイルマネージャの場面はキーを 1 つ足しただけ。測れる増えは無い (screens の全体は前回 8〜9 分、今回 11.3 分だが、ビルドの時間の差がほとんどで、場面の時間は同じ程度)。

## 5. 残り・決めていないこと

- MCP の `web_*` ツールには launch を足していない (`fmrb_web.rb launch` は shell から使える)。足すかどうかは使う場面が出てから。→ 2026-11-16 ユーザの頼みで `web_app` (action `spawn`) を足した (family-mruby の tools/mcp)。
- regress:run の VM プールも決まった組で測るようにした。本物の一覧 (45 項目) の重さは測らなくなったが、項目 1 つあたりの重さが増えれば 12 項目でも出る。本物の数で測りたい場合は戻せる (`sim_run.rb` の `seed_launcher_cache` を外す) — ユーザが決めてよい。→ 2026-11-16 ユーザ: 代表的なものでよい。ランチャーは決まった組のまま、起動して測るアプリを代表の組 (エンジンごと・エディタ・ゲーム) に広げた (usage.md 7.2)。
- `test/native/` の確認 (wl・sdl・deb・net) の固定の port (15565 から) は `Tree.port` を通していない (今回の重なりは起きていない)。→ 2026-11-16 `Tree.free_port` で空きを取るようにした (usage.md 13 章)。コンテナ名と run dir は固定のまま。
