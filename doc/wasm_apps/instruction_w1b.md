# W1b 指示書: WAMR を本体に組み込み、TinyGo のアプリを P4-Nano で動かす (worktree で並行)

> 親: plan.md の段 W1b と決定 (1〜18、「W1a の後に決まったこと」)。W1a の結果は report/w1a.md、試作は
> `tools/wamr_poc/` (WAMR 2.4.5 は `tools/wamr_poc/vendor/`)。殻は doc/app_boundary の W3 (report/w3.md、
> `main/app/fmrb_app_shell.c`)、境界のつなぎは W4a〜W4b-2 (`components/fmrb_common/include/fmrb_gen/` の `_wasm.inc`、
> `sdk/c/fmrb_boundary.h`)。時間の目安は doc/regression/usage.md 0 章。

## ユーザの希望 (2026-11-15)

- **今日中に、TinyGo で書いた簡単なアプリが実機 (P4-Nano、NARYAv4) で動く様子を見たい**。
- 段の順は W1b → W5 → W6 → W2 に変えた (W2 の P4 での計測は後回し)。この段では計測は最小限 (起動して動く・
  内蔵 RAM の静的な増分) でよい。
- 境界 (doc/app_boundary) は手を抜かない。こちらでは境界の規則や生成の道具を変えない。

## 作業場所 (並行のため)

本体の checkout では doc/app_boundary の W4b-3 (FmrbApp を境界に載せる) のサブが sim を使って動いている。この段は
worktree で行う:

```
git -C /home/kishima/fmrb/family-mruby/fmruby-core worktree add /home/kishima/fmrb/wt/w1b-core -b wt/w1b develop
```

- submodule (components/ の下) は本体の checkout から rsync で写す (`.git` は除く)。`.env`・`config/wifi.toml`・
  `flash_local/`・`hosted/vendor/`・`vendor/`・`tools/wamr_poc/vendor/` も写す (前の worktree と同じ。report/w4b_1.md の末尾)。
- 作業・ビルド・コミットは worktree の中 (`wt/w1b`)。本体の checkout・build/・sim・docker の fmruby_* コンテナ・
  MCP の sim/native/web・シリアルと flash には触らない (焼くのは親)。
- 使えるのは worktree の中の `rake build:linux`・`rake build:esp32` (`FMRB_HW_TARGET=NARYAv4`)・`rake regress`・
  `rake regress:spinel`・`tools/wamr_poc`。sim での確認は親が取り込んでから。

## やること

1. **WAMR を本体の部品にする**: classic インタプリタ、決定 1 の機能の組 (参照型・拡張された定数式)。ESP32-P4
   (RISC-V) と Linux (sim・ネイティブ版) でビルドが通ること。ブラウザ版はビルドが通れば良い (動かすのは後でよい)。
   ESP32-S3 (NARYAv3) には入れない (今は P4 と Linux だけ。S3 のビルドが壊れないこと)。W1a の土台の層を
   `fmrb_hal` の上に移す。メモリは決定 5 (アプリの枠から切り出す。WAMR の小さな共通のプールだけ起動時、または最初の
   wasm のアプリの起動時に PSRAM から)。**内蔵 RAM の静的な増分 0** (超えるなら理由と数字を書いて止まる)。
2. **アプリの種類を足す**: `.app.wasm` (と `.app.toml`) を起動できる。W3 の殻に乗せ、殻がフック (on_create・
   on_update・on_event に当たる wasm の export) を呼ぶ。import は W4a〜W4b-2 で生成した `_wasm.inc` (FmrbGfx・
   FmrbAudio・Log・時計など)。殻の関数の形は本体の checkout の W4b-3 で変わる可能性があるので、殻の中は最小限の
   変更にし、変えた所を report に正確に書く (親が取り込む)。
3. **TinyGo の SDK の最小の形とデモ**: `sdk/` の下に、生成した C のヘッダ (`sdk/c/fmrb_boundary.h`) に合わせた Go の
   宣言 (`//go:wasmimport`) を置く。手書きでよいが、生成のヘッダと名前・型を照合する小さな確認を付ける (生成に
   切り替えるのは W5)。デモは 1 本: 例えば四角が跳ね、キーやクリックで色が変わり、文字を出す。TinyGo の GC は
   決定 1 の既定 (解放する GC)。ビルドの手順を 1 つの rake か台本に (`~/toolchains/tinygo`)。
4. **C のアプリでも同じ経路を通す** (決定 4・5・12): `tools/wamr_poc/apps/c_boundary` 相当を本体で起動できること。
5. **P4 で動く準備**: `FMRB_HW_TARGET=NARYAv4 rake build:esp32` が通り、デモの `.app.wasm` が storage の
   `/app/demo/` (など、置き場は既存の決まりに合わせる) に入ること。焼くのは親。焼いたら確かめてほしいログの行
   (起動・wasm の読み込み・フックの呼び出し) を report に書く。

## 確かめること (worktree の中で、これだけ)

- `rake regress`・`rake regress:spinel` (Ruby のアプリが壊れていない)。
- Linux のビルドで、デモと C のアプリが起動して描く所まで: sim は使えないので、ネイティブ版のプログラムを
  worktree で起動して画面を撮れるならそれで (MCP の native_* は使わず、`tools/` の台本や SDL dummy で)。無理なら
  ホストの試験 (wamr_poc のように本体の部品を単体で動かす) で。どこまで確かめたかを正直に書く。
- `FMRB_HW_TARGET=NARYAv4 rake build:esp32` と `FMRB_HW_TARGET=NARYAv3 rake build:esp32` が通る。P4 の内蔵 RAM の
  静的な増分 (`idf.py size-components` か `rake regress:size` 相当を worktree で)。
- 終わったら worktree で `rake clean_all` をせず、P4 のビルドを残す (親が焼く)。裏の処理は残さない。

## 触ってよい範囲

- worktree の fmruby-core。graphics-audio は変えない。生成の道具 (`tools/boundary/`) と sig は変えない (足りなければ
  report に書いて止まる)。
- コミットは `wt/w1b` に。件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。

## 止まる条件

- 内蔵 RAM の静的な増分が 0 にならない。境界の規則・sig を変える必要。仕様が変わる選択 (Ruby のアプリの書き方・
  既定値)。ユーザの作業が要るとき。本体の checkout や sim・シリアルを使う必要が出たとき。

## report (report/w1b.md)

部品の置き方、メモリの取り方 (決定 5 の結果)、殻に足した所 (正確に)、TinyGo の SDK の形とデモ、C のアプリ、確かめた
結果と数字 (内蔵 RAM・flash)、親が焼いた後に見るログの行、W5・W6・W2 への注意。開始と終了の時刻は `date` で書く。
