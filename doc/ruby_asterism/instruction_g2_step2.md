# Asterism G2 ステップ 2 指示書: 部屋 (署名つきの鍵で入る、期限つきの名前空間) を作る

> 状態: 指示 | 更新: 2026-10-14 | ステップ 1 (report/g2_step1.md) の案と、ユーザの決定 (plan.md の G2 の節「ステップ 1 の結果と決めたこと」) のとおりに、部屋を作る。console・door・picoruby-asterism-zenoh・ブラウザ版。ゲームホストと麻雀は次のステップ

前提: plan.md の「G2: 部屋 (パスワードで入る期限つきの名前空間)」の節全体 (使い方の前提と守りの重さ、ステップ 1 の決定)、report/g2_step1.md と試作 (C:\Users\Public\asterism_deploy\g2_step1\)、game_rooms_comparison.md、asterism-console (Relay::CloudConfig、bin/ws_door、signer、deploy/vps、docs/install.md)、picoruby-asterism-zenoh、fmruby-core の wasm/web。

言葉: 文書は常体、「端末」「確認」「ステップ」。技術の用語は例えに置き換えない (ACL・登録・証明書・鍵)。IT で定着したカタカナ語 (パスワード・バックアップ) はそのまま。console と asterism の文書・画面は英語。

## 1. 決まっていること

- 部屋は全世界に公開しない。リンク (鍵) は知り合いに配る。中の人を疑う守りは軽く、入口の手前の守りは必ず入れる: door は鍵を確かめる前に zenohd へ何も渡さない、接続数・頻度・1 通の大きさの上限、最新に保つ。
- 枠 8 個 (設定で変えられる)、枠ごとに証明書と ACL を一度だけ作る。console は空いた枠を部屋に割り当て、使い回すときは前の部屋の接続を door が全部切ってから。
- 1 部屋の接続の上限 16 (卓 4 + 観戦 12)、door 全体の部屋の接続の上限 48 (既定、設定で変えられる)、zenohd の max_sessions 64 → 96。
- 部屋の鍵: console が Ed25519 で署名 (部屋・枠・期限・接続の上限・乱数の ID、役割は入れない)、door は公開鍵 (kid つきで複数) で確かめる。**署名の秘密鍵は signer の volume**。取り消しは console が書く一覧を door が読み、入っている接続も切る。期限の上限 24 時間、既定 3 時間。リンクのフラグメントで渡し、サーバの記録に残さない。
- 試す回数の制限: nginx の頻度の上限と、署名の正しくない鍵にだけかける上限。
- キーは `room/<枠>/asterism/...`。picoruby-asterism-zenoh の C の層で前置きを付け外しする (zenoh-c の `namespace` と線の上で同じ形)。アプリも asterism の Ruby の層も変えない。
- 今の証明書の仕組み (mTLS・固定のポート、家の IoT・端末) はそのまま残す。今のブラウザのパス (console のアカウント・ハンドル) の入口もそのまま動く。
- ゲームホスト (CRuby) は枠の証明書で tls/ に直接入る形を想定しておく (このステップでは作らないが、枠の証明書を CRuby で使える形にしておく)。

## 2. 作るもの

1. **asterism-console**: 部屋 (Room) の作成・一覧・削除・期限の画面 (管理者)、枠の割り当てと使い回し、鍵の署名 (signer に署名を頼む口、kid)、取り消しの一覧、リンク (ブラウザ版のページ + フラグメントの鍵 + 開くアプリ)、Relay::CloudConfig に枠ごとの subject と ACL (`room/<枠>/**` と、ステップ 1 で見つかった `@adv` の分)、max_sessions。部屋の様子 (接続数) を管理者の画面に。docs (install.md に部屋の節と、nginx の設定の追加)。
2. **door** (今の bin/ws_door を広げる): 部屋の鍵の確認 (署名・期限・取り消し・枠)、ws を mTLS に詰め替えて枠の証明書で zenohd の tls/ に繋ぐ (InitAck の batch_size の書き換え)、上限 (部屋ごと・全体・1 通の大きさ)、期限と取り消しで切る、Ruby の thread の stack を絞る。今のブラウザのパスの入口はそのまま。
3. **picoruby-asterism-zenoh**: 名前空間の前置きの付け外し (キーを渡す口 7、受ける口 4。ステップ 1 の数え)。設定で前置きを渡せる口。試験。
4. **ブラウザ版** (fmruby-core の wasm/web): フラグメントの部屋の鍵を読み、前置き `room/<枠>` を設定し、door への URL に鍵を付ける。部屋に入っていることを画面に出す。console の今のパスのリンクとの違い (ノード ID の扱い) を決めて書く (部屋の中ではノード ID は自由でよいか、ブラウザ自身の ID か)。
5. **今の役割の `@adv` の穴の確認**: match・demo の ACL で advanced pub/sub の履歴が届かないかを確かめ、届かなければ Fill in のひな形と既存の役割の直し方を出す (直すかはユーザ)。

## 3. 受け入れ条件

1. 手元の構成 (deploy/vps と同じ形、nginx・door・signer を含む) で: 管理者が部屋を作り、リンクを 2 つのブラウザで開くと、同じ部屋の中でオセロが指せる。別の部屋と、登録済みの端末 (家のルータ・P4・AtomS3R 役の CRuby) とは、どちら向きにも何も届かない。
2. 期限切れ・書き換えた鍵・取り消した部屋は入れない。取り消すと入っている接続が切れ、つなぎ直しも断られる。枠を使い回すとき、前の部屋の接続は残らない。
3. door は鍵を確かめる前に zenohd へ 1 バイトも渡さない (試験で示す)。上限 (部屋 16・全体 48・1 通の大きさ) が効く。
4. 今の仕組み (ハンドルのパスのブラウザ版、mTLS の端末) がそのまま動く。
5. console・asterism の試験、rubocop、picoruby-asterism-zenoh の試験、wasm のビルド、sim の標準構成の起動が通る。
6. 測る: door を通る遅れ、1 接続のメモリ、16 接続の部屋が 3 つのときの VPS 相当のメモリ。

## 4. 範囲・決まり

- **触ってよいもの**: asterism-console (main)、picoruby-asterism-zenoh (main)、asterism (main、要るなら)、fmruby-core (`feature/asterism-g2-rooms`、develop から)。すべてコミットのみ、push しない、タグを打たない、版を上げない (console の版は上げてよいが、タグは打たない)。コミットはファイルを指定する (`git commit -a` を使わない、fmruby-core の `.env` に触らない)。
- **触らないもの**: VPS (ssh しない)、`~/asterism-home/`、端末 (焼かない)、sdkconfig。
- ユーザに見せるファイルは `C:\Users\Public\asterism_deploy\` の下だけ (直下にフォルダを作らない)。
- **終わったら**: 起動したものを全部止める。試し用の image と volume を消す。fmruby-core の build/ は Linux の標準構成 (x86-64)、作業コピーは develop。
- **止まる条件**: 1 章で決めきれない選択が出た (案を書いて返す)。端末のファームの C や sdkconfig を変えないとできない。VPS のメモリに収まらない見込み。
- **report** (`doc/ruby_asterism/report/g2_step2.md`、常体): 作り、受け入れ条件ごとの結果と数字、`@adv` の確認の結果、ユーザがすること (VPS の更新・nginx の追加・部屋の作り方)、残った課題。
