# P1-B: 実機側の前提の確認 (2026-09-02)

> 状態: 完了 (sim・ブラウザ未) | 配布鍵つきのアプリを Linux sim で動かし、
> spec.md の前提を実地で確かめた。**1 つ外れていた**。

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
  2 つの写しが黙ってずれるため。
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

## 撮った写し

`registry.json` の写しを仮のものから本物に差し替えた。

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

## 残り

- **ブラウザ版での確認**が未了。`web_up` + `web_fs` で `/flash/app/usr/` に
  置いて起動し、再読み込みで残ることまで見る。
- **Retro 実機での確認**はユーザに依頼する (遠隔操作の口が無い)。
- 写しは Modern (426x240) で撮ったもの。Retro (320x240) の絵ではない。
  縮小は縦横比を保つので表示上の問題は無いが、`retro` を名乗るアプリの
  写しとしては後で撮り直す価値がある。
