# あなたが書くのはこのファイルだけ。ロボットの頭脳 (Python 版)。
#
# robo_pilot_py が import して使う。編集したら robo_pilot_py を起動し直すこと。
# ロボットについて知れるのは state だけ、できるのは命令を返すことだけ。
#
# state (辞書、キーは文字列):
#   "x","y"      いまの場所            "front"  前のマス ("floor"/"wall"/
#   "dir"        向き ("N"/"E"/"S"/"W")          "key"/"door"/"goal")
#   "goal"       ゴールの方角 (8方位)   "keys"   持っている鍵の数
#   "done"       ゴールしたか           "steps"  手数
#   "view"       正面の廊下の見え方。[左が壁, 右が壁, そのマスにある物] が
#                手前から並ぶ (最大 5 個)。壁は 1/0、物は front と同じ言葉。
#                リストが床で終われば、その先は壁。
#
# 命令 (辞書を返す。None なら何もしない):
#   {"op": "move"}                前へ 1 マス
#   {"op": "turn", "to": "L"}     左を向く ("R" で右)
#   {"op": "wait"}                1 ターン待つ
#   {"op": "reset"}               最初からやり直す (手詰まりの脱出用)
#
# リセット (R や reset 命令) で世界が最初に戻ると、MyPilot も作り直される
# (属性は消える)。覚えたことは新しい回に持ち越されない。
#
# このファイルは import されるので、FmrbApp や FmrbGfx や Log は見えない。
# 見えるのは渡された state と、ここに書いた定数だけ。

# キーの番号 (HID Usage ID)。シミュレーションでも実機でも同じ値になる。
KEY_R = 0x15
KEY_RIGHT = 0x4F
KEY_LEFT = 0x50
KEY_DOWN = 0x51
KEY_UP = 0x52


class MyPilot:
    def __init__(self):
        self.mode = "try_right"

    # キーが押された。命令を返すと送信される。
    def on_key(self, scancode, state):
        if scancode == KEY_UP:
            return {"op": "move"}
        if scancode == KEY_LEFT:
            return {"op": "turn", "to": "L"}
        if scancode == KEY_RIGHT:
            return {"op": "turn", "to": "R"}
        if scancode == KEY_DOWN:
            return {"op": "wait"}
        if scancode == KEY_R:
            return {"op": "reset"}
        return None

    # 自動運転 (S で開始/停止) の間、一定の間隔で呼ばれる。
    # None を返せば何もしない。
    #
    # 見本: 右手法。右手を壁に当てて歩き続けると、この迷路の通路は
    # いつか全部回れる。やることは 2 つだけ:
    #   1. まず右を向いてみる (右が空いているなら右へ行きたい)
    #   2. 前が塞がっていれば左へ回り直し、空いたら進む。進んだらまた 1 へ
    # 鍵を持っていない間は、扉も壁とみなすのがコツ (開かない扉に頭を
    # 打ち続けないため)。self.mode が「いま 1 と 2 のどちらか」の記憶。
    def think(self, state):
        front = state["front"]
        blocked = front == "wall" or (front == "door" and state["keys"] == 0)
        if self.mode == "try_right":
            self.mode = "look"
            return {"op": "turn", "to": "R"}
        if blocked:
            return {"op": "turn", "to": "L"}
        self.mode = "try_right"
        return {"op": "move"}

    # 命令への返事が届いた。result["ok"] が False なら result["reason"] に理由。
    def on_result(self, result, state):
        pass
