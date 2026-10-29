# 実装指示書 L15: VM の確認で見つけたものの直し

VM の確認 (2026-10-29) の結果と見立ては plan.md の「後で直すもの」の
「VM の確認 (2026-10-29 ...)」の項。ユーザが直すと決めたものだけをやる。

作業ブランチ: fmruby-core の `feature/linux-native` (本体 checkout、直列)。

## 直すもの

### A. 既定のアプリ (native/sys/linux_mime.c)

- 症状: PDF・PNG が Okular・Gwenview でなくブラウザで開く。
- 裏が取れた原因: セッションが `XDG_CURRENT_DESKTOP=family-mruby` を名乗るので、
  Kubuntu が KDE の既定を書く `/usr/share/applications/kde-mimeapps.list` を読まない。
  汎用の mimeinfo.cache は Chrome・Firefox が先。
- 直し方の方針: `XDG_CURRENT_DESKTOP` の値は変えない (Qt・portal などへの影響を
  避ける)。mimeapps.list を探す順に、自分の名前の一覧の後へ、**この機械の本来の
  デスクトップの一覧**を足す。どの名前を足すかは機種の定義 (config/machines の
  linux_pc_wl) の新しい欄で持ち、既定は `["kde"]` などにする (欄の名前と形は任せ、
  report に書く。Kubuntu 以外 (GNOME なら gnome-mimeapps.list) でも設定で済むこと)。
- **入っていないアプリは飛ばす**: 既定に書かれた .desktop が見つからない
  (VM では `okularApplication_pdf.desktop` が無い) ときは、次の候補に落ちる。
  `xdg-mime` の実際の振る舞いに合わせる。
- 確かめ: 一覧の組み合わせを再現する試験 (既存の linux_mime の試験の形に合わせる)。
  PNG が Gwenview、PDF は Okular の .desktop が無ければ次の候補になること。

### B. Firefox の窓が崩れる (native/wlr/)

- 症状 (ユーザの画面写し、手元の Windows の `Downloads` にある
  `VirtualBox_kubuntu_29_10_2026_15_03_11.png`。WSL からは
  `/mnt/c/Users/<user>/Downloads/` で読める。リポジトリに入れない — 中に個人の
  パスが写っている): File Manager から PDF を「Linux のアプリで開く」で Firefox
  (snap) が開いたとき、Firefox の中身が代役の枠より大きく描かれ、代役の題の帯は
  短い幅のまま、はみ出た右側が後ろの Konsole の代役の穴から見えた。
- 調べること: Firefox が頼んだ大きさに従わない (セッションの復元などで自分の
  大きさで commit する) ときの、代役の大きさ・切り抜き (`wlr_scene_subsurface_tree_set_clip`
  の当て方と、いつ当て直すか)・題の帯の描き直し。手元のコンポジタ
  (`rake native:wl_run` などの入れ子) で、大きさに従わない窓を作って再現する
  (Firefox が無ければ、同じ振る舞いの小さな試しのクライアントでよい)。
- 直したあと: 窓の中身が代役の枠の外に出ないこと (切り抜き)、代役の大きさと
  題の帯が窓の実際の大きさに追従すること (どちらに合わせるかは見て決め、report に
  理由)。wl の確認の一式に 1 つ足す。

### C. 起こし直しの記録を残す (native/package/family-mruby-session)

- 症状: `pkill -SEGV` 1 回でログイン画面に戻ったことが 1 度あった (再試行では
  起き直した)。セッションの記録 (`~/.local/share/sddm/wayland-session.log`) は次の
  ログインで上書きされ、原因が追えなかった。
- 直し方: セッションの台本の自分の行 (起動・終わり方・起こし直し・諦め) と、
  起こし直しの前後の Family mruby の出力の末尾を、**次のログインで消えない
  場所** (例: `$XDG_STATE_HOME/family-mruby/session.log`、既定 `~/.local/state/...`)
  にも書く。大きさは上限を決めて回す (前の 1 つを残す程度)。
- 起こし直しの直後の失敗を見分けられる行を足す (例: 起動から何秒で終わったか)。
- 失敗の原因そのものは推測で直さない (記録が取れてから)。

### D. リサイズの印を三角に (共有のコード、全端末)

- ユーザの要望: 右下の印は四角だと目立つので、右下に寄せた直角三角形にする。
  Linux の窓の代役 (linux_window.app.rb の GRIP) だけでなく、**大きさを変え
  られる窓すべて**に出す。
- 窓の枠を描く共通の所 (picoruby-fmrb-app の `draw_window_frame` など) で、
  大きさを変えられる窓 (カーネルが右下の角で大きさの変更を受ける窓。その判定の
  正はカーネルの input_router の側) に描く。代役は自分の四角をやめて共通の印に
  寄せる。大きさ・色はテーマの色から (0x01 を使わない)。
- 全端末の見た目が変わるので、`rake regress` の描画の記録と `regress:screens` の
  基準が変わる。**基準の更新は別のコミット**にし、変化を report に書く (更新した
  基準の検収は親がする)。
- 内蔵 RAM +0 (`rake regress:size`)。

### E. Flappy の鳥の輪郭 (flash/usr/share/sprites/flappy/)

- 原因: bird_body.bmp などの輪郭の画素が 0x01 (透明色) で、どの端末でも透ける。
- 輪郭の 0x01 を、ごく暗い別の色 (例: 0x00 がスプライトの透明なら 0x24 など、
  見た目の近いもの) に置き換える。bird_*.bmp と、同じ問題のある flappy の他の
  BMP があればそれも。他のアプリの BMP に 0x01 を持つものがあれば一覧だけ
  report に書く (直すのは flappy だけ)。

### F. Linux のアプリのショートカット (デスクトップ、Linux 版だけ)

- ユーザの要望: Linux 版で Konsole・Firefox・Chrome があれば、背景 (デスクトップ)
  のショートカットのアイコンで出したい。
- 入っているものだけを出す。候補の一覧 (desktop の ID の並び。例:
  `org.kde.konsole`、`firefox` / `firefox_firefox`、`google-chrome` /
  `com.google.Chrome`) は設定で持ち、利用者が変えられる形にする (置き場所は
  desktop.toml か機種の定義、任せる)。アイコンは「Linux」の区分と同じ仕組み
  (Breeze の SVG) を使う。押すと「Linux」の区分から起動するのと同じ動き。
- Linux 版以外の端末では何も変わらないこと。
- 確かめ: wl の確認の一式に 1 つ足す (手元のコンテナにあるアプリで)。

## 確認

- `rake regress` (D の基準の変化は別コミットで)、`regress:size`、
  `regress:screens ONLY=retro,modern,native`、wl の確認の一式 (既存の全部と足した分、
  両エンジン)。
- 画面を撮るもの: D の三角 (Family mruby の窓と Linux の窓の代役)、F のアイコン。
  doc/linux_native/report/ に置いてよい (個人のパスが写らないもの)。
- 終わったら amd64 の .deb を作る (今までの段と同じやり方。共有フォルダへの写しは
  親がする)。

## 触ってよい範囲

- native/、main/prebuild_scripts/kernel/ (代役・デスクトップ・カーネル)、
  lib/add/picoruby-fmrb-app/、config/machines/、flash/usr/share/sprites/flappy/、
  flash/usr/share/desktop/、試験と回帰の道具の基準、doc/linux_native/。

## 止まる条件

- A で `XDG_CURRENT_DESKTOP` を変えないと直せないと分かったとき。
- B の直し方が「Linux の窓の大きさを Family mruby が決める」今の仕様を変える
  (例: 代役を Linux の窓に合わせて勝手に広げる) とき — 候補と振る舞いを書いて返す。
- D で、大きさを変えられる窓の判定が端末やアプリで食い違い、印の出る窓と出ない
  窓の決め方に仕様の判断が要るとき。
- 内蔵 RAM が増えるとき。見立てと違う事実が出たとき。

## 守ること

- `rake clean_all` はしない (要るなら止まって返す)。sudo しない。本当の HOME・
  /etc・/usr に書かない (試しは一時の HOME)。
- tools/fmrb_web.rb が開いているブラウザは閉じない。WSL ホストのネットワーク・
  Bluetooth・音の設定を変えない。
- コミットは `feature/linux-native` に (英文、Co-Authored-By)。push しない。文書に
  手元の絶対パスやユーザ名を書かない。
- report は `doc/linux_native/report/l15.md`: A〜F ごとの計測値・見立てと違った点・
  撤回した仮説・残件。
