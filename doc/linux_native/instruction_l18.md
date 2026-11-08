# L18 指示書: セッションの機種では Exit を「Log Out / ログアウト」に

> 状態: 実装中 | 更新: 2026-11-08 | ユーザ決定 (2026-11-08)。CRT の C6b の後

## 決定事項 (ユーザ)

- Family mruby をログインのセッションとして動かす機種 (`linux_pc`・`linux_pc_wl` など、
  電源の項目に logind の電源切り・再起動・休止があるもの) では、電源の項目の Exit を
  **「Log Out」/「ログアウト」**と表示する。確認の文は **"Log out?" / 「ログアウトしますか?」**。
- 働きは今の Exit と同じ (Family mruby が終わり、セッションが終わってログイン画面に戻る)。
- それ以外の機種 (`sdl` の Window で動かす形、Cardputer など) は今の **Exit / 終了** と
  確認の文のまま。

## 範囲

1. 機種がセッションかどうかを、すでにある情報 (機種の定義の電源の項目、logind の有無など)
   から決める。新しい設定項目が要るなら、機種の定義に 1 つ足す形でよい
   (足したら report に書く)。どちらで決めたかを report に書く。
2. デスクトップの電源のメニュー (`system_desktop.app.rb`) と、キーボードだけの形
   (`system_desktop_keys.app.rb`) の両方。文字は `i18n.rb` に `power_logout`・
   `power_logout_confirm` を足す (英語・日本語)。
3. ついでの整理 (見た目は変わらない): 機種の定義の CRT の強さの既定
   (`native/machine/native_machine.c` の 55・30・…) を、C6b で作った値の表
   `hosted/crt/crt_presets.h` の `CRT_PRESET_STANDARD` を読む形に揃える。
4. 試験 (`test/native/wl/` の power_exit を押す場面など) と回帰の基準が変わるなら、
   変化を report に書き、基準の更新は別のコミットにする。

## 確認

- `rake regress`、`rake regress:wl`、`rake regress:screens`。
- `linux_pc_wl` の形を headless で起動し、電源のメニューに「Log Out」、確認に
  "Log out?" が出ること。日本語でも。`sdl` の形では今の Exit のままであること。
- GPD (ssh、鍵。`.deb` は `~/fm-deb/fm.deb` に送り
  `sudo -n apt install -y /home/kishima/fm-deb/fm.deb`) に入れる。ログインし直しと目視はユーザ。

## 触ってよい範囲

fmruby-core の `main/prebuild_scripts/kernel/` のデスクトップと i18n、`native/` の機種の
定義と電源まわり (`native_machine.c` の CRT の既定を含む)、`test/native/`、回帰の基準、`doc/linux_native/`。ほかが要るなら止まって返す。

## 止まる条件

- 上の決定と違う動きが要る (Cardputer など、ほかの機種の表示を変えることになる等)。
- 範囲外の変更が要る。

## report

`doc/linux_native/report/l18.md`: どこでセッションの機種を見分けたか、変えた画面、確認の結果、残件。
