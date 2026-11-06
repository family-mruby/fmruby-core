# 公開のブラウザ版に mruby-checkpoint を入れる

> 状態: 検収済 | 更新: 2026-11-06 | 2026-11-06 の決まり (全部の構成に入れる、まず公開のブラウザ版、D4 = 測ってから) に従い、同じコミットの公開のビルドと gem 入りのビルドを測った。wasm は +382,091 B (+6.1%、gzip で +149,150 B)、起動の時間は差なし、普段のアプリの VM は +56 B、運べるアプリの基準は 87,696 B (表を使う)。gem を公開のビルド (既定) に入れ、測るための別のビルド `FMRB_WASM_CHECKPOINT=1` の口と、公開の道具が断る分岐を片付けた。`regress:checkpoint` のブラウザ版の場面 (前の `WEB=1`) は既定で流すようにした (`WEB=0` で飛ばす)

計画は `doc/checkpoint/plan.md` (2026-11-06 の決まったこと)。前の測定は `report/f4.md` の 4.2 (gem だけ、表の前) と `report/ff.md` (基準の表)。

## 1. 測ったもの (D4)

同じコミット (468c4d8c) で 2 つ作った。公開のビルド (`rake spinel:gen wasm:mruby wasm:dist`、`wasm/build`) と、
gem 入りの別のビルド (`FMRB_WASM_CHECKPOINT=1 rake wasm:mruby wasm:dist`、`wasm/build-ckpt`)。Release、emcc 6.0.6。

### 1.1 大きさ (`wasm:dist` の中身)

| 何を | 公開のビルド (gem なし) | gem 入り | 差 |
|---|---:|---:|---:|
| `core_web.wasm` | 6,236,069 B | 6,618,160 B | +382,091 B (+6.1%) |
| 同 gzip -9 | 2,631,064 B | 2,780,214 B | +149,150 B (+5.7%) |
| `core_web.js` | 179,036 B | 179,475 B | +439 B |
| `core_web.data` | 4,254,444 B | 4,254,444 B | 0 |
| dist 全体 | 10,843,465 B | 11,225,995 B | +382,530 B (+3.5%) |

- F4 の測定 (+128 KB、gzip +47 KB) より大きいのは、Ff の基準の表 (名前・メソッドの一覧・クラスの写し・irep のハッシュ、
  `fmrb_checkpoint_table.c`) がファームに入ったため。表は VM ごとの費用を約 230 KB 減らす代わりに、ファームに 1 回だけ載る。
- 片付けた後の公開のビルド (gem 入り、`family-mruby-wasm` の名前) は `core_web.wasm` 6,618,194 B で、上の gem 入りと
  34 B しか違わない (ビルドの名前とパスの文字列)。同じ形のビルドになったことの確認。

### 1.2 起動の時間

ヘッドレスの Chromium (Playwright の Linux のもの)、自分の port の開発サーバ (`rake wasm:serve DIST=1`)、localhost、
`?autostart=1`、毎回空のプロファイル (キャッシュなし)。Chromium を起こしてからページのログに `Boot complete` が出るまで
(Chromium の起動を含む)。交互に 3 回ずつ。

| 公開のビルド (gem なし) | gem 入り |
|---|---|
| 5,103 / 4,948 / 4,958 ms | 4,995 / 5,000 / 4,935 ms |

差なし (揺れの中)。localhost なので回線の分 (gzip で +149 KB) は入っていない。遅い回線ではその分だけ伸びる
(1 Mbps で約 1.2 秒、10 Mbps で約 0.1 秒)。

### 1.3 VM のプール

`?app=` でアプリを 1 つ起動し、ログの `VM pool after compile` (アプリの VM、1.5 MB のプール) を比べた。

| アプリ | gem なし used / peak | gem 入り used / peak | 差 |
|---|---|---|---|
| kamon (普段のアプリ、movable でない) | 277,576 / 376,096 | 277,632 / 376,152 | +56 B |
| pub_demo (movable でない) | 255,480 / 283,928 | 255,536 / 283,984 | +56 B |
| ckpt_counter (`movable = true`) | 253,664 / 284,912 | 343,064 / 508,912 | +89,400 B (peak +224,000 B) |

- 普段のアプリは 1 VM +56 B (gem のモジュールの準備と `mrb_state` の hook の欄)。
- 運べるアプリの基準 (ログの `ckpt:` の行): 名前 3,578 個、表を使い (表に無い名前は `V:Asterism@@profile_icon` の 1 つ)、
  プールの 87,696 B、作る間に一時的に 174,944 B 多く要る。Ff の表 (322,248 → 90,176 B) と同じ水準。
  部分ごとは `ptrs 14312, obj_idx 10448, func_idx 3864, keep 12056, ireps 6116, irep_idx 6116, slots 7569` ほか。
- 標準構成のカーネル・デスクトップ・エディタ・Shell は Spinel で、mruby の VM を持たないので変わらない。
- `regress:run` (VM のプールの基準) は sim の道具で、ブラウザ版の場面は無い。ブラウザ版の普段の使い方は上のアプリの起動と
  `regress:screens` の web の場面 (2 章) で見た。

## 2. 入れ方と片付けたもの

gem 入りの形 (F4b で作った別のビルドの中身) をそのまま既定にした。

- `lib/add/family_mruby_wasm.rb`: `MRB_VM_SAFEPOINT_HOOK`・`FMRB_HAVE_CHECKPOINT` と gem `mruby-checkpoint` を常に。
  ビルドの名前は `family-mruby-wasm` の 1 つだけ。
- `wasm/CMakeLists.txt`: `FMRB_WASM_CHECKPOINT` の option を外し、`fmrb_app_checkpoint.c` の define・gem のヘッダと
  基準の表 (`fmrb_ckpt_table`、emcc で作った表の作り手を node で動かす) を常に。
- `hosted/hosted.cmake`: `MRB_VM_SAFEPOINT_HOOK` (ABI の定義) を hosted の両方 (ネイティブ版・ブラウザ版) に。
- `rakelib/wasm.rake`: `FMRB_WASM_CHECKPOINT` の印、別の libmruby の名前 (`family-mruby-wasm-ckpt`)・別の build の場所
  (`wasm/build-ckpt`) と、`wasm:site`・`wasm:deploy` が断る分岐を外した。`FMRB_WASM_BUILD_DIR` (同じコミットの比べる
  ためのビルドを横に置く口) は残した。
- `tools/regress/checkpoint_web.rb`: 公開のビルドの `wasm/build/dist` を使い、作るのは `rake wasm:mruby wasm:dist`。

別のビルドを残す理由は無い (中身が公開のビルドと同じになった) ので消した。手元に `wasm/build-ckpt/` や
`components/picoruby-esp32/picoruby/build/family-mruby-wasm-ckpt/` が残っていれば、もう使われない (消してよい)。

基準の表が公開のビルドでも作られ・使われることを確認した: `wasm/build/ckpt_table/fmrb_checkpoint_table.c` ができ、
公開のビルドで `ckpt_counter` を起動すると `table: used with differences (1 names of its own, 0 missing, ...)`。

## 3. 確認

| 何を | 結果 |
|---|---|
| `rake regress` | ホストの試験 21/21、描画の記録 205/205 |
| `rake regress:screens ONLY=web` | 5/6。落ちた `editor_type` は基準の PNG が古いためで、この変更とは関係ない (下) |
| エディタの 1 打鍵 | `editor_type` の場面でエディタを開いて `a` を打ち、本文に出た (画面で確認した) |
| `regress:checkpoint WEB=1` (この変更の後、公開のビルドで) | 全部 ok (標準・全部 mruby・web。6.2 分)。web: カウンタをブラウザ版へ移して戻す (どちらも次の 1 歩から続く。運ぶ 31・33 ms、止めていた 79・65 ms)、`ckpt_calls` は `linux_settings` が足りないと出して移る、`ckpt_requires` は止まる前に断られる、`--hold 1` は A で続きブラウザ版の写しは止められる |
| 既定の `regress:checkpoint` (`WEB` なし、`ONLY=standard NO_BUILD=1`) | 道具に `--web` が渡ることは確認した。2 回流したが、どちらも同じ時間に別の `regress:checkpoint` (別の木から) が流れており、run dir (`/tmp/fmrb-native-ckpt-<uid>`)・debug の port 5585/5586・ルータのコンテナを取り合って落ちた (ネイティブ版どうしの `asterism late` も落ちた)。中身は上の `WEB=1` の通しと同じ |

- `editor_type` の差 (216,206)-(243,213) は、エディタの状態行の `[A]` が 9 画素左にあり、右下に大きさを変える印 (三角)
  が出ていること。2026-10-29 の `bcf2ebb8` (エディタが右下の角を印に空ける) で retro・modern・native の基準は
  `c0cd675f` で直したが、web の基準 (`test/regress/screens/baseline/web/editor_type.png`、2026-10-26) は直されていない。
  基準の更新は人が見てからなので、ここでは直していない。`rake regress:screens:update ONLY=web` で受け入れるかは親が決める。

## 4. `regress:checkpoint` の `WEB=1`

F4 の 8 は「公開のビルドの判断が出るまで入れない」だった。判断が出て gem が公開のビルドに入ったので、**既定で流す**ようにした
(`rake regress:checkpoint` も `regress:full` の `SIM=1` も。`WEB=0` で飛ばす。`BOARD_ONLY=1` のときは流さない)。

流す時間 (この PC、作り直しが少ないとき。`regress:checkpoint WEB=1`、全部で 6.2 分):

| 部分 | 時間 |
|---|---:|
| ネイティブ版を作る (2 構成、差分だけ) | 約 65 秒 |
| ブラウザ版を作る (`spinel:gen`・`wasm:mruby`・`wasm:dist`、差分だけ) | 約 67 秒 |
| 標準の構成の場面 | 100 秒 |
| 全部 mruby の構成の場面 | 97 秒 |
| ブラウザ版の場面 | 44 秒 |

- ブラウザ版の分は約 1.9 分 (作る 67 秒 + 流す 44 秒) で、ブラウザ版なしの約 4.3 分に対して +44%。何も作っていない木からだと
  ブラウザ版を作るのに 5 分ほどかかる (F4 の 8)。
- 入れた理由: 公開のブラウザ版が運ぶ相手になったので、ブラウザ版 ⇔ ネイティブ版 (32 bit ⇔ 64 bit、Asterism の ws/) は
  普段の確認で守るべき道になった。sim の場面 (`SIM=1`) より安い。自前の開発サーバ (8067)・自前のヘッドレスの Chromium・
  自前のルータを使い、人の開いているページには触らない。
- 代わりに要るもの: emsdk (`~/emsdk/emsdk_env.sh`) と Linux の Chromium (`FMRB_CHROME`、`/usr/bin`、Playwright のもの)。
  無ければ「web: ...」で落ちる (黙って飛ばさない)。その環境では `WEB=0` で流す。
- `wasm/build` (人の開発サーバが読むもの) を作り直す。`regress:screens` の web と同じ。

## 5. 配置 (公開) の手順

配置は親が行う。この worktree では作っただけで、公開先には触っていない。

1. emsdk を入れたシェルで (`source ~/emsdk/emsdk_env.sh`)、このコミットを取り込んだ木で:
   `rake spinel:gen && rake wasm:mruby && rake wasm:site` (`wasm:site` は `wasm:dist` → `wasm:web`・`wasm:scan` を通る)。
   site の checkout は `tmp/family-mruby.github.io` か family-mruby の隣のもの、または `FMRB_SITE_DIR=`。site の checkout は
   先に fetch して最新にする。
2. site のリポジトリで `docs/studio/` の差分 (`core_web.wasm` が約 +382 KB、`core_web.js`、`index.html` の版の印) をコミットし、
   push すると deploy の workflow が https://family-mruby.github.io/studio/ に出す。
3. 注意:
   - `wasm:mruby` は明示的に流す。libmruby に gem が入る (前の `family-mruby-wasm` の libmruby には gem が無い。
     `wasm:mrb` は lib/add の方が新しいときに作り直すが、時刻に頼らない)。`rake clean_all` は wasm の libmruby も消す。
   - アプリの Ruby を変えた後は `spinel:gen`・`wasm:mrb` (wasm:web が呼ぶ) で生成物が作り直されたかを見る。
   - 前に `FMRB_WASM_CHECKPOINT=1` で作った `wasm/build-ckpt` を `FMRB_WASM_BUILD_DIR` で指して出さない (古いコミット)。
   - 配る量は gzip で約 +149 KB。GitHub Pages は自分で圧縮する。
   - 公開のページには転送のアプリの設定 (`/home/asterism/receive.toml`) が無いので、何も受け取らない (2026-11-04 の決まり)。
     移す相手になるのは、使う人が設定を置いたときだけ。

## 6. サブの仮決め

1. 別のビルドの口 (`FMRB_WASM_CHECKPOINT`) は残さず消した。中身が公開のビルドと同じで、同じ形のビルドが 2 つ残る
   だけになるため。比べるためのビルドは `FMRB_WASM_BUILD_DIR` で作れる。
2. 起動の時間は Chromium を起こすところから測った (F4 の「load から」に近いが、Chromium の起動の分を含む。両方に同じ
   だけ入るので差には影響しない)。
3. ブラウザ版の場面を既定に入れた (4 章)。Chromium や emsdk が無い環境で黙って飛ばす形にはせず、落ちて `WEB=0` を案内する
   形 (`web_check!` の今の振る舞い) のままにした。
4. `regress:screens` の web の `editor_type` の基準が古い件 (3 章) は、この変更と関係ないので基準を直していない。

## 7. 気づいたこと

- `regress:checkpoint` は木が違っても、run dir (`/tmp/fmrb-native-ckpt-<uid>`・`-b-`・`-c-`)、debug の port (5585・5586)、
  ルータのコンテナ名 (`fmrb_regress_ckpt_zenohd`) と port が同じ。2 つの木で同時に流すと互いの状態の file を消し合い、
  場面が落ちるうえ、片方のネイティブ版が止められずに残る (この作業で 1 つ残り、port 5585 を持ったままになった)。
  worktree で並べて流すなら、port と run dir を木ごとに変える口が要る。今は「同時に流さない」(`regress:run`・
  `regress:screens` と同じ扱い) を守る。
