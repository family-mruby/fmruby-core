# 実装指示書 F5: Asterism でイメージを送り、受け取った側で起動する

`doc/checkpoint/plan.md` (決まったこと、F4 の検収まで。F5 の「止める前の問い合わせ」)、`report/f2.md` (移す段取りと
デバッグサーバの道)、`report/f4.md` (`SND0`・`requires`・`mrb_checkpoint_sent_names` / `has_method`)、
`movable_apps.md`、`flash_table.md`、mruby-checkpoint の `doc/plan.md` (D1〜D16、特に D13・D14・D15) と
`doc/survey_family_mruby.md` §7 (転送)、`doc/wasm_apps/plan.md` §4 (受け口の共用) を先に読む。Asterism の
Family mruby での使い方は、fmruby-core の `doc/ruby_asterism/` ほか関係する文書と、Asterism を使う既存のサービス・
アプリのコードで確かめる。

## 目的

2 つの Family mruby (まずネイティブ版 ⇔ ネイティブ版、次にネイティブ版 ⇔ ブラウザ版) の間で、申告済みのアプリを
**Asterism (Zenoh) の上で**移す。F2 の開発用の道 (デバッグサーバの `file_*`) を、正式な道に置き換える。

## 決まっていること

- D13: 受け口のサービスが Asterism で `begin` / `chunk` / `commit` を公開し、`caller:` で送り手を絞る。Zenoh の
  1 回の上限 (4 KB) に合わせて分けて送る。端末の証明書による相手の確認がそのまま効く。
- この受け口は、wasm のアプリのインストール (`doc/wasm_apps/plan.md` §4・W5) と共用にする。F5 では checkpoint の
  イメージを受ける種類を作るが、受け口の作り (分けて受け、`commit` で確定し、種類ごとの処理に渡す) は種類を足せる
  形にしておく。
- D14: 止める・起動するの段取りはカーネル (Ruby) に入れる。カーネルは Spinel の構成と全部 mruby の構成の両方で
  確かめる。
- D15: 移動元は、着いた側が起動できたと返事をしてから閉じる。それまで止めたまま (移動中の印を出す)。断られた・
  返事が来ないときは続ける。
- **止める前の問い合わせ** (2026-11-04): 移動元は、アプリを止める前に、運ぶコードが呼ぶメソッド名の一覧
  (`mrb_checkpoint_sent_names`) と `requires` を相手に送る。相手は自分の表 (`mrb_checkpoint_has_method`。Ff の後は
  flash の表) で引いて「全部ある / これが無い」を返す。`requires` に無いものがあれば、止める前に断る。そうでない
  無い名前は警告として移す側に返し、移す。各ビルドは自分の表だけを持ち、ほかのプラットフォームの表は埋め込まない。
- ブラウザ版の公開のビルドに checkpoint を入れるのは F5 の後 (F5 で道ができてから)。F5 の中では、F4 の opt-in の
  別ビルドで確かめてよい。公開のビルドに入れるかの最終の判断は、F5 の結果を見てユーザが決める。
- 内蔵 RAM +0。ESP32 に入れるのは F6。

## やること

1. **Asterism の受け口**: どこに置くか (サービスのアプリか、カーネルか、C か) を、D14 と今の Asterism の使われ方
   (どのプロセス・どのエンジンが Asterism のセッションを持てるか) から決める。分けて受ける・確定する・途中で
   切れたら捨てる・同時に 1 つ (または複数) の扱い・大きさの上限。`caller:` で絞る相手の決め方 (同じ網の中の
   決まった端末、設定で許す相手)。
2. **移す段取り**: 「問い合わせ → 止める (移動中の印) → イメージを送る → 着いた側で起動 → 返事 → 移動元を閉じる
   (または続ける)」を、Asterism の上で行う。期限と失敗のときの扱いは F2 にそろえる。
3. **頼み方**: 開発の道具 (`tools/fmrb_native.rb`・MCP の `native_app`)、Shell のコマンド、アプリの API
   (`FmrbApp.checkpoint` の移す版、または新しい API) から、移す先の端末を名前で指して頼めるようにする。名前の
   決め方は Asterism の端末の名前にそろえる。
4. **ブラウザ版**: F4 の opt-in の別ビルドで、ブラウザ版 ⇔ ネイティブ版を Asterism で移せることを確かめる
   (ブラウザ版の Zenoh は WebSocket 経由)。大きさ・起動の時間の数字を更新する。
5. **試験**: `rake regress:checkpoint` に、Asterism で移す場面を足す (成功、問い合わせで断る (`requires`)、警告つきで
   移す、返事が来ない)。必要なら Zenoh のルータ (`zenohd`) を試験の中で起こす。カーネルが Spinel の構成と全部
   mruby の構成の両方で。ブラウザ版の場面は、別ビルドで流せる形にして、既定に入れるかを report に書く。
6. **文書**: `report/f5.md` (受け口の置き場所と作り、段取り、計測 (運ぶ時間・止めている時間)、サブの仮決め、
   F6 と W5 に要るもの)、`plan.md` の F5 の行を「実装済・検収待ち」に、`movable_apps.md` (移す先の指し方)、
   `doc/regression/usage.md`。

## 確かめること

- `rake regress:checkpoint` (既定と `SIM=1`)、`rake regress`、`rake regress:run`、
  `rake regress:screens ONLY=retro,modern,native,native-keys` (人が開いているブラウザがあるので web は流さない)、
  共有のコードを変えたら `rake regress:size` (内蔵 RAM +0)。`rake regress:live` (Asterism を使う場面がある) も。
- mruby-checkpoint や Asterism の gem に手を入れたら、それぞれの試験。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core は本体の checkout の `feature/linux-native` にコミット (push しない)。mruby-checkpoint は worktree の
  ブランチにコミット (push しない。開発中は `MRUBY_CHECKPOINT_DIR`、固定点は親が合わせる)。family-mruby の
  リポジトリ直下 (`tools/`) も触ってよい (コミットまで)。Asterism の gem (ruby-asterism の各リポジトリ) を変える
  必要が出たら、止めて報告する (公開済みの gem のため)。
- 止まる条件: 利用者から見た動きが変わる直し方、Asterism の gem や console を変える必要、Asterism のセキュリティの
  決まり (証明書・許可) を緩める必要、D1〜D16 と食い違う事実、スケジューラの作りを変える必要。
- 人が開いているブラウザ (ポート 8016) に触らない。ブラウザ版の確認は自分のポートのヘッドレスで。
- 文書は常体、定着したカタカナ語を使う。コミットする文書にローカルの絶対パスや実在の IP・端末の ID を書かない
  (コミットの前に、差分を検索して確かめる)。
