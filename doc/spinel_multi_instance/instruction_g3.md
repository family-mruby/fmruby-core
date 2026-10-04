# 作業指示書 G3: 起動中に終わったアプリの後、キー入力の行き先を元に戻す

対象: 実装担当のサブエージェント。前提: plan.md、report/g1.md (G2)。report は `report/g1.md` に「G3」の節を足す。
作業ブランチは `feature/spinel-gem-fallback` (G1・G2 と同じ)。

## 症状 (ユーザの実機確認、2026-10-04。親が再現しログで確認)

Mic Spectrum が動いているときに 2 つ目の Mic Spectrum を起動すると、断る窓が出たあと、キー入力が Mic Spectrum に
届かなくなる (E キーがデスクトップのショートカットとして受け取られ、エディタが開いた)。マウスは効く。

ログ: `spx: HID target set to new app pid=6` → 断られて終了 → `spx: HID target back to pid=2 (app 6 terminated)`。
カーネルの `main/prebuild_scripts/kernel/fmrb_kernel/app_lifecycle.rb` が、起動の時点でキー入力の行き先を新しい
アプリに移し (78 行目付近)、アプリが終わると「起動した親 (`@run_parent`) か、無ければデスクトップ」に戻す
(835-862 行目付近)。元の行き先 (Mic Spectrum) には戻らない。起動直後に終わるアプリ全部 (Python の 2 つ目、
large_memory の取り合い、Spinel のプログラムの 2 つ目、`single_instance` / `exclusive_group`、起動時のエラー) で起きる。

## 決定 (ユーザ、2026-10-04): 案 a

- 起動のときに、それまでのキー入力の行き先を覚えておく。アプリが**窓を出す前に終わった** (起動中に終わった) ら、
  覚えておいた行き先に戻す (そのアプリがまだ生きていて、フォーカスできる場合。生きていなければ今の規則のまま)。
- **窓を出したあとに普通に閉じたときの動きは変えない** (今のまま、起動した親かデスクトップに戻る)。
- 「窓を出した」の判定は、カーネルが既に持っている状態 (起動の表示 announce_starting の終わり、main の canvas の作成、
  など) から選び、理由を report に書く。
- 全画面のアプリ・F5 で起動したアプリ (`@run_parent`、`@parked_fullscreen_pid`) の既存の扱いを壊さない。

## 検証

- 実機 P4-Nano (ミュートのまま、動いているアプリは止めてよい): Mic Spectrum の 2 つ目 → 窓を閉じて E キーが Mic Spectrum
  に届く (版が変わる)。fft_bench と mic_spectrum の組み合わせ、spinel_hello の 2 つ目、エディタ (Spinel) の 2 つ目でも
  同じ。普通にアプリを閉じたときは今までどおりデスクトップに戻ること。エディタの F5 で起動したアプリを閉じると
  エディタに戻ること (全画面の復帰も)。
- sim (標準・互換)、`rake test`、ビルド (TAB5 / NARYAv4 / S3 / Linux / wasm)。標準構成の sim ではエディタの起動と 1 打鍵も。
  静的な D/IRAM が develop (NARYAv4 125,612 B) を超えない。カーネルは Spinel で生成されるので、ivar を足すときは
  CLAUDE.md の注意 (ivar のレイアウト) と、Spinel で動くかを sim で必ず確かめる。

## 作業の決まり

- `.env` は書き換えずコミットしない (scratchpad の env_before_gem.diff と一致)。`rake clean_all` は使ってよい。
- sdkconfig、graphics-audio、submodule、Spinel のフォークは変えない。
- 自分が変えたファイルだけをパスで指定してコミット。push とマージはしない。最後にこの版を実機に焼いて残す。
- **シリアルを開き直すと P4-Nano が再起動することがある** (2026-10-04 に親の serial_start で再起動した)。調べたい状態がある
  ときは開き直さない。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 上に書いた以外に、利用者から見た動き (普通に閉じたときの行き先など) を変える必要が出たとき。
