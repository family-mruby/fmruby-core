# 3.1.0 の前の直し その 2: F1 のヘルプを 1 つのファイルにまとめる

先に読む: `doc/release_3_1/plan.md` (3 章)、`doc/reference/flash_layout.md` (3.2)、`doc/reference/sig_writing_and_coverage.md`。
ブランチは今の `app-uart`。

## 何が困っているか

エディタの F1 で読む長文ヘルプ `/help` は、`rake ti:help` (`tools/ti/gen_help.rb`) が `sig/` から作る 268 ファイル
(クラスごとに `<Class>.ja.md`・`<Class>.en.md`、と `index.txt`) で、中身は約 836 KB。littlefs は 1 ファイルを 4 KB の
ブロック単位で持ち、フォルダの目録も増えるので、storage を約 1.5 MB 使っている。P4-Nano・Tab5 (storage 6.7 MB) と
NARYA v3 (5 MB) の空きを圧迫している。

## 決定 (ユーザ 2026-11-26)

**ヘルプを 1 つのファイル (か言語ごとに 1 つ) にまとめ、索引つきにして、F1 はその中から読む。中身は変えない。** 全機種。

## 作る物

1. `tools/ti/gen_help.rb` の出力を、まとめた形にする。形は自分で決めてよい (例: 言語ごとに 1 つの本文のファイルと、
   名前 → (位置, 長さ) の索引。今の `index.txt` (メソッド名 → クラス名) の役目も入れる)。端末の上で、索引だけを読んで
   クラスの本文を位置で読める (本文の全部をメモリに読まない) こと。picoruby に `seek` が無いので、読む側 (エディタ) が
   どう位置へ進むかを先に確かめる (C の境界の関数に、位置と長さを指定して読む物があるか: `FmrbFile` の sig を読む。
   無ければ、今ある API の範囲で速く読める形 (例: クラスごとの本文を数十 KB 以下の塊に分けて数ファイル) にする)。
2. エディタの F1 (`main/ruby/default_app/editor/ti_ui.rb` ほか) を、新しい形から読むようにする。表示は今と同じ。
   古い形 (`/help/<Class>.<lang>.md`) が残っている端末でも、新しいファームが新しい形を読めればよい (古い物は
   app_sync の PRUNE で消える)。
3. ブラウザ版・WebConsole (`tools/web/wasm/help.json` を作る所) など、`/help` の形を読むほかの所を洗い出し、合わせる。
   サイトのクラスリファレンス (`rake ti:site`) は sig から作るので変えない見込み (確かめる)。
4. ビルドの storage の像に入る `/help` の大きさが小さくなることを確かめる (ファイル数・中身のバイト・ブロックの見積もり)。
   P4-Nano 用の像のブロックの数え方は `doc/reference/flash_layout.md` 3.2 (ESP32 のビルドは親がするので、像を作らずに
   ファイルの大きさから見積もる)。
5. 試験: 生成の試験 (今ある `rake ti:test`・`ti:help` の試験に足す)、エディタの読み方のホストの試験。

## 触ってよい範囲・確認・止まる条件

- fmruby-core の上の物と、その試験・生成物・文書。sdkconfig・`vendor/`・`lib/upstream/`・ほかのリポジトリは触らない。push しない。
- 確認: `rake regress`、`rake ti:test`、sim (`rake clean` → `rake build:linux`) のエディタで F1 を、ASCII の名前・`::` の
  入ったクラス (`Asterism::Proxy` など)・メソッド名から引く場合・日本語と英語の両方で開いて、今と同じ本文が出ること
  (画面を撮る)。エディタを開いて F1 を出すまでの時間とメモリが悪くならないこと (sim の VM プールの数を、直す前と後で)。
- **ESP32 のビルド・実機・シリアル・MCP の tab5_*・flash は触らない**。`rake clean_all` を流さない。重い回帰は流さない。
- 止まる: 表示や引き方が今と変わる選択が要るとき、範囲の外を変える必要が出たとき、内蔵 RAM の静的な増分が出るとき。

## report (`doc/release_3_1/report/r2.md`)

形の決め方と理由、ファイル数と大きさ (前と後)、読む速さとメモリ、見立てと違った点、残り。実在の値を書かない。
終わったら sim を止め、置いた物を消す。
