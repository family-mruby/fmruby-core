---
title: Family mruby<br>ちいさな Ruby の機械
subtitle: 日本語の発表サンプル
author: Family mruby プロジェクト
theme: default
allotted_time: 5m
---

# はじめに

Family mruby は、Ruby だけで動く小さなコンピュータです。電源を入れると
Ruby が立ち上がり、そのまま書いて動かせます。

- キーボードとテレビだけで完結する
  - 母艦の PC は要らない
- ぜんぶ Ruby で書いてある

# 中身のはなし

{基板|きばん}の上には ESP32 が載っていて、その上で mruby が動きます。

1. 画面と音は別のマイコンが担当する
2. 二つは UART でつながっている

> 小さいけれど、{侮|あなど}れない性能です。

# 積みかさね

```fmrb
gfx = $fmrb_gfx
x = $fmrb_x
y = $fmrb_y
w = $fmrb_w

layers = [
  [0xE0, "ユーザのアプリ"],
  [0x1C, "Family mruby OS"],
  [0x03, "PicoRuby / mruby VM"],
  [0x49, "ESP32"],
]
bh = 14
i = 0
while i < layers.length
  layer = layers[i]
  gfx.fill_rect(x, y + i * (bh + 2), w, bh, layer[0])
  tx = x + (w - gfx.text_width(layer[1], :default)) / 2
  gfx.draw_text_mixed(tx, y + i * (bh + 2) + 3, layer[1], 0xFF, layer[0])
  i += 1
end
$fmrb_y = y + layers.length * (bh + 2) + 4
```

{::wait/}

**下から上まで、境目なく Ruby です**

# 書きかた

本文には **太字** と `inline code` が書けます。

ルビは `{kanji|kana}` の形で書きます。

```ruby
class Hello < FmrbApp
  def on_create
    @gfx.clear(0x03)
  end
end
```

# おわりに

作って、動かして、直す。

Family mruby
{:.center}

`github.com/family-mruby`
{:.center}
