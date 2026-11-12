# native:test の session_test がときどき落ちる

> 状態: 完了 | 更新: 2026-11-12 | `rake regress` の中で `test/native/session_test.rb` がときどき「SIGTERM to the program ... after 1s」で落ちた。原因は試験の側で、秒単位の経過時間を 0 秒と決め打ちしていた。経過時間は `\d+s` で見るようにし、ついでに固定の 0.3 秒待ちを止めて、代役が trap を張ったことを待つようにした。負荷をかけた 50 回で 0 件。同じ負荷の `rake regress` で見つかった native clip の試験の取りこぼし (読み手が書き手より先に回り終わる) も直した

## 1. 何が起きていたか

session の台本 (native/package/family-mruby-session) は、1 回の実行の長さを `date +%s` の差 (秒の切り捨て同士の差) で出す。代役は一瞬で終わるが、始まりと終わりの間に秒の境目をまたげば 1s になる。試験は次の 3 か所で 0s を決め打ちしていた。

- 「SIGTERM to the program」の `ended by SIGTERM after 0s (run 1)`
- 残すログの `ended by SIGKILL after 0s (run 1)` と `run 2 ran 0s`

ふだんは 1 回が数十 ms なので境目をまたぐ確率は小さい。`rake regress` は試験の組を並べて流すので、負荷で 1 回が延びると当たりやすくなる。台本の方は正しい (1 秒をまたげば 1s と書くのは意図どおり) ので直していない。

もう 1 つ、「SIGTERM to the session」は代役のログに 1 行目が出てから固定で 0.3 秒待って TERM を送っていた。代役が trap を張る前に TERM が届くと、代役は trap なしで死に「got TERM」が残らない。負荷の下では起こり得るので、代役が trap を張った後に印のファイルを置き、それを待つようにした。代役の `sleep 30` も trap の中で止める (30 秒残っていた)。

## 2. 確認

| 条件 | 直す前 | 直した後 |
|---|---|---|
| 24 コアを全部ふさぐ負荷 (`while :; do :; done` を 24 本) | 30 回で 4 回落ちる (ログの 0s が 3 回、SIGTERM の 0s が 1 回) | 50 回で 0 回 |
| 負荷なし | - | 50 回で 0 回 |
| `rake regress` | - | 負荷なしで 5 回通過。負荷の下では 3 章の clip の試験が落ち、それを直した後は 3 回とも通過 |

## 3. 同じ負荷で見つかったもう 1 つ: native clip の test_threads

24 本の負荷の下で `rake regress` を 2 回流すと、2 回とも native:test が `FAIL test_threads:166: 0 torn of 0 reads` で落ちた (test/native/clip.c)。読み手が 20000 回読む間に、書き手のスレッドが一度も動かなかった (読めた数 0)。これも試験の側の決め打ちで、クリップボードの中身は壊れていない (torn 0)。読み始める前に最初の書き込みが見えるまで待つ (1 ms ずつ、10 秒まで) ようにした。直した後、同じ負荷で `rake regress` 3 回すべて通過。

ふだんの `rake regress` で落ちた報告があるのは session_test の方だけ。clip の方は 24 コアを全部ふさぐ極端な負荷でだけ出た。
