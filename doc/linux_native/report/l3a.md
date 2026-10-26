# L3a 報告: aarch64 のビルドと .deb (WSL2 の中で)

> 状態: 完了 (受け入れ 7 点) | 更新: 2026-10-26 | 指示書は instruction_l3a.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)。ルートのリポジトリは変更なし

## ビルドの環境

| 項目 | 内容 |
|---|---|
| Dockerfile | `native/docker/Dockerfile`。`FROM debian:trixie-20261020` (日付つきの tag。amd64 の digest は `sha256:15a71cc8…`、arm64 は `sha256:0daf3052…`) |
| 入れたもの | build-essential・cmake・pkg-config・git・ruby・rake・bison・dpkg-dev・fakeroot・`crossbuild-essential-arm64`、SDL2・libdrm・ALSA・OpenSSL の開発用を amd64 と arm64 の両方 (`dpkg --add-architecture arm64` の multiarch) |
| 版 | gcc 14.2.0 (Debian 14.2.0-19、cross も同じ)、glibc 2.41、cmake 3.31.6、ruby 3.3.8、SDL2 2.32.4、libdrm 2.4.124、ALSA 1.2.14、OpenSSL 3.5.7 |
| イメージ | `fmrb-native-build:trixie-20261020-40492cf3` (base の日付と Dockerfile の hash の頭 8 桁。Dockerfile を変えると別の tag になり、古いものを黙って使わない)。2.06 GB。手元だけで、どこにも push しない |
| 作る時間 | 1 分 27 秒 (初回、base の取得を含む) |
| CMake の cross の設定 | `native/docker/aarch64-linux-gnu.cmake` (`aarch64-linux-gnu-gcc`、pkg-config は `/usr/lib/aarch64-linux-gnu/pkgconfig` だけを見る) |

使い方 (fmruby-core で。`spinel:gen` の生成物と、ホストの mrbc で作るバイトコードが前提。どちらも機種に依存しないのでそのまま使う):

```
rake native:image               # イメージ (無ければ作る)
rake native:cross ARCH=arm64    # libmruby と本体 (arm64 は cross、amd64 はイメージの gcc)
rake native:root                # 配布物の木 (native/build-deb/root)
rake native:deb ARCH=arm64      # .deb を作り、続けて native:scan
rake native:scan ARCH=arm64     # 配布物の確認だけ
```

- リポジトリを `/src` にマウントし、呼んだ利用者の uid で動かす (checkout に root の
  ファイルを作らない)。出力は全部 `native/build-deb/` (git 管理外。`.gitignore` に足した)。
- libmruby の aarch64 版: `lib/add/family_mruby_native.rb` に `FMRB_NATIVE_ARCH`
  (`arm64` / `amd64`、無ければ従来どおり) を足し、arm64 では `aarch64-linux-gnu-`
  の gcc・ar を使う。変わるのは target のコンパイラだけで、MRB_* の定義は同じ 1 枚の
  ファイルから来る (ABI の決まりは自動的に揃う)。ホストの道具 (mrbc) は x86_64 のまま。
- picoruby のビルドの木は `MRUBY_BUILD_DIR=native/build-deb/<arch>/mruby` に分けた。
  開発用の `components/picoruby-esp32/picoruby/build/` (ホストの Ubuntu の gcc で作る)
  と混ざらず、`rake clean_all` にも消されない。CMake には `FMRB_HOSTED_MRUBY_DIR` で
  渡す (hosted.cmake は未指定なら従来どおり)。
- かかった時間 (build-deb を消した状態から、イメージは作成済み): `native:deb ARCH=arm64`
  1 分 35 秒、`ARCH=amd64` 1 分 10 秒。どちらも picoruby のビルドが約 1 分。

## パッケージ

| | arm64 | amd64 |
|---|---|---|
| ファイル | `family-mruby_<版>_arm64.deb` | `family-mruby_<版>_amd64.deb` |
| 大きさ | 3.19 MB (xz)、入れると 12.0 MB | 3.36 MB、入れると 12.2 MB |
| 本体 | `/usr/share/APPLaunch/bin/family-mruby` (strip 後 6.4 MB) | `/usr/bin/family-mruby` (6.6 MB) |
| 起動用 | `/usr/share/APPLaunch/bin/family-mruby-hdmi` (66 KB) | なし |
| 入口 | `/usr/share/APPLaunch/applications/family-mruby-hdmi.desktop` | なし (L7) |
| アイコン | `/usr/share/APPLaunch/share/images/family-mruby.png` | なし (L7) |
| 配布物の木 | `/usr/share/family-mruby/root/` (578 ファイル、5.9 MB) | 同じ |

- 版は `git describe --tags --long` から作る (`2.1.0+git<数>.<hash>`。tag ちょうどなら
  `2.1.0`)。
- Depends は `dpkg-shlibdeps` がイメージの trixie のパッケージから出したもの:
  `libasound2t64 (>= 1.0.16), libc6 (>= 2.38), libdrm2 (>= 2.4.99), libgcc-s1 (>= 3.0), libsdl2-2.0-0 (>= 2.0.12), libssl3t64 (>= 3.0.0), libstdc++6 (>= 5)`
  (amd64 は libgcc-s1 が `>= 12`)。手で書かないので、ライブラリを足せば追従する。
- Maintainer は `Family mruby developers <noreply@family-mruby.invalid>` (実在しない
  宛先。個人のメールは書かない)。`FMRB_DEB_MAINTAINER` で変えられる。Homepage は
  公開している organization の URL。
- `.desktop`:

  ```
  [Desktop Entry]
  Type=Application
  Name=Family mruby (HDMI)
  Comment=Family mruby on the cardputer_zero_hdmi machine definition
  Exec=/usr/share/APPLaunch/bin/family-mruby-hdmi
  Icon=/usr/share/APPLaunch/share/images/family-mruby.png
  Terminal=false
  Categories=Development;Education;
  ```

  `Exec` は実行できるファイルの絶対パスだけ (引数・shell のメタ文字なし)。
- 起動用の実行ファイル (`native/package/launcher.c`): 自分の場所 (`/proc/self/exe`) の
  隣の `family-mruby` を `--machine cardputer_zero_hdmi` と受け取った引数で `execv`
  する。`exec` なので APPLaunch から見た signal と終了コードは本体のもの。機種は
  `native.rake` の表 (`NATIVE_DEB_LAYOUT`) に 1 行足せば増える (CMake の
  `NATIVE_LAUNCHERS` が `family-mruby-<名前>` を作る)。本体の画面 (320x170) の入口は
  L4・L5 で足す。
- アイコン: `flash/boot/boot.png` (200x200 の起動の絵。ロゴと「Family mruby」の文字)
  をそのまま写した。既に 1:1 の PNG で、アプリに固有なので変換は要らなかった。
  `flash/usr/share/icon/` の BMP は 24x24 で、ランチャーのアイコンには小さい。
  App Store が求める大きさがあれば L5 で作り直す。

### 配布物の木 (`native:root`)

- `git ls-files flash` の物だけを写す (ブラウザ版の `wasm:webflash` と同じ考え方)。
  `flash_local/` は入れない。除くもの:
  - NSF は `usr/share/sounds/nsf/` の下のものだけ (ブラウザ版と同じ権利の決まり)。
  - native の振り分けで利用者・キャッシュの側になる場所 (`etc` `home` `app/usr`
    `var` `cache` `data` `tmp`)。配布物の側にあっても読まれないので入れない
    (`app/usr/.keep`、`var/cache/.keep`、`etc/hid_devices.toml` の 3 つが該当)。
- 足す生成物: `etc/` と `machines/` (`native:etc` が config/ から作る物。最初の起動の
  /etc の種で、`/etc` の更新の元)、`help/` (`ti:help` が sig/ から作るエディタの F1)、
  `usr/share/asterism/msgs/` (`rake setup` が固定した asterism から写す物)。
- `/home` は入れない (空で出荷。file HAL が最初の起動で作る)。

### 確認の道具 (`native:scan`)

`native:deb` の最後に流れる。staging (`native/build-deb/<arch>/pkg`) と .deb を見る。

1. 配布物の木のどのファイルも、`flash/` で git が追跡しているか、上の生成物
   (`native/build/root/{etc,machines}`、`help/`、`usr/share/asterism/msgs/`) のどれか。
   木の外は本体・起動用・APPLaunch の場所だけ。
2. `wifi.toml`・`secrets.toml` の名前、`usr/share/sounds/nsf/` の外の NSF、`/home`。
3. すべてのファイルの中身 (本体の実行ファイルも) を、この機械の本物の資格情報
   (`~/.openai_key`、wifi.toml・secrets.toml の値。`wasm:scan` と同じ関数) と、
   OpenAI の鍵の形・PEM の秘密鍵 (見出しの後に本体が続くもの) で探す。
4. `dpkg-deb -c` の一覧と staging の一覧が一致する。

結果: arm64 583 ファイル、amd64 580 ファイル、資格情報 3 件を探して 0 件、
追跡外 0 件。わざと追跡外の `.rb` と PEM の鍵を木に置くと 3 件 (追跡外 2・鍵 1) を
出して止まることを確かめた。

## 実行ファイルが場所を見つける仕組み

| | 開発用 (`rake native:core`) | パッケージ (`NATIVE_PACKAGE=ON`) |
|---|---|---|
| 配布物の木 | リポジトリの `flash/` (コンパイル時の絶対パス) | 実行ファイルの場所 (`/proc/self/exe`) から `FMRB_NATIVE_DATA_REL`: arm64 は `../../family-mruby/root`、amd64 は `../share/family-mruby/root` |
| 機種の定義 | `native/build/root/machines` | 配布物の木の `machines/` |
| 最初の /etc | `native/build/root/etc` | 配布物の木の `etc/` |
| 利用者 | `$XDG_DATA_HOME/family-mruby-dev` | `$XDG_DATA_HOME/family-mruby` |
| キャッシュ | `$XDG_CACHE_HOME/family-mruby-dev` | `$XDG_CACHE_HOME/family-mruby` |

- パッケージのビルドには checkout のパスが 1 つも入らない (`FMRB_NATIVE_DEFAULT_*` を
  定義しない)。木が見つからなければ「`<探した場所>` に無い (入れ直すか
  `--system-dir`)」と言って終了コード 2 で止まり、`flash/` を探しに行かない。
- 実行ファイルからの相対にしたので、`.deb` を `dpkg-deb -x` で任意の場所に広げても
  そのまま動く。`--system-dir` などの引数と `FMRB_NATIVE_*` の環境変数は従来どおり
  上書きできる。
- root で動けば `$HOME=/root` なので、Cardputer (APPLaunch は root) では
  `/root/.local/share/family-mruby/` と `/root/.cache/family-mruby/` になる。

## `/etc` の更新の規則

### 実装 (`native/etc/etc_update.{h,c}`)

`main_native.c` が、振り分けの表を決めた後・機械 (FreeRTOS) を起こす前に
`native_etc_update(種, 利用者の etc)` を呼ぶ。利用者の etc がまだ無ければ何もしない
(最初の起動は file HAL が種を丸ごと写す)。native だけのファイルなので、端末・sim・
ブラウザ版は変わらない。

| ファイル | 規則 |
|---|---|
| `system_conf.toml`・`system_conf.factory.toml` | 利用者に無いキーだけ足す。一番上のキーは利用者の一番上の最後のキーの後、`[表]` のキーはその表の最後のキーの後、利用者に無い表は末尾に丸ごと。`[[配列の表]]` は利用者が 1 つも持っていないときだけ全部を足す (一覧は利用者の物) |
| `services.toml` | 利用者に無いサービスの節 (`[名前]` と `[名前.*]`) だけ末尾に足す。ある節は 1 文字も変えない (`enable` など) |
| ほかのファイル (下の階層も) | 利用者に無いファイルだけ写す |

- 「無い」の判定は tomlc99 (既存の読み方と同じ) で行い、どこからどこまでが 1 つの
  文か (複数行の配列・`"""` の文字列を含む) は小さな行の読み取りで決める。
  足す文には、種の側でその直前にあるコメントの行を付ける。利用者の文章は
  書き換えず、差し込むだけなので、利用者のコメントと並びは残る。
- 利用者のファイルが読めない (TOML として壊れている) ときと、足した結果が読めない
  ときは、そのファイルに触らずログに理由を出す。書くときは一時ファイルに書いて
  rename する。
- 足したものはログに出す: `native: /etc: system_conf.toml: added zenoh_router`、
  `native: /etc: services.toml: added [net]`、
  `native: /etc: associations.toml: copied (the user had none)`、最後に件数。
- 計画の「`system_conf.factory.toml` から足す」は、種の `system_conf.toml` から足す
  形にした (`native:etc` が同じ中身を両方に写しているので同じ)。利用者の
  `system_conf.factory.toml` も同じ規則で足す (無いキーが入るだけで、値は変えない)。
- main スレッドで機械の前に動くので、fmrb の allocator とログは使えない
  (native_machine.c と同じ事情)。中身は static の作業領域 (1 ファイル 64 KB まで、
  超えたら触らない) と libc、ログは `native: ` で始まる stderr の行。

### 試験

- host 試験 (`rake native:test` の `etc`、`rake test` に含まれる): `test/native/etc/`
  の `old/` (古い利用者の etc、利用者が変えた値とコメントつき) に `seed/` を当てて、
  `expected/` とバイト単位で一致すること。上のキー 3 つ (うち 1 つは複数行の
  `"""` 文字列)、表のキー 1 つ、無い表 1 つ (複数行の配列入り)、無い配列の表 2 つ、
  利用者が持っている配列の表は足さない、サービスの節 2 つ (`[timesync]` と
  `[timesync.config]` を 1 つの節として)、無いファイルを写す、壊れた
  `system_conf.factory.toml` には触らない。2 回目は 0 件で何も変わらない。
  利用者の etc が無いときは作らない。期待の 1 文字を変えると落ちることも確かめた。
- インストールした形 (下の受け入れ 6): L2n より前の `/etc` を置いて起動した。

## 受け入れ条件

確認の道具は `test/native/deb/deb_check.rb`。固定した base の素の trixie のコンテナ
(ビルドの道具は入っていない) に .deb を入れ、root でヘッドレス (SDL の dummy) に起動し、
コンテナの外から操作する (run dir を host のディレクトリにマウントし、制御と注入の
ソケットをルートの `tools/fmrb_native.rb shot`・`tools/fmrb_input.rb --socket` で使う)。
画面の比べ方は `rake regress:screens` と同じ (`tools/regress/screens/image.rb`、
時計 `(w-90,0,90,13)` とポインタ `(w-6,60,6,16)` を除いて画素ごと)。結果は
`tmp/deb_check/<arch>/` (写し・ログ・summary.json)。

```
# fmruby-core で
rake native:deb ARCH=arm64 && ruby test/native/deb/deb_check.rb --arch arm64
rake native:deb ARCH=amd64 && ruby test/native/deb/deb_check.rb --arch amd64
```

| 条件 | 結果 |
|---|---|
| 1. `.deb` ができる | `rake native:deb ARCH=arm64` → `family-mruby_<版>_arm64.deb` (3.19 MB)、本体は `ELF 64-bit LSB pie executable, ARM aarch64 ... stripped`。`ARCH=amd64` → `_amd64.deb` (3.36 MB) |
| 2. arm64 の trixie で | `docker run --platform linux/arm64 debian:trixie-20261020` (`uname -m` は aarch64、qemu)。`apt-get install /deb/family-mruby_*_arm64.deb` で依存 57 個が入り成功 (apt の取得と展開で 129-133 s)。起動用の `family-mruby-hdmi` は `native: refused: machine cardputer_zero_hdmi: display: no DRM device: /dev/dri has no card*` で終了コード 3 (起動用が機種を渡し、コンテナに DRM が無いので断る、が期待どおり)。root で `--machine sdl`: **起動から Boot complete まで 4.5 s** (起動の絵と音の約 3 s を含む)。デスクトップの写し 426x240 は `baseline/native/desktop.png` と**時計とポインタの外で違う画素 0**。メニューからエディタ (Spinel) が 3.4 s で開き、`a` は送ってから 0.1 s で画面に出て、`baseline/native/editor_type.png` とも**違う画素 0** |
| 3. amd64 の trixie で | 依存 59 個 (11 s)。Boot complete まで 4.1 s、デスクトップ 0、エディタ 2.4 s で開き `a` が出て editor_type も 0 |
| 4. 配布物の木に書かない・利用者の物は XDG の場所 | 起動から終了まで: 配布物の木の全ファイルの md5 が同じ、開始より新しいものは 0。利用者の側は `/root/.local/share/family-mruby/{etc,home,app}`、キャッシュは `/root/.cache/family-mruby/{cache,data,tmp,var}`。`find / -xdev -newer` で、`/tmp`・run dir・この 2 つ以外に書かれたファイルは 0 (arm64・amd64 とも) |
| 5. 追跡外・資格情報なし | `native:scan`: arm64 583・amd64 580 ファイル、追跡外 0、資格情報 0、.deb の中身と staging が一致。わざと置いた追跡外のファイルと PEM の鍵は捕まえる (上) |
| 6. `/etc` の更新 | host 試験 (上) に加え、インストールした形で: 利用者の `system_conf.toml` を L2n より前 (e22bfb7f) の `config/system_conf_native.toml` に `audio_mute = true # the user's` と `language = "ja"` を書き込んだもの、`services.toml` を当時の `services_wasm.toml` (clock と hourly_chime だけ) の clock を `enable = false` にしたものに替え、`associations.toml` を消して起動。ログは `system_conf.toml: added zenoh_router`、`services.toml: added [net]`・`[timesync]`・`[tts]`、`associations.toml: copied`、`5 addition(s)`。古いファイルの行はすべて同じ順で残り (diff は追加だけ)、`audio_mute = true`・`language = "ja"`・clock の `enable = false` はそのまま。arm64・amd64 とも |
| 7. 回帰 | `rake regress`: host 19/19 (native の host 試験は 392 項目)、描画の記録 205/205。`rake regress:screens`: **26 passed / 0 failed** (retro 8/8、modern 10/10、web 6/6、native 2/2、9.8 分)。基準は更新していない。build/ は TAB5 (esp32p4) に戻った |

qemu での速さ: 起動の絵の時間が大きく、実時間の差は小さかった (Boot complete
4.5 s 対 4.1 s、エディタが開くまで 3.4 s 対 2.4 s)。極端に遅い・特定の命令で落ちる、
は起きなかった。

## 回帰

- 開発用のビルド (`rake native:core`) は従来と同じ定義 (`NATIVE_PACKAGE` は既定で OFF)。
  変わったのは `/etc` の更新が起動のたびに走ること (開発用の利用者の場所
  `~/.local/share/family-mruby-dev/etc` にも効く。L1・L2 で作った古い etc には、
  次の起動で `zenoh_router` と net・timesync が足される)。
- `hosted/hosted.cmake` の変更は `FMRB_HOSTED_MRUBY_DIR` を外から渡せるようにした
  だけで、未指定ならブラウザ版も従来どおり。
- `lib/add/family_mruby_native.rb` は native の libmruby だけ。`FMRB_NATIVE_ARCH` を
  付けなければ従来と同じコマンド。
- 端末・sim・ブラウザ版のコード (main/・components/) は変えていない。

## L3b (実機) で確かめること

APPLaunch と OS:

- APPLaunch の一覧に「Family mruby (HDMI)」とアイコンが出るか。アイコンの大きさ・
  背景 (白い四角の PNG) の見え方。名前の衝突 (アイコンは共有の場所)。
- `.desktop` の `Exec` の扱い: 引数を渡せるか (渡せるなら起動用の実行ファイルは
  要らなくなる)、`Exec` が shell を通るか、`execv` で起動用から本体へ替わった後も
  APPLaunch がプロセスを追えるか。終了のさせ方 (signal の種類。SIGTERM と SIGINT は
  受けて終わる) と、終わった後に画面・入力がランチャーに戻るか。
- 本当に root で動くか、`$HOME` (と `XDG_*`) が何になるか。`/root/.local/share/family-mruby/`
  に利用者の物ができるか。cwd。stderr の行き先 (ログを読む場所)。
- `apt install ./family-mruby_*_arm64.deb` が実機の Raspberry Pi OS trixie で依存を
  解決するか (Depends の版はコンテナの trixie のもの)。

画面・入力・音 (L2 の一覧と合わせる):

- DRM: HDMI のカードが `card0`/`card1` のどちらか (`auto` が拾うか)、1280x720 が出て
  1278x720 に Modern が出るか、APPLaunch (と labwc など) が DRM の主導権を握ったまま
  起動されるか (取れなければ `cannot become the DRM master` で断る)、page flip、
  終了後に元の画面へ戻るか、全画面の高解像度モード (640x360 の 2 倍)。
- evdev: USB のマウス・キーボードと本体のキーボード (`tca8418` の名前、Sym・Fn の層)。
  APPLaunch が装置を掴んだままか。打ったキーが VT やランチャーにも届かないか。
- ALSA: ES8389 の PCM の名前 (`default` で鳴るか)、15720 Hz の変換、100 ms で詰まらないか。
  HDMI の音。
- 速さ: Boot complete までの時間、エディタが開くまで、打鍵の遅れ (qemu の数字は
  当てにならない)。メモリ (512 MB の機械で、プール 10 MB 前後と SDL・DRM)。
- 開発中の道具: 実機でも `FMRB_NATIVE_RUN_DIR` の制御のソケットで画面を取れるか。

## 見立てと違った点・撤回した仮説・踏んだ罠

- **qemu の上でも速かった**。極端に遅いことを止まる条件に挙げていたが、Boot complete まで
  4.5 s (x86_64 の 4.1 s と大差なし)。時間の大半が起動の絵と音の固定の待ちのため。
  apt での展開は qemu の上で 129-133 s かかった (amd64 は 11 s)。
- **アーキテクチャの差で画面は 1 画素も変わらなかった**。aarch64 の gcc 14 で作った
  描画 (LovyanGFX・CPU 合成) が x86_64 の基準と時計の外で一致した。浮動小数点の
  差で縁がずれることを心配していたが出なかった。
- 秘密鍵の検査が本体の実行ファイルで鳴った: TLS のライブラリが PEM の見出し
  (`-----BEGIN RSA PRIVATE KEY-----` など) を文字列として持っている。見出しの後に
  鍵の本体 (base64 が 40 文字以上) が続くものだけを鍵とみなすようにした。
- `dpkg-shlibdeps` はパッケージの形 (debian/control) を求めるので、作業用の
  一時ディレクトリに最小の control を置いて呼んだ。「binaries to analyze should already
  be installed in their package's directory」の警告は出るが結果は正しい。
- picoruby の `rake` は `MRUBY_BUILD_DIR` を受けるので、cross の木を checkout の
  picoruby の build/ に混ぜずに済んだ (混ぜると、ホストの Ubuntu と trixie の mrbc が
  同じ場所を取り合う)。
- 起動用の実行ファイルが 66 KB あるのは、aarch64 の ld が節を 64 KB の境界に揃える
  ため (中身は数百バイト)。
- 確認の道具では、コンテナの中の pid を host から signal しないようにした
  (`fmrb_native.rb down`・`status` は pid ファイルを host の pid として扱うので使わず、
  制御のソケットの `quit` で止め、最後は `docker rm -f`)。run dir のソケットは root の
  ファイルだが、ディレクトリが自分の物なので消せる (umask 0 で作らせ、外から
  つなげるようにした)。

## 残件

- **debugd が全部の口で待っている**: native の debugd は TCP 5565 を `INADDR_ANY` で
  開く (main/drivers/debug/fmrb_debug_transport_tcp.c。sim は docker の外から使うので
  これが要る)。パッケージは Cardputer で root で動くので、同じ網の誰でも認証なしに
  アプリを起動できる。L3b で網につなぐ前に、パッケージでは loopback だけにするか
  起動しないかを決める (main/ の変更になるので L3a の範囲の外。決めるのは親)。
- 配布物の `system_conf.toml` は開発用と同じ `config/system_conf_native.toml` で、
  `debug_mode = true`、`zenoh_router = "tcp/127.0.0.1:7447"`、`display_mode = "sdl2"`
  のまま。起動の表示も「Development mode」。パッケージ用の値 (または機種ごとの値) は
  L3b・L5 で決める。
- 起動の入口は HDMI の 1 つだけ。320x170 の入口と App Store の画面写真は L4・L5、
  amd64 のログイン画面の項目とアイコンは L7。
- アイコンの大きさの決まり (App Store) は未確認。今は 200x200。
- `/etc` の更新は、利用者が消したキーも「無い」として足し直す (消したのか、
  古いのかは区別できない)。既定値で動くキーなので害は無い見込みだが、気になる
  キーが出たら「足さない」一覧を持つ。
- `deb_check.rb` は docker と network (apt) が要るので `rake regress:*` には入れていない。
- 版の文字列は git describe の tag (2.1.0) から作るので、パッケージの版の付け方
  (リリースの tag との関係) は App Store に出すときに決める。

## 最後に残っているもの

- `build/`: regress:screens が退避して戻した ESP32-P4 (TAB5) のビルド。
- `native/build/fmruby-native` (開発用、標準構成)。`native/build-deb/` に arm64・amd64 の
  ビルドの木 (278 MB・183 MB) と 2 つの .deb。
- docker: `fmrb-native-build:trixie-20261020-40492cf3` (2.06 GB) と
  `debian:trixie-20261020` (確認のコンテナの base、amd64 と arm64)。確認のコンテナは
  終わりに消した。
