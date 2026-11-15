# W4a 指示書 (後半): 規則と生成の道具を作り込む

> 親: plan.md の作るもの 2・段 W4a と「W4a で決めたこと」(2026-11-15)。前半の結果は report/w4a.md (1 章・2 章)。
> 時間の目安は doc/regression/usage.md 0 章。

## やること

1. **印を決めた形に**: クラスに `%a{fmrb.boundary}`、載せないメソッドに `%a{fmrb.internal}`、フックに `%a{fmrb.hook}`。前半の
   メソッドごとの印 (`fill_rect`) を直す。生成の道具は、本体の C に実体の無いメソッド (Ruby で書かれたもの) を載せようとしたら
   名前を出して止まる。
2. **規則を全部入れる** (plan.md 作るもの 2 の型の規則・決定 10 の幅の別名・バイト列を返す形・記録型・`_raw` の名前・フック):
   道具が sig から読み取れる型を全部扱えるようにし、扱えない型は名前を出して止まる。
3. **まず `FmrbGfx` のクラス 1 つを全部載せる**: `FmrbGfx` にクラスの印を付け、載せないものに印を付けて、その全部の関数の
   つなぎ (mruby・Spinel・Lua・MicroPython・WAMR・C のヘッダ) を生成で置き換える。包みを持つ関数は `_raw` の名前で出し、
   Ruby の包みはそれを呼ぶ。ほかのクラス (機能のまとまり 7 つ) は W4b。
4. **Lua と MicroPython の書き方の変化**: Lua の `gfx:draw_text` を `(x, y, text, color)` の順に (2 本で 6 か所)、Lua の
   小さな前置き (省略できる引数)、Lua の大きさを関数に。MicroPython は包みを持つメソッドだけ手書きの既定値を残す。
   引数名を自然な名前に直す (`str` → `text` など)。Lua の README と MicroPython の文書を直す。
5. **生成物の古さの検出**: `rake test` (`boundary:check`) が、型定義と SDK のヘッダ・コミットされた生成物 (MicroPython の qstr の
   表を含む) の一致を見る。MicroPython に関数を足すと qstr の表を作り直してコミットする必要がある件を、文書と検査に書く。
6. **ついでに直す取りこぼし** (T1 の残り): `flash/usr/share/template/app.rb.template` の `super` と `draw_window_frame`、
   `sig/fmrb_app.rbs` の `draw_window_frame` の説明 (「大きさが変わるアプリは毎回呼ぶ」が今の作りと合わない)。

## 確かめること (これだけ)

- `rake regress` (35 秒)。sig を直したら `rake clean` してからビルド。
- `rake regress:spinel` (20 秒)。
- `FmrbGfx` を使うアプリが各言語で正しく描くこと: 描画の記録 (Ruby) と、sim で Lua・MicroPython・Spinel のエディタ (開いて 1 打鍵)。
- `ruby tools/wamr_poc/run.rb boundary` (C のアプリが生成した境界を呼べる)。
- 最後に 1 回: `rake regress:screens` と `rake regress:size` (内蔵 RAM の静的な増分 0)、`rake regress:run` (VM プールが変更前と
  揺れの幅の中。KB 単位で増えたら理由を書く)。
- 直しのやり直しでは、変えた所が影響する道具だけを流し直す。

## 触ってよい範囲

- fmruby-core (develop)。作業ツリーの既存の未コミット (components/ のサブモジュール 2 つ、wasm/vendor/) には触らない。
  `~/toolchains/` は消さない。キャッシュは `~/toolchains/cache`。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。
- 終わったら Linux のビルド (x86-64)、裏の処理を残さない。

## 止まる条件

- 仕様が変わる選択 (決めたこと以外で、アプリの書き方・名前・既定値が変わる)。内蔵 RAM の増分。VM プールの KB 単位の増分で
  理由が説明できないもの。ユーザの作業が要るとき。

## report (report/w4a.md の 3 章以降)

道具の規則の一覧、載せた関数と載せなかった関数 (理由)、生成物の置き場、Lua・MicroPython の変化、確かめた結果と数字、W4b への注意。
開始と終了の時刻は `date` で書く。
