# B1 指示書: 端末の証明書の束から G3 を外す

> 状態: 指示 | 更新: 2026-10-22 | ESP-IDF の証明書の束 (全部入り) から、外す一覧 (今は emSign ECC Root CA - G3 だけ) を除いた自前の束を rake で作り、sdkconfig.defaults で端末に使わせる。計画は plan.md (同じフォルダ)

言葉: 文書 (report) は常体、「確かめ」ではなく「確認」。コードのコメントは英語。周辺の道具は Ruby (ルートの CLAUDE.md)。

## 1. 作るもの

1. **外す一覧** `config/cacrt_exclude.txt`: 1 行に 1 つ、IDF の束の中の証明書の名前 (PEM の前にある名前の行と同じ文字列)。`#` から行末は注記。最初の中身は `emSign ECC Root CA - G3` と、その理由 (2026-10、G3 の下の中間認証局 8 つの秘密鍵が誰でも計算できた。Mozilla・Google・Apple・Microsoft が遮断、Chrome は 2026-10-27 にルートを外す)。
2. **束を作る道具** (Ruby、`tools/` に。rake の `certs:bundle` から呼ぶ):
   - 入力は IDF の **全部入りと同じもの**: `$IDF_PATH/components/mbedtls/esp_crt_bundle/` の `cacrt_all.pem` と、IDF が全部入りに足す `cacrt_local.pem` (IDF の `esp_crt_bundle` の CMakeLists を読んで、全部入りのときに何を連ねているかを確かめ、同じにする)。IDF はビルド用の docker image の中にある (rake の他の作業と同じ docker の呼び方で読む。ホストには IDF が無い)。
   - 出力は `config/cacrt_fmrb.pem` (git に入れる。Mozilla の CA の一覧は MPL-2.0 で配ってよい)。先頭の注記に、元にした IDF の版、元の束の SHA-256、外した名前、作り方 (`rake certs:bundle`) を書く。
   - 外す一覧の名前が束に見つからなければ、エラーで止まる (名前の書き違いで外したつもりにならないため)。
   - 同じ入力からは同じ出力になる (2 回作って同じ SHA-256)。
3. **host の試験** (`rake test` から呼ばれる形か、既存の試験の流儀に合わせて): 出力の束に G3 が無いこと、枚数が「元の枚数 − 外した数」であること、各 PEM が読めること (Ruby の OpenSSL::X509::Certificate で全部読める)。
4. **sdkconfig.defaults** (ユーザが許可した。この 4 行だけを足し、他は触らない): Wi-Fi を使う 4 つ `config/sdkconfig.defaults.p4`・`config/sdkconfig.defaults.naryav4`・`config/sdkconfig.defaults.atoms3r`・`config/sdkconfig.defaults.n16r8` に
   ```
   CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
   CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_NONE=y
   CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y
   CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH="config/cacrt_fmrb.pem"
   ```
   パスの基準 (IDF がどこから相対で読むか) を IDF の Kconfig か CMakeLists で確かめ、正しい書き方にする。`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_MAX_CERTS` (今 200) に収まることも確かめる。**`sdkconfig` そのもの (ルートの生成物) は編集しない**。
5. **文書**: ASSETS.md か README の該当箇所に、束の出どころと作り直し方を 1〜2 行 (既存の流儀に合わせる)。

## 2. 確認

1. `rake certs:bundle` を 2 回流して同じ出力、試験が通る、`rake test` が通る。
2. **Tab5 向けのビルド**: `.env` の `FMRB_HW_TARGET` が TAB5 であることを確かめ、`rake clean_all && rake build:esp32`。生成された `build/sdkconfig` (または同等) に 4 行が効いていること、ビルドが作った束 (IDF が作る `x509_crt_bundle` の生成物) の枚数が自前の束と同じで、G3 を含まないことを確かめる (生成物のどこを見ればよいかは IDF の CMakeLists から)。`file build/fmruby-core.elf` で ESP32 のビルドであることを確かめる。
3. **焼かない** (Tab5 への書き込みと HTTPS の確認は親がする)。ビルドの成果物は build/ に残してよい (親がそのまま焼く)。
4. ほかの機種 (P4-Nano・AtomS3R・Retro) はビルドしない (sdkconfig.defaults の 4 行の書き方が同じなので、Tab5 で確かめれば足りる)。

## 3. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop): `config/cacrt_exclude.txt`・`config/cacrt_fmrb.pem`、`tools/` の道具、`rakelib/` の rake、試験、上の 4 つの sdkconfig.defaults の 4 行だけ、ASSETS.md か README の 1〜2 行、report。build/ は作り直してよい。
- **触らないもの**: `sdkconfig` (生成物)、他の sdkconfig.defaults、`lib/patch/picoruby-socket` の C (束の付け方は変えない)、submodule の中、fmruby-graphics-audio、VPS、Tab5・シリアル (焼かない)、ユーザの鍵。
- push・タグはしない。`git commit -a` は使わない。件名は `tools:` / `config:` など履歴に合わせ、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値を差分で grep してからコミット。fmruby-core の doc (plan・この指示書・report) はコミットしない (親がする)。
- **止まる条件**: IDF の全部入りの作りが想定 (cacrt_all.pem + cacrt_local.pem) と違い、同じものを作れない。4 行では自前の束に替わらない (Kconfig の名前や組み合わせが違う)。ビルドが通らず、直すのに範囲外の変更が要る。止まったら report に状況と選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ca_bundle/report/b1.md`、常体): 道具と rake の使い方、元の束 (IDF の版・枚数・SHA-256・Mozilla の日付)、自前の束 (枚数・SHA-256)、4 行の効き方の確認 (build の sdkconfig と生成物の枚数)、試験の結果、コミット。
