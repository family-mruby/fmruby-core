# S6 指示書: AI ホストの通し (sig を足す前と後で、返るコードを比べる)

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (5 章の S6、6 章の受け入れ条件 6)。
> 進め方の手本は doc/ai_coding/report/a6b.md の 5 章と a6.md の 3 章 (同じ道具・同じ並べ方)。

## 目的

sig を 67 クラス 815 メソッドから 125 クラス 1,470 メソッドに増やした (S0〜S5)。AI ホスト (asterism-tutor 0.3.1) は
sig を知識のバンドルとして LLM に渡すので、(1) 足した範囲の API を型どおりに使うコードが返るようになったか、
(2) 前より悪くなった頼みが無いか、(3) バンドルが大きくなった分の費用と時間、を本物の LLM で確かめて記録する。

## やること

1. **2 つの AI ホストを手元に並べる** (a6.md 3 章のやり方。ルータは手元の zenohd、設定は scratchpad):
   - 前: fmruby-core の `c6d0fbd8` (sig を足す前。VPS で動いている版の sources)。
   - 後: fmruby-core の手元の `develop` (未 push。**GitHub からは取れない**ので、a6b.md 5 章と同じく手元の写し
     (scratchpad に `git clone --bare` した物) を `sources` の取り先にする)。
   - asterism-tutor は `/home/kishima/fmrb/family-mruby/asterism-tutor` の main (0.3.1)。**変えない**。
   - LLM は anthropic、既定のモデル。1 日の上限は 0.5 ドル。鍵は `~/asterism_keys/api/tutor_anthropic_api_key` を
     **環境変数で渡すだけ** (ファイル・記録・報告・コミットに書かない。中身を表示しない)。
2. **決まった頼み 10 個**を、前と後に 1 回ずつ流す (`test/sender.rb`。mode は作る頼み)。足した範囲に当たるように:
   1. キーで動く四角 (`FmrbConst` のキーの番号)
   2. 1 秒ごとに数が増える時計 (`Machine` の時刻)
   3. ファイルに得点を保存して次の起動で読む (`File`)
   4. `/home` のファイルの一覧を出す (`Dir`)
   5. ボタンを押すと数が増える (`FmrbUI` の button・label)
   6. 文字を入れる欄と「消す」ボタン (`FmrbUI` の text_field)
   7. ドレミを鳴らす (`MmlPlayer`)
   8. 設定を JSON で保存して読む (`JSON`)
   9. サイコロを振る (乱数)
   10. ほかの端末と短い文をやり取りする (`FmrbAsterism::Link`、publish / subscribe)
   - 頼みの文は子ども向けの短い日本語にし、前と後で同じ文を使う。文は報告に並べる。
3. **返ったコードを見る**。頼みごとに、前と後で:
   - AI ホストの検査 (`Check`) の結果、メタ情報コメント (`#---fmrb`) があるか。
   - 使っている API が**後の sig に実在するか・引数の形が型どおりか** (sig を grep して確かめる。実在しない
     メソッドや、形の違う呼び出しを数える)。
   - 返ったコードは scratchpad に置く (`.../scratchpad/s6/before_NN.rb`・`after_NN.rb`)。
   - できれば `ruby -c` 相当の構文の確認まで。**sim では動かさない** (親が要るものだけ動かす)。
4. **数を控える**: 頼みごとの時間・LLM の呼び出しの数・往復 (`search_source`・`read_source` の回数)・出力トークン・
   キャッシュの書き / 読み・費用。バンドルの大きさ (バイトとトークン) の前と後。

## 報告 (doc/archive/sig_coverage/report/s6_ai_host.md)

1. 並べ方 (版・sources・モデル・設定で変えた所)。
2. 頼み 10 個の表: 前と後の、検査・実在しない API の数・形の違う呼び出しの数・往復・費用・時間。
3. 良くなった頼み・変わらない頼み・**悪くなった頼み** (あれば、何がどう。sig のどの記述が元か)。
4. バンドルの大きさと 1 回の読みのトークンの前後、費用の合計。
5. sig の記述のせいで LLM が間違えたと読める所 (直す候補。直さない)。確かめていないこと。

## 範囲と止まる条件

- 触ってよい: `doc/archive/sig_coverage/report/s6_ai_host.md` と `rake docs:index` による `doc/README.md` (fmruby-core の
  ブランチ `sig-s6` にコミット)。ほかは scratchpad だけ。
- 変えない: fmruby-core の実装・sig・道具、asterism-tutor、VPS・端末・家のサーバ。**fmruby-core でビルドをしない**
  (`rake build:*`・`rake clean*` を流さない。親が同じ checkout で大きさの回帰を流している)。
- 本物の LLM は合計 30 回・0.5 ドルまで。超えそうなら止めて、そこまでを報告する。
- 鍵・実在の値 (ドメイン・IP・ノード名・人の名前) を報告・コミットに書かない。ノード名は仮のもの (`fmruby-test01`)。
- push しない・マージしない。ユーザに質問しない。終わったら、起こしたプロセスとコンテナを止める。
  コミットは英語、報告は日本語 (常体)。
