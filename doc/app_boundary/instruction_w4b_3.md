# W4b-3 指示書: アプリと Window (FmrbApp) を境界に載せる

> 親: plan.md の作るもの 1・2、段 W4b (機能のまとまり 7 つの最後)、「W4a で決めたこと」「W4b-1 の後に決めたこと」、
> wasm_apps の決定 18 (FmrbApp に残ったクラスメソッドは、状態を読むだけのものを境界に載せる)。前段は report/w3.md
> (殻)・w4a.md・w4b_1.md・w4b_1b.md・w4b_2.md。時間の目安は doc/regression/usage.md 0 章。

## 方針 (ユーザ、2026-11-15)

- 速く進める。小さな差 (VM プールの数百バイトなど) は理由を書けば止まらなくてよい。KB 単位で増えて理由が説明
  できないときだけ止まる。
- 速さの確認は Ruby のエンジン (mruby・Spinel) だけ。ほかの言語は致命的でなければ多少遅くてよい。
- C の名前: 本体の中の実体は `fmrb_bnd_app_<メソッド>`、アプリから見える名前は `fmrb_app_<メソッド>`。本体の
  内部の `fmrb_app_*` (アプリの task の API) とぶつからないこと (W4b-1 の決定)。

## やること

0. **W4b-2 の後の決定を入れる**: FmrbAudio の C の境界を受け手の番号を取る形に戻す (本体では番号を使わない。
   ABI のハッシュが変わったら新しい値を書く)。`NO_RECEIVER` は FmrbApp だけ。FmrbApp に印を付けたら `STAGED` を消す。

1. **調べて表にする** (report の最初の章): FmrbApp (sig/fmrb_app.rbs) のメソッドを、(a) 境界に載せる頼みごと、
   (b) フック (`on_create`・`on_update`・`on_event` など、`%a{fmrb.hook}`)、(c) 載せない内側 (`%a{fmrb.internal}`、
   エンジンのオブジェクトの一生・Ruby で書かれたもの) に分ける。W3 の殻 (main/app/fmrb_app_shell.c) が持つ処理と、
   mruby・Spinel の基底クラスが持つ処理の重なり (report/w3.md の一覧) を、この段でどこまで殻に寄せるかを書く。
   **アプリの書き方が変わる所は、変える前に一覧にして止まって返す**。
2. **型定義に印と型を付けて生成に切り替える** (W4a・W4b-2 と同じ作り)。フックは SDK に一覧を出し、wasm の側で
   アプリが公開する関数の名前と型 (殻が呼ぶもの) を C のヘッダに出す (wasm_apps の W1b で使う)。
3. **mruby と Spinel の包みを揃え、mruby の gem のつなぎを生成に**。Lua・MicroPython は生成で使えるように
   (Lua の大きさの関数 `FmrbApp.user_area_width()` などは W4a の決定の形を保つ)。
4. `boundary:check`・sig/README.md・MicroPython の qstr の表。
5. 毎フレーム・毎イベントで呼ぶもの (メッセージ待ち・タイマー・イベントの座標など) の mruby の命令数を前後で測る。
   増えたものは理由と、C に戻す候補を report に (戻すかは親が決める)。

## 確かめること (これだけ)

- `rake regress`・`rake regress:spinel`・`ruby tools/wamr_poc/run.rb boundary`。sig を直したら `rake clean` してから
  ビルド。
- `rake regress:checkpoint` (FmrbApp の mrblib が変わると版の違う移動は断る作りなので、同じ版の移動が今までどおり
  通ること)。
- 最後に 1 回: `rake regress:screens`・`rake regress:size` (内蔵 RAM の静的な増分 0)・`rake regress:run`。
- 直しのやり直しでは、変えた所が影響する道具だけを流し直す。

## 触ってよい範囲

- fmruby-core (develop)。graphics-audio は変えない。既存の未コミット (components/ のサブモジュール 2 つ、wasm/vendor/) には
  触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。
- 終わったら Linux のビルド (x86-64)、sim を片付け、裏の処理を残さない。

## 止まる条件

- アプリの書き方・名前・既定値が変わる選択 (一覧にして返す)。内蔵 RAM の増分。VM プールの KB 単位の増分で理由が
  説明できないもの。graphics-audio の変更が要るとき。ユーザの作業が要るとき。

## report (report/w4b_3.md)

調べた表 (頼みごと・フック・内側)、殻と基底クラスの重なりの扱い、載せた関数と載せなかった関数 (理由)、各言語の変化、
命令数の前後、確かめた結果と数字、W4b の締め (plan.md の段 W4b を完了にできるか) と wasm_apps の W1b への注意。
開始と終了の時刻は `date` で書く。
