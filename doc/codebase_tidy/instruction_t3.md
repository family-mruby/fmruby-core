# T3 指示書: アプリの自動起動

> 親: plan.md の T3 と「決めたこと」。材料は report/t0.md の T3 の節、report/t1.md (メタデータの読み手 `fmrb_app_meta`)。

## やること

1. **mruby**: `FmrbApp.inherited` で、読み込み中のファイルで新しく定義された FmrbApp の子を覚える。カーネルがファイルを読み終えた後
   (t0 の `fmrb_app.c` の `mrb_task_run` の後など、report の候補) に、覚えた子を 1 つ起動する (`X.new` → `start`、今の末尾の起動コードと
   同じ流れ)。子が 2 つ以上なら `.app.toml` / `#---fmrb` の `class =` で名指したものを起動し、無ければ理由を出す。
   同じクラスを複数のファイルで開き直す (mahjong の 9 ファイル) のは子が 1 つとして扱う。
2. **Spinel**: `gen_app_combined.rb` (生成) が、アプリの子クラスを見つけて起動の末尾を足す。名指しは同じ `class =`。
3. **二重起動を防ぐ**: C の ctx (PSRAM) に「インスタンスを作った」旗を置き、末尾の起動コードが残っていても 2 つ目を作らない。
   checkpoint の復元は別の経路で、この旗とぶつからないこと。
4. **失敗の見せ方**: 子が見つからない・`class =` の名前が無い・起動で例外のときは、エラーのダイアログ (今のアプリの異常終了の見せ方に
   合わせる) とログに理由を出す。黙って何も起きない、を無くす。
5. **末尾の起動コードを全部消す**: flash の 107 本・組み込み 11 本・テンプレート 3 本 (report/t0.md の一覧)。形のばらつき (rescue の
   中身でやっていること) で、消すと動きが変わるものがあれば一覧にし、基底でまとめて扱えるか判断して report に書く (仕様が変わるなら止まる)。
6. **Python (`.app.py`) は今回はしない** (今の末尾の起動のまま)。BASIC など他の言語も今のまま。
7. skill fmrb-app-new と、アプリの書き方の文書・sig の help を「末尾は書かない」に直す。`class =` の項目を、メタデータの文書 (t1 で
   書いた両形式の表) に足す。

## 確かめること

- `rake regress`、`rake regress:screens`、`rake regress:run` (基準は前から古いので、変更前の木と比べて差が幅の中か)、
  `rake regress:live`、`rake regress:checkpoint` (標準と全部 mruby、web を含む)、`rake regress:size` (内蔵 RAM の静的な増分 0)、
  `rake regress:spinel`。標準構成でエディタを起動して 1 打鍵まで。
- 試験用に、子が見つからないアプリ・`class =` で名指すアプリ・末尾の起動コードを残したアプリ (二重に起動しないこと) を
  `flash/app/test/` に置いて sim で確かめる。

## 触ってよい範囲

- fmruby-core (develop) と skill。graphics-audio は触らない。作業ツリーの既存の未コミット (core の components/ サブモジュール 2 つ、
  wasm/vendor/、doc/wasm_apps/、doc/README.md の wasm_apps の分) には触らない (`rake docs:index` を流すときは doc/README.md の
  差分のうち自分の分だけをコミットする)。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない (親がする)。
- 終わったら Linux のビルド (x86-64) に戻し、sim・web・ネイティブ版を片付ける。

## 止まる条件

- 仕様が変わる選択 (起動の順序・既定の動き・エラーの見せ方が今と違う形になるもの)。内蔵 RAM の増分。ユーザの作業が要るとき。

## report (report/t3.md)

入れた場所 (mruby・Spinel・C の旗)、消した末尾の数と形ごとの扱い、試験の結果、見立てと違った点、T4 以降への注意。
