# アプリの状態を覚える共通の口

> 状態: 完了 | 更新: 2026-10-29 | S1 検収済 (20c8bae6)。 アプリが小さな状態 (最後に開いたフォルダなど) を覚える共通の API。最初の利用者は File Manager。置き場所はどの端末でも /tmp/<アプリ>.state.toml

## 目的

- File Manager を閉じたときに最後に開いていたフォルダを覚え、次に開いたときに
  そこから始める (ユーザの要望、2026-10-29)。
- 同じことを各アプリが自前で書かなくて済むよう、共通の口にする (今はエディタの
  /home/editor.toml、麻雀の /home/mahjong.toml などが個別に書いている)。

## 決定事項 (2026-10-29 ユーザ)

- **共通の API を作り、最初は File Manager だけに使う**。他のアプリの移し替えは
  後で、別に決める。窓の位置・大きさを覚えることは今回しない。
- **置き場所はどの端末でも `/tmp/<アプリ>.state.toml`** (ユーザ決定。端末ごとの
  切り分けはしない)。どれだけ残るかは各端末の /tmp 次第 (実機は PSRAM で電源を
  切ると忘れる。フラッシュを傷めない)。

## 方針

- 覚えるのは小さな値 (文字列・数・真偽) の表。アプリが名前 (app_handle_name) ごとに
  1 つ持つ。
- 書くのは「閉じるとき (または明示の保存) に、変わっていれば」だけ。毎回は書かない。
- 読めない・壊れている・無いときは空の表で始める (アプリは既定に戻るだけ)。
- 状態のファイルを消せば既定に戻る。利用者のファイル (/home 直下) に混ぜない。
- mruby と Spinel の両方の基底で同じ形にする (File Manager はデスクトップの中で
  Spinel のカーネル側でも動く)。
- 内蔵 RAM は増やさない。

## 受け入れ条件

- File Manager で /mnt/host/x に移って閉じ、もう一度開くと /mnt/host/x から始まる
  (同じ起動の間。/tmp が消えるまで)。
- 覚えたフォルダが消えていたら、既定 (今の初期フォルダ) で始まる。
- 状態のファイルを壊しても起動する。
- `rake regress`・`regress:size` (内蔵 RAM +0)・`regress:screens` が通る。

## 段取り

| 段 | 中身 |
|---|---|
| S1 | 共通の API (mruby と Spinel)、置き場所の切り分け、File Manager の最後のフォルダ |

## 結果 (S1、2026-10-29 検収済)

- mruby と Spinel の基底に `app_state(who)`・`save_app_state(values, who)`・
  `app_state_path(who)` (= `/tmp/<who>.state.toml`)。値は文字列・整数・真偽の平らな
  表。基底にはメソッドだけ (ivar・定数なし)。閉じるときの保存はアプリが呼ぶ。
- ファイルはアプリが `save_app_state` を呼び、値が変わったときだけできる。
- File Manager は固定の名前 `file_manager` で、マウスと keys の両方のデスクトップ。
  覚えたものが無い・開けないときは "/"。
- 残る期間: 実機は電源・再起動まで、sim は core の再起動まで、ネイティブ版は
  プログラムを終えるまで、ブラウザ版は再読み込みまで。
- 回帰の場面 `fmgr_remember` (retro・modern・native) と `keys_fmgr_remember` を足した。
- 残件: Spinel のデスクトップ (`FMRB_APP_ENGINE_DESKTOP=spinel`) はこの変更の前から
  `spinel:gen` が 9 件の NoMethodError で通らない (battery_level・power・
  linux_settings ほか)。他のアプリをこの API へ移すかは別に決める。
