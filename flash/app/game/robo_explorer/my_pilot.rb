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
#   "view"       正面の廊下の見え方。[左が壁, 右が壁, そのマスにある物] が
#                手前から並ぶ (最大 5 個)。壁は 1/0、物は front と同じ言葉。
#                リストが床で終われば、その先は壁。
#
# 命令 (Hash を返す。nil なら何もしない):
#   { "op" => "move" }                 前へ 1 マス
#   { "op" => "turn", "to" => "L" }    左を向く ("R" で右)
#   { "op" => "wait" }                 1 ターン待つ
#   { "op" => "reset" }                最初からやり直す (手詰まりの脱出用)

class MyPilot
  def initialize
    @mode = :try_right
  end

  # キーが押された。命令を返すと送信される。
  def on_key(scancode, state)
    return { "op" => "move" }               if scancode == FmrbConst::KEY_UP
    return { "op" => "turn", "to" => "L" }  if scancode == FmrbConst::KEY_LEFT
    return { "op" => "turn", "to" => "R" }  if scancode == FmrbConst::KEY_RIGHT
    return { "op" => "wait" }               if scancode == FmrbConst::KEY_DOWN
    return { "op" => "reset" }              if scancode == FmrbConst::KEY_R
    nil
  end

  # 自動運転 (S で開始/停止) の間、0.2 秒ごとに呼ばれる。
  # nil を返せば何もしない。
  #
  # サンプル: 右手法。右手を壁に当てて歩き続けると、この迷路の通路は
  # いつか全部回れる。やることは 2 つだけ:
  #   1. まず右を向いてみる (右が空いているなら右へ行きたい)
  #   2. 前が塞がっていれば左へ回り直し、空いたら進む。進んだらまた 1 へ
  # 鍵を持っていない間は、扉も壁とみなすのがコツ (開かない扉に頭を
  # 打ち続けないため)。@mode が「いま 1 と 2 のどちらか」の記憶。
  def think(state)
    front = state["front"]
    blocked = front == "wall" || (front == "door" && state["keys"] == 0)
    if @mode == :try_right
      @mode = :look
      return { "op" => "turn", "to" => "R" }
    end
    if blocked
      { "op" => "turn", "to" => "L" }
    else
      @mode = :try_right
      { "op" => "move" }
    end
  end

  # 命令への返事が届いた。result["ok"] が false なら result["reason"] に理由。
  def on_result(result, state)
  end
end
