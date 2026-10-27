# L11 指示書: Linux のファイルを Linux のアプリで開く

> 計画: doc/linux_native/plan.md (「Linux のプログラムとの連携」の 3、段取りの L11)。
> 前段の結果: report/l7.md (/mnt の素通し)、report/l9b.md (起動の口、`.desktop` の
> 読み方)、report/l10.md

## ユーザの決定 (2026-10-28、変えない)

- **既定の開き方**: Family mruby が開ける種類 (今の `/etc/associations.toml` に
  ある .rb・.txt・画像など) は今どおり Family mruby のアプリで開く。**それ以外の
  種類は Linux の既定のアプリ** (MIME の関連付け、xdg-mime / mimeapps.list) で開く。
- File Manager の右クリックに **「Linux のアプリで開く」** を足し、その種類を
  開ける Linux のアプリの候補から選べるようにする (Family mruby が開ける種類にも
  出す)。
- 関連付けの表 (`associations.toml`) にも「Linux のアプリ」という行き先を書ける
  ようにする (例: 特定のアプリ、または Linux の既定)。
- 権限は全部の Family mruby のアプリに許す (L7 の決定)。

## 作業の内容

- MIME の種類の判定 (shared-mime-info の glob、無ければ拡張子)、既定のアプリ
  (`mimeapps.list` の決まりの順)、その種類を開けるアプリの一覧 (`.desktop` の
  `MimeType`) を読む口を、L9b の `.desktop` の読み方の隣に足す。`Exec` の
  `%f %F %u %U` は渡すファイルで置き換える (L9b の分け方を使う)。
- 渡すパスは Family mruby の仮想の場所 (`/mnt/host/...`・`/mnt/root/...`) を
  file HAL の振り分けで実体に直して渡す。`/mnt` の外 (`/home` など Family mruby
  の場所) のファイルも、実体の場所に直せるなら渡す。直せないものは断る。
- File Manager:
  - ダブルクリック・Enter で開くとき、関連付けに無い種類は Linux の既定で開く。
    Linux の既定も無ければ、今どおり「開けない」と出す。
  - 右クリックの「Linux のアプリで開く」で、既定を先頭にした候補の一覧
    (多ければ数を絞り、残りは「その他」) を出し、選んだアプリで開く。
  - Linux のアプリを起こせない機種 (端末・sim・ブラウザ版・Cardputer など
    Linux の窓を出せない形) では項目を出さず、動きは今と同じ。
- 起こしたアプリは L9b の起動の口で起こし、終わり方 (Exit で閉じる) も同じ。
- 文字列は i18n の表に足す (en・ja)。

## 触ってよい範囲

- fmruby-core: `native/` (sys・wlr)、`main/kernel/` の `fmrb_platform_sys` の口、
  `lib/add/` の該当箇所 (native だけの部分)、`main/prebuild_scripts/kernel/`
  (File Manager・関連付け・デスクトップ)、`flash/etc/associations.toml` の説明の
  注釈 (行き先の書き方の例)、`test/`、`rakelib/`、`tools/regress/`、
  `doc/linux_native/report/`、`doc/regression/usage.md`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、plan.md。

## 作業場所と git

- 本体の checkout、fmruby-core の `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は今 NARYAv4 (ESP32-P4) のビルド。自分で `rake clean_all` はしない。
- HOME・XDG の場所は一時ディレクトリ (mimeapps.list・`.desktop` の試験の分も)。
  利用者のホームや `/usr` に書かない。WSLg に窓を出しっぱなしにしない。
- kernel・デスクトップの Ruby を変えたら、標準 (Spinel) と全部 mruby の両方で
  確かめる。

## 受け入れ条件 (`rake regress:wl` の一式に足して自動で確かめる)

1. 一時の XDG の場所に置いた mimeapps.list と `.desktop` で、`.pdf` 相当の種類の
   既定と候補が決まりどおりに読める (host 試験)。
2. コンポジタの機種で、`/mnt/host` に置いた Family mruby の知らない種類のファイルを
   File Manager で開くと、既定の Linux のアプリが実体のパスを受け取って起きる
   (試験用のアプリで受け取ったパスを確かめる)。`.rb` は今どおりエディタで開く。
3. 右クリックの「Linux のアプリで開く」で候補が出て、選んだアプリで開く。
4. `associations.toml` に Linux の行き先を書いた種類が、その行き先で開く。
5. 端末・sim・ブラウザ版では項目が出ず、`rake regress:screens` が通る。
   `rake regress`、`rake regress:size` (内蔵 RAM +0)、`rake native:test`、
   `rake regress:wl` が通る。
6. 2・3 を標準と全部 mruby の両方で。
7. 最後に amd64 の `.deb` を作る。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- 端末・sim・ブラウザ版の動きを変えないとできないとき、内蔵 RAM が増える作り
  しか無いとき。
- `rake clean_all` や sudo が要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l11.md) に書くこと

- MIME の判定・既定・候補の読み方、パスの直し方
- File Manager の開き方と右クリックの作り、関連付けの書き方
- 受け入れ条件ごとの結果と数値、VM で人が確かめる手順、残件
