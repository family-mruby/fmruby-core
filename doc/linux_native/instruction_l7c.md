# L7c 指示書: File Manager の削除の前に確認を出す

> 計画: doc/linux_native/plan.md (段取りの L7c)。前段の結果: report/l7b.md
> (確認のダイアログが Enter = はい、Esc = いいえ に答えるようになった)

## ユーザの決定 (2026-10-27、変えない)

- File Manager の削除 (右クリックの Delete と Delete キー) の前に、
  「<名前> を削除しますか?」の確認を出す。はい (または Enter) で消し、
  いいえ (または Esc) で何もしない。
- 全部の端末で同じにする (mouse のデスクトップも keys のデスクトップも)。
- 理由: `/mnt/host` で利用者の本物のファイルが見えるようになり、誤って消す
  危険が増えた。

## 作業の内容

- 今の削除は `system_desktop/file_manager.rb` の `fmgr_delete_file` が
  その場で消す。これを「確認を開く」と「答えが はい なら消す」に分ける。
- mouse のデスクトップは既存の確認のダイアログ (`confirm_dialog.rb`) を使う。
  今のダイアログは はい のとき kernel へ命令を送る作りなので、デスクトップの
  中で済む処理 (削除) を はい に結び付ける形を足す。既存の使い方 (電源・
  Reset・再読み込み) の動きは変えない。
- keys のデスクトップは `FileManagerMixin` を共有しているが、確認の
  ダイアログを持っていない。320x170 で読める確認の出し方 (確認の
  ダイアログの mixin を keys でも使えるようにする、など) を作りを読んで
  選び、report に理由を書く。キーだけで答えられること (Enter・Esc)。
  Esc の長押し (アプリを閉じてメニューへ) とぶつからないことを確かめる。
- 消す対象がディレクトリのときも、今の動き (消せる・消せない) は変えず、
  確認だけ足す。
- 名前が長いときは、今のダイアログの切り詰めの決まりに従う。
- 文字列は i18n の表に足す (en・ja)。

## 触ってよい範囲

- fmruby-core: `main/prebuild_scripts/kernel/system_desktop.app.rb`、
  `main/prebuild_scripts/kernel/system_desktop_keys.app.rb`、
  `main/prebuild_scripts/kernel/system_desktop/`、`test/`、
  `doc/linux_native/report/`。
- 触らないもの: kernel、C の側、fmruby-graphics-audio、sdkconfig、
  submodule の中、`flash/`、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
  `rake regress:size` は道具が build/ を退避して戻すので使ってよい。
- native を試すときは HOME を一時ディレクトリにし、利用者のホームに
  書かない。電源は `system.power_log_only=true`。

## 受け入れ条件

1. native の `--machine sdl` で、一時の HOME の `/mnt/host` にファイルを置き、
   File Manager の右クリックの Delete と Delete キーの両方で確認が出る。
   Esc と いいえ では消えず、Enter と はい では消える。
2. `--machine sdl_keys` で、Delete キーで確認が出て、Enter で消え、Esc で
   消えない。Esc の長押しの動きは今どおり。
3. 1・2 を標準と全部 mruby の両方で。
4. `/mnt/root` の下の削除は、確認の後に今どおり読み出し専用のエラーになる
   (確認の前に断るほうが分かりやすければそれでもよい。report に書く)。
5. `rake regress`、`rake regress:screens` が通る。`rake regress:size` で
   内蔵 RAM が増えていない。画面の基準が変わったら、変化を report に書き、
   基準の更新は別のコミットにする。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- kernel や C の側を変えないとできないとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l7c.md) に書くこと

- 確認と削除の結び付け方、keys での確認の出し方とその理由
- 受け入れ条件ごとの結果と、確かめたコマンド、内蔵 RAM・flash の差
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
