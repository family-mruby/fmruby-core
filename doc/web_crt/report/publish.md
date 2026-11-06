# 報告: CRT 風の表示と額縁の取り込みと公開

> 状態: 完了 | 更新: 2026-11-05 | feature/web-crt (C1・C3) を作業の枝へ合流し、ブラウザ版を作り直して nginx と GitHub Pages の両方に出し直した。束の版は 605f46d4075d

計画は [../plan.md](../plan.md)、中身は [c1.md](c1.md)・[c3.md](c3.md)。

## 合流

- feature/web-crt を作業の枝 (feature/linux-native、f17c8ee3) へ merge した (1520da9f)。別の worktree で行い、本体の checkout の作業中の変更には触れていない。
- 衝突は 2 つ。
  - `rakelib/test.rake`: 作業の枝の `desktop_icons:test`・`native:test` を残し、web_crt の `webcrt:test` (と `namespace :webcrt`) を足した。`rake test` の説明文も両方を含める。
  - `doc/README.md`: 生成物なので `rake docs:index` で作り直した。
- 合流で変わるのはページ (`wasm/web/`)・`rakelib/wasm.rake` (dist に `crt.js` を足す)・`rakelib/test.rake`・`test/web_crt/`・文書だけ。core・アプリ・flash には差分が無いので、端末の絵は合流の前と同じ。

## 作り方

- `source ~/emsdk/emsdk_env.sh`、`rake spinel:gen`、`rake wasm:mruby`、`rake wasm:dist`。checkpoint の gem は入れていない (前回の公開と同じ標準の構成)。
- `wasm:scan`: 603 個の file、資格情報なし。dist は 9 file・10.33 MB (前回より `crt.js` の 1 つ多い)。

## 確認

| 何を | 結果 |
|---|---|
| `rake test` | 21 suites 全部通る (`webcrt:test` を含む) |
| `rake regress` | 205 runs 全部通る |
| `node test/web_crt/warp_test.js` | 55/55 |
| ブラウザ版の場面 (`screens.rb --only web --attach web`、自分の開発サーバと自分のヘッドレスの Chromium) | 5/6。落ちた `editor_type` は下の「気づき」 |
| 既定の見た目 (dist を自分の port で、保存の無い状態から) | Look は Sharp (`flat`)、額縁は液晶モニタ風 (`lcd`)、重ねる canvas は隠れている。WebGL2 は使える。計画の決定どおり |
| `?look=crt` | ブラウン管テレビ風の額縁と CRT の表示。絵が変わったときだけ描く (upload 48・draw 48・frame 48) |
| `?look=crt&frame=monitor` | CRT モニタ風の額縁 |

画面の写し:

- 既定 (Sharp・液晶モニタ風): [publish_default_sharp_lcd.png](publish_default_sharp_lcd.png)
- CRT・テレビ: [publish_crt_tv.png](publish_crt_tv.png)
- CRT・モニタ: [publish_crt_monitor.png](publish_crt_monitor.png)

## 公開

- nginx: 先に VPS の置き場を控え (`fmrb-web-2026-11-05-1602`)、`rake wasm:deploy` で置いた。ページの版は 605f46d4075d、`crt.js` は 200 で COOP/COEP 付き。
- GitHub Pages: site の checkout を fetch してから `rake wasm:site`、コミット 797a45d を push。MkDocs の deploy は成功し、Pages の公開 (pages build and deployment) は進行中の時点でこの報告を書いた。反映後に https://family-mruby.github.io/studio/ の版が 605f46d4075d になり `crt.js` が配られることを見る (前回は反映に 28 分かかった)。

## 気づき

- ブラウザ版の場面 `editor_type` は、エディタの状態行の右端 (`[A]` の印と、Window の右下の大きさ変えの三角) の 123 画素が基準と違う。基準を作り直した c0cd675f (大きさ変えの印に右下を譲る、bcf2ebb8) が retro・modern・native の基準だけを更新し、web の基準を更新していなかった。差分は c0cd675f の説明 (`[A]` が右端より 9 画素手前で終わり、隅に三角が出る) と同じ形で、今回の合流とは関係しない (合流は端末の絵の側に差分が無い)。web の基準の更新はしていない (`rake regress:screens ONLY=web` の後に `--update` で直せる)。
- 実機のスマホでの見た目は、公開先で見られるようになった (C3 の残件)。
