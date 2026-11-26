# R4 の指示: ファイルの出し入れ (get・put) と大きい cp

先に読む: `doc/remote_shell/plan.md` (7 章が正)、`report/r1.md`〜`r3.md`、asterism-console の `docs/remote_shell_r3.md`・
`docs/terminal.md`。両方のリポジトリのブランチ `remote-shell` で作業する。

## 決めてあること (親の決定。plan.md 7 章 6 の「put は転送に乗せる」を変える)

- **`get` も `put` も、リモートシェルのメソッドで運ぶ** (転送のアプリの受け取りには乗せない)。理由: `put` を転送に
  乗せると、端末で「受け取り」のスイッチと console の「送ってよい」印も要り、許可が 2 か所になる。リモートシェルの
  許可 (スイッチ・端末の印・人の権限) だけで閉じる方が分かりやすい。
- 1 回の呼び出しで運ぶのは 8 KB まで。1 ファイルの上限は 16 MB (console の設定で変えられる形にする)。
- 途中の物は見せない: 端末は別の名前 (隠しの `.part`) に書き、終わりで名前を付け替える。

## 作る物

### 端末の側 (fmruby-core `flash/usr/share/transfer/rshell.rb` ほか)

1. `read` は R1 の物を使う (足りない所があれば直す。バイナリがそのまま運べること)。
2. 書く側のメソッド (変える権限。`exec_write` と同じ確認を通る):
   `write_begin(id, path, size, overwrite)` → 鍵の場所・読み取り専用・空き容量・同じ名前 (overwrite が偽なら断る) を
   確かめる。`write_chunk(id, pos, data)` → 今の位置を返す (前の片の聞き直しは読み飛ばす、先の片は受けない)。
   `write_end(id)` → 大きさを確かめて名前を付け替える。`close`・時間切れ・別の `write_begin` で、途中の `.part` を消す。
   console の ACL と汎用の `call` の塞ぎは `transfer/rshell` 全体なので、メソッドを足すだけで守られる (確かめる)。
3. 大きい `cp` (と、付け替えが通らないときの `mv`): 1 回の呼び出しで全部写さず、`kill`・`open` と同じ「頼んだ →
   `more` で進み具合と結果」の形にして、アプリの周期の中で少しずつ写す。小さいファイル (64 KB 以下) は今のままでよい。
4. `open` の答えの `commands` に `get`・`put` は入れない (console の側の語)。`version` の `rshell` を上げる。
5. 試験 (`test/rshell/run.rb`) を足す。sig は変わらない見込み (アプリが呼ぶ API を足したときだけ)。

### console の側 (asterism-console)

1. `get <端末のパス>` (見るだけの人も使える): bridge が `read` で順に取り、console の一時の置き場に置く。Terminal に
   進み具合 (何 KB / 全体) を出し、Ctrl+C で止まる。終わったらブラウザがそのファイルを落とす (頼んだ本人だけが 1 回
   落とせるリンク。落としたら消す。落とされないまま一定時間で消す)。フォルダ・鍵の場所・上限超えは理由つきで断る。
2. `put [端末の先のパス]` (変えるまでの人): ブラウザでファイルを選ぶ (複数でもよい)。選べる形はブラウザの決まり
   (利用者の操作から開く必要) に合わせ、Terminal へのドラッグ&ドロップも受ける。console へ上げてから、bridge が
   `write_begin`・`write_chunk`・`write_end` で端末の今いる場所 (か、渡した先) に置く。同じ名前があるときは Terminal で
   「上書きしますか (y/N)」を聞く。本体の領域への `put` は、変えるコマンドと同じ y/N の確認を通す。
3. 記録: `get`・`put` を Call log に残す (どの端末・だれ・パス・大きさ・結果)。一時の置き場の掃除。
4. 補完: `get` は端末のパス、`put` の引数も端末のパス。
5. 試験と偽物 (`test/test_helpers/fake_rshell.rb`) を本物の形に合わせて足す。`docs/terminal.md`・CHANGELOG を直す。

## 確認

- fmruby-core: `rake regress` と足した試験。console: `env -u ASTERISM_CREDENTIALS bin/rails test`。
- 本物どうしの通し (手順は `report/r3.md` のとおり。sim は `rshell_uncertified`、TLS の道はネイティブ版):
  テキスト・かな・バイナリ (PNG など)・1 MB 前後のファイルを `get` して元と一致 (SHA-256)、`put` して端末の側で一致。
  同じ名前の y/N、鍵の場所の断り、見るだけの人の `put` の断り、途中の Ctrl+C で `.part` が残らないこと、
  数 MB の `cp` が期限切れにならず進み具合が出ること。画面を撮る (置き場は下)。1 MB の `get`・`put` にかかる秒数を測る。
- ESP32 のビルドはしない。重い回帰 (`regress:run`・`size`・`screens`) は流さない。push しない・タグを打たない・VERSION を上げない。

## 止まる条件

- 利用者から見た動き (仕様) が変わる選択が要るとき。7 章・上の決定と食い違う事実が出たとき (例: バイナリが 1 回の
  呼び出しで運べない、flash への細かい書き込みが極端に遅い)。
- 触らないもの (シェル・`vendor/`・`lib/upstream/`・sdkconfig・asterism の gem) を変える必要が出たとき。

## 後片付けと report

sim・ネイティブ版・サーバ・bridge・ルータ・ブラウザを止め、試験用のファイルと設定を消し、console の開発用の DB を元に
戻す。画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/2488ad3e-d680-4f55-88f7-804cff8c4d43/scratchpad/r4/`。
`doc/remote_shell/report/r4.md` (日本語の常体) にメソッドの形・通した結果・測った数・見立てと違った点・残件を書く。
console の側は `docs/` に R3 と同じ形で。実在の値 (ドメイン・IP・ノード名・人の名前) を書かない。
