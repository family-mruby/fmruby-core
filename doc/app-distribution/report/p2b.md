# P2b: `startup_app` — 起動と同時に 1 本開く (2026-09-02)

> 状態: 完了 (Retro 実機を除く) | 設定に書いた 1 本を desktop が起動時に開く。
> ブラウザは URL の `?app=` をその設定に変える。

計画は plan.md。P2 で「ページはアプリを起動できない」と分かって切り出した段。

## 仕組み

```
ページ  ?app=/app/game/tetris.app.rb
   -> --fmrb-conf=startup_app="/app/game/tetris.app.rb"   (main.js)
   -> /etc/system_conf.toml を書き換え                     (page_settings_wasm.c)
   -> desktop が起動後に読んで spawn                        (system_desktop.app.rb)
```

実機では設定ファイルに直接書く。**1 つのことをする機械**が、電源を入れたら
それになる。

`?app=` は 2 つの書き方を受ける。`/` を含めばパスそのまま、含まなければ
店で入れたアプリの置き場 (`/app/usr/<id>/<id>.app.rb`) を組み立てる。

## 実測

| | 結果 |
|---|---|
| sim (`startup_app = "/app/tool/appstore.app.rb"`) | 起動直後に店が開いた |
| ブラウザ `?app=/app/game/tetris.app.rb` | Tetris が開いた |
| ブラウザ `?app=paint_pad` | `/app/usr/paint_pad/...` に展開されて開いた |

`spawn_startup_app` は `finish_boot_animation` の末尾に置いた。ここが
「デスクトップが完全に動く」地点で、カーソルも合成の切替も済んでいる。

## 詰まったこと

### `FmrbApp.config` は平の鍵を返さない

最初これで読もうとしたが、`FmrbApp.config(section)` が返すのは**節の表の配列**
(`[[launcher_exclude]]` のような) で、top-level の文字列を返す道が無い
(`app.c:1024` 以降)。C を足すより、**設定ファイルを 1 回読んで行を歩く**方が
軽い。ランチャーが `.app.toml` を読むのと同じ形で、Regexp も要らない。

### `conf_set` は既存の鍵しか置き換えない

ページからの `--fmrb-conf=` は既に汎用だが、受け側 (`page_settings_wasm.c`
の `conf_set`) は**その鍵が既に設定ファイルにあるときだけ**書き換える。
新しい鍵は素通りする。

なので `config/system_conf_*.toml` **7 枚すべてに `startup_app = ""` を
既定として置いた**。ページから渡せるようになるのに加えて、鍵が配布物の中に
見えるようになる (実機で使う人が見つけられる)。

### Spinel の確認

desktop は Spinel で生成されるので、足したコードが生成を通るかを別に確かめる
必要がある。**エディタを起動して 1 打鍵まで**見た (`Ln 1, Col 7 *`)。
エラー行 0、crash マーカー 0。

## できないこと

**リンクからの導入はできない。** `?app=` が開けるのは**すでにこの機械に
ある**アプリだけである。ページはアプリを入れられない — 店はアプリであって
ページの一部ではない (P2 の判断)。

リンク 1 つで「入れて開く」まで行くには、店に引数を渡す仕組みが要る
(`startup_app` で店を開き、「この id を入れろ」を伝える)。アプリへの引数渡しは
まだ無い (`doc/user_extension` の案 3)。**別の段**。
