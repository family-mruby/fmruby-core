# 実装指示書 L16: 行数の多い Linux のファイルを分ける

VM の確認 (2026-10-29) でユーザが挙げた「行数の多い Linux のファイルの分割」を、
L15 の後にやる (ユーザ決定)。動きは一切変えない、ファイルの切り分けだけの段。

作業ブランチ: fmruby-core の `feature/linux-native` (本体 checkout、直列)。

## 分けるもの

| ファイル | 行数 (29d30a20) | 今ある節 (`/* ---- ... */`) |
|---|---|---|
| `native/wlr/output_wlr.c` | 3511 | state・xdg と X11 共通・the picture・focus・windows・X11 windows・keyboard・pointer・output・screen lock・tools' devices・autostart・output interface |
| `native/sys/linux_apps.c` | 2110 | helpers・Exec field・icons・icon themes・Qt's look・reading the entries・interface・opening files・starting and ending |

- 今の節の区切りを手がかりに、意味のまとまりごとのファイルに分ける。目安は
  1 ファイル 1000 行以下 (超える所は理由を report に)。分け方・ファイル名は任せる
  (既存の `wl_clip.c`・`wl_ime.c`・`linux_mime.c` などの名付けにそろえる)。
- ファイルの間で共有する状態・関数は、外に出さない内部のヘッダ (例:
  `native/wlr/output_wlr_internal.h`) にまとめる。今 `static` のものは、使う
  ファイルが 1 つなら `static` のまま。外 (他の部品) から見える口
  (`output_wlr` の公開の関数、`linux_apps.h`) は変えない。
- 中身は移すだけ。名前の変更・共通化・直しは混ぜない (移しながら気づいた不具合・
  整理の候補は report に書くだけ)。コメントは関数と一緒に移す。
- ビルドの一覧を追従させる: `native/CMakeLists.txt` (ソースと
  `set_source_files_properties` のコンパイル オプション)、`rakelib/native.rake` の
  ホスト試験 (linux_apps の試験は `linux_apps.c` を直接並べている)、他に名前で
  参照している所 (doc・コメント・.deb の作り) を grep で全部。
- 他に 1500 行を超える `native/` の C ファイルがあれば、一覧を report に書く
  (分けるのはこの 2 つだけ)。

## 確認

- `rake regress`。
- `rake regress:wl` (両エンジン) — コンポジタの一式。
- `regress:screens ONLY=native`、linux_apps の試験 (`rake native:` のホスト試験)。
- 内蔵 RAM・ESP32 のファームには関係しない (native だけ) ので `regress:size` は不要。
- 警告が増えていないこと (分ける前後でビルドの警告の数を比べ、report に書く)。
- `git diff --stat` で、消した行と足した行がほぼ釣り合うこと (内部のヘッダと
  include の分だけ増える)。差を report に書く。
- 終わったら amd64 の .deb を作る (共有フォルダへの写しは親)。

## 触ってよい範囲

- `native/wlr/`、`native/sys/`、`native/CMakeLists.txt`、`rakelib/native.rake`、
  ファイル名を書いている doc とコメント、`doc/linux_native/`。

## 止まる条件

- 分けるには関数の形や公開の口を変えないといけないと分かったとき。
- 試験・確認のどれかが分ける前から落ちていたとき (分ける前の結果を report に書いて返す)。
- 見立てと違う事実が出たとき。

## 守ること

- `rake clean_all` はしない (要るなら止まって返す)。sudo しない。本当の HOME・
  /etc・/usr に書かない。
- tools/fmrb_web.rb が開いているブラウザは閉じない (`--port 8016`)。
- コミットは `feature/linux-native` に (英文、Co-Authored-By)。push しない。文書に
  手元の絶対パスやユーザ名を書かない。コミットは output_wlr と linux_apps で分ける。
- report は `doc/linux_native/report/l16.md`: 分け方と理由、各ファイルの行数、警告の
  数、diff の釣り合い、気づいた点 (直さなかった候補)、残件。
