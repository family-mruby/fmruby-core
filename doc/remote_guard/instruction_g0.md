# G0 指示書: 開発のビルドで読み書きを自由にし、/home を WiFi で退避・書き戻しする

> 親: plan.md の方針 (開発のビルドとリリースのビルドで分ける) と段 G0。今の作りは doc/dev_remote_ctl/plan.md、
> `main/drivers/devctl/devctl_http.c`、`fmrb_secret_path.h`。道具は family-mruby の `tools/fmrb_rd_fs.rb` と MCP
> (`tools/mcp/`)。時間の目安は doc/regression/usage.md 0 章。

## ユーザの希望 (2026-11-15)

- 開発のビルドは読み書きを自由にしたい (開発をスムーズに)。鍵の読み出しも含める。
- /home の退避をもっと効率的に (今は storage 8 MB をシリアルで読み出し、PC で /home を重ねて全体を 115200 bps で書いている)。
- CH34x の端末 (NARYAv3・P4-Nano) の書き込みは最初から 115200 bps。

## やること

1. **開発のビルド (`FMRB_DEV_REMOTE_CTL`) の devctl**: `/fs/get` の鍵の除外 (FMRB_PATH_SECRET・PASSWORD) と、道具が触れる
   場所の制限 (`/app`・`/home`・`/usr/share`・`/mnt/sd` 以外を断る所) を外し、`/` の下を全部読み書きできるようにする。
   BLE の console の決まり (`fmrb_secret_path.h` の共有) は変えない。リリースのビルドは devctl が入らないので今は影響が
   無いが、後で G1・G2 を入れるときに分けられるよう、「開発のビルドだけ自由」をコードの 1 か所 (定数か関数) で表す。
2. **/home の退避と書き戻しの道具**: `tools/fmrb_rd_fs.rb` に、鍵ごと /home を取る `backup <dir>` と、戻す `restore <dir>` を
   足す (pull/push の上に。空のディレクトリ・属性の扱い、途中で切れたときのやり直し)。退避の置き場の既定は
   `~/asterism_keys/devices/<機種>-<MAC 下 3>/home_backup_<日時>/` (700/600。機種と MAC は `/status` から。取れなければ
   引数で)。中身 (鍵) を標準出力に出さない。
3. **焼くときの流れ**: `rake flash:keep_home` (fmruby-core) と MCP の flash の選択肢 `keep_home: true` で、
   (a) WiFi で /home を退避 → (b) 全体を焼く (CH34x は 115200 bps) → (c) 起動して WiFi が上がるのを待つ → (d) /home を
   書き戻す → (e) 再起動。WiFi の設定は /home にあるので、(c) で WiFi がつながらない端末 (/etc の既定で上がらない) では
   止まって「シリアルの退避を使う」と返す。今の手順 (read_flash → littlefs で重ねる) は道具の中で予備として残す。
4. **文書**: doc/dev_remote_ctl に開発のビルドの決まりを足し、README の「開発用・無認証」の注意を更新。CLAUDE.md の
   Tab5 遠隔の節に退避と書き戻しの使い方を 1 行。

## 確かめること

- `rake regress`。devctl の変更は sim・ネイティブ版に同じ口があればそこで (無ければ report に)。
- 実機 (P4-Nano、親が焼く): 親が焼いた後、`backup` → 中身の確認 (鍵のファイルがあること、大きさだけ表示) → 小さな
  ファイルを足して `restore` → 戻ったこと。`rake flash:keep_home` の通しは親が P4-Nano で流す (サブはシリアルと flash を
  使わない)。

## 触ってよい範囲

- fmruby-core (develop)・family-mruby の tools/ と tools/mcp/。graphics-audio は変えない。既存の未コミット
  (components/ のサブモジュール 2 つ、wasm/vendor/) には触らない。シリアル・flash・/dev/tty* は使わない。
- 鍵の中身を表示しない・ログに出さない・リポジトリに入れない。退避の像は ~/asterism_keys/ の下だけ。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。

## 止まる条件

- リリースのビルドの動きが変わる。仕様が変わる選択 (上の決まり以外)。ユーザの作業が要るとき。

## report (report/g0.md)

変えた所、道具の使い方、退避の置き場、焼く流れと所要時間の見込み (今の手順との比較)、確かめた結果。開始と終了の時刻は
`date` で書く。
