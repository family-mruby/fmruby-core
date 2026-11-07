# develop の CI 失敗 (2026-11-07、run 42356597664)

> 状態: 完了 | 更新: 2026-11-07 | develop を 14bca675 へ進めた直後の CI で 7 ジョブが落ちた件の原因と直し。原因は 3 つで互いに独立: Spinel のデスクトップに Linux 版の FmrbApp のメソッドが無かった (spinel-gen・build-linux (spinel))、ランナーの ubuntu-26.04 に 32 ビットのヘッダが無い (build-esp32 の 4 つ)、libpng の無いホストで試験の前提が崩れる (test-host)。直しは e3ab8127・3503ca24・e7d37d0c。push して CI を見るのは親

## 1. 落ちたジョブと原因

| ジョブ | 原因 | 手元で通っていた理由 |
|---|---|---|
| spinel-gen、build-linux (spinel) | Linux 版の作業 (L6・L14・L9d) で FmrbApp に足したメソッドが mruby の binding にだけあり、Spinel の FmrbApp (fmrb_app_base_spinel.rb) に無い。デスクトップがそれを呼ぶので Spinel のコンパイラが 9 件拒否した | 手元の .env も回帰の道具もデスクトップを mruby で作る。デスクトップを Spinel で作るのは CI のこの 2 ジョブだけ。Spinel のコンパイラの版は CI と手元で同じ (0bba87dc、SPINEL_PIN どおり) |
| build-esp32 (NARYAv3・TAB5・ATOMS3R・STICKS3) | `rake build:esp32` は checkpoint の表をホスト (ランナー) で作り、`-m32` でもう一度コンパイルする (tools/ckpt_table)。ランナーが ubuntu-26.04 (gcc 15) になり、gcc-multilib が無いので `bits/libc-header-start.h` が見つからない | 手元のホスト (Ubuntu 22.04) には gcc-multilib が入っている。この段は ESP-IDF のイメージの中でなくランナーの上で動く |
| test-host | test/native/apps.c の test_themes が hicolor の fakeicon.png を探すが、その PNG は libpng があるときだけ書かれる (make_tree)。ランナーには libpng-dev が無く、探索が pixmaps/ の SVG に行って 1 件落ちる。**上の 2 つとは無関係に単独で落ちる** | 手元のホストには libpng がある |

Spinel が拒否したのは battery_level・power・linux_settings (2 か所)・linux_keyboard・wifi_networks・notices_gen・notices・bt_devices。POWER_ITEMS の警告は、クラス本体の定数式が拒否に巻き込まれたもので、直したあとは出ない。

## 2. 直し

- **e3ab8127 (spinel, desktop)**: Spinel の FmrbApp に次を足した。
  - power_actions・power・battery_level・battery_charging?: FFI を 3 つ足し
    (fmrb_spx_app_power_actions / _power / _battery、main/app/fmrb_spx_app.c)、
    mruby の binding と同じく fmrb_platform_sys を呼ぶ。端末で Spinel の
    デスクトップを使っても Reset が残る。
  - wifi_networks・bt_devices・notices_gen・notices・notice_dismiss・linux_settings:
    ネイティブ版 (そのデスクトップは mruby) にしか無いものなので、既にある
    linux_apps と同じく「持たない機械の答え」("" / -1 / false) を返す。
  - linux_keyboard は Spinel には定義しない。config_dialog が
    `respond_to?(:linux_keyboard)` で「auto」を出すかを決めているので、
    定義すると端末でも auto が出てしまう。呼び出しを cfg_linux_keyboard に
    移し、その中身を `#:spinel-strip-begin/end` で Spinel 版から外した。
- **3503ca24 (test)**: hicolor の確認を `#if NATIVE_HAVE_PNG` に入れた
  (書く側と同じ条件)。
- **e7d37d0c (ci)**: build-esp32 に「Install 32-bit host headers」
  (`sudo apt-get install -y gcc-multilib`) の段を足した。共有の docker
  イメージは変えていない。表も 32 ビットの確認も残る。

## 3. 確認

- spinel-gen: `FMRB_KERNEL_ENGINE=spinel FMRB_APP_ENGINE_DESKTOP=spinel
  FMRB_APP_ENGINE_EDITOR=spinel rake spinel:gen` が、直す前は同じ 9 件で
  落ち、直したあとは警告・拒否なしで通る。きれいな clone (ローカルの
  リポジトリを scratch に clone して submodule を取り直したもの、vendor/spinel
  も自前で取る) でも同じく通る。
- build-linux (spinel): `rake clean_all` のあと同じ環境変数で
  `rake build:linux` が通る (build/fmruby-core.elf は x86-64)。
- build-esp32: `FMRB_HW_TARGET=NARYAv3 rake clean_all && rake build:esp32`
  が通る。checkpoint の表 tmp/ckpt_table/NARYAv3/fmrb_checkpoint_table.c は
  直す前の手元の物と**バイト単位で同じ** (生成側も "unchanged" と出す)。
  measure.txt は「keep (addresses)」が 3676 → 3652 件、プールが 101784 →
  102352 バイトと動いたが、表は同じなのでホストでの実行ごとの揺れ
  (CI の NARYAv3 も 102280 と別の値)。
- 32 ビットの段: ubuntu:26.04 のコンテナで gcc 15 だけ入れて同じ
  `cc -m32 ...` を流すと CI と同じ `bits/libc-header-start.h` で落ち、
  gcc-multilib を入れると通る。
- test-host: 失敗した試験を `-DNATIVE_HAVE_PNG=0` でコンパイルすると、直す前は
  CI と同じ `FAIL test_themes:473` (103/104)、直したあとは 103 件すべて通る。
- `rake regress`: host 21 suites 全通過、描画の記録 205 件全通過。

## 4. 親がすること

- develop を push し、run の 7 ジョブ (spinel-gen・build-linux 2 つ・
  build-esp32 4 つ・test-host) を見る。ランナーのイメージで初めて動く所
  (apt の gcc-multilib、TAB5・ATOMS3R・STICKS3 の build-esp32) は手元で
  流していない。
- ランナーが ubuntu-26.04 になったことで、今後もホストのパッケージの差
  (libpng-dev など) で落ちうる。ネイティブ版の試験の PNG 側をランナーでも
  通したいなら、test-host に `libpng-dev` を入れる段を足す選択肢がある
  (今回は足していない)。
