# T7 指示書: 受け取りの有効・無効と、端末での編集

> 状態: 実装中 | 更新: 2026-11-10 | 正は plan.md の「決定事項 (2026-11-10 ユーザ、T7)」

## 範囲

1. **置き場所と読む順**
   - 配布物に `flash/etc/asterism/receive.toml` を足す (`enabled = false`、相手の一覧は空、短い説明の注釈)。
   - 利用者の設定は今の `/home/asterism/receive.toml`。`/home` にあればそれを、無ければ `/etc` を読む
     (カーネル・転送のアプリ・`FmrbReceiveConf`・Shell の `asend -l` のすべてで同じ読み方。読む所は 1 か所にまとめる)。
   - `enabled` の行が無いファイルは無効と読む。
2. **起動の条件**: カーネルの `spawn_transfer_if_receiving` は「`enabled = true` のとき」に変える
   (ファイルの有無ではなく)。
   - 動いている転送のアプリは、`enabled` が false になったら新しい `begin` を断って終わる。
     途中の受け取りの扱いは report に書く (止めるか、終わるまで待つか。決めかねたら止まって返す)。
3. **保存の仕組み (画面と Shell で共通)**: `FmrbReceiveConf` (か、その隣) に、`enabled` の切り替え、
   種類ごとの相手の追加・削除、保存を足す。
   - 保存は `/home` へ。初めてなら `/etc` の写しから作る。
   - 手書きの分だけを変え、`[managed]` の節 (console が配った分) と印の行は書き換えない。
   - 書くのは中身が変わったときだけ (ESP32 の flash)。
   - 有効にしたら転送のアプリを起こし、無効にしたら止める (既にある起動・停止の口を使う。無ければ report に書く)。
   - 相手の名前の決まりは今の receive.toml と同じ (英数字と `.` `_` `-` `*`、64 行まで)。
4. **アプリ** `flash/app/tool/` の新しいアプリ (名前は既存に合わせて決め、report に書く。表示名は「受け取り」/ "Receiving")。
   - ESP32 (Retro 320x240・Modern)・ブラウザ版・ネイティブ版で同じものが動く。日本語と英語 (language に従う)。
   - 有効・無効のスイッチ。種類 (file・checkpoint) ごとに許す相手の一覧。
   - 相手は今ネットワークに見えているノードから選べる (Asterism のノード一覧を使う)。
     見えていない相手は名前を打って足せる。
   - `[managed]` の行は「console から」と分かる形で、読むだけで見せる。
   - アプリの書き方は fmrb-app-new skill の規約に従う。
5. **Shell の `areceive`** (`asend` の隣、shell_commands.rb)
   - `areceive`: 状態 (有効・無効、どのファイルを読んでいるか、種類ごとの相手、手書きか console か)。
   - `areceive on|off`、`areceive allow|deny <file|checkpoint> <ノード>`。
   - Shell の help・文書に足す。
6. **console (asterism-console) の表示**: 受け手の端末が無効のとき、行のページの
   「The list on its machines」で分かるようにする。無効の端末では転送のアプリが動いていないので、
   配れないときの理由の文言に「受け取りが無効かもしれない」旨を足す程度でよい
   (端末に新しい口を足すほどなら、止まって返す)。CHANGELOG の Unreleased に。
7. **文書と試験**
   - 既存の試験・場面 (`regress:transfer`・`regress:transfer:quick`・`regress:checkpoint`) で receive.toml を
     置いている所に `enabled = true` を足す。
   - quick に場面を足す: `/etc` だけ (無効) で転送のアプリが起きない・`areceive on` で起きて受ける・
     `enabled` の行の無い /home のファイルは無効・`areceive off` で断る。
   - plan.md は親が更新する。利用者向けの文書 (receive.toml の書き方がある所) は直す。
     次のリリースの案内に入れる 1 行 (「今の receive.toml は `enabled = true` を足すまで受けない」) を report に書く。

## 確認

- `rake regress`、`rake regress:transfer:quick` (WEB=1 も)、`rake regress:transfer`、`rake regress:checkpoint`
  (実機の BOARD= は除く)。console は `bin/rails test`。
- 新しいアプリの画面: ネイティブ版 (native_up) とブラウザ版 (web_up) で撮り、Retro の大きさは sim で撮る。
  画像は scratchpad に置き、パスを report に。
- `rake regress:size` (内蔵 RAM の増分 0)。

## 触ってよい範囲

fmruby-core の転送まわり・カーネルの起動の条件・Shell・新しいアプリ・flash/etc・試験の道具・文書。
asterism-console の表示と CHANGELOG。asterism gem は触らない (要るなら止まって返す)。
コミットまで (fmruby-core は develop、console は main)。push・タグ・公開・VPS はしない。
ユーザの ROM・NSF などをリポジトリに入れない。実在の IP・ドメイン・ホスト名を書かない。

## 止まる条件

- 決定と違う動きが要る (例: managed を書き換えないと作れない、`enabled` の無いファイルを有効と読む必要が出た)。
- 転送のアプリの止め方で、途中の受け取りを捨てるかどうかを決めかねたとき。
- 範囲外 (gem・ほかのアプリ) の変更が要るとき。

## report

`doc/asterism_file_transfer/report/t7.md` (作ったもの・計測値・見立てと違った点・撤回した仮説・残件)。
終わったら build/ は回帰の道具が戻した状態のままでよいが、自分で作った worktree は残さない。
