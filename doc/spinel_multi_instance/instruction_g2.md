# 作業指示書 G2: G1 の仕上げ (表示・FFT アプリと spinel_hello の同時起動の禁止・メモリ切れの確認)

対象: 実装担当のサブエージェント。前提: plan.md、report/g1.md (止まった 3 点と「見つけたが直していない」)。
report は `report/g1.md` に「G2」の節を足す。作業ブランチは G1 と同じ `feature/spinel-gem-fallback`。

## 決定 (ユーザ、2026-10-03)

1. **Raycaster**: 画面の版の表示を、実際に動いている版 (`@caster.backend`) にする (1 行)。
2. **fft_bench**: 切り替わったときは「選んだ版>実際の版」(例: `spinel>ruby`) と表示する (G1 の案 b)。`bench` の結果は、
   選んだ版と実際の版の両方を返す (例: `:backend` は選んだ版、`:ran_on` は実際の版)。ログも合わせる。
3. **FFT を使うアプリは同時に起動できないようにする** (fft_bench と mic_spectrum のどちらかが動いていれば、もう一方も、
   同じものの 2 つ目も起動を断る)。G1 の report の 3 (C の版のバッファが共有で鍵が無い) も、これで同時に使われなくなる。
   mic_spectrum の画面の版の表示は、同時起動が無くなれば切り替わりが起きないので、変えなくてよい (変えない)。
4. **spinel_hello**:
   - まず、G1 の report の「sim で約 70 回の挨拶でメモリ切れ (本体ごと止まる)」が、**develop の sim でも起きるか**を確かめる
     (develop = e6b313c2 のビルドと G1 のビルドで同じ手順を比べる)。G1 で入った退行なら、原因を特定して直す。develop でも
     起きるなら、原因を調べて report に書く (直し方が小さければ直してよいが、利用者から見た動きを変えるなら止まって返す)。
   - そのうえで、**spinel_hello のアプリは 2 つ同時に起動できないようにする** (Spinel の動作確認のためのアプリで、Spinel で
     動かないと意味がないため)。

## 作り方の指針

- 3 と 4 の「同時に起動できない」は、既存の断り方 (Python の 1 つだけ、Spinel のプログラムの 1 つだけ、large_memory) と
  同じ窓の出し方にそろえる。アプリごとに C に名前を書くのではなく、`.app.toml` で宣言できる形にするのが望ましい
  (例: `exclusive = "fft"` を持つアプリは同じ値のアプリと同時に動かない、`single_instance = true` は同じアプリの 2 つ目を断る)。
  キーの名前と意味は既存の `.app.toml` のキーの付け方に合わせ、`doc/` の `.app.toml` の説明 (どこにあるか探す) と
  fmrb-app-new の手順 (`.claude/skills/fmrb-app-new/`、親のリポジトリ) に足す。断る窓の文言は既存に合わせ、英語と日本語。
- 宣言の無いアプリの動きは変えない。

## 検証

- 実機 P4-Nano (ミュートのまま、動いているアプリは止めてよい): Raycaster で B を押して版の表示、fft_bench と mic_spectrum の
  同時起動が断られる (どちらが先でも、同じものの 2 つ目も)、spinel_hello の 2 つ目が断られる。閉じれば次が開ける。
  kill の後も開ける。
- sim (標準)、`rake test`、ビルド (TAB5 / NARYAv4 / S3 / Linux / wasm)。静的な D/IRAM が develop (NARYAv4 125,612 B) を
  超えない。

## 作業の決まり

- `.env` は書き換えずコミットしない (scratchpad の env_before_gem.diff と一致)。`rake clean_all` は使ってよい。
- sdkconfig、graphics-audio、submodule (lib/add で直す)、Spinel のフォークは変えない。親のリポジトリは
  `.claude/skills/fmrb-app-new/` の説明だけ変えてよい (gitignore されているならファイルを直すだけでコミット不要)。
- 自分が変えたファイルだけをパスで指定してコミット。push とマージはしない。最後にこの版を実機に焼いて残す。
- シリアルのポートを別のセッションが使っていることがある。書き込みが止まったら `fuser` で確かめ、他の処理には触らずに待つ。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 上に書いた以外に、利用者から見た動きを変える必要が出たとき。
- spinel_hello のメモリ切れの原因が、Spinel の実行時やフォークの変更を要するとき (案として返す)。
