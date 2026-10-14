# ブラウザ版の網の上の ID は console が決める: 実装の報告

> 状態: 完了 | 更新: 2026-10-14 | plan.md「ブラウザ版の網の上の ID は console が決める (2026-10-14 ユーザ決定)」の実装と確認の記録。asterism-console 0.1.2 はどのリンクにも合言葉とノードの ID (ハンドルから) を入れ、ブラウザ版は合言葉だけのリンクを断る。console の無い網では今どおり自分の `wasm-<16 進 6 桁>` を使う。手元の構成 (deploy/local の通し稽古) で 3 種類とも確認した

## 1. 結果の要約

| リポジトリ | ブランチ | 中身 | push |
|---|---|---|---|
| asterism-console | main | 0.1.2: どのリンクも合言葉 + ノードの ID、ハンドルが無ければ合言葉を作らない、`{node}` の無い `ASTERISM_BROWSER_PAGE` は使わない | していない (タグも無し) |
| fmruby-core | feature/asterism-console-node (develop から) | ブラウザ版 (`wasm/web/index.html`・`main.js`) が合言葉だけのリンクを断る、browser.md、plan.md、この報告 | していない |

## 2. asterism-console (0.1.2)

- 「Make a pass」も、ハンドルから決めたノード (`wasm-<handle>`、使われていれば `_2` …) の合言葉を作る。リンクは「Open the browser build」と同じひな形で、`{app}` だけ空 (`...#zenoh_pass=<合言葉>&zenoh_node=wasm-<handle>&app=`)。前は合言葉だけのリンクで、ページは自分の ID で入っていた (2026-10-13 の夜の `wasm-dd17aa`)。
- ハンドルが無いユーザには、合言葉を作るボタンを出さず、ハンドルを決める画面への案内を出す。直接 POST しても、アカウントの画面 (`#handle`) へ戻して作らない。
- ひな形 `ASTERISM_BROWSER_PAGE` は、`#` の後の `{pass}` に加えて `{node}` が無ければ使わない (ボタンもリンクも出ない)。`bin/asterism setup` はその設定で止まる。
- ひな形の無い console では、合言葉をリンク無しで出し、ノードの ID も並べて出す (「両方をページに渡す」の注意つき)。
- `PassLink.url` はノードを必須の引数にした (空なら例外)。リンクがノード無しで作られる道は残っていない。
- 版: `VERSION`・compose の既定・`env.example`・install.md を 0.1.2、CHANGELOG に更新で要ること (`{node}` の確認、ハンドル、新しいブラウザ版)。試験 318 件 失敗 0、rubocop 0 件。

## 3. ブラウザ版 (fmruby-core)

- 合言葉があってノードの ID が無い (または ID に使えない形の) リンクで開かれたら、機械を起動しない。起動の画面の覆いを「This link has a pass but no node ID, so the page does not connect. Open the browser build again from the console (Browser passes).」に替え、押しても (`?autostart=1` でも) 起動しない。状態の行も同じ文、Network の枠は `node: none (the link has a pass but no node ID; not connecting)`。合言葉は WebSocket に渡さない (使い切りの合言葉が残る)。
- 古い形の `?zenoh_pass=` (クエリ) も同じ扱い。
- 起動しない形を選んだ理由: 合言葉を捨てて起動すると、保存してあるルータや `?zenoh=` のルータへ自分の ID でつなぎに行けてしまい、「URL で ID が変わる」が残るため。
- 合言葉の無いページは何も変わらない (`wasm-<16 進 6 桁>` を localStorage に持ち、2 つ目のタブは使い捨て)。

## 4. 確認 (手元の構成)

asterism-console の deploy/local の通し稽古 (nginx 1.27・door・zenohd 1.10.1・console・bridge・signer。image はこのコミットから作った試し用)。ブラウザ版はこのブランチの `rake wasm:web`・`rake wasm:dist` の束を `play.localtest` で配った。ブラウザは docker の Playwright (Chromium、v1.56.0) を管理者の住所から。ハンドルは仮の名前 `kaede`、役割 match の「Opens in a browser page」にオセロを入れた。

| # | 確認したこと | 結果 |
|---|---|---|
| 1 | ハンドルの無い管理者: Browser passes にボタンが無く、案内が出る。手で `POST /passes` しても作られない | ボタン無し、`/account#handle` へ戻り、合言葉 0 件 |
| 2 | 「Make a pass」のリンク | `...#zenoh_pass=***&zenoh_node=wasm-kaede&app=`。リンクは `app` が空なので、クエリに `?app=/app/test/othello.app.rb` だけ足して開いた (フラグメントはそのまま)。Network の枠 `node wasm-kaede (from the link, this visit)`、WebSocket は `wss://router.localtest:7443/?pass=***&page=***` 1 本、オセロの名前も `wasm-kaede`、console の図に `wasm-kaede` |
| 3 | 「Open the browser build」(オセロ) | 2 のページを閉じて図から消えた後に開き、`wasm-kaede` で入った (枠・WebSocket・図とも 2 と同じ) |
| 4 | 合言葉だけの手作りのリンク (2 のリンクから `zenoh_node` を消したもの、と古い形の `?zenoh_pass=`) | どちらも覆いに断りの文、起動せず、WebSocket 0 本。その合言葉は使われないまま (`live`、使った回数 0) |
| 5 | 合言葉の無いページ + LAN 風のルータ (認証なしの zenohd 1.10.1 を `ws/` で。ページは `http://localhost` で配って `?zenoh=ws/lanrouter:7449&app=...`) | `node wasm-ba87eb`、`ws://lanrouter:7449/` (合言葉なし)、オセロが `wasm-ba87eb up`。読み直しても同じ ID (localStorage に保持) |

## 5. 気づいたこと

- console の合言葉は、使われる前は名前を予約する (G1.5b の決まり)。4 で作った合言葉が使われずに残ると、次の合言葉は `wasm-kaede_2`・`_3` になる。手で作ったリンクを断ると予約が残るので、取り消すか 10 分待つ。
- `rake wasm:dist` は `wasm/build/dist` を作り直すので、そこを bind mount した nginx の container は Docker Desktop で起動できなくなる (消えたディレクトリを指す)。通し稽古ではスクラッチパッドに写して配った。

## 6. ユーザが行うこと

- asterism-console: `v0.1.2` のタグを付けて push (image が作られる)。VPS では `.env` の `CONSOLE_VERSION=0.1.2`、`ASTERISM_BROWSER_PAGE` に `{node}` があることを確認し、pull と起動し直し (移行なし)。ブラウザ版を開く人はハンドルを決める。
- ブラウザ版: このブランチを develop に入れた後、公開の置き場 (`rake wasm:site` など) と、console の `ASTERISM_BROWSER_PAGE` が指す置き場を作り直す。古いブラウザ版は合言葉だけのリンクを自分の ID で開いてしまう。
