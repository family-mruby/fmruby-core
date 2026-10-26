# RG6 指示書: 揺れの幅を道具が自分で測る (calibrate)

> 状態: 指示 | 更新: 2026-10-26 | RG3b (`rake regress:run`) と RG4 (`rake regress:boot`) の幅は、サブが同じビルドを 5 回流して数字を見比べて決めた。端末を足すたびに AI がこれをやると、端末の数だけトークンが増える。道具が N 回流して揺れを測り、幅の案と基準を書き出し、AI は数行の要約を読んで取り込むだけにする (2026-10-26 ユーザ決定)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- doc/regression/usage.md (7・8 章)、report/rg3b.md (sim の幅の決め方)、report/rg4.md (起動ログの幅の決め方)。
- tools/regress/sim_run.rb・boot.rb・boot_log.rb と、幅を持っている所 (定数・基準の json)。

## 2. 作るもの

1. **`rake regress:boot:calibrate BOARD=<名前> [RUNS=5] [FLASH=0]`**: 同じファームで RUNS 回起動 (焼くのは最初の 1 回だけ、既定は FLASH=0 で今のファームのまま) し、項目ごとの揺れ (最小・最大・幅) を測る。幅の案 = 揺れに余裕を足して丸めた値 (今の決め方: rg4.md の表と同じ考え方。余裕の掛け方は report に)。結果は `tmp/regress/boot/<board>.calibrate.json` に書き、要約を数行で出す (項目・揺れ・今の幅・案)。
2. **`rake regress:run:calibrate [ONLY=retro|modern] [RUNS=5]`**: sim の台本を RUNS 回流して同じことをする。
3. **取り込む口**: `... :calibrate APPLY=1` (または `:calibrate:apply`) で、測った基準の値と幅を基準の json に書く。幅を端末ごとに持てるようにする (今は全端末共通の定数なら、端末ごとの上書きを基準の json に持てる形に。既定の幅は残す)。人が要約を見てから流す、の決まりは基準の更新と同じ。
4. usage.md の 7・8 章に「端末を足したとき: calibrate → 要約を読む → APPLY=1」を足す。端末を足す手順 (2 章) にも 1 行。

## 3. 確認

- P4-Nano (つながっていれば、`/dev/ttyACM0`) で `rake regress:boot:calibrate BOARD=naryav4 RUNS=3` が通り、案が今の幅と同じ程度になる。つながっていなければ、作り物のログで解析と幅の計算を試験し (test/ に、`rake regress` に入る形で)、実機は「未確認」と report に。
- `rake regress:run:calibrate ONLY=modern RUNS=3` が通る。
- APPLY=1 で基準が書かれ、そのあと `rake regress:boot FLASH=0` と `rake regress:run` が通る。試しに書いた基準は、今の基準と違えば戻す (今の基準を残す。変えるのは人が決める)。
- 出力の行数と時間を report に。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の tools/regress/、test/regress/、test/ (計算の試験)、rakelib/、doc/regression/usage.md。
- **触らないもの**: sdkconfig、`.env` の既定、アプリ・gem・ファームのコード、ルートの tools/ (読むだけ)、VPS、`~/asterism_keys/`。実機は P4-Nano だけ。全体の書き込み (/home が消える) はしない。
- push・タグはしない。`git commit -a` は使わない。件名 `tools:` `test:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc/regression の指示書・report はコミットしない。usage.md はコミットしてよい。実在の値を基準・コードに入れない。
- 終わったら sim を片付け、build/ を元に戻す (TAB5 の esp32p4)。
- **止まる条件**: 揺れが大きく幅の案が意味を持たない、ルートの tools/ を直さないと進めない。
- **report**: `doc/regression/report/rg6.md` (常体)。
