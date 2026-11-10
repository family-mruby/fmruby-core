# T8 指示書: ブラウザの名乗りをルータで確かめ、クラウドでブラウザも転送に加える

> 状態: 実装中 | 更新: 2026-11-10 | 正は plan.md の「決定事項 (2026-11-10 ユーザ、T8)」

## 背景 (読むもの)

- 今の形: ブラウザは `wss + pass → door (bin/ws_door) → zenohd ws/`。zenohd は全部のブラウザを 1 つの
  相手として見て、ACL の `${node}` は `wasm-*` (asterism-console docs/g1_5.md「Browsers」)。
  ブラウザどうしは互いの名前で書けて呼べる。
- 部屋の席 (docs/rooms.md「Seats」、`lib/relay/room_door.rb`・`room_certs.rb`・`cloud_config.rb`):
  door が席の証明書で zenohd の `tls/` につなぎ、ACL は席ごとに 2 つの subject (呼べない・
  `link_protocols: ["tls"]` で自分の名前でだけ呼べる)。これを普通のブラウザへ広げる。
- WebSocket の 1 メッセージ = Zenoh の 1 batch を TLS の枠 (2 バイトの長さ) に載せ替える調べと
  InitAck の batch の大きさを 2 減らす件: docs/g1_5.md「Telling browsers apart at the router」と
  fmruby-core `doc/ruby_asterism/report/g1_5.md` の 7 章。部屋の door が既にしているならそれを使う。
- 転送: fmruby-core `doc/asterism_file_transfer/plan.md` (T6・T7 の決定) と report/t6.md・t7.md。
  端末は console の一覧 (`policy_set`) を「証明書つきの接続 (`tls/`) のときだけ」受ける。

## 範囲

1. **利用者ごとの鍵と証明書** (asterism-console)
   - 利用者が handle を決めたとき (と、既にいる利用者には移行か初めて要るとき) に、signer が
     `wasm-<handle>` の鍵と証明書を作り、サーバの中 (door が読める volume、席と同じ扱い) に置く。
     ブラウザには渡さない。期限と更新は席に合わせる。handle を変えたら古いものは使わない。
   - 証明書は利用者ごとに 1 つ。
   - ACL に載るのは Apply の後。Members と同じく「Apply 待ち」を画面に出す。
2. **door**: pass から利用者を割り出し、その利用者の証明書が ACL に載っていて、その名前のページが
   ほかに今つながっていなければ、その証明書で zenohd の `tls/` につなぐ。そうでなければ今の共有の
   接続 (`ws/`) につなぐ。どちらになったかをページに知らせる (下の 4 で使う)。
3. **ACL** (`Relay::CloudConfig`): 証明書のあるブラウザ (`wasm-<handle>`) は、自分の名前の下にだけ
   書け、自分の名前でだけ呼べる (席と同じ 2 subject の形)。ロールの「Browsers have this role」の
   中身は、その人のブラウザにも今どおり効く (`${node}` がその人の名前になる)。
   - 共有の接続 (`wasm-*`) からは、転送の鍵 (`asterism/*/transfer/**` の呼び出しと読み書き) を断る。
   - T6 の Members の行の「Receives files from」の候補に、証明書のある利用者 (`wasm-<handle>`) を足す。
     受け手としての利用者 (ブラウザが受ける) も選べるようにする (行の作りは Members に合わせて決め、
     report に書く。大きく作りを変える必要が出たら止まって返す)。その一覧から ACL の行を作る
     (送り手の ask と答え、受け手の transfer の rw。T6 と同じ)。
4. **ブラウザ版 (fmruby-core)**: door が証明書でつないだことをページが知ったら、転送のアプリが
   console の一覧 (`policy_set`) を受け入れられるようにする (今は接続の locator が `tls/` のときだけ)。
   共有の接続のときは今どおり断る。ページの表示 (Network の欄など) に「確認済みの名前」かどうかを出す。
5. **文書**: console の docs (install.md の更新の節、g1_5.md、files.md)、CHANGELOG の Unreleased。
   fmruby-core の転送の文書。

## 確認

- console: `bin/rails test` (ACL の生成は `AclEvaluator` で zenohd の判断を再現: 証明書のあるブラウザが
  ほかのブラウザ・端末の名前で呼べない、共有の接続から転送の鍵が断られる、Apply 前は共有の接続)。
  `bin/rubocop`。
- 手元で zenohd (mTLS と ACL)・door・console・bridge を立て、手元のブラウザ版のビルド (wasm:dist) を
  2 つの利用者で開き (ヘッドレスの Chromium でよい)、Members の一覧で互いを受ける相手にして、
  ブラウザどうしでファイルが届く・名前を騙った呼び出しが断られる・共有の接続のページは送れない、を示す。
  ネイティブ版の端末 (tls の資格情報が無ければ CRuby の受け手で代える) との間も 1 方向は示す。
- fmruby-core: `rake regress`、`rake regress:transfer:quick WEB=1`、`rake regress:checkpoint` (BOARD= なし)。

## 触ってよい範囲

asterism-console (door・signer・ACL・Members・画面・docs)、fmruby-core のブラウザ版のページ
(wasm/web) と転送まわりと文書、試験の道具。asterism gem は要るときだけ (CHANGELOG の Unreleased)。
コミットまで (console は main、fmruby-core は develop)。push・タグ・公開・VPS・ブラウザ版の配置はしない。
実在のドメイン・IP・ホスト名を書かない。鍵や証明書の中身を表示しない・コミットしない。

## 止まる条件

- 決定と違う動きが要る (例: 利用者ごとに 1 つで足りない、Apply なしで載せたい事情が出た)。
- door の載せ替えが部屋の作りでは足りず、zenohd の振る舞いの前提が崩れた。
- 既存の部屋・席・pass の動きが変わる必要が出た。
- 移行で既存の利用者のデータや VPS の手順に大きな変更が要る。

## report

`doc/asterism_file_transfer/report/t8.md` (作ったもの・計測値・見立てと違った点・撤回した仮説・
残件・VPS の更新に要ること)。
