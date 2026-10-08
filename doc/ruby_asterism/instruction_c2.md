# Asterism C2 指示書: ruby-asterism のリポジトリへの分割と、正の移し替え

> 状態: 指示 | 更新: 2026-10-08 | Asterism を GitHub の ruby-asterism (非公開) の 3 つのリポジトリに分け、Ruby だけで書いた層の正を asterism 側に移す。fmruby-core は PIN で取り込む側になる

## 0. 決まったこと (2026-10-08 ユーザ)

- オーガナイゼーションは `ruby-asterism` (作成済み)。リポジトリは**非公開**で作り、**役割ごとに分ける**。
- **Ruby だけで書いた層の正は asterism 側**に置く。fmruby-core は取り込む側。
- CRuby らしいブロックの API は、いずれ足す (C2 ではやらない)。
- mruby 版との小さな違い (版の定数の名前、peer の上限、自分のトークン) はそろえない。CRuby 版はリッチな環境で動くので。
- 配布は後で決める。まずは「入れるときに C をコンパイルする」形が楽、というのがユーザの見立て (zenoh-c は入れるときに取る)。

## 1. リポジトリの分け方 (親の案)

| リポジトリ | 中身 | 使う側 |
|---|---|---|
| `ruby-asterism/asterism` | Ruby だけで書いた層の**正** (オブジェクトの代理・CDR・ROS)、型の変換器、同梱の型、CRuby の gem `asterism` の gemspec、**mrbgem としての定義** (`mrbgem.rake` と mrblib) | CRuby (gem)、mruby / PicoRuby (mrbgem) |
| `ruby-asterism/asterism-zenoh` | CRuby の C 拡張 (zenoh-c)。gem `asterism-zenoh` | CRuby |
| `ruby-asterism/picoruby-asterism-zenoh` | mruby / PicoRuby の zenoh-pico の mrbgem (今の `lib/add/picoruby-asterism-zenoh/`) | mruby / PicoRuby (Family mruby、R2P2 など) |

- `asterism` は 1 つの正から CRuby の gem と mrbgem の両方になる (写しを作らない)。
- CRuby の試験 (今の `test/`) のうち、Zenoh だけの試験は asterism-zenoh へ、Ruby の層の試験は asterism へ。
  型の試験 (fmruby-core の `rake asterism:test` の中身) も asterism へ移す。
- ライセンス:
  - Asterism 自身のコードは MIT。各リポジトリに LICENSE を置く。
  - 同梱の ROS 2 の型の定義から作ったもの (と元の定義) は Apache-2.0 なので、NOTICE と出所を書く。
  - zenoh-pico・zenoh-c の一部を元にしたファイル (`ports/` の TCP 層など) は、元の条件 (EPL-2.0 / Apache-2.0) を確かめて
    ヘッダに書く。
  - 外から取ってくるもの (zenoh-pico、zenoh-c) は同梱しない。

## 2. fmruby-core の側

- `lib/add/picoruby-asterism/` と `lib/add/picoruby-asterism-zenoh/` は、ruby-asterism のリポジトリを**PIN で取り込む形**にする。
  `lib/add/PICORUBY_TI_PIN` と同じ形。
  - rake で vendor/ (gitignore) に取得し、`rake setup` で picoruby の mrbgems に写す。
  - ZENOH_PICO_PIN は picoruby-asterism-zenoh の側に移すか、fmruby-core に残すかを決めて README に書く。
  - 開発中に手元のチェックアウトを使うための環境変数による上書き (`ZENOH_PICO_DIR` と同じ考え方) を付ける。
- 同梱の型 (`flash/usr/share/asterism/msgs`):
  - asterism のリポジトリの中身を storage に入れる形にする。
  - flash/ は「追跡分 = 配布してよいもの」の決まり。ビルドのときに storage の作りかけの木へ写すか、写しを追跡するかを決めて書く。
- 非公開のリポジトリから取るので、取得は今の git の認証 (ssh) で通る形にする。
  - CI (GitHub Actions) で fmruby-core をビルドすると取れなくなる。CI で asterism を入れないか、公開するまで待つか、
    影響を report に書く。CI の設定は変えない。
- 試しのアプリ (`flash/app/test/asterism_*`、`ros2_*`、`zenoh_*`) は fmruby-core に残す。
- 親リポジトリの `tools/fmrb_ros2_types.rb` と `tools/ros2_jazzy/` は、asterism 側へ移すのがよければ移し、親には呼び出しだけ残す。

## 3. 受け入れ条件

1. 3 つのリポジトリがローカルに出来ていて、それぞれ単独で試験が通る (asterism: Ruby の層と型の試験、asterism-zenoh: Zenoh の
   試験 (zenohd を使ってよい)、picoruby-asterism-zenoh: 単独で検査できるもの (host でのビルドの確認など))。
2. fmruby-core が PIN で取り込んで、sim の標準構成と互換構成、P4 (NARYAv4) のビルドが通る。sim で asterism_demo・
   ros2_types・zenoh_echo が動く。P4-Nano で asterism_demo と ros2_types が動く (焼き直しは app_only)。
3. `rake test` (fmruby-core) が通る。型の試験は asterism 側で通る。
4. 同じ中身が二重に追跡されていない (fmruby-core に写しを残す場合は、PIN と一致することを確かめる仕組みがある)。
5. 親が GitHub に push する前の状態: 各リポジトリの `main` にコミット済み、remote は未設定。

## 4. 触ってよい範囲

- `family-mruby/asterism/` (今の C1 のリポジトリ) を 3 つのリポジトリに分ける。新しいディレクトリは `family-mruby/` の下に置き、
  親の `.gitignore` に足す。
- fmruby-core: `lib/add/picoruby-asterism*`、`lib/add/*_PIN`、Rakefile / rakelib の取得と setup の部分、`flash/usr/share/asterism/`、
  `.gitignore`、`doc/ruby_asterism/report/c2.md`、README の類。
- 親リポジトリ: `.gitignore`、`tools/fmrb_ros2_types.rb` と `tools/ros2_jazzy/` (移す場合)。
- 触らないもの: `.env` (TAB5 のまま)、sdkconfig、パーティションの表、submodule の中、fmruby-graphics-audio、CI の設定、
  既存の compose のサービスとポート。

## 5. 止まる条件

- PIN で取り込む形にすると、fmruby-core のビルドの仕組みを大きく変える必要があるとき。
- ライセンスで判断が要るものが出たとき (書いて返す)。
- 範囲の外の変更、利用者から見た動きの選択が要るとき。

## 6. 作業の決まり

- **ブランチとコミット**
  - fmruby-core と親リポジトリは、`feature/asterism-c1` から `feature/asterism-c2` を切ってコミットする。
  - 新しいリポジトリは `main` にコミットする。
  - コミットは英文で `<領域>: <要約>` の形にし、Co-Authored-By を付ける。
  - **GitHub への push、リポジトリの作成はしない (親が行う)**。develop へのマージもしない。
- **機体: P4-Nano (192.168.10.15)**
  - ミュート中。
  - 親の capture が向いているので、`serial_start` は呼ばない。
  - 焼き直しは MCP の `flash` (`app_only`) で行う。
  - tab5_* には ip を渡す。
- **終わったら**
  - sim は sim_down する。
  - zenohd・ROS 2 は `down` する。
  - `build/` は Linux の標準構成 (x86-64) に戻す。
- **report**: `report/c2.md` (fmruby-core の `feature/asterism-c2`) に、リポジトリの構成、取り込みの仕組み、ライセンスの扱い、
  CI への影響、受け入れ条件の結果を書く。
