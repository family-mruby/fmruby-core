# Asterism G0・W4 指示書: インターネットのルータと、Web に出す console (手元で本番と同じ構成)

> 状態: 指示 | 更新: 2026-10-12 | VPS に置く「ルータ (zenohd) + console + bridge」の一式を、VPS と同じ形 (VPS 本体の nginx が TLS を終えて転送する) で手元の docker に組み、守り (mTLS・ACL・ブラウザの合言葉・管理の画面の分離・本番の設定) を確かめる。VPS に足す手順書を作る。VPS には触らない

前提: plan.md の「G0: インターネットのルータと守り」「W4: console を Web に出す」(決定事項)、W1・W2 の report (report/w1.md・w2.md: mTLS・ACL・台帳・`bin/signer`)、親リポジトリの `docker-compose.relay.yml` と `docker/zenoh-relay/`、asterism-console の README と docs (w2 の管理、v2_5 まで)、doc/ruby_asterism/browser.md (B2: ブラウザ版の `wss://`・ルータの設定・`?zenoh=`)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. VPS の今の作り (2026-10-12 に読むだけで確かめた)

- サービスは `/opt/docker-compose-service/<名前>/` に docker compose で置く流儀。そこに `asterism/` を足す想定。
- 入口は VPS 本体の nginx。証明書は Certbot (Let's Encrypt)。各サービスは `127.0.0.1` のポートにだけ出し、nginx が転送する。443 番にはほかのサイトがある。別のアプリは同じドメインで専用の TLS のポート (19292-19294 番) を使っている。メールサーバ・Minecraft もある。
- **公開のリポジトリにドメイン・IP・ポートの実際の割り当てを書かない** (`router.example.org`、`asterism.example.org`、`192.0.2.x`)。

## 2. 作るもの

置き場所: **asterism-console のリポジトリの `deploy/vps/`** (Asterism は fmrb と別のプロジェクトなので、VPS の一式は console の側に置く)。

1. **compose 一式** (`deploy/vps/docker-compose.yml` など):
   - zenohd: (a) `ws/` を `127.0.0.1` にだけ出す (nginx の後ろ)。(b) mTLS の `tls/` を外に出す (自前の CA の証明書、`enable_mtls`)。(c) ACL (下の 3)。(d) 管理用の空間 (`@/**`) は外から見えない。(e) 接続数の上限 (使う版 1.10.1 で使える項目を確かめる)。
   - console (Rails、production) と bridge: `127.0.0.1` にだけ出す。bridge は同じ compose の zenohd に中でつなぐ。
   - **`bin/signer` (CA の秘密鍵) は入れない**。VPS の console は証明書を発行しない (手元の console で署名し、証明書だけを持ち込む)。その状態で台帳・ACL の生成・反映が動くようにする (発行の画面は「この console では発行しない」と出す)。
   - 秘密の値 (Rails の鍵、DB など) は `.env` 系のファイルで渡し、リポジトリに入れない (例のファイルだけ入れる)。
2. **nginx の設定の例** (`deploy/vps/nginx/`): VPS 本体の nginx に足す分。
   - console: サブドメイン (`asterism.example.org`、443、Certbot)。画面の自動更新 (Action Cable) の WebSocket も通す。
   - **管理の画面の道** (ルータの台帳・ACL・利用者の管理など、W2 の管理の機能) は、**全体の IP の絞り込みを外した後も** nginx で IP を絞る (`allow` / `deny`)。どの道が管理かを console の側でまとめ (例: `/admin/...` の下に寄せる、または一覧を docs に書く)、nginx の設定と食い違わないようにする。
   - ブラウザ版の `wss://`: nginx が TLS を終えて zenohd の `ws/` に転送する。**合言葉の確かめ** (下の 4) を nginx の `auth_request` で console に問い合わせる形にする。
3. **ACL** (W2 の台帳から作る今の仕組みに乗せる):
   - 対戦用のキーの範囲だけを許す (例: `asterism/*/game/**`。G1 の作りに合わせて決め、docs に書く)。デモ (`demo`) など、ほかの範囲は既定で通さない。
   - プレイヤーごとに、自分のノードの場所 (`asterism/<自分の ID>/**`) にだけ書ける (証明書の名前で)。
   - ブラウザ版 (合言葉で入る) のノードに許す範囲も決める。
4. **ブラウザ版の合言葉 (W4 の決定)**:
   - console にログインした利用者が、短い時間 (例: 10 分) だけ有効な合言葉を出せる (画面にボタン。ブラウザ版の URL `?zenoh=` に入れて開けるリンクを出す)。合言葉は 1 人ごと、記録に残る。
   - `wss://` の入口 (nginx) が、接続のたびに console に問い合わせて確かめる。期限切れ・取り消しは入れない。
   - ブラウザ版の側: 合言葉をどう渡すか (URL のクエリ、WebSocket の接続の URL に付ける) を、B2 の作り (`zp_ws_emscripten.c` の URL の組み立て) を読んで決める。**ブラウザ版 (fmruby-core) の直しが要るなら、案を書いて返す** (この段では fmruby-core のコードは触らない)。直しなしで確かめられる範囲 (例: 合言葉を URL に付けて手で開く、CRuby で `wss://` に合言葉付きでつなぐ) で確かめる。
5. **console を Web に出すための守り (W4)**:
   - 利用者は招待制 (管理者が招待のリンクを出す。勝手に登録できない)。
   - ログインの総当たりに制限 (回数の上限と待ち。例: rack-attack など、使い慣れた形でよい)。
   - 管理者は TOTP 必須。
   - 未ログインの人には何も見せない (ログインの画面だけ)。記録 (MCAP) は管理者だけが見られる。
   - 本番の設定: production、HTTPS だけの cookie (secure・SameSite)、画面の安全の設定 (CSP)、ログに秘密の値を出さない。
   - 重いトピック (カメラ) の見る頻度に上限。
6. **手元での確かめの構成**: VPS の nginx の代わりに nginx のコンテナ (自己署名の証明書、`asterism.localtest` などの名前) を立て、上の一式を VPS と同じ形でつなぐ。IP の絞り込みは、nginx の `allow` / `deny` とコンテナの網で再現できる範囲で。
7. **VPS に足す手順書** (`deploy/vps/README.md`): 置き場所への配置、秘密の値の作り方、証明書 (手元の console で署名して持ち込む)、nginx の設定の追加と `nginx -t`・再読み込み、ufw のポートごとの IP の絞り込み、さくらのパケットフィルタ (コントロールパネル) の設定、起動、動いているかの確かめ、止め方・戻し方。**今動いているほかのサービスに影響しない** ことを手順の中で確かめる手順を入れる。

## 3. 受け入れ条件 (手元の構成で)

1. 端末 (sim か AtomS3R か P4-Nano) が家のルータ経由 (W1 の形) で、CRuby が mTLS で直接、ブラウザ版 (または CRuby) が `wss://` と合言葉で、手元の「VPS 役」のルータにつながり、console の図に出る。
2. 証明書の無い mTLS の接続、合言葉の無い・期限切れの `wss://` が拒まれる。
3. ACL: 許した範囲だけが通り、他人の場所に書けない。管理用の空間が外から見えない。
4. console: 未ログインでは何も見えない。招待なしに登録できない。総当たりが止まる。管理の画面の道は、全体の絞り込みを外した設定でも、許した IP 以外から 403。記録は管理者だけ。
5. VPS の console に CA の秘密鍵が無い状態で、台帳・ACL の反映が動く。
6. 手順書がある。読み合わせで、ほかのサービスのポート・nginx の設定に触れないことが分かる。
7. 既存のテストが通り、新しい守りにテストを足す。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (ブランチ `feature/g0-w4`。コミットのみ、push しない)。親リポジトリの compose を参考にしてよいが、VPS の一式は console の `deploy/vps/` に置く。fmruby-core の `doc/ruby_asterism/report/g0_w4.md` (develop にコミットのみ。**コミットは必ずファイルを指定する。`git commit -a` は使わない**)。
- **触らないもの**: **VPS (ssh を含め、一切つながない)**。fmruby-core のコード (ブラウザ版の直しが要るなら案)、公開済みの gem、`.env`。
- **手元の資源**: fmruby-core の `build/` とブラウザ版のビルドの場所は、別の作業 (P4-Nano のビルド) が使っているので**触らない**。ブラウザ版が要るなら、写し `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/demo_dist/` (B2 の bundle) を使う。sim は使わない。端末は AtomS3R (WiFi、`/home/zenoh_echo.txt` で家のルータを指す) を使ってよい。
- **終わったら**: 起動したものを全部止める。
- **止まる条件**: ここに無い仕様の選択 (使う人から見える名前・画面・ACL のキーの範囲の決め方が G1 の作りに強く依存する場合など)、公開済みの gem の直し、ブラウザ版の直し (案を書く)。
- **report**: Japanese plain style (常体)。console の docs は英語。
