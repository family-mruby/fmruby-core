# 実装指示書 S1: 状態を覚える共通の API と File Manager

計画は doc/app_state/plan.md (決定事項が正)。

作業ブランチ: fmruby-core の `feature/linux-native` (本体 checkout、直列)。

## 作るもの

1. **共通の API** (picoruby-fmrb-app の基底、mruby と Spinel の両方):
   - アプリが自分の状態の表を読む・書く口。形は既存の基底の書き方に合わせて
     決める (例: `app_state` が Hash を返し、`save_app_state` で書く。閉じるとき
     変わっていれば自動で書く、など)。名前と形は report に理由を書く。
   - Spinel の型の制約 (doc/spinel_aot、memory の地雷は fmrb-app-new skill) に
     合う形にする。値は文字列・整数・真偽に限ってよい。
   - TOML の読み書きは既存の道具 (他のアプリが /home/*.toml を読み書きしている
     やり方) を使う。
2. **置き場所**: どの端末でも `/tmp/<app_handle_name>.state.toml` (ユーザ決定、
   2026-10-29。端末ごとの切り分けはしない)。/tmp はディレクトリを作れない (fmrb_tmpfs.h の不変条件) ので平らな名前にする (2026-10-29 ユーザ決定)。File Manager の名前は固定の `file_manager` (keys のデスクトップと共有、ユーザ決定)。各端末で
   どれだけ残るか (ブラウザの再読み込み・ネイティブ版の再起動で消えるか) を
   report に書く。
3. **File Manager** (main/prebuild_scripts/kernel/system_desktop/file_manager.rb ほか):
   - 閉じるときに今のフォルダを覚え、次に開くときそこから始める。
   - 覚えたフォルダが無い・読めないときは今の初期フォルダ。
   - keys のデスクトップ (system_desktop_keys) の File Manager も同じ扱いにできる
     なら同じにする (できなければ report に)。

## 確認

- `rake regress`、`rake regress:size` (内蔵 RAM +0)、`regress:screens ONLY=retro,modern,native`、
  Linux 版の wl の確認の一式のうち File Manager に関係するもの。
- 受け入れ条件 (plan.md) を、sim・ネイティブ版・ブラウザ版で確かめる。ブラウザ版は
  tools/fmrb_web.rb で (開いているブラウザは閉じない。`--port 8016`)。
  ブラウザ版の wasm はこのブランチで作り直す (wasm の文書に従う。`rake clean_all`
  はしない)。
- 実機は動かさない。実機の経路 (/tmp) はコードの経路で示す。

## 触ってよい範囲

- lib/add/picoruby-fmrb-app/、main/prebuild_scripts/ (spinel の基底・カーネルの
  デスクトップ)、sig/ (型を足すなら。直したら `rake clean`)、試験・回帰の道具、
  doc/app_state/。

## 止まる条件

- 新しい C の関数が要るとき。
- Spinel で共通の形が作れず、アプリごとの書き方が分かれるとき。
- 内蔵 RAM が増えるとき。見立てと違う事実が出たとき。

## 守ること

- `rake clean_all` はしない (要るなら止まって返す)。sudo しない。本当の HOME・
  /etc・/usr に書かない。
- tools/fmrb_web.rb が開いているブラウザは閉じない。
- コミットは `feature/linux-native` に (英文、Co-Authored-By)。push しない。文書に
  手元の絶対パスやユーザ名を書かない。
- report は `doc/app_state/report/s1.md`: API の形と理由、置き場所の見分け方、
  計測値、見立てと違った点、残件。
