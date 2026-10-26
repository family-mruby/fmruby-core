# RG2 指示書: 台本で sim とブラウザ版を動かし、画面を基準と比べる

> 状態: 指示 | 更新: 2026-10-26 | plan.md の RG2。「アプリを起動 → キー・クリックを送る → 撮る」を台本ファイルに書き、`rake regress:screens` で sim (Modern・Retro) とブラウザ版を順に動かして基準の画像と画素で比べる。RG1 (描画の記録) が受け持てない所 (Spinel のプログラム、実際の画素、sim・ブラウザ版の画面) を受け持つ。**実機は使わない**

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- doc/regression/plan.md (方針・未確定事項 2〜4)、usage.md、report/rg1.md (受け持てない所の一覧)、report/rg3b.md (`tools/regress/sim_run.rb` の sim の起こし方・片付け・切り替え直後の 1 回目が上がらない件)。
- 画面と入力の道具: `tools/fmrb_screenshot.py`・`tools/fmrb_input.rb` (sim)、`tools/fmrb_web.rb` (ブラウザ版)、`tools/fmrb_pngdiff.rb`・`fmrb_pngscan.rb`、ルートの CLAUDE.md の sim とブラウザ版の節。
- app_refactor R6 の report (r6.md) の「画面での確認」(サブが手で撮っていたもの。今回はこれを台本にする)。

## 2. 作るもの

1. **台本の書き方**: Ruby の小さな DSL (例 `scene "editor_type" do; launch "/app/..."; key "a"; wait_for ...; shot; end`)。TOML より条件待ち・繰り返しが書きやすいので Ruby にする (plan.md の未確定事項 2 はこれで決める)。置き場 `test/regress/screens/*.rb`。
   - 時計・空きメモリ・秒数など変わる所は、台本で隠す範囲 (矩形) を書ける。
   - 「待つ」は時間ではなく画面の条件 (前の画像と同じになるまで、ある領域が変わるまで、など) を主にし、上限の秒数を持つ。
2. **場面** (少なくとも):
   - 標準構成 (Spinel のカーネル・エディタ) の Modern と Retro で: デスクトップの起動、エディタを開いて 1 打鍵 (CLAUDE.md の必須の確認)、ランチャー、Config の画面。
   - 全 mruby の構成で: デスクトップとエディタの 1 打鍵 (engine_policy の 2 点)。構成を切り替えるビルドが重ければ、`FULL=1` のような別口にしてよい。
   - mruby のアプリを数本 (RG1 にあるものから、画面の画素で見る意味がある物: 画像・スプライト・BMP を使う walk_scroller、日本語の文字、FmrbUI の部品の画面)。
   - ブラウザ版: デスクトップ、エディタの 1 打鍵、アプリ 1 本。
3. **基準**: 画像は git に入れる (plan.md の未確定事項 4 はこれで決める)。大きさを report に。大きすぎる (目安 5 MB 超) なら、画像の代わりに画素の要約 (領域ごとのハッシュ) を基準にする案を選択肢で返す。
4. **入口**: `rake regress:screens` (`ONLY=modern|retro|web`、場面名で絞る口)、`rake regress:screens:update`。`rake regress:full` に入れる。出力は「通った N / 落ちた M」と、落ちた場面の差分画像の場所だけ。
5. ビルドの切り替え (linux の Retro・Modern、wasm) は道具が受け持ち、終わったら build/ を元に戻す (`build_tree.rb`)。誰かの sim・ブラウザが動いていたら始めない (sim_run.rb と同じ)。

## 3. 確認

- 全場面が 3 回続けて通る (揺れで落ちない)。揺れた所と、隠した範囲・幅の決め方を report に。
- わざと壊す確認: ファームのコードを変えずにできる範囲で (基準の画像を 1 枚いじって落ちる、など)。アプリの文字を 1 px 動かす確認をするなら必ず戻す。
- 流した時間 (全体、端末ごと) と出力の行数を report に。
- R6 で手で撮った確認 (19 枚) のうち、どれが台本で置き換わったか。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の tools/regress/、test/regress/、rakelib/、doc/regression/usage.md。ルートの tools/ は読むだけ (直す必要があれば止まる)。
- **触らないもの**: 実機、sdkconfig、`.env` の既定、アプリ・gem・ファームのコード、VPS、`~/asterism_keys/`。
- push・タグはしない。`git commit -a` は使わない。件名 `tools:` `test:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc/regression の指示書・report はコミットしない。usage.md はコミットしてよい。
- 終わったら sim・web を片付ける (前から動いていたものは残す。port 8006 の `rake wasm:serve` は親のもの)。
- **止まる条件**: 揺れが止められない場面が多い、基準の画像が大きすぎる (選択肢で返す)、ルートの tools/ を直さないと進めない、ファームの不具合が見つかった (追わずに返す)。
- **report**: `doc/regression/report/rg2.md` (常体)。
