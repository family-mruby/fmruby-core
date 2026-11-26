# R3 の指示: console の Terminal から端末に入る

作業するリポジトリは **asterism-console** (fmruby-core の隣の checkout)。ブランチ `remote-shell` を
main から切って作業する。

先に読む (読むだけ。fmruby-core は変えない):
- fmruby-core の `doc/remote_shell/plan.md` (7 章の決定が正)
- 同 `report/r0.md` (3 章が console の側の調査の要点)、`report/r1.md` (2 章が端末のメソッドの引数と答えの形)
- 同 `instruction_r2.md` (端末の側で今作っている変えるコマンド。`kill`・`open` は「頼んだ」と返し、結果は `more` で取る)
- asterism-console の `CLAUDE.md` (あれば)・`docs/terminal.md`・`docs/files.md`・`README.md` の試験の節

端末の側のオブジェクトは `<node>/transfer/rshell`。スイッチが切の端末は公開が無く、呼ぶと gem の `no_object` が返る。

## 作る物

1. **Terminal に `remote <node>` と `exit`**。
   - `remote` だけなら、入れる端末の一覧 (今いるノードのうち `transfer/rshell` を公開しているもの) を出す。
   - `remote <node>` で `open` を呼び、成功したらプロンプトを `<node>:<cwd>$ ` に替える (幅は全角を数える)。以後の行は
     CLI でなく端末へ渡す。`exit` (と Ctrl+D) で `close` を呼んで戻る。
   - 状態の置き場は調査のとおり: bridge のメモリ (ページごと。セッションの番号はブラウザに渡さない) と、Rails が読める
     `TerminalSession` の 1 列。ページを閉じたとき・bridge が止まるときに `close` を呼ぶ。bridge が起き直したら列を消す。
   - 入っている間の行: コマンドの表は端末の `open` の答え (`commands`: 名前 → `read`/`write`) を使い、`read` は `exec_read`、
     `write` は `exec_write` で呼ぶ。表に無い語は端末に渡さず「このリモートシェルに無いコマンド」と返す。答えの `more` が
     ある間は `more` を続けて呼んで出力し、合間ごとに Ctrl+C (stop) を見る。`cut` のときはその事が出力の最後に出る
     (端末が書くので足さない)。終了の値 `code` を記録に使う。
   - 入っている間の Tab: 端末の `complete(id, line, pos)` を呼び、今の補完の入れ方 (1 つなら入れる、複数なら共通の頭まで
     + 一覧) に渡す。候補は `\ ` 付きの打てる形で届くので、ブラウザの側の絞り込み・共通の頭の計算がそれで壊れないように
     する。
   - 入れない理由を分かる言葉で返す: 端末がいない / リモートシェルが切 (`no_object`) / この console が許されていない /
     別の人が入っている / 確かめられた接続でない / 利用者に権限が無い。端末が返した理由の文は、言い換えた後に括弧で添える。
2. **人ごとの権限** (ユーザ決定: 人ごとに許す)。
   - 利用者 (User) に「リモートシェル: なし / 見るだけ / 変えるまで」の列を足す (既定は なし。admin も既定は なし とし、
     admin が自分を含めて選ぶ)。選ぶ画面は、今ある利用者の管理の画面に足す (admin だけ)。
   - 「なし」の人は `remote` を断る。「見るだけ」の人は `write` のコマンドを端末に渡さず理由つきで断る。照合は Rails
     (行を受けた所) と bridge (流す直前) の両方でする (今の Terminal の 2 重の確認と同じ)。
   - Terminal や図のページの汎用の `call` から `transfer/rshell` のメソッドを呼ぶ道は塞ぐ (Call permissions の表で
     許されていても、`rshell` のオブジェクトは汎用の呼び出しの対象から外す)。リモートシェルの道だけが呼ぶ。
3. **端末へ許可を配る**。
   - Members の端末の行 (とブラウザ版の利用者の行) に「Remote shell from the console」の印を足し、今の受け取りの一覧を
     配る仕組み (`ReceiverLists`・`Bridge::Receivers` の `policy_set`) に種類 `rshell` を足して配る。印が無い端末には
     空の一覧が届く。
   - ルータの ACL (`lib/relay/cloud_config.rb`): 種類 `rshell` は transfer/inbox の送り手の行にしない。console から
     `asterism/<node>/transfer/rshell/**` を呼べる行が要るか (今の `asterism/<node>/transfer/**` の行で足りるか) を
     確かめ、要る分だけ足す。印の無い端末・ほかのノードから `rshell` を呼べないこと。
4. **本体の領域を変える前の確認** (決定 11)。`write` のコマンドで、対象のパスが `/home`・`/app`・`/mnt/sd`・`/tmp` の外に
   解かれるとき (今いる場所からの相対を console の側でも解く)、流す前に「本体の領域です。続けますか (y/N)」と Terminal で
   聞き、y のときだけ流す。
5. **記録**: 入った・出た・打った行 (どの端末・だれ・コマンド・`read`/`write`・終了の値・出力の頭) を、今の Terminal の記録
   (`TerminalCommand`、Call log の画面) に残す。Call log で、リモートシェルの行だと分かること。
6. **文書**: `docs/terminal.md` にリモートシェルの節 (使い方・権限・端末の側で要るもの・断られる理由の一覧)。CHANGELOG の
   未リリースの節。版 (VERSION) は上げない (親が R5 で上げる)。

`get`・`put` (ファイルの出し入れ) は R4。今回は表に在っても「この版の console には無い」と返す (端末の `commands` に
`get`・`put` は無い見込み。console の側の語として予約だけする)。

## 触ってよい範囲

asterism-console のリポジトリの中。asterism の gem (別リポジトリ)・fmruby-core は変えない (gem に手を入れないと作れないと
分かったら止まる)。作者の環境 (アドレス・ノード名・人の名前) をコード・試験・文書に書かない (console は OSS として配る)。
コメントとコミットは英語、件名は `<種類>(<場所>): <要約>`。コミットしてよい。push しない。タグを打たない。

## 確認

- `bin/rails test` が全部通る。足す試験: 行の読み方と権限 (なし・見るだけ・変えるまで)、bridge の入る・流す・`more`・
  補完・`exit`・ページを閉じたときの `close`、配る一覧と ACL、汎用の `call` から `rshell` を呼べないこと。
- 端末の代わり: 今の live の試験 (`test/lib/bridge/terminal_live_test.rb`) と同じ形で、CRuby のノードに
  `transfer/rshell` (report/r1.md 2 章の答えの形のまま。`kill` の「頼んだ」→ `more` で結果、も含む) を公開する偽物を
  試験の中に置き、本物の bridge から通す。**fmruby-core の sim は起こさない** (別の作業が使っている。sim との通しは親がする)。
- 画面: `script/headless/` (Playwright) で、入る・`ls`・Tab・見るだけの人の断り・`exit` を撮れるなら撮る。手元でサーバを
  起こすときは、ほかの作業のポートとぶつからないこと。終わったら止める。

## 止まる条件 (止まったら報告して返す)

- 利用者から見た動き (仕様) が変わる選択が要るとき。計画の 7 章と食い違う事実が出たとき。
- gem・fmruby-core を変える必要が出たとき。データベースの今ある列の意味を変える必要が出たとき。
- 今ある試験が、直す以外の理由で落ちるとき (もとから落ちているものは、落ちていたと書いて進む)。

## report

asterism-console の `docs/remote_shell_r3.md` (console の docs の段ごとの報告の形に合わせる) に、作った物・行の流れ・
権限の照合の場所・配る一覧と ACL・試験・見立てと違った点・残件 (R4 に渡す事、端末の側に頼みたい事) を書く。
