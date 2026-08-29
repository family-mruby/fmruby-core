---
title: Family mruby<br>Ruby だけで動く小さな機械
subtitle: 日本語での発表サンプル
author: Family mruby プロジェクト
theme: default
allotted_time: 10m
chime: true
---

# きょうの話

1. Family mruby とは何か
2. 中身がどうなっているか
3. 何が作れるか
4. これから

{::wait/}

質問は最後にまとめて受けます

# Family mruby とは

電源を入れると Ruby が立ち上がる、小さなコンピュータです。

- キーボードと画面があれば、それだけで完結する
  - 母艦の PC は要らない
  - {書|か}いて、{動|うご}かして、{直|なお}す、を手元で回せる
- OS もアプリも、ぜんぶ Ruby

# 二つの機械

- **Retro** は遊ぶ機械
  - テレビ (NTSC) につないで、ESP32-S3 で動く
- **Modern** は作る機械
  - HDMI か内蔵パネルで、ESP32-P4 で動く

{::wait/}

> 遊ぶ機械と作る機械。どちらも同じ Ruby が動きます

# 中身のはなし

{基板|きばん}の上に ESP32 が載り、その上で mruby が動きます。

画面と音は別の仕事です。アプリは描画命令を送るだけ。
命令は束ねて送られるので、転送で詰まりません。

{::wait/}

**座標と色を並べれば、絵になる**

# アプリはこう書く

```ruby
class Hello < FmrbApp
  def on_create
    gfx.draw_text_mixed(8, 8, "Hi", theme_fg)
    gfx.present
  end
end
```

- 毎フレーム描かない。変わったときだけ `present`

# 何が作れるか

- ゲーム (スプライトと効果音)
- 道具 (エディタ、ファイル管理、モニタ)
- 音楽 (内蔵音源と MIDI 出力)
- そして**この発表そのもの**

{::wait/}

この画面も Ruby のアプリです

{:.center}

![60%](images/desktop.png)

{:.center}

# これから

- 日本語の入力と表示を、もっと当たり前に
- 作ったものを、そのまま人に渡せるように
- {迷|まよ}ったら、小さいほうを選ぶ

{::goal/}

# ご清聴ありがとうございました

ここからは質疑の時間です。

うさぎは前のページでゴールしています。

{:.center}
