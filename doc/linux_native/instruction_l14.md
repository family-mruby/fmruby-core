# L14 指示書: 毎日使う環境 (3) 設定と丈夫さ

> 計画: doc/linux_native/plan.md (段取りの L14)。
> 前段の結果: report/l6.md (OS のサービス: Wi-Fi の表示・音量・電源)、
> report/l9a.md 〜 l9e.md、report/l13.md

## 決め方 (2026-10-28 ユーザ)

ユーザは「L14 まで、推奨で実装してみて。判断はあとでまとめてやる。多少の
手戻りは許容する」と指示した。下の「親の仮決め」は親が推奨で決めたもので、
ユーザがあとでまとめて確かめる。**仮決めの範囲で作り、仮決めに無い仕様の
選択が出たら、推奨を 1 つ選んで作り、report の「仮決めに足した選択」に
理由と別の案を書く** (止まらない)。ただし端末・sim・ブラウザ版の動きを
変える選択は、止まって返す。

## 親の仮決め (ユーザ確認待ち)

- **PC だけの機能にする** (Linux の機種。コンポジタの形を主に、`sdl` の形でも
  無理なくできる所まで)。端末・sim・ブラウザ版は変えない。
- **Wi-Fi**: 今の Network のダイアログ (L6 では表示だけ) から、NetworkManager
  (D-Bus、無ければ nmcli) で、見えるネットワークの一覧・つなぐ (パスワードの
  入力)・切る、ができるようにする。パスワードは NetworkManager に渡し、Family
  mruby の側に残さない。
- **Bluetooth**: BlueZ (D-Bus) で、入り切りと、つないだことのある機器の一覧と
  つなぐ・切る。新しい機器のペアリングは L14 ではしない (Linux の道具を案内
  する 1 行を出す)。
- **音量**: PipeWire / PulseAudio があればそちら (wpctl・pactl、または D-Bus) で
  出力の音量とミュートを操作し、無ければ今の ALSA のまま。音量のキー
  (KEY_VOLUMEUP/DOWN/MUTE) も効かせる。
- **通知**: freedesktop の通知の口 (org.freedesktop.Notifications、D-Bus の
  セッションバス) を Family mruby が受け持ち、Linux のアプリの通知を Family mruby
  の見た目の小さな知らせとして画面の右上に数秒出す。Family mruby のアプリから
  出せる Ruby の口も足す (PC だけで使える)。通知の履歴は持たない。
- **画面の共有と録画**: xdg-desktop-portal-wlr で、ブラウザの会議などから画面を
  共有できるようにする (wlr-screencopy は L8 で出してある)。portal の設定
  (portals.conf) とセッションの環境を整え、.deb の Recommends に
  xdg-desktop-portal-wlr を足す。
- **落ちたときの復帰**: セッションの台本が、Family mruby が Exit 以外で終わった
  とき (落ちたとき) に自動で起こし直す。1 分に 3 回を超えたら起こし直しを
  やめてログイン画面に戻る。落ちたことと理由はログに出し、起こし直した後の
  Family mruby が「前回は落ちて再起動した」と短く知らせる。Linux のアプリは
  コンポジタと一緒に終わる (Wayland の決まり) ことを report に書く。

## 作業の内容

上の仮決めのとおりに作る。D-Bus は libsystemd の sd-bus (今の Depends にある)
で、待たない作り (協調の作り、L2n) にする。OS のサービスの口
(`fmrb_platform_sys`) の作りに合わせ、端末は「使えない」と答える。

## 触ってよい範囲

- fmruby-core: `native/`、`hosted/`、`config/machines/`、`main/kernel/` の
  `fmrb_platform_sys` の口、`main/prebuild_scripts/kernel/` (デスクトップの
  ダイアログ・メニューバー、PC だけの部分は機種で切り替える)、`lib/add/` の
  該当箇所、`native/package/`、`test/`、`rakelib/`、`tools/regress/`、`sig/`、
  `doc/linux_native/report/`、`doc/regression/usage.md`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない (コンテナの像に NetworkManager・BlueZ・PipeWire・portal などを
  入れて試すのはよい)。手元の WSL や利用者の PC のネットワーク・Bluetooth・
  音の設定を変えない (試験は偽の D-Bus のサービスかコンテナの中で)。HOME・XDG の
  場所は一時ディレクトリ。WSLg に窓を出しっぱなしにしない。
- kernel・デスクトップの Ruby を変えたら、標準 (Spinel) と全部 mruby の両方で
  確かめる。

## 受け入れ条件

1. 偽の NetworkManager (D-Bus) で、一覧・つなぐ・切るがダイアログから働く。
2. 偽の BlueZ で、入り切りと既知の機器のつなぐ・切るが働く。
3. 音量とミュートが PipeWire (コンテナ) で変わり、音量のキーも効く。無いときは
   ALSA のまま。
4. `notify-send` の通知が Family mruby の知らせとして出て、数秒で消える
   (`rake regress:wl` に足す)。
5. portal: コンテナで xdg-desktop-portal-wlr が画面の取り込みに応じる所まで
   (試せる範囲で。試せない所は作りと理由を report に)。
6. 落ちたとき: Family mruby を SIGSEGV で止めると、セッションの台本が起こし直し、
   知らせが出る。1 分に 4 回目でやめる。Exit では起こし直さない。
7. 端末・sim・ブラウザ版の動きと画面の基準が変わらない。`rake regress`、
   `rake regress:screens`、`rake regress:size` (内蔵 RAM +0)、`rake native:test`、
   `rake regress:wl`、deb_check が通る。
8. Ruby を変えた所は標準と全部 mruby の両方で。
9. 最後に amd64 の `.deb` を作る。

## 止まる条件

- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える作り
  しか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l14.md) に書くこと

- 各機能の作り (D-Bus の使い方、待たない作り)、仮決めに足した選択 (理由と別の案)
- 受け入れ条件ごとの結果と数値、VM で人が確かめる手順、残件
