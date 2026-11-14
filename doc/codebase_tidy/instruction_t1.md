# T1 指示書: 古い契約の残りとメタデータの読み手

> 親: plan.md の T1 と「決めたこと」。材料は report/t0.md の T1 の節。

## やること

1. `super(ev)`: テンプレート (`flash/lib/templates/window.rb`・`fullscreen.rb`)、`flash/app/tool/receiving.app.rb`、skill
   (`/home/kishima/fmrb/family-mruby/.claude/skills/fmrb-app-new/SKILL.md`) の不要な呼び出しと記述を消す。sig の
   `on_frameless_right_click` の例 (sig/fmrb_app.rbs:431,442 付近) は正しい使い方なので残す。
2. `draw_window_frame`: テンプレートと skill の不要な呼び出し・記述を消す (アプリ 64 本は T4 で扱うので触らない)。
3. `clear_time` → `clear_timer`: mruby・Spinel・Python の 3 系統の実装、sig・help、呼んでいるアプリ (timer_test) を変える。
   古い名前は残さない (互換は考えない)。
4. メタデータの読み手を C の spawner 1 つに: `.app.toml` と `#---fmrb` の両方を読み、項目ごとに `.app.toml` が勝つ形で合わせる。
   ランチャー・desktop・Shell・checkpoint (今それぞれ `.app.toml` を自分で読んでいる) は spawner の結果を問う口 (C の関数を
   Ruby・Spinel から呼ぶ) を使う。項目の名前と意味は両形式で同じにし、文書 (アプリの書き方の文書と skill) に両形式と優先を書く。
   ランチャーの一覧はブート時に作る (右クリックで作り直す) 今の流れを保つ。
5. skill fmrb-app-new を 1〜4 に合わせて直す。

## 確かめること

- `rake regress` (落ちたものだけ見る)、`rake regress:screens` (Retro・Modern・web)、`rake regress:checkpoint`、`rake regress:size`
  (内蔵 RAM の静的な増分 0)。標準構成でエディタを起動して 1 打鍵まで (fmruby-core/CLAUDE.md のテストの節)。
- `#---fmrb` だけのアプリ 7 本、`.app.toml` だけのアプリ、両方あるアプリ (試験用に 1 本作ってよい。`flash/app/test/` に置く) が、
  ランチャーで同じ名前・アイコンで出て、起動できること (sim で)。
- 基準の更新が要る変化は report に書く (更新は親)。

## 触ってよい範囲

- fmruby-core (develop) と skill。graphics-audio は触らない。作業ツリーの既存の未コミット (components/ のサブモジュール 2 つ、
  wasm/vendor/) には触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。
- 終わったら build/ を Linux のビルド (`file build/fmruby-core.elf` が x86-64)、sim・web・ネイティブ版は片付ける。

## 止まる条件

- 仕様が変わる選択 (ランチャーの見え方・既定値・項目の意味が変わる)。内蔵 RAM の静的な増分。ユーザの作業が要るとき。

## report (report/t1.md)

変えた所の一覧、メタデータの項目の表 (両形式の書き方と優先)、確かめた結果、見立てと違った点、T3 以降への注意。
