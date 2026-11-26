# R1 の指示: 端末の側の見るコマンドと補完

先に読む: `doc/remote_shell/plan.md` (とくに 7 章の決定) と `report/r0.md`。7 章が正で、ほかの章と食い違えば 7 章に従う。

## 作る物

1. `flash/usr/share/transfer/rshell.rb` (新規。約 500 行)。画面なしで動くコマンドの実行部。転送のアプリから使う。
   - 行の読み方: 空白で分け、`"a b"` と `a\ b` を読む。
   - パス: 今いる場所 (セッションごと。始まりは `/home`) からの相対を解き、`.`・`..`・`//` を畳む。
   - 鍵の線引き: 下の 3 の判定で、鍵に当たるパスは読ませず理由を返す。`ls` の一覧には名前を出してよいが、鍵のフォルダ
     (`/home/asterism`) の中身は出さない。
   - 見るコマンド: `pwd`・`cd`・`ls` (`-l` で大きさ)・`cat`・`head [-n N]`・`tail [-n N]`・`stat`・`df`・`free`・`ps`・`ver`・`help`。
     使える API は report/r0.md の調査のとおり (`File.stat`・`mtime`・`seek` は無い)。
   - 変えるコマンド (`cp`・`mv`・`rm`・`mkdir`・`kill`・`open`) は**まだ作らない**。表には「変える」として名前だけ載せ、
     呼ばれたら「この版には無い」と返す (R2 で作る)。
   - 出力は 1 回 8 KB (UTF-8 の境目で切る)、続きは `more`、1 コマンドの合計は 64 KB で切ってその事を返す。大きいファイルの
     `cat` で全部を一度にメモリに読まない。
   - 補完: 行の先頭はコマンドの名前、ほかはパス (フォルダは末尾 `/`、空白のある名前は `\ ` で返す、候補は 200 まで)。
     鍵のフォルダの中身は候補に出さない。
   - `read(id, path, pos, len)`: ファイルの一部を返す (len は 8 KB まで。答えに全体の大きさ)。鍵は断る。
2. 転送のアプリ (`flash/usr/share/transfer/transfer.app.rb`) に、オブジェクト `rshell` を公開する所を足す。
   - メソッド: `open`・`exec_read`・`exec_write`・`more`・`complete`・`read`・`close` (`caller: :from`)。
   - 受ける条件: `receive.toml` の持ち主の部分に `rshell_enabled = true`、かつ呼んだノードが種類 `rshell` の一覧
     (手書きか console が配った部分) にあること。満たさないときは理由つきで断る。転送の今の確認 (`refused`・`certified?`) の
     作りに沿う。`flash/usr/share/transfer/receive_conf.rb` の `KINDS` に `rshell` を足す。
   - `rshell.rb` は `rshell_enabled` のときだけ読む (切の端末のメモリを増やさない)。
   - `open` の答え: セッションの番号・今いる場所・端末の版・コマンドの表 (名前 → 見る / 変える)。
   - セッションは 1 つ。別のノードからの `open` は、今のが 5 分呼ばれていなければ入れ替え、そうでなければ断る。
   - カーネルが転送のアプリを起こす条件 (`main/ruby/kernel/fmrb_kernel.rb` の `spawn_transfer_if_receiving`) と、転送の
     アプリが「全部切なら終わる」所に `rshell_enabled` を足す。スイッチの画面 (受け取りのアプリ) は R2。R1 の確認では
     `receive.toml` を手で書く。
3. 鍵の判定を Ruby に出す。`main/kernel/fmrb_secret_path.c` の判定を呼ぶクラスメソッドを 1 つ (置き場は既存の
   `FmrbFile` か `FmrbApp` の C の定義に合わせる。例: `FmrbFile.secret_path?(path)`)。`sig/` に書き、`rake sig:coverage`
   の doc gaps を 0 に保つ (`doc/reference/sig_writing_and_coverage.md`)。
4. 試験: `rshell.rb` の、行の読み方・パスの正規化・鍵の線引き・出力の分け方・補完を、ホストで流せる試験にする
   (置き場と流し方は `test/api_fixes/run.rb` など今ある形に合わせ、`rake test` に入れる)。

## 触ってよい範囲

fmruby-core の上の 1〜4 のファイルと、その試験・sig・生成物 (`tools/boundary/gen.rb` の SDK など、道具が求めるもの)。
シェル (`main/ruby/default_app/shell*`)・`vendor/`・`lib/upstream/`・sdkconfig・ほかのリポジトリ (console・asterism gem) は触らない。
ブランチは今の `remote-shell`。コミットはしてよい (件名は `<種類>(<場所>): <要約>`、英語)。push しない。

## 確認

- `rake regress` を流し、落ちたものだけ見る。`rake sig:coverage`。足した試験。
- sim で通す: `rake clean` → `rake build:linux` → sim を起こし、`receive.toml` に `rshell_enabled = true` と呼び手を書いて、
  PC の asterism の CLI (`call <node>/transfer/rshell open` → `exec_read`) で `ls /home`・`cat`・`ps`・大きいファイルの
  `cat` と `more`・鍵のパスの断り・`complete` を確かめる。sim と PC の CLI のつなぎ方は
  `doc/asterism_file_transfer/report/` と `doc/regression/usage.md` (`regress:live`) にある。
- `rshell_enabled` が無い端末では、転送のアプリの VM の使用量が始める前と変わらないこと、ON のときの増分 (バイト) を測る。
- **ESP32 のビルドはしない** (親が検収でする)。重い道具 (`regress:run`・`size`・`screens`) も流さない。

## 止まる条件 (止まったら report に書いて返す)

- 利用者から見た動き (仕様) が変わる直し方を選ぶ必要が出たとき。計画の 7 章と食い違う事実が出たとき。
- 触ってよい範囲の外を変える必要が出たとき (とくにシェル・gem・カーネルの起動の条件のほかの所)。
- 1 回の呼び出しで 8 KB が通らない、公開したメソッドの形が gem で作れない、など作りの前提が崩れたとき。

## report (`doc/remote_shell/report/r1.md`)

メソッドの引数と答えの形 (console の側が R3 で読む)、コマンドごとの出力の例、測った数 (VM の増分)、見立てと違った点、
撤回した仮説、残件。実在の値 (ドメイン・IP・ノード名・人の名前) を書かない。終わったら sim を止める。
