# CLI4 指示書: 流れている値を MCP と CLI で読む

> 状態: 指示 | 更新: 2026-10-20 | MCP (`asterism mcp`) で見えるのはノード・オブジェクト・@status まで。オセロの盤面のように、アプリが自分のキー (`asterism/<node>/<app>/<object>/<more>`) に put している値は読めない。ユーザが「オセロの対戦状況を MCP でモニタリングしたい」。見るだけの証明書 (viewer) のまま読めるよう、MCP と CLI に道具を足す

前提: asterism (`/home/kishima/fmrb/family-mruby/asterism`、main、v0.9.0 = 9eca444、rubygems 公開済み)。`lib/asterism/cli.rb`・`lib/asterism/cli/*.rb` (CLI3 の名詞 動詞、`status echo` の差分、`-o yaml|json`、補完)、MCP の server (`asterism mcp`、道具 `asterism_nodes` / `asterism_objects` / `asterism_status` と `--allow` の呼び出し)、`docs/protocol.md` 2.2 (アプリ自身のキーの put は object 層と交わらない、queryable は 4.3 の形にしか答えない)。asterism-games のオセロの卓は `asterism/games/games/othello1/state` に卓の様子 (盤面・番・石の数・結果、メソッド `table()` と同じ中身) を put している。console の viewer の役割は `read asterism/**`・`watch asterism/**` を持つ (put を受け取れる、呼び出しはできない)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism の文書は英語。

## 1. 決まっていること (2026-10-20 ユーザ、選択形式)

- **MCP に「流れている値を読む」道具を足す**。viewer の証明書のまま、オセロも麻雀もほかのアプリも同じ道具で読める。CLI にも同じものを足す。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **put は残らない**: Zenoh の put は保存されず、get でも答えが無い (アプリの queryable は 4.3 の形だけ)。よって「最新の値」は受け取って覚えておくしかない。
- **MCP**: server が起動したときから `asterism/**` (アプリ自身のキーの put。`@profile` / `@status` は既存の道具のまま) を subscribe し、キーごとに最後の値と受け取った時刻を覚える。道具を足す:
  - `asterism_values` (名前は ros2 や既存の道具に合わせて決めてよい): 引数はキーの型 (`node/app/object/**` の形、`*` `**`、既定は全部)。覚えているキーと値 (JSON ならそのまま、そうでなければ文字列か長さ)、受け取った時刻、何秒前か を返す。まだ 1 度も受け取っていないキーは出ない。**引数 `wait` (秒、上限あり)** で、型に合う put がまだ無いときはその秒数まで待つ (卓の様子は変わったときにしか put されないので、起動直後や静かな卓に要る)。
  - 値が大きいときは切り詰めて、切り詰めたことを示す。覚える量には上限 (キーの数・1 つの大きさ) を置き、溢れたら古いものから忘れる。
  - 去った app のキーは覚えたままでよいが、その app が網に居ないことが分かる印を付ける (古い値を今の様子と取り違えないため)。
  - 道具の説明 (description) に、「put は変わったときだけ来る、wait で待てる、覚えているのは server を起動してから受け取った分」を書く (AI が読む)。
- **CLI**: `asterism object get PATH [KEY]` (型に合う最初の put を待って出す、`--timeout`、既定は数秒) と `asterism object echo PATH [KEY]` (put を流す。CLI3 の `status echo` と同じく、JSON なら前回からの差分の形、`--once`、`--raw` で全体)。`-o json|yaml` も効く。補完も足す。`asterism mcp` の道具と同じ中身を返す部分は共通にする。
- **ACL**: viewer で読めること (`read asterism/**`)。読めない型のときは、ルータが黙って何も送らないので、`wait` / `--timeout` が切れたら「何も来なかった (ACL で読めないか、まだ変わっていない)」と言う。
- **鍵・秘密**: put の値をそのまま返す。ゲームホストの卓の state に token は入っていないことを確認する (入っていたら止まって返す)。
- 依存は増やさない。既存の道具・コマンドの出力は変えない。版は **0.10.0**。`rake gem` で `pkg/asterism-0.10.0.gem` を作る (公開は親がユーザに頼む)。

## 3. 作るもの

1. MCP の道具、CLI の `object get` / `object echo`、補完、README (MCP の道具の一覧と、オセロの盤面を AI に見せる例)、CHANGELOG 0.10.0、`lib/asterism/version.rb`。
2. 試験: 覚える・忘れる (上限)、型の一致、`wait` で待つ・切れる、去った app の印、切り詰め、`object get --timeout`、`object echo` の差分と `--once`、既存の出力が前と同じ。
3. 確認: 手元の zenohd とゲームホスト (asterism-games の作業コピーか gem、`ASTERISM_GAMES_STATE_DIR` は scratchpad、外の AI の API は呼ばない。CPU 同士か `play othello` でオセロを 1 局) に対して、MCP の道具 (stdio の server に JSON-RPC を送る形でよい) と CLI で、対局中の盤面が読めること、終局の結果が読めること。写しを report に。

## 4. 受け入れ条件

1. `rake test` (全体) が通る。
2. 上の確認が通り、report に写す。
3. 既存の道具・コマンド・`--json` の出力が前と同じ。

## 5. 範囲・決まり

- **触ってよいもの**: asterism (main)。fmruby-core は report だけ (ファイルとして書くだけ、git の操作はしない)。
- **触らないもの**: asterism-zenoh、asterism-games・asterism-console のコード (試しに動かすのはよい)、VPS、GitHub の設定、Tab5。ユーザの `~/asterism_keys/` の証明書を使わない (VPS につながない)。
- 自分の試験の zenohd・ホストは別の名前・別のポートで起こし、終わったら名前で止めて消す。docker の prune はしない。pid で止める (`pkill -f` は使わない)。
- push・タグ・`gem push` はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/cli4.md`、常体): 道具とコマンドの形、覚え方と上限、確認の写し (盤面と結果)、試験の結果、0.10.0 の gem の状態。
