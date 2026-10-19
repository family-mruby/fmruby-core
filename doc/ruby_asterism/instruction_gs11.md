# GS11 指示書: `asterism-games play` (端末でオセロと麻雀を遊ぶ)

> 状態: 指示 | 更新: 2026-10-19 | 端末でゲームを遊ぶコマンドを `asterism-games play <ゲーム>` にまとめる。今の `othello-tty` をその 1 つにし、麻雀の端末版を新しく作る。麻雀の牌は絵文字 (🀇🀈🀉) で出す

前提: asterism-games (UP8 の後の main、0.16.3)。`bin/asterism-games` (ホストのコマンド)、`bin/othello-tty` (オセロの端末版: `--router` / `--credentials` / `--namespace` / `--table` / `--name` / `--commands` / `--until-over`、on_join / on_leave でホストの出入りを追う)、`bin/mahjong-bots` (麻雀を自動で打つボット)、麻雀の卓 (`app/asterism_games/mahjong/table.rb`: view の鍵は README の表、`act` などのメソッド、MJ9 の `in` (局の始まり) と `ok` (結果の OK)、AI の席、観戦)、README「The othello table」と麻雀の節。asterism の CLI は `ASTERISM_CREDENTIALS` / `ASTERISM_ROUTER` を読む (asterism の未公開の main、ca139a7)。fmruby-core の麻雀アプリ (`flash/app/game/mahjong/`) は画面の作りの参考 (読むだけ)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-games の文書は英語。端末の表示の言葉は日本語 (麻雀アプリの決まりに合わせる: 東1局、本場、供託、ドラ、リーチ、ポン …)。

## 1. 決まっていること (2026-10-19 ユーザ、選択形式)

- 端末で遊ぶコマンドは **`asterism-games play <ゲーム>`** にまとめる (`play othello`、`play mahjong`)。
- 麻雀の牌は **絵文字の牌** (Unicode の麻雀牌、🀇🀈🀉 …)。見た目の例 (ユーザが選んだもの):

```
東1局 0本場  供託0  残り62  ドラ 🀅
南 Jev    25000  🀃 🀏 🀙 🀂
西 CPU2   25000  🀆 🀙 🀖
北 Claude 24000* 🀀 🀈 [🀔]
東 自分   26000  🀄 🀡

🀇🀈🀉🀜🀝🀞🀒🀓🀔🀀🀀🀆🀆 🀕
 a b c d e f g h i j k l m  n

自分の番: 打つ牌 (文字か 6s)、riichi、tsumo
> 
```

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **幅の崩れへの備え**: 絵文字の牌は端末・フォントで幅が揃わない (🀄 は色つきの絵文字で 2 幅になりやすい)。牌の下の選ぶ文字がずれにくい並べ方を工夫する (例: 牌と文字を「a🀇 b🀈」のように組にする、牌ごとに空白を入れる、など。実際の端末で見て決める)。表示が崩れる端末のために **`--tiles mpsz`** (123m 456p … の略号) に切り替えられる。既定は絵文字。
- **打ち方**: 手牌の文字 (a〜n) か、MPSZ の略号 (`6s`、赤は `0p`)。鳴き・宣言は `riichi` `tsumo` `ron` `pon` `chi` (候補が複数なら番号か牌で選ぶ) `kan` `pass` `ok` (結果の OK) など、画面の下に「今できること」を出す。
- **見せるもの**: 局と本場・供託・残り・ドラ、4 人の名前・点数・リーチ (`*`)・自分の席、各席の河 (リーチの牌は `[ ]`)・鳴き、自分の手牌とツモ牌、持ち時間、結果 (役と点数、MJ9 の OK を待つ表示)、局の始まり (MJ9 の `in`)。待ち合わせ (座る・立つ・CPU で埋める・始める) もできる。観戦もできる。
- **つなぎ方**: `othello-tty` と同じ指定 (`--router` / `--credentials` / `--namespace` / `--table` / `--name`)。何も指定が無ければ、asterism の CLI と同じく `ASTERISM_CREDENTIALS` / `ASTERISM_ROUTER` を読む (gemspec の asterism の版で使えない場合は、play 側で同じ決まりで読む)。
- **`othello-tty`**: `asterism-games play othello` として動くようにし、`bin/othello-tty` は互換のために残す (中身は play othello を呼ぶ)。
- 依存は asterism の gem (と標準ライブラリ) だけ。curses などの gem は足さない (行ごとに描き直す、または ANSI のエスケープで画面を消して描く程度)。

## 3. 作るもの

1. `asterism-games play` の入口 (`play` の後のゲーム名、`--help`、ゲームの一覧)。
2. 麻雀の端末版 (新規)。卓の一覧 → 入る → 待ち合わせ → 対局 → 結果 → 終局。ホストの出入り (on_leave / on_join) で止まらない。
3. オセロ (今の othello-tty を play の下へ)。
4. 試験: 牌の文字への変換 (絵文字と MPSZ)、入力の読み取り (文字・略号・宣言)、表示の幅の扱い (崩れにくい並べ方の単体試験)、手元のホストと CPU で麻雀を 1 局終局まで打てる試験 (自動入力で)。
5. 文書: README (play、麻雀の端末版の見た目と操作、`--tiles mpsz`)、CHANGELOG (0.17.0)、VERSION。

## 4. 受け入れ条件

1. `MJ_GAMES=4 bundle exec rake test` が通る。
2. 手元の一式 (zenohd + ゲームホスト、CPU だけ。外の AI の API は呼ばない) で、`asterism-games play mahjong` で座って CPU と東風戦を終局まで打てる (自動入力の台本でよい)。画面の写し (端末の出力) を report に貼る。絵文字と `--tiles mpsz` の両方。
3. `asterism-games play othello` と `bin/othello-tty` が今どおり動く。
4. 実際の端末 (WSL の端末など) での見え方は親とユーザが確認する (report に「確認してほしいこと」を書く)。

## 5. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は report だけ (ファイルとして書くだけ、git の操作はしない)。
- **触らないもの**: asterism・asterism-console・riichi_engine・fmruby-core のコード、VPS、GitHub の設定、Tab5。
- 外の API を呼ばない。push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- 起こしたもの (zenohd、ホスト) は名前で止めて消す。docker の prune はしない。
- **止まる条件**: 1・2 章に無い仕様の選択が要る (例: ゲームホストのメソッドが足りず、ホスト側の変更が要る)。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/gs11.md`、常体): コマンドの形、画面の例 (絵文字と MPSZ)、操作の一覧、幅の扱い、試験の結果、確認してほしいこと。
