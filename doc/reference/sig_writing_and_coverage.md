# sig の書き方と数え方

> 更新: 2026-11-25 | `sig/*.rbs` (API の型定義) を足す・直すときの手順と、抜けを数える道具の読み方。
> 書式の正は `sig/README.md`、経緯と決定は doc/archive/sig_coverage/plan.md。ここは「何をどの順で流すか」だけを持つ。

## 1. sig は何に使われるか

`sig/*.rbs` は 1 つの元から 4 つの物になる。書いた 1 行が 4 か所に出るので、実装を読んで書く (推測で書かない)。

| 行き先 | 作る物 | 読む人 |
|---|---|---|
| エディタの型支援 | 型データベース (`rake ti:gen`。ファームにリンクされる) | 補完 (Tab)・シグネチャと要約・検査 (Ctrl+E)・型 (Ctrl+T) |
| エディタの F1 | 長文ヘルプ (`rake ti:help` → `flash/help`) | 端末でアプリを書く人 |
| AI ホスト | 知識のバンドル (asterism-tutor が sig を文字で読む) | LLM |
| ドキュメントサイト | クラスリファレンス (`rake ti:site`) | PC でアプリを書く人 |

## 2. 足す・直すときの手順

1. **実装を読む**。Ruby は `lib/gems/picoruby-fmrb-*/mrblib/` と `vendor/asterism/mrblib/`、C は `mrb_define_*` の行。
   使われ方は `flash/app/` (試験の `flash/app/test/` は数えない)。
2. **書く** (`sig/README.md` の決まり)。要点だけ:
   - 要約 (1 行目) は `日本語 <<en>> English` で、両言語あわせて 111 バイトまで。
   - 素直な API は例 (```ruby) だけ。つまずく API は型紙の順 (何が起きるか・呼べる場面・失敗のしかた・例)。
   - 引数は実装のとおり: 省略できる引数は `?`、キーワードは `name:`、形が複数なら `|`。
   - 強調 `**` は 1 行の中で閉じる。例は 39 桁以内。実在の値 (端末の名前・アドレス) を例に書かない。
   - 実装にあってもアプリが呼ばない内側のものは sig に書かず、`tools/sig_coverage/ignore.yml` に理由つきで載せる。
     gem の側で `@api private` の印があるものは、道具が自分で除く。
3. **確かめる** (どれも docker 不要):
   - `rake sig:coverage` (約 1 秒): 抜けと書き方の欠け。「3. doc gaps」は全部 0 が今の状態で、0 のままにする。
   - `rake ti:test`: 型エンジンの試験 (補完の候補やシグネチャを文字で見ている期待値がある。落ちたら期待値も見る)。
   - `rake regress`: 境界の検査 (`boundary:check`) が落ちたら `ruby tools/boundary/gen.rb` で SDK を作り直す。
4. **基準を更新する**: `rake sig:coverage:update` (変化が意図どおりと確かめてから。別コミットにする)。
5. **端末で見る**なら `rake clean` → `rake build:linux` (clean を忘れると古い型データベースがリンクされたままになる)。
   シェルの irb で例の値を、エディタで補完・F1・Ctrl+E を見る。
6. 新しい sig のファイルを足したら、`tools/ti/gen_site.rb` の `AREAS` (サイトの領域分け) にも入れる。

## 3. 数える道具の読み方 (`rake sig:coverage`)

明細は `tmp/sig_coverage/report.md` (コミットしない)。終了コードは数に関わらず 0 (報告だけ)。

| 節 | 数えるもの | 読み方 |
|---|---|---|
| 1. used by apps, not in sig/ | 同梱のアプリが使っているのに sig に無いクラス・定数・メソッド | 「known receiver」は受け手の型が分かった呼び出しで、確かな抜け。「untyped receiver」は受け手が分からない名前の一覧で、アプリ自身のメソッドも混ざる (目安) |
| 2. implemented, not in sig/ | 実装 (Ruby の `def`、C の定義) にあって sig に無いもの | 新しい API を足して sig を書き忘れるとここが増える |
| 3. doc gaps | 要約なし・111 バイト超・`<<en>>` なし・長文なし・例なし・行をまたぐ `**` | 0 を保つ |
| vs baseline | 前に受け入れた結果 (`baseline.json`) との差 | 増えた行だけ見る |

- `ignore.yml` に載せたものは 3 つの数から除かれる。理由 (`reason`) を必ず書く。「直すまで外す」ものは専用の節に分ける。
- 道具自身の試験は `rake sig:test` (`rake test` に入っている)。

## 4. 今の数 (2026-11-25、S0〜S5 の後)

- sig: 142 クラス・1,475 メソッド (始める前は 67 クラス・815 メソッド)。
- 実装にあって sig に無い: Ruby 6、C 15 (`Log`・`FmrbGfx`・`FmrbMic`・`IO`・`Dir`・`FmrbVideo` の残り)。
- 型データベース: クラス 143 (上限 255)、メソッドの行は約 5,200、シグネチャの領域 58,256 バイト・要約の領域 111,805 バイト
  (どちらも位置は 24 ビット)。

## 5. 知っておく制約

- **エディタの型エンジンは、入れ子のクラス (`A::B`) を受け手に解けない** (`FmrbAsterism::Link.new(...)` の結果に型が
  付かない)。sig に書いてあっても、その先の補完と検査は働かない。AI ホストとサイトには影響しない (doc/reference/TODO.md)。
- 相手のメソッドを method_missing で呼ぶもの (`Asterism::Proxy` の上の呼び出し) は型に書けない。長文で説明する。
- アプリが定義して部品から呼ばれるメソッド (`TableClient` が呼ぶ `table_state` など) は宣言にせず、クラスの長文に並べる。
