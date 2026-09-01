# 配信先: 自前の nginx に置く (2026-09-01)

> 状態: 完了 | 公開済み。GitHub Pages ではなく自前の nginx から配る

## なぜ Pages をやめたか

P5 は「GitHub Pages に置ける」ところまで作ってあり、実際に置ける。やめた
理由は 2 つで、**どちらも公開してみる前には見えていなかった**。

**バイナリが文書リポジトリの履歴に積まれる**。1 回の公開で約 2.6MB
(`core_web.wasm` は圧縮しても 2.19MB、しかも wasm は差分が効かないので毎回
まるごと)。10 回出し直せば 26MB、しかも**一度 push すると履歴からは消せない**。
文書のリポジトリにそれを溜めたくない、というのがユーザ判断。

**初回訪問のリロードが要らなくなる**。Pages はヘッダを出せないので
coi-serviceworker が自分を登録してページを 1 回リロードする。**自前の
nginx なら COOP/COEP を直接返せる**ので、最初の読み込みからそのまま起動する。
同梱の coi-serviceworker は「既に isolation 済みなら何もしない」ので、
置いたままでよい。

分割 (ページは Pages、バイナリは別ホスト) も検討したが採らなかった。
`FMRB_WASM_BASE` があるので技術的には可能だが、**COEP 下では別オリジンの
ものは CORP を返さないと読めない**うえ、2 か所の版がずれる事故を新しく作る。

- **GitHub Releases に置く案は不成立**: 添付は CORP を返さないので COEP に
  弾かれる。
- **git-lfs も不成立**: GitHub Pages は LFS の実体を配らない (ポインタの
  テキストが返る)。

## 置き方

`https://silentworlds.info/fmrb/`。既存ホストのパスに足しただけで、DNS も
証明書も足していない。`location /fmrb/` は `location /` より長い接頭辞なので
先に当たり、下の WordPress には触れない。

```nginx
location /fmrb/ {
    alias /var/www/fmrb-web/;
    index index.html;
    add_header Cross-Origin-Opener-Policy   same-origin  always;
    add_header Cross-Origin-Embedder-Policy require-corp always;
    gzip_static on;
}
```

配布は `FMRB_WEB_DEST=host:/var/www/fmrb-web/ rake wasm:deploy` (dist を作り、
`.gz` を添えて rsync する)。

## 踏んだ罠 2 つ

### `types { }` は表に足すのではなく置き換える

nginx 1.18 の `mime.types` に `wasm` が無い (追加は 1.21 から)。無いと
`application/octet-stream` で配られてストリーミング実行が失敗するので、
最初 location の中に `types { application/wasm wasm; }` と書いた。

**これで html と js が型を失った**。`types` はそのコンテキストの MIME 表を
**丸ごと置き換える**。`index.html` が `application/octet-stream` になり、
ブラウザはページを表示せずダウンロードする。

正解は **`/etc/nginx/mime.types` に 1 行足す**こと。新しい nginx は同じ行を
標準で持っているので、将来上げても重複するだけ。

### `holdload` は開発サーバにしか無い

`?holdload=N` は「遅い画像 `probe-hold` を読ませて load イベントを開いた
ままにする」仕掛けで、**実装しているのは `rake wasm:serve` だけ**。公開先
では 404 が即返り、**load イベントがすぐ発火する**。

この違いに気付かず、公開先の画面を**起動の t≈0 で撮って**「起動しない」と
誤診した。無関係な gzip を疑って外させ、1 往復を無駄にしている。

**公開先を自動で確かめる手段はまだ無い**。切り分けを決めたのは
**サーバの access.log** で、そこには全部 200 と、`probe-hold` の 404 だけが
並んでいた。**両端のログを見るより先に、間にいるサーバのログを見る**べき
だった (sim のホイールで踏んだのと同じ形の間違い)。

## 残り

- 公開先に対する自動確認 (DevTools プロトコルで状態を問い合わせる等)。
- `/fmrb/favicon.ico` が無く、`location /` に落ちて WordPress のものが出る。
