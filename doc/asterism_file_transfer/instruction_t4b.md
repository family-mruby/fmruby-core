# T4b 指示書: console の送り受けの相手を Members の行ごとに

> 状態: 実装中 | 更新: 2026-11-08 | 正は plan.md の「(T4b)」の 2 項 (2026-11-08 ユーザ)

## 範囲 (asterism-console だけ)

1. Admin > Members の端末の行ごとに 2 つの印: 「console へファイルを送ってよい」(受ける)・
   「console からファイルを受ける」(送る)。
2. 印から、ルータの ACL を台帳の生成で作る (今の Roles・台帳から cloud の ACL を作る仕組みに
   載せる。Apply で反映。Roles を手で直さなくてよくなる):
   受ける → `asterism/console/transfer/inbox/call/<端末>` の ask、送る → `asterism/<端末>/transfer`・
   `asterism/<端末>/transfer/**` の rw (今 docs/files.md に手で書けと書いてある行)。
3. bridge の受け手の許可は印から読む (`.env` の `ASTERISM_TRANSFER_FROM` は消す。README・
   env.example・docs も)。ページから送れる端末も印から (Call permissions の kind=file の行と
   「Allow sending files」は消す。0.3.13 で作られた file の行は移行で、型に当たる Members の行の
   印に写してから消す。当たらない型は移行のログに出す)。`kind` の列自体を残すか消すかは
   作りの都合で決めてよい (report に)。
4. Files のページの「On the network now and allowed」と「Taken from」も印から。

## 確認

- `bin/rails test` (印・ACL の生成・移行・ページ・bridge の許可)。
- 手元 (WSL) で zenohd と console (`bin/dev`) を動かし、CRuby の送り手 (`fmruby-core/tools/fmrb_send.rb`)
  で、印のある相手からは受け、印の無い相手は断ることを示す。生成した ACL に行が出ることを示す。
  ネイティブ版の端末はビルドしない (別の作業が fmruby-core でビルド中)。
- 版は 0.3.14 (CHANGELOG の Unreleased、docs/install.md に 0.3.13 → 0.3.14 の節 (移行と Apply が要る))。

## 触ってよい範囲

asterism-console だけ (コミットまで)。push・タグ・VPS はしない (親が行う)。fmruby-core と gem は触らない。

## 止まる条件

- 台帳・ACL の生成の作りを大きく変える必要が出た。既存の役割の動きが変わる。
- 仕様が変わる直し方を選ぶ必要が出た。

## report

`fmruby-core/doc/asterism_file_transfer/report/t4.md` に「T4b」の節を足す。
