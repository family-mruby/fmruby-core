# 3.1.0 の前の直し その 3: 転送のアプリを本体に焼き込む

先に読む: `doc/release_3_1/plan.md` (3 章)、`doc/reference/TODO.md` の「転送のアプリのタスクのスタックの余りが 644 バイト」
(測った数と見立て)、`doc/remote_shell/plan.md` 7 章 (リモートシェルの決定)、`doc/reference/flash_layout.md`。
ブランチは今の `app-uart`。

## 何が困っているか

転送のアプリ (`/usr/share/transfer/transfer.app.rb`。ファイルの受け取り・送り・アプリの移動・リモートシェル) は、起動のたびに
storage から約 220 KB のソース (`transfer.app.rb`・`transfer_inbox.rb`・`receive_conf.rb`・`app_package.rb`・`fmrb_transfer.rb`・
`rshell.rb`、gem の `/usr/share/asterism/transfer/transfer.rb`・`sha256.rb`) を `require` でコンパイルしている。
- VM のプールの山が 2 MB のうち約 1.5 MB (`.mrb` にした試しで約 937 KB に下がった)。
- タスクのスタック (16 KB) の余りが 644 B。`.mrb` にしても変わらなかったので、原因はコンパイルではなく `require` の仕組み
  (`Sandbox#load_file`: 読むファイルごとに task を作って実行して待つ) と見ている (仮説)。本体に焼き込んだアプリ (シェル
  など。部品をビルドでまとめ、`require` しない) は同じ 16 KB で余りが数 KB ある。
- storage を約 0.2 MB 使う。

## 決定 (ユーザ 2026-11-26)

**転送のアプリを、シェルやエディタと同じ「ビルドでまとめて本体に焼き込む」形に移す。**

## 作る物

1. 転送のアプリとその部品を、`main/ruby/default_app/` の焼き込むアプリと同じ仕組み (`main/ruby/compile_ruby_to_bytecode.cmake`、
   部品をまとめる `# combine:` やサブフォルダ、`FMRB_MIXIN_*`) でまとめてバイトコードにし、本体に入れる。`require` を通らない形に。
   - カーネルが起こすパス (`main/ruby/kernel/fmrb_kernel.rb` の `TRANSFER_APP`) とその使い手 (`spawn_transfer_if_receiving`・
     `adopt_transfer`・`FmrbReceiveConf.tell_transfer`・シェル・受け取りのアプリ) を、焼き込んだアプリの起こし方に合わせる。
   - `.app.toml` の属性 (`large_memory`・`single_instance`・`movable`・`default_window_mode` ほか) を焼き込んだアプリでも保つ。
   - **リモートシェルの「切のときは実行しない」を保つ** (今は `rshell_enabled` のときだけ `rshell.rb` を読む)。焼き込んだ
     コードの中で、切のときはクラスを作らない・公開しない・メモリを使わない形にする (例: クラスの定義は入れるが、
     インスタンスを作らない)。切のときの VM の使用量の増分を測る。
   - checkpoint (アプリの移動。`movable = true` と名前の表) が今までどおり働くこと。
   - ほかのアプリが `require` している部品 (受け取りのアプリ・シェル・`install_ask.app.rb` が `receive_conf.rb`・
     `fmrb_transfer.rb` などを読む) は、storage に残すか、それぞれも同じ形にするかを、使い手を洗い出して決める。
     **同じソースを 2 か所に写さない** (焼き込む方の元は今の `flash/usr/share/transfer/` のままでもよい)。
2. storage から、焼き込んだ物だけを外す (`config/storage_exclude_common.txt` か、置き場所を移すか)。
3. ブラウザ版・ネイティブ版・sim も同じ形で動くこと (`rakelib/hosted.rake` の `hosted_regen_mrb!` など、ビルドの道ごとに)。
4. 試験: 今ある転送の試験 (`rake regress:transfer:quick`)、`test/rshell`・`test/receive_conf` が通ること。

## 測る物 (前と後)

- sim: 転送のアプリの VM のプールの山と、落ち着いた後の使用量 (切のときと、リモートシェルが on のとき)。
  `regress:run` の `running_transfer` の場面 (`rake regress:run ONLY=modern` は流してよい)。
- ファームの大きさの増分は見積もり (焼き込むバイトコードの大きさ)。ESP32 のビルドとスタックの実測は親がする。
- storage から減る量。

## 触ってよい範囲・確認・止まる条件

- fmruby-core の上の物と、その試験・生成物・文書。sdkconfig・`vendor/`・`lib/upstream/` の上流の写し・ほかのリポジトリ・
  gem の `transfer.rb` の元 (asterism のリポジトリ) は触らない (写しの置き場を変えるのはよい)。push しない。
- 確認: `rake regress`、`rake regress:transfer:quick`、sim (`rake clean` → `rake build:linux`) で受け取りのアプリから
  受け取り・リモートシェルを on/off し、PC の asterism の CLI から `rshell` の `open`・`exec_read` が通る (report/r1.md の手順、
  `rshell_uncertified`)。シェルの `asend`・`areceive`。ネイティブ版とブラウザ版のビルドが通る。
- **ESP32 のビルド・実機・シリアル・MCP の tab5_*・flash は触らない**。`rake clean_all` を流さない。重い回帰は上の 1 つだけ。
- 止まる: 利用者から見た動きが変わる選択が要るとき、焼き込むと S3 (NARYA v3。ファームの空き 4%) に入らない見込みが
  高いとき (見積もりを出して止まる)、範囲の外を変える必要が出たとき、内蔵 RAM の静的な増分が出るとき。

## report (`doc/release_3_1/report/r3.md`)

まとめ方と、ほかの使い手の扱い、測った数 (前と後)、S3 のファームの見積もり、見立てと違った点 (スタックの仮説が sim で
確かめられたか)、残り。実在の値を書かない。終わったら sim を止め、置いた物を消す。
