# 画面の直しを確かめる手順

段 2 で、静止画を眺める確認は**描画の不具合 6 件のうち 5 件を見逃した**
(issues_s3.md 課題 E)。目で見て「直ったように見える」で止めない。ここは
段 3 の T2/T3 で実際に使った手つきをそのまま書いたもの。

道具はリポジトリルート (family-mruby) の `tools/` と、下の 2 本の小さな
スクリプト (どちらも Pillow だけ。作業用に置いて使い、コミットしない)。

## 1. 撮る前に、カーソルを毎回同じ場所へ戻す

マウスカーソルは canvas に描かれるので、**動かしたままだと差分に出る**。
比較する 2 枚は、必ず同じ座標にカーソルを置いてから撮る。壁紙の上の
邪魔にならない点 (426x240 なら (410,200) 等) を 1 つ決めておく。

```
ruby tools/fmrb_input.rb move 410 200 sleep 600
python3 tools/fmrb_screenshot.py base.png
```

時計はメニューバーで毎秒動くので、**比較範囲から y<13 を外す**。

## 2. 「元に戻ったか」は差分の画素数で見る

窓やダイアログを開いて閉じたあと、地が元どおりかを数える。

```python
# pngdiff.py a.png b.png [x y w h]  -> 矩形内で色の違う画素を数えて並べる
from PIL import Image
a = Image.open(sys.argv[1]).convert("RGB"); b = Image.open(sys.argv[2]).convert("RGB")
pa, pb = a.load(), b.load()
diff = [(x, y, pa[x, y], pb[x, y])
        for y in range(y0, y0 + h) for x in range(x0, x0 + w) if pa[x, y] != pb[x, y]]
```

```
python3 pngdiff.py base.png after.png 0 13 426 227
rect (0,13,426,227) of 426x240: 0 differing pixels
```

**0 であること**が合格。1 画素でも残るなら跡が残っている。段 3 では
デスクトップの Set Clock / Config / Network / Launcher の開閉と、Storage の
ボタン入替の戻りをこれで見た。

## 3. 枠と部品の境目は、行 (列) の連長で読む

「枠が消えた」「白い矩形が出た」は、その行を左から読んで色の並びを見る。

```python
# pngscan.py img.png row Y [X0 X1]  /  pngscan.py img.png col X [Y0 Y1]
# 同じ色が続く区間をまとめて出す
  100- 102 (  3px) #b66d55     <- パネルの外 (壁紙)
  103- 103 (  1px) #6d0000     <- 枠線
  104- 177 ( 74px) #ffffff     <- パネルの地
```

読み方の型:

- **パネルの中に壁紙の色が現れていないか** (透明キーで穴が空いていないか)
- **枠の 1 画素が生きているか** (`row 100` に左枠 x=5 があるか)
- **消えた部品の跡が地の色 1 色になっているか** (monitor の kill 後の行)

## 4. 角の丸みは、前後の同じ矩形を突き合わせる

四隅は 3-4 画素の話なので目視では分からない。**操作の前後**で角の小矩形を
差分にかける。

```
python3 pngdiff.py before.png after.png 0 205 20 20      # 左下
python3 pngdiff.py before.png after.png 285 205 25 20    # 右下
```

nsf_player の起動 -> Play がこれで `0 differing pixels`。

## 5. 検査が生きていることを、わざと壊して確かめる

「警告が 0 件」は、検査が死んでいても同じ見え方になる。

- ホストテストなら、**直した実装を元に戻して走らせる** (段 3 では
  `flush` を 1 周版に戻すと 1 件、`handle` の押下発火の分岐を戻すと 10 件
  落ちた)。
- sim なら、**わざと壊した一時アプリを 1 本置く**。`flash/app/` 以下は
  実行時に読まれるので**再ビルドが要らない**。T4 では窓からはみ出した
  ボタンを持つアプリを置いて、警告が 1 行出ることを見てから消した。

## 6. 「自分の変更が原因か」は、変更前の版を同じビルドに同梱して並べる

コードを読んだだけの判断は段 2 で 2 回外しかけた。疑わしいときは、変更前の
ソースを一時アプリとして**同じビルドに入れて**並べて撮る。費用はビルド
1 回で、確実。

同じことがホストテストでもできる (そちらが速い)。段 3 の T2 で「Storage の
ボタンが欠けるのは自分のせいか」を、painter を使わない素の FmrbUI で
再現して**既存の性質**だと確定させた。**sim を回す前に、まずホストで
再現を試みる**。

## 7. ログも一緒に見る

```
docker logs fmruby_core 2>&1 | grep -cE "^E \(|Exception|abort"      # 0 か
docker logs fmruby_core 2>&1 | grep "FmrbUI:"                        # はみ出し警告
```

## 8. 気をつけること

- sim は**3 コンテナまとめて**上げ直す (core だけ再起動すると framebuffer が
  死ぬ)。ビルドし直したら `docker compose down` してから
  `tools/dev_run_check.sh --keep`。
- `rake build:linux` は古い esp32 の build/ が残っていると Xtensa/RISC-V の
  まま "Linux build complete" と出す。撮る前に
  `file build/fmruby-core.elf` で x86-64 を確認する。
- アプリを 6 本ほど開くと VM プールが尽きて `No free context slots` になる。
  検証の都合で並べたのなら、`kill` してから次を起動する。
