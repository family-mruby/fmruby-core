# L3c 指示書: パッケージ版の決まり (debugd と配布用の設定)

> 計画: doc/linux_native/plan.md「置き場所 (L1 で確定)」のパッケージ版の決まり。
> 前段の結果: report/l3a.md

## 目的

L3a で作った `.deb` に、2026-10-26 のユーザの決定を入れる。

1. **debugd**: パッケージ版 (`NATIVE_PACKAGE=ON`) では既定で起動しない。
   設定で有効にしたときも `127.0.0.1` だけで待ち受ける。
2. **配布用の system_conf**: パッケージ版には配布用の値を入れる
   (`debug_mode = false`、起動の表示は通常 = 「Development mode」を出さない、
   `zenoh_router` は空)。

開発用のビルド (`rake native:core`、`NATIVE_PACKAGE` なし)、sim、端末、
ブラウザ版の動きは変えない。

## 作業の内容

- debugd の有効・無効と待ち受けの場所を、system_conf の項目
  (例: `debugd = false`、`debugd_bind = "127.0.0.1"`) で決める形にする。
  項目名は既存の system_conf の書き方に合わせて選ぶ。sim と端末は今の動き
  (sim は全部のインターフェースで待ち受け、端末は BLE) を既定値として保つ。
  `main/drivers/debug/fmrb_debug_transport_tcp.c` を触るのはこのためだけ。
- 配布用の system_conf: `config/system_conf_native_release.toml` のように
  開発用と別に持つ (config/ が正、という決まりのまま)。`native:root` で
  パッケージを作るときにこちらを使う。開発用との差は最小にし、差の一覧を
  report に書く。
- `/etc` の更新の規則 (L3a) との関係: すでにパッケージを入れて使った
  利用者の場所に、開発用の値 (`debug_mode = true` など) が残っていても、
  規則は利用者の値を変えない。これは正しい動きなので直さない
  (L3a の版はまだ配っていない)。
- `zenoh_router` が空のとき、Asterism を使うアプリがどう振る舞うかを
  確かめる (固まらない、分かるエラーになる)。

## 触ってよい範囲

- fmruby-core: `config/`、`native/`、`rakelib/native.rake`、
  `main/drivers/debug/` (上の目的のためだけ)、`test/`、
  `doc/linux_native/report/`。
- 触らないもの: L3a の指示書と同じ (graphics-audio、sdkconfig、submodule の中、
  `flash/` の中身、plan.md、端末・sim・ブラウザ版の動き)。

## 受け入れ条件

1. パッケージ版を arm64 のコンテナで起動すると、debugd が待ち受けていない
   (`ss -ltn` などで確かめる)。system_conf で有効にすると 127.0.0.1:5565 だけで
   待ち受け、コンテナの外のアドレスからは繋がらない。
2. パッケージ版の起動の表示に「Development mode」が出ず、debug_mode は false。
3. 開発用の native (`ruby tools/fmrb_native.rb up`) は今どおり debugd が動き、
   MCP の `native_app` が使える。
4. `rake regress` と `rake regress:screens` が通る (sim の debugd を使う道具も
   今どおり動く)。`ruby test/native/deb/deb_check.rb --arch arm64` が通る
   (debugd を使って確かめていた所は、別の確かめ方に直してよい)。

## 作業場所・git・止まる条件・report

- L3a の指示書と同じ (本体の checkout、`feature/linux-native`、マージ・push しない、
  `rake clean_all` しない、仕様が変わる直し方は止まって返す)。
- report は `doc/linux_native/report/l3c.md` (短くてよい: 項目名、差の一覧、
  受け入れ条件の結果)。
