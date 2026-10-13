# Asterism G2a 指示書: 端末が TLS (mTLS) でクラウドのルータに直接繋ぐ

> 状態: 指示 | 更新: 2026-10-13 | 家のルータを置かずに、Family mruby の端末 (P4-Nano・AtomS3R) が、端末ごとの証明書でクラウドのルータへ mTLS で直接繋ぐ。端末 1 台が 1 つの拠点になる。2 つのステップに分け、ステップ 1 (調べと sdkconfig の案) でいったん止まってユーザの承認を待つ

前提: plan.md の G2a の行と「ルータなしで直接つなぐ」の段落、O1 の 6 (証明書を tar 1 つで渡す)、report/o1.md (tar の中身・`asterism.json`・CRuby の `credentials:`)、report/g1_5b.md・g1_5b_p4.md (端末のつなぎ直し、`zp_tcp_esp32.c` の読みの直し)、report/h3.md (AtomS3R の Asterism、zenoh-pico の確保は PSRAM)、picoruby-asterism-zenoh (`include/zenoh_generic_config.h`、`ports/esp32/`、ZENOH_PICO_PIN)、asterism の `mrblib/` (portable API の `Asterism.connect`)、asterism-console の `deploy/local/` (手元の通し稽古の構成) と `docs/install.md`。

言葉: 文書では「端末」を使い「機体」は使わない。IT で定着したカタカナ語 (バックアップ、パスワード等) はそのまま使う。

## 1. 決まっていること (2026-10-12・10-13 ユーザ)

- ESP32 の端末は TLS が使えないわけではない (ESP-IDF の mbedTLS)。今まで使っていなかっただけ。
- 端末ごとに証明書を持ち (console の Registry で client として登録、役割を選ぶ、名前 = 端末のノード ID)、クラウドのルータへ mTLS で繋ぐ。
- 証明書は O1 の tar の形で渡す。端末の側でも tar 1 つで繋がる形を目指す (CRuby の `credentials:` と同じ考え)。
- **内蔵 RAM は増やさない** (増分 0 が最低線)。mbedTLS の確保は PSRAM に回す。速度より RAM を優先する。
- **sdkconfig は変えない。変更が要るなら、項目と値と理由と見積もりを案にしてユーザの承認を取る** (ステップ 1 の終わりで止まる)。
- 家のルータ経由の今の形 (平文の TCP、LAN の中) は残す。どちらで繋ぐかは端末の設定で選ぶ (`/home/zenoh_echo.txt` の宛先が `tls/...` なら TLS、など。形は案を出す)。

## 2. ステップ 1: 調べと案 (ここで止まって返す)

1. **zenoh-pico の TLS**: ピンの zenoh-pico (ZENOH_PICO_PIN) に TLS のリンク (mbedTLS) があるか、ESP-IDF の上で使えるか、mTLS (クライアントの証明書) と CA の指定と名前の確かめ (`verify_name_on_connect` に当たるもの) ができるか。無い・足りないなら、binding の ESP-IDF の口 (`ports/esp32/`) に TLS の口を足す案 (esp-tls か mbedTLS を直接)。G1.5b のつなぎ直し (`Z_FEATURE_AUTO_RECONNECT`、`_z_tcp_read_exact` の直し) と同じ穴が TLS の読みにも無いか。
2. **メモリの見積もり**: TLS の握手と接続中に要るメモリ (mbedTLS の入出力の buffer、証明書の解析、握手の一時の確保) を、ESP-IDF の既定の設定で見積もる。内蔵 RAM から取られるものと PSRAM に回せるものを分ける。P4-Nano と AtomS3R (S3、内蔵 RAM が少ない) の両方。
3. **sdkconfig の案**: 要る項目 (例: `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`、`CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` / `OUT_CONTENT_LEN`、可変長の buffer、使う暗号の絞り込み、証明書の束 (bundle) を外す、など) を、対象の構成 (NARYAv4・TAB5・ATOMS3R、ほかの構成への影響) ごとに、今の値・案の値・理由・見積もった増減で表にする。**書き換えはしない**。sdkconfig の変更なしで済む道があるなら、それを第一案にする。
4. **端末への証明書の入れ方**: tar を端末の `/home` に置いて gem (portable の `Asterism.connect(credentials:)`) が読む形か、tar を展開して 3 つのファイルを置く形か。端末の Ruby (picoruby) で tar を読めるか (Regexp なし、`pack` なし、`File.binread` なし、の制約: memory の既知の制約)、C の側で読む方がよいか。鍵の置き場所の権限 (端末にはファイルの権限が無い) と、遠隔の道具 (`/fs/*`) で鍵が見えてしまうことの扱い。
5. **手元の試しの構成**: asterism-console の `deploy/local/` (または同じ形の docker) で、mTLS の zenohd に端末を直接繋ぐ試しの手順。端末の通信先は PC の LAN の IP。
6. **ステップ 2 の作業の見積もり**: どこに何を足すか (picoruby-asterism-zenoh・asterism・fmruby-core)、flash の増え、繋ぐまでの時間の見込み (目安 1-2 秒)。

ステップ 1 の report (`doc/ruby_asterism/report/g2a.md` の前半) を書いて、**コミットして止まって返す**。ファームの書き換え・sdkconfig の変更・焼き直しはしない (調べのためのビルドはしてよい。そのとき sdkconfig は変えない)。

## 3. ステップ 2: 実装と確かめ (ユーザがステップ 1 の案を承認してから)

親から承認の内容とともに続きの指示が来る。そのときに、承認された sdkconfig の変更を入れ、TLS の口・証明書の読み・設定の選び方を作り、次を確かめる:

1. P4-Nano と AtomS3R が、手元の mTLS の zenohd に、家のルータなしで繋がり、オセロ (P4) と `asterism_demo` (AtomS3R) が動く。console の図に出る。
2. 内蔵 RAM の増分 0 (同じコミットで、TLS ありとなしを比べる。周期のダンプの値と、握手の最中の最低値)。PSRAM の増え。flash の増え。
3. 繋ぐまでの時間、つなぎ直し (ルータの起動し直し) の時間。
4. 証明書が無い・期限切れ・別の CA のとき、断られて落ちない。
5. 家のルータ経由 (平文) の今の形が、そのまま動く。
6. 作者の VPS での確かめは、ユーザが console で端末を登録してから、親が一緒に行う (このステップでは手元まで)。

## 4. 範囲・決まり

- **触ってよいもの**: picoruby-asterism-zenoh・asterism (main、コミットのみ、版は上げない)、fmruby-core (`feature/asterism-g2a`、develop から、コミットのみ)。push しない。`git commit -a` は使わない (fmruby-core の `.env` にユーザの手元の変更がある)。コミットは必ずファイルを指定する。
- **触らないもの**: **sdkconfig と sdkconfig.defaults (ステップ 1 では一切。ステップ 2 では承認された項目だけ)**、`.env` (ターゲットは環境変数 `FMRB_HW_TARGET=...` で渡す)、fmruby-graphics-audio、公開済みの gem の版、**VPS (ssh で何かをすることは一切しない)**、`~/asterism-home/`。
- **端末**: ステップ 1 では焼かない。ステップ 2 では P4-Nano (192.168.10.10、NARYAv4) と AtomS3R にアプリの区画だけを焼いてよい (全体の焼き直しは `/home` を消すのでしない)。シリアルは MCP の serial_* を使う。ミュートのまま。
- **終わったら**: 起動したものを全部止める。fmruby-core の `build/` は Linux の標準構成 (x86-64) に戻す。作業コピーは develop に戻す。
- **止まる条件**: ステップ 1 の終わり (必ず)。ステップ 2 で、内蔵 RAM を増やさずにできない・承認された範囲の外の sdkconfig の変更が要る・ファームの C の大きな作り替えが要る、と分かったとき。
- **report** (`doc/ruby_asterism/report/g2a.md`、常体): ステップ 1 は調べの結果・メモリの見積もり・sdkconfig の案の表・証明書の入れ方の案・ステップ 2 の見積もり。ステップ 2 は作り・受け入れ条件ごとの結果と数字・残った課題・ユーザがすること (console での端末の登録、VPS での確かめ)。
