# L3a 指示書: aarch64 のビルドと .deb (WSL2 の中で)

> 計画: doc/linux_native/plan.md。前段の結果: report/l1.md、l2.md、l2n.md

## 目的

native を、配る形にする。Cardputer Zero (Raspberry Pi OS trixie、arm64) 向けと
Linux PC 向け (x86_64) の `.deb` を作り、**WSL2 の中の arm64 のコンテナ
(qemu)** で入れて起動するところまで確かめる。実機での確認は L3b。

手元の WSL2 では、docker で `--platform linux/arm64` の Debian trixie が動く
(qemu の binfmt が登録済み)。

## 作業の内容

### 1. ビルドの環境 (コンテナ)

- Debian trixie (amd64) のコンテナで、`crossbuild-essential-arm64` と arm64 の
  開発用ライブラリ (SDL2・libdrm・ALSA・OpenSSL) を multiarch で入れ、aarch64 を
  クロスビルドする。x86_64 の配布用も同じイメージで作る (配布物の glibc を
  trixie に揃える。手元の Ubuntu 22.04 でのビルドは開発用として残す)。
- Dockerfile は `native/docker/` に置き、版は固定する (イメージの日付か
  digest。`latest` にしない。docker-compose.yml の冒頭の方針と同じ)。
- libmruby の aarch64 版: picoruby の build_config に aarch64 のクロスの
  設定を足す (`lib/add/family_mruby_native.rb` の延長でよい)。**MRB_* の ABI の
  定義を揃える**決まりは同じ。mrbc などのホストの道具は x86_64 のまま。
  Ruby のバイトコードと Spinel の生成物は機種に依存しないので、そのまま使う。
- rake のタスク (例: `native:image`、`native:cross ARCH=arm64|amd64`、
  `native:deb ARCH=...`)。
- 手元の docker のイメージを作る・取ってくるのは、sudo が要らない範囲でよい。

### 2. パッケージの形

| | arm64 (Cardputer Zero) | amd64 (Linux PC) |
|---|---|---|
| 実行ファイル | `/usr/share/APPLaunch/bin/family-mruby` | `/usr/bin/family-mruby` |
| 配布物の木 | `/usr/share/family-mruby/root/` (両方同じ) | 同左 |
| 機種の定義 | 配布物の木の `machines/` (両方同じ) | 同左 |
| 起動の入口 | `/usr/share/APPLaunch/applications/` の `.desktop` | (L7 でログイン画面の項目を足す。この段は無し) |
| アイコン | `/usr/share/APPLaunch/share/images/family-mruby.png` | (L7) |

- 配布物の木は、`flash/` の **git で追跡しているものだけ** から作る
  (`flash_local/` は入れない。ブラウザ版の `wasm:webflash` と同じ考え方で、
  `wasm:scan` 相当の「認証情報・追跡外のファイルが入っていない」確認を通す)。
  `/home` は空で出荷する。
- 実行ファイルは、インストールされた場所から配布物の木と機種の定義を
  見つける (開発中の `flash/` の場所と取り違えない)。利用者の場所は
  XDG の決まり (`$XDG_DATA_HOME/family-mruby/`、`$XDG_CACHE_HOME/family-mruby/`)。
  APPLaunch は root で動くので、Cardputer では `/root/.local/share/family-mruby/`
  になる。開発中の `family-mruby-dev` とは名前を分ける。
- `.desktop` は、APPLaunch の決まり (`Exec` に shell のメタ文字を入れない、
  `/` を含むなら実行できるファイルを指す、`Terminal=false`) に合わせる。引数を
  渡せるか分からないので、**機種ごとの小さな起動用の実行ファイル**
  (例 `family-mruby-hdmi`、中で `--machine cardputer_zero_hdmi` を付けて
  本体を exec する) を置き、`Exec` はそれを指す。この段で置く入口は HDMI 用の
  1 つだけ (本体の画面 320x170 は L4・L5 で足す)。
- アイコン: 既存の資産 (`flash/usr/share/icon/` の BMP など) から、1:1 の PNG を
  作る (画像の生成は Python のままでよい決まり。既存の道具があれば使う)。
  App Store の決まりは「1:1 の PNG、アプリに固有」。
- `DEBIAN/control` の `Architecture`・`Depends` (libsdl2・libdrm・libasound2・
  libssl の trixie の版) を正しく書く。`Maintainer` に個人のメールを書かない
  (公開物に実在の値を書かない決まり)。

### 3. `/etc` の更新の規則

L2n で、使い込んだ利用者の場所の `/etc` が古いまま (新しい services.toml の
項目や `zenoh_router` が入らない) という問題が出た。plan.md の決まりどおり、
起動時に **利用者の `/etc` に無い項目だけを、配布物の `/etc` から足す**。

- system_conf.toml: 利用者の側に無いキーだけ足す。利用者が変えた値は
  変えない。
- services.toml: 利用者の側に無いサービスの節だけ足す (`enable` などの
  利用者の値は変えない)。
- 他の `/etc` のファイル: 利用者の側に無いファイルだけ写す。
- 足した項目はログに出す。TOML の読み書きは既存の読み方に合わせ、
  コメントや並びを壊さない書き方を選ぶ (追記で済むなら追記)。
- この規則は native だけ (端末・sim・ブラウザ版は変えない)。

### 4. 確認

- arm64 の Debian trixie のコンテナ (`--platform linux/arm64`) に `.deb` を
  `apt install ./family-mruby_*.deb` で入れ、依存が解決されること。
- そのコンテナの中で、root で `--machine sdl` をヘッドレス (SDL dummy) で
  起動し、デスクトップまで上がること。画面の取得 (L1 の道具か、コンテナ内で
  取って外に出す) で 426x240 の PNG を取り、x86_64 の native の基準
  (`test/regress/screens/baseline/native/desktop.png`) と時計を除いて比べる。
- エディタの 1 打鍵 (標準の構成)。qemu で遅いので待ち時間は長めでよい。
- amd64 の `.deb` も、素の trixie (amd64) のコンテナで同じことを確かめる。
- `/etc` の更新の規則: 古い `/etc` を用意して起動し、無い項目だけが足され、
  利用者の値が残ること (host 試験にできるならする)。
- `rake regress` と `rake regress:screens` が通る (native の手元のビルドを
  壊していないこと)。

## 触ってよい範囲

- fmruby-core: `native/` (docker・パッケージの道具を含む)、`hosted/` (必要なら)、
  `lib/add/` (native の build_config。lib/ の決まりに注意)、`config/`、
  `rakelib/native.rake`、`test/`、`doc/linux_native/report/`。
- family-mruby (ルート): `tools/` (確認の道具に要るもの)。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/` の
  中身、`doc/linux_native/plan.md`、端末・sim・ブラウザ版の動き。

## 作業場所と git

- 本体の checkout、両リポジトリとも `feature/linux-native` に続けてコミットする
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
- develop へのマージと push はしない。docker のイメージをどこかへ push しない。
- `build/` は ESP32-P4 のビルド。自分で `rake clean_all` はしない。
- 作った `.deb` やイメージの大きさを report に書く。大きな生成物は git に
  入れない (`.gitignore` を確かめる)。

## 受け入れ条件

1. `rake native:deb ARCH=arm64` と `ARCH=amd64` (名前は任意) で `.deb` ができる。
2. arm64 の trixie のコンテナに入れて、依存が解決され、root でデスクトップまで
   上がり、画面が x86_64 の基準と (時計を除いて) 同じ。エディタの 1 打鍵が出る。
3. amd64 の素の trixie のコンテナでも同じ。
4. インストールされた形で、配布物の木 (`/usr/share/family-mruby/root/`) に
   書き込まず、利用者の物は root のホームの下の XDG の場所に入る。
5. 配布物の木に、git で追跡していないファイルと認証情報が入っていない
   (確認の道具の結果)。
6. `/etc` の更新の規則が働き、利用者の値を変えない。
7. `rake regress` と `rake regress:screens` が通る。

## 止まる条件

- `sudo` が要るとき (docker は sudo なしで使える前提)。
- arm64 の trixie に要る版のパッケージが無いとき。
- qemu の上で起動が成り立たない事実が出たとき (極端に遅い、特定の命令で
  落ちる)。その場合は、何が起きたかを記録して返す。
- 端末・sim・ブラウザ版の動き (仕様) が変わる直し方を選ぶ必要が出たとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l3a.md) に書くこと

- ビルドの環境 (イメージの版・作り方・かかった時間)
- パッケージの中身の一覧と大きさ、依存
- 実行ファイルが場所を見つける仕組み (開発中とインストール後の違い)
- `/etc` の更新の規則の実装と試験
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (qemu での起動の時間)
- L3b (実機) で確かめること: APPLaunch からの起動、`.desktop` の引数、
  root のホーム、DRM・evdev・ALSA (L2 の一覧と合わせる)
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
