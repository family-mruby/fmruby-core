# TODO

## ホストのCanvas描画

Canvasごとに２枚のスプライト
１枚目は描画用
２枚目はレンダリング用
Presentコマンドでレンダリング用にPushする
　Push中はrender禁止だが、renderとシーケンシャルに行うなら意識不要


Canvas構造体を作る
スプライトのポインタ
Z Order
dirty管理
可視不可視フラグ
将来的には、子Canvasを持てるようにしたい。
　画像の読み込みや、ゲームのスプライトのようにして使いたい
親と子でChainできるようにする（Z-Oderでソートする機能も必要になる）

render_frame()の実装
Canvas合成
　Z-orderの低い方からCanvasを再帰的に描画
　　描画対象はレンダリング用のところ
　SystemAppのOderは0固定
