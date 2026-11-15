# W1c 指示書: ネイティブ版とブラウザ版で wasm のアプリを動かす (worktree で並行)

> 親: plan.md の段 W1b (sim とブラウザ版の本体に組み込む) の残り。前段は report/w1b.md・w1b_2.md (8 章の残り:
> ネイティブ版とブラウザ版の `hosted.cmake` に WAMR をつないでいない、ブラウザ版のスタックの境界は NULL)。W1a で WAMR が
> emscripten の上で動くことは確かめた (report/w1a.md)。時間の目安は doc/regression/usage.md 0 章。

## ユーザの希望 (2026-11-15)

- ブラウザ版で wasm のアプリが動けば、SDK の利用者が端末なしで試せる。SDK の手順書に「ブラウザで試す」を足す。
- 順番: SDK の手順書 → この段 (G0 と並行) → 手順書に足す → 公開のブラウザ版 (studio) への反映はユーザと相談。
- 回線が不安定なことがある。大きなダウンロードをしない (emscripten・WAMR は手元にある)。

## 作業場所 (並行のため)

本体の checkout では doc/remote_guard の G0 (devctl・道具・P4 の書き込み) が動く。この段は worktree で行う:

```
git -C /home/kishima/fmrb/family-mruby/fmruby-core worktree add /home/kishima/fmrb/wt/w1c-core -b wt/w1c develop
```

- submodule・`.env`・`config/wifi.toml`・`flash_local/`・`hosted/vendor/`・`vendor/` (`vendor/wamr` を含む)・
  `tools/wamr_poc/vendor/` を本体の checkout から rsync で写す (`.git` を除く。入れ子の `build/` を外さないよう `/build/`
  で除く)。`wasm/vendor/` も要れば写す。
- 本体の checkout・build/・sim・シリアル・flash には触らない。MCP の native_* と web_* は本体の checkout を見るので、
  使えるかを確かめ、使えなければ worktree の中で道具 (`tools/fmrb_web.rb` など) を直接動かす。使えないときは
  ホストの試験までにして、画面の確認は親が取り込んでから行う。

## やること

0. **イベントの記録の置き場 (決定 20)**: SDK が export する静的な置き場 (名前を決める) に本体が書き、無ければ今の
   スタックの形に戻る。`components/fmrb_wamr/fmrb_wasm.c`、生成の C のヘッダ (`tools/boundary/templates` の SDK の
   ヘッダ。C のアプリが 1 行で置き場を用意できるマクロなど)、family-mruby-sdk の TinyGo の `fmrb` パッケージ
   (`tinygo/fmrb/`、ASCII のみ、ローカルのコミットまで)。この 0 に限り生成の道具の型紙を変えてよい。ABI のハッシュが
   変わったら書く。スタックを使わない最小の TinyGo のアプリ (scratchpad) が読み込まれることを確かめ、SDK の
   手順書 (`docs/getting_started*.md`) のトラブルシューティングから回避策を消す。fmruby-core の `flash/app/` の wasm の
   アプリ (bounce・hello・c_check) は作り直す。

1. **ネイティブ版**: `hosted/hosted.cmake` (と関係するビルド) で `components/fmrb_wamr` をつなぎ、`FMRB_HAVE_WASM` を
   有効にする。`.app.wasm` が起動し、bounce と c_check が描いてイベントを受けること。
2. **ブラウザ版**: emscripten 向けに WAMR をビルドしてつなぐ (W1a の設定を参考に)。スタックの境界 (今は NULL) を
   どう扱うか決めて書く (emscripten のスタックの範囲が取れればそれを、取れなければ命令数の上限に頼る理由を)。
   ブラウザ版の bundle に `/app/tinygo` と `/app/test/c_check` が入ること (`config/system_conf_wasm.toml`・bundle の
   決まりに合わせる)。大きさの増分 (wasm の本体の .wasm と bundle) を report に。
3. **確かめる**: ネイティブ版とブラウザ版で bounce と c_check を起動し、描画・クリック・キー・`q`・閉じる・再起動。
   画面を撮る (道具が使えれば)。
4. **回帰**: `rake regress`・`rake regress:spinel`。`regress:screens ONLY=web` と `ONLY=native` は、worktree で流せるなら
   流す (流せなければ親が取り込み後に)。基準の更新はしない。

## 触ってよい範囲

- worktree の fmruby-core と family-mruby-sdk。graphics-audio は変えない。境界の規則・sig は変えない。生成の道具は 0 の型紙だけ。
- コミットは `wt/w1c` に。件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。

## 止まる条件

- 境界や殻の形を変える必要。Ruby のアプリの動きが変わる。大きなダウンロードが要る。ユーザの作業が要るとき。

## report (report/w1c.md)

つなぎ方 (ネイティブ版・ブラウザ版)、スタックの境界の扱い、大きさ、確かめた結果と画面、取り込み後に親が流すもの。
開始と終了の時刻は `date` で書く。終わったら worktree で `rake clean_all` して build/ を消す。
