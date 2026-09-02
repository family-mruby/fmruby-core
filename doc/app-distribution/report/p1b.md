# P1-B: 実機側の前提の確認 (2026-09-02)

> 状態: 完了 (sim + ブラウザ) | 配布鍵つきのアプリを sim とブラウザで動かし、
> spec.md の前提を実地で確かめた。**1 つ外れていた**。残るは Retro 実機のみ。

計画は plan.md、指示は instruction_p1.md、A の記録は report/p1a.md。

構成: NARYAv4 (Modern 相当) の Linux sim、426x240。両リポジトリを
`rake build:linux` で建て直してから (`file build/fmruby-core.elf` = x86-64 確認)、
サンプル 3 本を `flash/app/usr/<id>/` に手で置いて起動した。

## 結果

| 確かめたこと | 結果 |
|---|---|
| 配布鍵を足した `.app.toml` が警告なく読まれる | **成立** |
| 窓の名前・大きさが `.app.toml` のとおりになる | **成立** (Paint Pad 260x170) |
| 必要ヒープを省略したアプリが普通に起動する | **成立** |
| エラー行・crash マーカーが 0 件 | **成立** |
| **1 ファイル配布 (埋め込み記述) が使える** | **外れ。使えない** |

## 外れていた前提: 埋め込みだけのアプリはランチャーに出ない

`hello_store` は `.rb` 冒頭の `#---fmrb` 囲みに記述を持ち、`.app.toml` を
持たない形で作ってあった。spec.md はこれを「1 ファイル配布」として推していた。

### 何が起きたか

起動はする。しかし**窓の名前が `hello_store`** (= ファイル名) になり、
`app_screen_name = "Hello Store"` が効かない。ログに理由が出ていた。

```
I: [spawn] 6 toml_load '/app/usr/hello_store/hello_store.app.toml'
I: Loaded comment toml from /app/usr/hello_store/hello_store.app.rb
W: app_screen_name in the comment toml of ... is ignored
   (launcher metadata needs a .toml sidecar)
I: No /app/usr/hello_store/hello_store.app.toml: using 'hello_store' as the app name
```

**spawner は埋め込みからランチャー向けの項目 (名前・アイコン・
`launcher_visible`) を意図的に読まない。**

### もっと悪いこと: 一覧に 1 度も出ない

名前が出ないだけなら軽い。`/var/cache/launcher_index` を消して sim を
起動し直し、**全走査をやらせて**確かめた。

```
Found app: Paint Pad (/app/usr/paint_pad/paint_pad.app.rb)
Found app: Wide Only (/app/usr/wide_only/wide_only.app.rb)
```

`hello_store` は無い。再生成された一覧 37 件にも 1 行も無い。

機序は `launcher.rb:372` で、走査は**ディレクトリの中の `.toml` で終わる
名前だけを見る**。`.rb` を開いて囲みを探すことはしない。

**つまり埋め込みだけのアプリは、入っても入れた本人が二度と辿り着けない。**
パス指定 (`sim_app spawn`) でしか起動できない。

### 直した

- `.app.toml` を**必須**にした (spec.md 3.3)。
- `Manifest.load` が sidecar 無しを拒む。
- `validate` が「`.rb` に囲みが残っている」も落とす。sidecar が勝つので、
  2 つの複製が黙ってずれるため。
- `hello_store` に最小の `.app.toml` を足した。窓・記憶域・スタックの鍵は
  引き続き 1 つも書かない (既定を試す標本としての役目は変わらない)。
- 検体を 2 つ足して 19 件にした。

**埋め込みの形そのものは実機の機能として正しい。** エディタや shell から
自分で起動する補助スクリプトのためのもので、配布の形ではなかった。

## ついでに分かったこと

### 既定の窓は 100x100 で、1 行 15 文字しか入らない

`hello_store` は鍵を 1 つも書かないので既定の 100x100 で開く。最初
「Hello from the store」(20 文字) を書いたら**文字が自分と重なった**。

font は 6 px 幅なので 100 px は約 15 文字。サンプルは短い文に直し、その
計算をコメントに書いた。**既定を貰うアプリは狭い**ことを、作例を読む人が
そこで知れる方がよい。

### 注入した mouse_move は間引かれる

Paint Pad の線を引くのに `down / move x5 / up` を送ったら、**最初の 1 本しか
描かれなかった**。move の間に `sleep 80` を挟むと全部描かれた。50 ms の
tick に対して連続した move が畳まれている。

絵を作るとき以外は害が無いが、**「描けていない」を実装の不具合と読み違える
道がある**ので記録しておく。

## 撮ったスクリーンショット

`registry.json` が指すスクリーンショットを仮のものから本物に差し替えた。

| アプリ | 中身 |
|---|---|
| `paint_pad` | 4 色で線を引いた状態 |
| `hello_store` | 5 回押した状態 (`clicks: 5`) |
| `wide_only` | 「画面は 426 px、要るのは 400」と目盛り |

縮小した BMP を実機と同じ読み方 (画素 = RGB332) で読み直して確認した。
`paint_pad` の色数が仮の絵の 7 から **30** に増えている。

## 作法の確認

ソースを建て直したので、標準構成の決まりどおり**エディタを起動して 1 打鍵**
まで見た。`puts 1` が入り `Ln 1, Col 7 *` になる。ビルドは健全。

## 後始末

`flash/app/usr/` に置いた 3 本は消し、`flash/` は git 追跡分だけに戻した
(配布物と検証用の置きものを混ぜない)。`/var/cache/launcher_index` は
gitignore されており、次のブートで作り直される。

## ブラウザ版 (2026-09-02)

`rake wasm:web` で束を作り直し (**先に `source ~/emsdk/emsdk_env.sh` が要る**。
無いと 277 ファイルを staging したところで止まる)、`web_fs` で
`/flash/app/usr/` に 2 本置いた。

### 永続化する — spec.md 4 節のとおり

置いたあと再読み込みして `ls /flash/app/usr` を見ると、両方残っていた。
`/flash/app/usr` が `main.js:475` の `STORES` に入っている効果が実物で出ている。

### ランチャーの落とし穴はブラウザに無い — spec.md 13.6 のとおり

**再読み込みだけで一覧に「Hello Store」が出た。** 実機で要る再走査の操作も、
`/var/cache/launcher_index` を消す処理も要らない。

機序も期待どおり: `flash/var/cache/launcher_index` は gitignore されており、
束は git 追跡分しか集めないので、ブラウザには最初からキャッシュが無い。
毎回全走査になる。

### 動く

- Hello Store: 既定の 100x100 で開き、押すたびに数が増える (`clicks: 3`)。
- Paint Pad: 260x170、色を選んで線が引ける。
- どちらも**窓の配色がテーマ (cyberpunk) に追従**している。

### 削除も効く

ファイルを 4 つ消して再読み込みすると、一覧から「Hello Store」が消えた。
**`web_fs` に rmdir が無いので空のディレクトリだけが残る**が、走査は
`.toml` を探して見つからず素通りするので害は無い。ブラウザ側の店 (P2) を
書くときは、殻が残ることを承知しておく (消す手段がページ側に要る)。

### 注入した move の間引きは sim より軽い

sim では `sleep 80` を挟まないと 1 本しか描けなかったが、ブラウザでは
`sleep 90` で全区間が描けた。**同じ操作でも 2 つの環境で結果が違う**ので、
描画の確認をどちらか片方で済ませない。

## 残り

- **Retro 実機での確認**はユーザに依頼する (遠隔操作の口が無い)。
- スクリーンショットは Modern (426x240) で撮ったもの。Retro (320x240) の絵ではない。
  縮小は縦横比を保つので表示上の問題は無いが、`retro` を名乗るアプリの
  スクリーンショットとしては後で撮り直す価値がある。

## 後始末 (ブラウザ)

置いた 4 ファイルは消した。ページと開発サーバは**元から動いていたもの**
(別セッションのエディタ検証の名残) を借りたので、落とさずに残してある。
