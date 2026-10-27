# L7 指示書: Linux PC の入口と Linux のファイル (と、音源の二重解放の直し)

> 計画: doc/linux_native/plan.md (段取りの L7、「ファイルの置き場所」の
> Linux のファイルを見せる、「Linux のプログラムとの連携」の権限)。
> 前段の結果: report/l6.md (残件の二重解放を含む)

## 0. 先に: 音源の二重解放 (L6 の残件、ユーザの指示で直す)

FMSQ の曲を読むアプリ (rpg_demo・blockgame・breakout) を開くと、native が
`double free or corruption` で落ちる。`components/apu_emu/src/apu_if_p4.c` の
`apuemu_free()` が `free()` を呼ぶが、確保は hosted の `heap_caps_malloc`
(見出しを付けた番地を返す) で、違う番地が `free()` に渡るため (report/l6.md)。

- 確保と解放の関数を揃える (`heap_caps_free` など)。同じ食い違いが apu_emu の
  ほかの所やブラウザ版の stub に無いか読んで確かめ、あれば同じく直す。
- 端末の動きは変えない (ESP-IDF では `free` と `heap_caps_free` は同じ解放)。
- 確認: native (標準と全部 mruby) で 3 つのアプリを開いて閉じても落ちない。
  ブラウザ版でも 1 つ開いて閉じる (`web_*` の道具か `ruby tools/fmrb_web.rb`)。
  この直しは別のコミットにする。

## ユーザの決定 (2026-10-27、変えない)

- **Linux のファイルの見せ方**: `/mnt/host` に利用者のホーム (読み書き)、
  `/mnt/root` に Linux の `/` 全体 (読むだけ)。
- **Cardputer (1)(2) も PC と同じ**にする (root で動くので、ホームは `/root`)。
- **権限**: Linux のプログラムの起動・Linux のコマンド・Linux のファイルは、
  **全部の Family mruby のアプリに許す** (アプリごとの許可の仕組みは作らない)。

## 1. Linux のファイルを見せる (native だけ)

- file HAL の振り分けの表 (`fmrb_hal_file_layout.h`) に「Linux の実ファイルへ
  素通しする」種類を足し、`/mnt/host` → `$HOME`、`/mnt/root` → `/` を置く。
  場所ごとの `if` を散らさない (表の 1 行で表す)。sim・ブラウザ版の表は変えない。
- **読むだけの保証**: `/mnt/root` への書き込み・消す・名前を変える・
  ディレクトリを作るは `FMRB_ERR_READ_ONLY`。Cardputer では root で動くので、
  OS の権限に頼らず Family mruby の側で止める。
- **ホームから外へ書けないこと**: `/mnt/host` の下の書き込みは、実際の場所
  (`realpath`、まだ無いファイルは親) がホームの中にあることを確かめ、外を
  指すシンボリックリンク経由の書き込みは断る (`..` の正規化も含む)。読むのは
  リンクの先が外でもよい (`/` は読めるので)。
- 普通のファイルとディレクトリ以外 (装置・FIFO・ソケット) は開かない
  (一覧に出すかは読みやすい方を選び、report に書く)。`/proc` や `/sys` の
  大きさの合わないファイルで File Manager が固まらないこと。
- 見せるかどうかと場所は機種の定義か system_conf で変えられるようにし、
  既定は上の決定のとおり (全部の機種で有効)。
- File Manager (mouse・keys) の一番上に `/mnt/host` と `/mnt/root` が見え、
  開いて辿れる。エディタで `/mnt/host` のファイルを開いて保存できる。
- 開発中 (WSL2) の `$HOME` は本当の利用者のホームになる。**試験で利用者の
  ホームに書かない**: 試験は `HOME` を一時ディレクトリに差し替えて行う。

## 2. Linux PC のログイン画面の入口 (cage)

- 計画どおり、まず **cage の中で SDL の全画面**で動かす形にする。
  - 新しい機種の定義 `linux_pc` (SDL・全画面・拡大は auto・mouse・
    `system.power = "logind"`・音は SDL か ALSA の扱いを決めて report に)。
  - ログイン画面の項目: `/usr/share/wayland-sessions/family-mruby.desktop`。
    `Exec` は小さな起動用の実行ファイル (例 `family-mruby-session`) で、
    中で `cage` の下に `family-mruby --machine linux_pc` を起こす。
    SDL が Wayland で動くよう環境を整える (`SDL_VIDEODRIVER=wayland` など)。
  - amd64 の `.deb` だけに入れる。`cage` は `Recommends` (無ければ項目は
    出しても起動時に分かるエラー)。arm64 には入れない。
  - Family mruby の電源の項目は、`logind` の 3 つに加えて「終了 (ログイン画面に
    戻る)」も出す (`exit` と `logind` を両方出す形を機種の定義で表せるように)。
- 画面の大きさ: 出力の大きさに整数倍で収め、余りは黒 (L2 の共通の部品)。
  縦長の液晶 (Chuwi MiniBook X は液晶が縦向きに付いている) のため、cage の
  回転か Family mruby の `display.rotate` のどちらで回すかを決め、
  設定の書き方を report に書く。
- `linux_pc_drm` (cage を使わず DRM に直接) も残す。こちらは VM と実機で
  ユーザが試す (WSL2 では断る)。

### WSL2 での確認の仕方

WSL2 には WSLg の Wayland (`WAYLAND_DISPLAY=wayland-0`) がある。cage は
入っていない (入れるには sudo が要る)。

- まず `cage` が入っているか確かめる。入っていなければ**止まらずに**、
  cage 無しで確かめられる部分 (`SDL_VIDEODRIVER=wayland` で WSLg の上に
  `--machine linux_pc` の全画面が出る、session の `.desktop` と起動用の
  実行ファイルの中身、`.deb` の中身) を進め、cage の中で動かす確認は
  「ユーザが `sudo apt install cage` した後に親が行う」と report に書く。
- cage があれば、WSLg の上で入れ子 (`WLR_BACKENDS=wayland cage -- ...`) で
  起こし、画面を取って確かめる。

## 触ってよい範囲

- fmruby-core: `components/apu_emu/` (0 の直しだけ)、ブラウザ版の該当の stub
  (0 の直しだけ)、`components/fmrb_hal/platform/posix/` (振り分けの表)、
  `main/prebuild_scripts/kernel/` (File Manager の表示に要るなら)、`native/`、
  `hosted/`、`config/`、`rakelib/`、`test/`、`doc/linux_native/report/`。
- family-mruby (ルート): `tools/`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/` の
  中身、plan.md、端末・sim・ブラウザ版の動き (0 の直しを除く)。

## 作業場所と git

- 本体の checkout、両リポジトリとも `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は ESP32-P4 のビルド。自分で `rake clean_all` はしない。
- sudo は使わない。利用者のホームに試験のファイルを書かない。

## 受け入れ条件

1. 0 の直し: native の標準と全部 mruby で FMSQ のアプリ 3 つを開いて閉じても
   落ちない。ブラウザ版でも 1 つ。
2. host 試験 (一時ディレクトリをホームと `/` に見立てる): `/mnt/host` の読み書き、
   `/mnt/root` の書き込みが `FMRB_ERR_READ_ONLY`、ホームの外を指すリンク経由の
   書き込みが断られる、`..` で外に出られない、装置のファイルを開かない。
3. native (`--machine sdl`、`HOME` を一時ディレクトリに) で、File Manager に
   `/mnt/host`・`/mnt/root` が見え、エディタで `/mnt/host` のファイルを開いて
   保存できる。keys (`sdl_keys`) の File Manager でも辿れる。
4. `--machine linux_pc` が WSLg の Wayland の上で全画面に出る (cage があれば
   cage の中で)。電源の項目に「終了」と logind の 3 つが出る (ログに出すだけの
   形で確かめる)。
5. amd64 の `.deb` に session の `.desktop` と起動用の実行ファイルが入り、
   arm64 には入らない。deb_check (arm64・amd64) が通る。
6. 回帰: `rake regress`、`rake regress:screens`、`rake regress:live` が通る。
   apu_emu を触るので `rake regress:size` で内蔵 RAM が増えていない。

## 止まる条件

- 上の決定と食い違う作りが要るとき。
- 端末・sim・ブラウザ版の動きが変わる直し方が要るとき (0 の直しで、解放の
  関数を揃える以外の変更が要る場合を含む)。
- `rake clean_all` や sudo が要るとき (cage が無いことは止まる理由にしない。上の
  とおり進める)。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l7.md) に書くこと

- 0 の直しの中身と、ほかに見つけた食い違い
- 振り分けの表に足した種類と、読むだけ・外へ書けないことの作り
- 特殊なファイルの扱い、設定の項目名
- session の入口の中身 (起動用の実行ファイル・環境変数)、`linux_pc` の定義、
  回転の決め方、音の扱い
- 受け入れ条件ごとの結果と、確かめたコマンド・数値
- ユーザが VM (Kubuntu) で試す手順 (ログイン画面で選ぶところから)
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
