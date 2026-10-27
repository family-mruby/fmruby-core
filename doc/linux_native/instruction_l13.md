# L13 指示書: 毎日使う環境 (2) 窓と画面

> 計画: doc/linux_native/plan.md (段取りの L13)。
> 前段の結果: report/l9a.md 〜 l9e.md、report/l12a.md、report/l12b.md

## 決め方 (2026-10-28 ユーザ)

ユーザは「L14 まで、推奨で実装してみて。判断はあとでまとめてやる。多少の
手戻りは許容する」と指示した。下の「親の仮決め」は親が推奨で決めたもので、
ユーザがあとでまとめて確かめる。**仮決めの範囲で作り、仮決めに無い仕様の
選択が出たら、推奨を 1 つ選んで作り、report の「仮決めに足した選択」に
理由と別の案を書く** (止まらない)。ただし端末・sim・ブラウザ版の動きを
変える選択は、止まって返す。

## 親の仮決め (ユーザ確認待ち)

- **PC だけの機能にする** (コンポジタの形 `linux_pc_wl`。窓の操作は `sdl` の形でも
  無理なくできる所まで)。端末・sim・ブラウザ版の見た目と操作は変えない。
- **最大化**: 窓の題の帯のダブルクリックで、デスクトップの作業領域 (メニューバー
  とタスクバーを除く所) いっぱいに広げ、もう一度で戻す。Family mruby の窓
  (大きさを変えられるもの) にも Linux の窓にも効く。Linux のアプリからの最大化の
  頼み (Firefox の □ など) にも応じる。題の帯に新しいボタンは足さない。
- **全画面**: Linux のアプリからの全画面の頼み (Firefox の F11、動画) に応じ、
  Family mruby の画面全体 (メニューバーも隠す) を覆う。アプリが解くか、窓を
  閉じると戻る。
- **キーで窓を並べる**: Super (Windows キー) + 左・右で画面の左半分・右半分、
  Super + 上で最大化、Super + 下で元に戻す。前にある窓に効く。
- **画面ロック**: 標準の画面ロックの口 (ext-session-lock-v1) を持ち、swaylock
  などのロックのプログラムが使えるようにする。電源のメニューに「ロック」を足し
  (ロックのプログラムがあるときだけ)、logind の Lock の知らせ (蓋を閉じた、
  休止の前) でもロックする。Family mruby 自身はパスワードを扱わない。
  .deb の Recommends に swaylock を足す。
- **複数のモニタ**: 最初のモニタに Family mruby を出し、ほかのモニタには同じ
  絵を映す (鏡)。広げる使い方は後で。
- **明るさ**: 液晶の明るさを logind の SetBrightness (backlight) で変える。
  明るさのキー (KEY_BRIGHTNESSUP/DOWN) と、メニューバーの右の音量の隣の操作
  (PC だけ) から。

## 作業の内容

上の仮決めのとおりに作る。最大化・半分の配置は kernel の window manager の
窓の位置と大きさの操作として作り (Linux の窓は代役を通して同じ操作が効く)、
コンポジタは結果に従う。全画面の Linux の窓は代役を全画面のアプリの扱いにする
か、コンポジタで覆うかを作りを読んで選ぶ。

## 触ってよい範囲

- fmruby-core: `native/`、`hosted/`、`config/machines/`、`main/kernel/` と
  `main/prebuild_scripts/kernel/` (window manager・入力の振り分け・デスクトップ、
  PC だけの動きは機種の定義か native の口で切り替える)、`lib/add/` の該当箇所、
  `native/package/`、`test/`、`rakelib/`、`tools/regress/`、`sig/`、
  `doc/linux_native/report/`、`doc/regression/usage.md`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- sudo は使わない (コンテナの像に swaylock などを入れるのはよい)。HOME・XDG の
  場所は一時ディレクトリ。WSLg に窓を出しっぱなしにしない。実機の電源・明るさは
  試さない (偽の backlight と logind の代わりで試す)。
- kernel・デスクトップの Ruby を変えたら、標準 (Spinel) と全部 mruby の両方で
  確かめる。

## 受け入れ条件

1. 題の帯のダブルクリックで、Family mruby の窓と foot が作業領域いっぱいになり、
   戻る。foot の最大化の頼みにも応じる (`rake regress:wl` に足す)。
2. Linux のアプリの全画面の頼みで画面全体を覆い、解くと戻る。
3. Super + 左・右・上・下が仕様どおりに効く。
4. ext-session-lock: コンテナで swaylock (または試験用のロックのクライアント) が
   画面を覆い、ロック中は Family mruby にも Linux の窓にも入力が届かない。
   ロックのクライアントが落ちても画面が開かない (決まりどおり)。
5. headless の出力を 2 つにして、2 つ目に同じ絵が映る。
6. 偽の backlight で明るさのキーとメニューの操作が値を変える。
7. 端末・sim・ブラウザ版の動きと画面の基準が変わらない。`rake regress`、
   `rake regress:screens`、`rake regress:size` (内蔵 RAM +0)、`rake native:test`、
   `rake regress:wl`、deb_check が通る。
8. 1・3 を標準と全部 mruby の両方で。
9. 最後に amd64 の `.deb` を作る。

## 止まる条件

- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える作り
  しか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l13.md) に書くこと

- 各機能の作り、仮決めに足した選択 (理由と別の案)
- 受け入れ条件ごとの結果と数値、VM で人が確かめる手順、残件
