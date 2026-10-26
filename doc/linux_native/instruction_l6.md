# L6 指示書: OS のサービス (電池・ネットワーク・音量・電源)

> 計画: doc/linux_native/plan.md「OS のサービス (`fmrb_platform_sys`)」と、
> 2026-10-26 のユーザ決定。前段の結果: report/l2n.md (net のサービスを sim と
> 同じ答えにした)、report/l4.md、report/l5a.md

## 目的

デスクトップの表示 (ネットワーク・音量・電池) と電源の操作を、OS に問い合わせる
1 つの受け口にまとめ、native では Linux から答える。端末・sim・ブラウザ版は
今の実装がその受け口の向こうでそのまま答える (動きは変えない)。

## ユーザの決定 (変えない)

- **Wi-Fi は状態の表示だけ** (繋がっているか・SSID・電波の強さ)。接続の設定は
  OS に任せ、Family mruby から OS のネットワークの設定を変えない。native の
  mouse のデスクトップで今の Wi-Fi のダイアログを開いたときは、状態を見せる
  だけにする (設定の操作は出さない)。
- **音量は OS の音量 (ALSA の mixer)**。デスクトップの音量とミュートの操作が、
  本体のスピーカーの音量そのものを動かす。
- **電源の操作は形で分ける**。(1)(2) (Cardputer) は「終了 (APPLaunch に戻る)」
  だけ。(3) (Linux PC) は電源断・再起動・休止。どちらかは機種の定義で選ぶ。

## 作業の内容

### 1. 受け口 (`fmrb_platform_sys`)

- 今デスクトップが使っている問い合わせ (`FmrbApp.wifi_info` /
  `wifi_connected?`、音量とミュート (doc/reference/audio_output.md)、
  再起動 (`FmrbApp.reboot`)、時計の設定、電池の値があればそれ) を読み、
  1 つの C の受け口 (関数の表か、機種ごとの実装を選ぶ形) にまとめる。
  置き場所と形は、L1 の input_source.h・display_backend_t・audio_backend_t の
  作りに合わせる。
- 端末・sim・ブラウザ版の実装は、今のコードを受け口の向こうに移すだけ
  (動きと内蔵 RAM を変えない)。電池の値の無いものは「値なし」を返す。
- Ruby 側 (FmrbApp) の呼び名は変えないでよい。足すもの (電池・電源の種類) は
  既存の名前の付け方に合わせる。

### 2. linux の実装 (native)

- **電池**: sysfs の power_supply (`type = Battery` のもの。残量・充電中)。
  無ければ値なし。
- **ネットワーク**: 繋がっているか・SSID・電波の強さ。NetworkManager の D-Bus か、
  無ければ sysfs と `/proc/net/wireless` などの読むだけで済む所から。D-Bus の
  ライブラリを増やすなら、arm64・amd64 の trixie にあるものだけにし、
  `.deb` の Depends に足す。**書き込み (接続・切断) の呼び出しは入れない**。
- **音量**: ALSA の mixer (simple mixer の要素。どの要素を使うかは機種の定義に
  項目を足す。例 `audio.mixer = "auto"` か要素の名前)。音量とミュートの
  値は OS に持たせ、Family mruby の中の音量 (今の音量の保存の仕組み) は
  native の ALSA の出力では 100% のままにする。mixer が開けないとき
  (SDL の出力、WSL2) は今の Family mruby の中の音量に落ちる (今と同じ動き)。
- **電源**: 機種の定義に項目を足す (例 `system.power = "exit" | "logind"`)。
  - `exit`: 終了だけ。プログラムを終える (APPLaunch に戻る)。
  - `logind`: 電源断・再起動・休止を logind (D-Bus) に頼む。
  - 確かめるときに本当に電源を切らないよう、**呼ぶ代わりにログに出すだけの
    形** (環境変数か `--set` の項目) を用意し、試験ではそれを使う。WSL2 で
    本物の logind を呼ぶ確認はしない。
  - `cardputer_zero`・`cardputer_zero_hdmi` は `exit`、`linux_pc_drm` は
    `logind`、`sdl`・`sdl_keys` は `exit`。
- 待ち続ける呼び出し (D-Bus など) は L2n の blocking-call gate に乗せる。
- 時計: native は Linux の時計をそのまま使い、Family mruby から変えない (L2n の
  決まり)。時計の設定の画面は native では読むだけか、出さない (今の
  sim の扱いを読んで合わせる)。

### 3. デスクトップ

- **mouse のデスクトップ (native)**: ネットワークの印・音量・電池 (値があれば)
  を受け口から出す。「再起動」の所は、電源の種類に合わせて「終了」だけ、
  または「電源断・再起動・休止」に。確認のダイアログは今のものを使う。
  端末・sim・ブラウザ版の見た目は変えない。
- **keys のデスクトップ**: 帯の右に電池 (値があれば)。メニューの最後に電源の
  項目 (電源の種類に合わせて「終了」または 3 つ)。Esc の長押しは今どおり。
- 文字列は i18n の表に足す (en・ja)。

## 試験

- linux の実装の読み取りは、**sysfs・procfs の場所を差し替えられる形**にし、
  偽の木 (電池あり・なし・充電中、無線あり・なし) で host 試験を書く
  (test/native/ に足して `rake regress` の host に載せる)。
- ALSA の mixer は WSL2 に装置が無い。開けないときの落ち方 (中の音量に落ちる)
  を試験し、mixer を実際に動かす確認は L3b・L5b (実機) に回す。arm64 の
  コンテナで ALSA の null か file の装置を使って確かめられるなら確かめる。
- 電源はログに出すだけの形で、各項目が正しい要求 (logind のメソッド名) になる
  ことを確かめる。

## 触ってよい範囲

- fmruby-core: 受け口のための `main/` と `components/` の該当箇所
  (端末の実装の移動を含む)、`main/prebuild_scripts/kernel/` (デスクトップ)、
  `native/`、`hosted/`、`config/` (機種の定義)、`rakelib/`、`test/`、
  `doc/linux_native/report/`。
- family-mruby (ルート): `tools/`。
- 触らないもの: fmruby-graphics-audio、sdkconfig、submodule の中、`flash/` の
  中身、plan.md、端末・sim・ブラウザ版の動きと見た目。

## 作業場所と git

- 本体の checkout、両リポジトリとも `feature/linux-native` に続けてコミット
  (規約は fmruby-core/CLAUDE.md、Co-Authored-By、`git add` は明示)。
  develop へのマージと push はしない。
- `build/` は ESP32-P4 のビルド。自分で `rake clean_all` はしない。
- 端末の実装を動かすので、`rake regress:size` (`ALL=1` が流せるなら全端末) で
  内蔵 RAM が増えていないことと、端末のビルドが通ることを確かめる。

## 受け入れ条件

1. 偽の sysfs の host 試験で、電池 (あり・なし・充電中) とネットワーク
   (繋がっている・いない) の読み取りが通る。
2. native の mouse (`--machine sdl`) と keys (`--machine sdl_keys`) で、
   電源の項目が `exit` の形 (終了だけ) で出て、選ぶとプロセスが終わる。
   `--set system.power=logind` とログに出すだけの形で、電源断・再起動・休止の
   3 つが出て、選ぶと logind への要求がログに出る (電源は切れない)。
3. WSL2 (mixer なし) で、音量とミュートの操作が今どおり中の音量に効く。
4. Wi-Fi のダイアログ (native の mouse) が状態だけを見せ、設定の操作が無い。
5. 端末・sim・ブラウザ版: `rake regress`、`rake regress:screens`、
   `rake regress:live` が通り、`rake regress:size` で内蔵 RAM が増えていない。
6. 2・3 を標準と全部 mruby の両方で。arm64 の `.deb` の deb_check が通る
   (Depends を足したなら、素の trixie で依存が解決される)。

## 止まる条件

- 端末・sim・ブラウザ版の動きや見た目を変えないと受け口にまとめられないとき。
- 上のユーザの決定と食い違う作りが要るとき (たとえば OS の設定を書き換えないと
  状態が読めない)。
- 内蔵 RAM が増える作りしか無いとき。
- `rake clean_all` や sudo が要るとき、trixie に無いライブラリが要るとき。
- 止まったら report に状況を書いて返す。

## report (doc/linux_native/report/l6.md) に書くこと

- 受け口の形と置き場所、移した端末の実装の一覧
- linux の実装の読み取り先 (sysfs・D-Bus・ALSA) と、足した機種の定義の項目
- 電源の形と、ログに出すだけの形の使い方
- 受け入れ条件ごとの結果と、確かめたコマンド・数値 (内蔵 RAM の差を含む)
- L3b・L5b (実機) で確かめること (電池の sysfs の名前、mixer の要素、
  NetworkManager の有無)
- 見立てと違った点、撤回した仮説、踏んだ罠、残件
