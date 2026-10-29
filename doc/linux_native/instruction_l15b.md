# 実装指示書 L15b: L15 の残件 (ユーザ決定、2026-10-29)

決定の正は plan.md の「L15 の残件の決定 (2026-10-29 ユーザ)」の項。L15 の経過は
report/l15.md。

作業ブランチ: fmruby-core の `feature/linux-native` (本体 checkout、直列)。

## 作るもの

### 1. 右下の角を空ける (三角の印が隠れる窓)

- エディタ (ステータス行) と LogViewer (スクロールバー) が、枠を描いた後で右下の
  角に自分の絵を描き、三角の印を隠す。各アプリの側で、印の分 (L15 の
  `draw_resize_mark` と同じ大きさ) の角を空ける。
- LogViewer はスクロールバーの下ボタンが角と重なって押せない。角を避けた位置・
  大きさにして押せるようにする。
- 他にも角に描いて印を隠すアプリが無いか、大きさを変えられるアプリ (resizable) を
  ひと通り見て、見つけたら同じに直すか report に一覧を書く。
- 全画面のときは印も角も関係しない (今の動きを変えない)。

### 2. MicroPython のアプリにも三角の印

- MicroPython のアプリの窓の枠の描き方を、Ruby の基底と同じ印を出すように直す
  (`components/micropython/` の Family mruby 側の部分と、必要な再生成。submodule の
  本体は直接編集しない — 規約は fmruby-core/CLAUDE.md)。
- 再生成の手順・`rake micropython:smoke` を report に書く。

### 3. デスクトップのアイコンの一覧 (`/home/desktop.toml`) を apps + hidden の形に

- 形: `apps = [...]` (表示する、上から順) と `hidden = [...]` (知っているが出さない)。
- 起動時: 既定の一覧 (`/usr/share/desktop/desktop.toml`) にあって `apps` にも
  `hidden` にも無いもの (新しく増えた既定) を `apps` の末尾に足し、書き戻す。
- アイコンのメニューで外すと `hidden` へ移す。足すと `hidden` から外して `apps` へ。
- 入っていない Linux のアプリ (`linux:` の項目) は `apps` にあっても出さない
  (今のまま)。
- `hidden` の無い今の形のファイルは、最新の既定で無条件に上書きする (まだ一般公開
  していないため、ユーザ決定)。
- `/home/desktop.toml` が無い人は今までどおり既定を使う (起動しただけでは
  ファイルを作らない。メニューで変えたときに作る)。
- keys のデスクトップのアイコンの扱い (あれば) も同じにする。
- アイコンのメニューから `linux:` の項目を足せるかを確かめ、足せなければ report に
  書く (足せるようにするのはこの段の範囲外)。

## 確認

- `rake regress`、`regress:size` (内蔵 RAM +0)、`regress:screens ONLY=retro,modern,native`、
  wl の確認の一式 (両エンジン)、`rake micropython:smoke`。
- 描画の記録・画面の基準が変わるなら、基準の更新は別のコミットにし、変化を
  report に書く (親が検収する)。
- 撮って report に置くもの: エディタと LogViewer の右下の角 (印が見えること)、
  MicroPython のアプリの窓の印。
- 3 の確かめ: 試験 (既定に項目を足す・`hidden` にある項目は戻らない・`hidden` の
  無い古い形は上書きされる・ファイルが無いときは作らない)。

## 触ってよい範囲

- main/prebuild_scripts/ (エディタ・カーネルのデスクトップ)、flash/app/ (LogViewer
  など)、lib/add/picoruby-fmrb-app/、MicroPython の Family mruby 側の部分と再生成物、
  flash/usr/share/desktop/、試験・回帰の道具と基準、doc/linux_native/。

## 止まる条件

- 角を空けるとアプリの使い勝手 (表示できる行数など) が目に見えて変わるとき
  (どれだけ変わるかを書いて返す)。
- MicroPython 側で submodule の本体を変えないと出せないとき。
- 内蔵 RAM が増えるとき。見立てと違う事実が出たとき。

## 守ること

- `rake clean_all` はしない (要るなら止まって返す)。sudo しない。本当の HOME・
  /etc・/usr に書かない。
- tools/fmrb_web.rb が開いているブラウザは閉じない (`--port 8016`)。
- コミットは `feature/linux-native` に (英文、Co-Authored-By)。push しない。文書に
  手元の絶対パスやユーザ名を書かない。
- 終わったら amd64 の .deb を作る (共有フォルダへの写しは親)。
- report は `doc/linux_native/report/l15b.md`。
