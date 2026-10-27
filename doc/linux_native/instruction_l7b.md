# L7b 指示書: メニューバーの右上の電源ボタン

> 計画: doc/linux_native/plan.md (段取りの L7b、「OS のサービス」の電源)。
> 前段の結果: report/l6.md (電源の項目と `FmrbApp.power_actions`)、report/l7.md

## ユーザの決定 (2026-10-27、変えない)

- mouse のデスクトップのメニューバーの**右上に電源ボタン**を置き、押すと
  電源の項目が**サブメニュー**として開く。
- **全部の端末で揃える**: Tab5・S3 の Reset も、native の終了・電源断・再起動・
  休止も、このサブメニューに入る。項目は今どおり `FmrbApp.power_actions` から
  作る。項目が無い所 (sim・ブラウザ版) ではボタンを出さない。
- 左上の「Family mruby」のメニューにある電源の項目は**消す** (右上だけにする)。
- keys のデスクトップ (メニューの最後の電源の項目) は変えない。

## 作業の内容

- 電源ボタンは、メニューバーの右の端 (時計より右) に置く。絵は小さな電源の
  印 (丸に縦線) にし、他の印 (Wi-Fi・音) と同じ大きさと色の決まりに合わせる。
  ほかの印の位置は今は右からの引き算の連なりで書かれているので、ボタンの
  幅のぶんを 1 か所で持てる形に整理してよい (見た目は、ボタンが入る分を
  除いて変えない)。
- サブメニューは、ボタンの下に右揃えで開く。見た目と操作は左上のメニューの
  作り (`draw_dropdown`・`hit_dropdown?`・行の高さ) を使い回す。外を押せば
  閉じる。項目を選んだ後は今の確認のダイアログを通す (`run_dropdown_item` の
  `:reset` と `:power_*` の扱いをそのまま使う)。
- キーボードでも開けること: 左上のメニューをキーで開く口
  (`open_dropdown_from_key` の周り) があるので、そこから電源のメニューへも
  届く形にする (例: 左上のメニューで左右の矢印でメニューを移る)。やり方は
  作りを読んで選び、report に書く。
- Retro (320 幅) でも収まること。Retro は Wi-Fi が無く印が少ないが、時計・
  メモリ・音の位置と重ならないことを座標で確かめ、report に数値を書く。
- 文字列は i18n の表の既存のものを使う。足すならその表に (en・ja)。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/kernel/system_desktop.app.rb` と
  `main/prebuild_scripts/kernel/system_desktop/`、`test/`、
  `doc/linux_native/report/`。
- 触らないもの: keys のデスクトップ、kernel の電源の受け渡し
  (`fmrb_kernel.rb` の reboot・power)、C の側、fmruby-graphics-audio、
  sdkconfig、submodule の中、`flash/`、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は ESP32-P4 のビルド。自分で `rake clean_all` はしない。

## 受け入れ条件

1. native の `--machine sdl` で、右上に電源ボタンが出て、押すと「終了」だけの
   サブメニューが開き、選ぶと確認を経てプロセスが終わる。
   `--set system.power=logind --set system.power_log_only=true` では 3 つが出て、
   選ぶと logind への要求がログに出る。`--machine linux_pc` (同じく log only、
   SDL はダミーでよい) では 4 つ出る。左上のメニューに電源の項目が無い。
2. キーボードだけで電源のサブメニューを開いて項目を選べる (native の sdl)。
3. sim (Retro・Modern) とブラウザ版ではボタンが出ず、左上のメニューも
   今と同じ (電源の項目はもともと無い)。`rake regress:screens` が通る。
4. 端末: 画面を撮れないので、Tab5 (426 幅) と S3 (320 幅) の座標を計算で
   示し、`rake regress:size` で内蔵 RAM が増えていないことと flash の増え方を
   書く。実機での目視はユーザに回す (report の残件に書く)。
5. 1・2 を標準と全部 mruby の両方で。`rake regress` が通る。
6. 画面の基準が変わったら、変化を report に書き、基準の更新は別のコミット
   にする。

## 止まる条件

- 上の決定と食い違う作りが要るとき (例: Retro の幅に収まらず、他の印を
  消さないと置けない)。
- keys のデスクトップや kernel を変えないとできないとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l7b.md) に書くこと

- ボタンとサブメニューの作り、キーボードでの開き方とその理由
- 端末ごとの座標 (426 幅・320 幅) と、内蔵 RAM・flash の差
- 受け入れ条件ごとの結果と、確かめたコマンド
- 見立てと違った点、撤回した仮説、踏んだ罠、残件 (実機の目視を含む)
