# CLI2 指示書: asterism の CLI の出力を人が読みやすく

> 状態: 指示 | 更新: 2026-10-19 | asterism 0.8.0 の CLI (`asterism tree` / `objects` / `watch` / `nodes`) は、状態を JSON の 1 行で、メソッドを 1 行に全部、説明文を折り返さずに出すので、人が読みにくい (ユーザが手元の網で試した感想)。端末で見るときの見た目を直し、環境変数の直し (ca139a7、未公開) と一緒に 0.8.1 で出す

前提: asterism (`/home/kishima/fmrb/family-mruby/asterism`、main、0.8.0 の後に ca139a7 (CLI が `ASTERISM_CREDENTIALS` / `ASTERISM_ROUTER` を読む) がある)。`lib/asterism/cli.rb` と `lib/asterism/cli/*.rb` (survey など)、`test/test_cli.rb` (`rake test:cli`)、README「The command line」、CHANGELOG の Unreleased。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism の文書は英語。

## 1. 決まっていること (2026-10-19 ユーザ、選択形式)

端末で見るときの見た目だけを変える。`--json` の出力は今のまま (機械向け)。4 つとも入れる:

1. **状態を段付けで表示**: `@status` の JSON を、入れ子ごとに段付けした複数行にする (例 `mahjong1: st=wait members=0 humans=0 cpus=4 ais=0 away=0`)。深い入れ子・配列・長い値の扱いは、読みやすい形を決めて試験する。
2. **tree はメソッドを数だけ**: `tree` ではオブジェクトごとに「17 methods」と数だけ出し、`--methods` を付けたときだけ、メソッドを端末の幅で折り返して並べる。
3. **説明文を端末の幅で折り返す**: `objects` の説明 (オブジェクトの説明、メソッドごとの説明) を、字下げをそろえて端末の幅で折り返す。メソッド名 (と引数) を太字にする。
4. **色と watch の差分**: 端末では色を付ける (ノード・アプリ・オブジェクト、出入りの `+` `-` `~`)。`watch` は状態の変わったところだけを出す (例 `~ status games/games mahjong1.members 0→1`)。最初の 1 回 (`(now)`) は全体を段付けで出す。

## 2. 決めごと

- **端末かどうか**: 標準出力が端末のときだけ色と端末の幅を使う。パイプ・ファイルでは色なし、幅は 80 (または `COLUMNS`)。`NO_COLOR` を尊重する。`--json` は一切変えない。
- 依存は増やさない (標準ライブラリだけ: `io/console` の `winsize` など)。
- 既存の試験が見ている出力が変わる場合は、試験を新しい形に直す (`--json` の試験は変えない)。

## 3. 作るもの

1. 上の 4 つ。`nodes` の状態も 1 と同じ段付けにする。
2. 試験: 段付けの形 (入れ子、配列、長い値)、`--methods` の有無、折り返しの幅、色の有無 (端末・パイプ・`NO_COLOR`)、`watch` の差分 (変わったキーだけ、増えた・消えたキー)。
3. README「The command line」の例を新しい出力に。CHANGELOG の Unreleased に足す。版は **0.8.1** に上げ (VERSION / `lib/asterism/version.rb`、README の gem の名前の例)、CHANGELOG の Unreleased を 0.8.1 にする (環境変数の直しと一緒)。`rake gem` で `pkg/asterism-0.8.1.gem` を作る (公開はしない: 親がユーザに `gem push` を頼む)。

## 4. 受け入れ条件

1. `rake test` (少なくとも `rake test:cli` と、CLI に関わる試験) が通る。
2. 手元の zenohd とゲームホスト (asterism-games の作業コピーで `ASTERISM_GAMES_STATE_DIR` を scratchpad に、外の AI の API は呼ばない) に対して、`tree` / `tree --methods` / `objects` / `nodes` / `watch` (人が卓に入る・座ると差分が出る) の実際の出力 (色なしの写し) を report に貼る。
3. `--json` の出力が前と同じ。

## 5. 範囲・決まり

- **触ってよいもの**: asterism (main)。fmruby-core は report だけ (ファイルとして書くだけ、git の操作はしない)。
- **触らないもの**: asterism-zenoh、asterism-games・asterism-console のコード (試しに動かすのはよい)、VPS、GitHub の設定、Tab5。
- push・タグ・`gem push` はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- 起こしたもの (zenohd、ゲームホスト) は名前で止めて消す。docker の prune はしない。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/cli2.md`、常体): 新しい出力の例 (前と後)、決めた形、試験の結果、0.8.1 の gem の状態。
