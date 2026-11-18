# V1c 報告: console でブラウザの利用者ごとに役割を選べるようにした

> 状態: 完了 | 更新: 2026-11-19 | asterism-console 0.4.2 (ブランチ user-roles、未 push・タグなし)。Members > Browsers の Edit で利用者ごとに役割を選べるようにし、その役割は利用者の証明書のブラウザ (door 経由の tls) にだけ効き、共有の ws のページには漏れない。役割を選んでいなければ生成される ACL は 0.4.1 とバイト単位で同じ。手元の rehearsal で、A の証明書からは tutor を呼べ、B・共有の ws・B を名乗った A からは呼べないことを確認した

指示は親の V1 検収の後の伝言 (V1c)。決定は plan.md 2 章の 10。

## 1. 今の作りの読み取り (0.4.1)

- **役割** (`RelayRole`): rw・read・ask・watch の鍵の型と、`for_browsers` (「Browsers have this role」) の印。
  台帳の行 (`RelayPeer`) が `relay_peer_roles` で役割を選ぶ。
- **利用者の証明書** (`Relay::BrowserCerts`、T8): handle を持つ利用者ごとに `wasm-<handle>` の証明書をサーバで作り、
  rooms の volume の `browsers/` に置く。door はパスの利用者の「最後に開いたページ」だけを、その証明書で zenohd の tls に
  つなぐ。それ以外のページ (2 つ目より前のページ、apply 前、証明書の無い名前) は共有の `ws/` に流れる。
- **ACL の生成** (`Relay::CloudConfig`): 利用者の証明書は主体を 2 つ持つ。common name だけの主体 (役割の鍵、全部の
  呼び出しの deny) と、common name + tls の主体 (役割の ask 行の呼び出しを自分の名前だけで allow)。どちらにも付く役割は
  `browser_role_names` = `for_browsers` の役割の全部。共有の `ws/` 主体も同じ役割を `${node}` = `wasm-$*` で持つ。
- **画面**: Members > Browsers (利用者ごとの証明書の状態と、ファイルの受け取りの一覧の Edit)、Members > Roles。
  利用者に役割を選ぶ欄は無い。
- V1 で見つけた 2 点は、この読みのとおりだった:
  1. ブラウザ向けの役割は `for_browsers` の 1 つの印で全ブラウザに付き、利用者ごとに選べない。
  2. 共有の `ws/` のページは `${node}` が `wasm-$*` なので、ほかのブラウザの名前で呼べる (cloud_config.rb の注記どおり、
     今の公開の動き)。

## 2. 直した形

- **表を 1 つ足した** (`relay_user_roles`: user_id・relay_role_id、一意の索引。`relay_peer_roles` と同じく
  ON DELETE CASCADE なし、モデルが消す)。`RelayUserRole`、`User#browser_roles`、`RelayRole#users`。利用者や役割を消すと
  組も消える。
- **ACL**: `BrowserCerts.config_entries` の各ブラウザに、選んだ役割があるときだけ `"roles" => [名前]` を足す。
  `CloudConfig#browser_cert_rules` は、その利用者の 2 つの主体に `for_browsers` の役割 + 選んだ役割を付ける
  (`${node}` は自分の名前、ask 行の呼び出しは tls の主体で自分の名前だけ。今の作りの deny と allow の組をそのまま使う)。
  共有の `ws/` 主体は今のまま `for_browsers` の役割だけ。消えた役割の名前は無視する。
- 2 点への答え:
  1. 利用者ごとに選べる。
  2. 共有の `ws/` の動きは変えていない (今パスで入る全員ができることはそのまま)。そのうえで、利用者ごとの役割は
     共有の `ws/` 主体に一切入らないので、その役割で開く鍵 (tutor の helper) には、`ws/` のページはどの名前を名乗っても
     届かない。`for_browsers` の役割での名乗りの問題は 0.4.1 のまま残る (変えると公開の動きが変わるので触らない)。
- **画面**:
  - Members > Browsers > Edit に「Roles of this browser」。選べるのは `for_browsers` でない役割 (全ブラウザが持つものは
    説明に名前を出すだけ)。保存で Apply の帯が出る (設定の digest が変わる)。ログに誰が選んだかを書く。
  - Members > Browsers の一覧に「Roles of its own」の列。
  - Roles の一覧の「Browsers」列に、その役割を持つ利用者のブラウザの名前。役割の頁の「What it becomes」に、
    利用者のブラウザごとの展開。
  - 役割の編集の「Browsers have this role」の説明に、一部の利用者だけなら印を付けず Browsers の Edit で選ぶ、と足した。
- 文書: docs/g1_5.md (「A user's own roles (0.4.2)」)、docs/install.md (12 章の説明と「From 0.4.1 to 0.4.2」)、
  CHANGELOG 0.4.2、VERSION 0.4.2 と compose の既定の版。

## 3. ACL の前後の比較 (受け入れ条件 c)

- 試験 (`test/lib/relay/browser_user_roles_acl_test.rb`): 選んでいない (`roles` 無し)・空・消えた役割の名前、のどれでも
  設定の本文が同じ。全ブラウザが持つ役割を選んでも同じ。選んだときに変わるのは、その利用者の policy と規則だけで、
  ほかの主体の policy と規則はすべて同じ。
- 手元の rehearsal (console のイメージをこのブランチから作った 0.4.2): 利用者 2 人 (handle alice・bob、証明書あり)、
  役割 tutor-host・tutor、行 tutor を作った DB で、**役割を選ぶ前**の `bin/rails relay:show` を、0.4.2 のイメージと
  公開の 0.4.1 のイメージ (同じ volume) で出し、`//` の行を除いて 4,790 行がバイト単位で同じ。
- **alice に tutor を選んだ後**の差: 足された規則は `browser__wasm_alice__tutor_{ask,read_in,read_out,rw}` と、
  ${node} の無い行の共有の規則 `role__tutor_{ask,read_in,read_out,watch}` (alice の policy だけが参照する)。変わったのは
  `browser__wasm_alice__calls` (tutor の呼び出しが 1 つ増えた) と policy `browser__wasm_alice` だけ。消えた規則は無し、
  subjects は同じ。
- 手元の開発の DB の写しでも、0.4.1 のコード (main) と 0.4.2 のコード (移行した写し) の `relay:show` が同じ
  (ただしこの DB には利用者の証明書が無い)。

## 4. 受け入れ条件

| # | 結果 | 根拠 |
|---|---|---|
| a | 通った | `bin/rails test` 681 runs 7139 assertions、失敗 0 (前は 675 runs)。`bin/rubocop` 違反 0。足した試験: ACL の試験 5 つ (A は自分の名前で呼べ helper の token と返事が届く・B と `ws/` は呼べず token も見えない・`ws/` の今の動きはそのまま・行の端末と係は今のまま・選ばなければ同じ設定・選ぶとその利用者だけが変わる)、Browsers の画面の試験 (選ぶ欄に for_browsers の役割は出ない、保存で利用者の役割と設定の項目と ACL が変わる、一覧と役割の頁に出る、外すと元の設定に戻る、役割を消すと組も消える)、利用者を消すと組も消える。`bin/brakeman` は recordings_controller の send_file などの既存の警告を出す。main でも同じ警告が出ることを確認した (この変更とは関係ない) |
| b | 通った (証明書つきのクライアントで) | rehearsal (project asterism-rehearsal、console 0.4.2 をこのブランチから作った) の隣に tutor のコンテナ (fake、devices に wasm-alice・wasm-bob・wasm-carol を全部書いた = ACL だけが壁)。A の証明書 (door が使うのと同じ `browsers/wasm-alice.{key,pem}`) で tls から: helper が見え、hello と質問が通った。B の証明書: 見えず、呼び出しは時間切れ。A の証明書で node を wasm-bob と名乗る: 時間切れ。共有の `ws/` (router の網の中から `ws/zenohd:7449` へ、node を wasm-alice と wasm-carol で): 見えず時間切れ。gem の記録に来たのは wasm-alice だけ |
| c | 通った | 3 章 |
| d | 済 | asterism-tutor の docs/install.md 9 章 (console 0.4.2 から。Browsers の Edit で tutor を選ぶ・Apply・`ASTERISM_TUTOR_DEVICES` に `wasm-<handle>`・役割に「Browsers have this role」を付けない、の理由)。3 章の役割の項にもつなぎを書いた |

door を通した本物のブラウザ版では確認していない。door は証明書の選び方を変えていないので、証明書つきの CRuby で
同じ主体 (同じ証明書・tls) を作って確かめた。ブラウザ版の今の版のバンドルは `rake wasm:web` が要る (流していない。
**親が流す**)。

## 5. 移行

- `db/migrate/20261119100000_relay_user_roles.rb`: 表を足すだけ (足し算)。既存の役割・行・利用者・招待は触らない。
- `db/schema.rb` は、開発の DB の写し (scratch) に移行を流して作った。開発の DB そのものは移行していない。
  そのとき `db/cable_schema.rb` まで書き換わったので戻した (DATABASE_URL が cable にも効くため。コミットには入れていない)。
- 戻すとき: 0.4.1 は新しい表を読まないので、`./update.sh 0.4.1` だけで動く (選んだ役割が効かなくなる)。表も消すなら
  更新の前の backup から戻す (docs/install.md に書いた)。

## 6. 止まった所

無い。利用者から見た動きの変化は、新しい画面の欄と、選んだときだけ変わる ACL に限られる。`for_browsers` の意味、既存の
利用者・端末の権限、共有の `ws/` の動きは変えていない。

## 7. 確認していないこと

- 本物のブラウザ版から door を通しての通し (4 章の注)。
- VPS の console の更新 (触らない決まり)。
- 選んだ役割のある利用者の 2 つ目のページ (door が `ws/` に流す) が係を見つけられないことは、`ws/` の確認で代えた。

## 8. ユーザに決めてほしいこと

1. console 0.4.2 のリリース (main へのマージ・push・タグ `v0.4.2`)。版はパッチにした (これまでの機能追加の流儀に合わせた)。
2. `for_browsers` の役割での `ws/` の名乗りの問題 (今の公開の動き) を、いつか直すか。今回は触っていない。

## 9. VPS に入れるときの材料 (順番)

1. console 0.4.2 をリリース (タグ → イメージ)、VPS の console を `./update.sh 0.4.2` (移行は表を足すだけ。update の
   setup がルータの設定を書き直して再起動する。選ぶまで ACL は 0.4.1 と同じ)。
2. console の画面で役割 tutor-host・tutor (tutor は「Browsers have this role」を付けない)、行 tutor の証明書、端末の行に tutor。
3. ブラウザを使う利用者に handle と証明書 (「in the ACL」)、Members > Browsers > Edit で tutor を選ぶ、Apply。
4. tutor の `.env` の `ASTERISM_TUTOR_DEVICES` に `wasm-<handle>` を足して `docker compose up -d`。
5. 利用者は console から開いたページで AI ヘルパーを起こす (同じ利用者の後から開いたページが名前を持つ)。

## 10. 後片付け

rehearsal の stack (VOLUMES=1。作った利用者・役割・行はその DB ごと消えた)、tutor のコンテナと volume、console の
`deploy/local/.env`・`certs/`、作ったイメージ (asterism-console-rehearsal:0.4.2・asterism-tutor-local:0.2.0) を消した。
console の checkout は main に戻した (ブランチ user-roles は残してある)。各リポジトリの git status に取りこぼしは無い。

## 11. コミット

asterism-console (ブランチ `user-roles`、main から):

- `feat(relay): roles for some users' browsers, for their own certificate only`
- `build(release): console 0.4.2 with roles for some users' browsers`

asterism-tutor (ブランチ `vps-container`):

- `docs(tutor): the browser build's users get the role tutor one by one`
