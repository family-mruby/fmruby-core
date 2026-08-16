# あなたが書くのはこのファイルだけ。ロボットの頭脳。
#
# robo_pilot が require して使う。編集したら robo_pilot を起動し直すこと。
# ロボットについて知れるのは state だけ、できるのは命令を返すことだけ。
#
# state (Hash, キーは文字列):
#   "x","y"      いまの場所            "front"  前のマス ("floor"/"wall"/
#   "dir"        向き ("N"/"E"/"S"/"W")          "key"/"door"/"goal")
#   "goal"       ゴールの方角 (8方位)   "keys"   持っている鍵の数
#   "done"       ゴールしたか           "steps"  手数
#
# 命令 (Hash を返す。nil なら何もしない):
#   { "op" => "move" }                 前へ 1 マス
#   { "op" => "turn", "to" => "L" }    左を向く ("R" で右)
#   { "op" => "wait" }                 1 ターン待つ

class MyPilot
  # キーが押された。命令を返すと送信される。
  def on_key(scancode, state)
    return { "op" => "move" }               if scancode == FmrbConst::KEY_UP
    return { "op" => "turn", "to" => "L" }  if scancode == FmrbConst::KEY_LEFT
    return { "op" => "turn", "to" => "R" }  if scancode == FmrbConst::KEY_RIGHT
    return { "op" => "wait" }               if scancode == FmrbConst::KEY_DOWN
    nil
  end

  # 0.2 秒ごとに呼ばれる。自動運転はここに書く。
  # 例 (前が空いていれば進む、だめなら右を向く):
  #   return { "op" => "move" } if state["front"] != "wall"
  #   { "op" => "turn", "to" => "R" }
  def think(state)
    nil
  end

  # 命令への返事が届いた。result["ok"] が false なら result["reason"] に理由。
  def on_result(result, state)
  end
end
