# M1・M2・M4 指示書: 描画の部品の名前、端末の画面の口、CI

> 状態: 指示 | 更新: 2026-10-12 | `display_p4` を「端末の中で描く描画」の名前に変え (M1)、描画の部品から端末名の分岐を追い出して「端末の画面の口」にまとめ (M2)、CI に AtomS3R のビルドを足す (M4)。M3 (M5GFX で M5 の機種をまとめ、M5StickS3 で確かめる) は端末がつながってから別の指示書で

前提: doc/headless_s3/plan.md の「M」の節、report/h1.md (描画の経路と、共通の部分を変えた一覧)、report/h3.md、fmruby-core/CLAUDE.md、doc/wasm/ (CPU 版の描画の経路、ブラウザ版のビルド)、`.github/workflows/build.yml`。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. M1: 名前を実態に合わせる

- `main/drivers/display_p4/` とその中のタスク・関数・型・ログの名前・CMake の名前・文書の中の参照を、端末の中で描く描画の名前 (`display_local` を基本に、読みやすい形を選ぶ) に変える。
- **振る舞いは変えない**。名前だけのコミットにして、中身の変更と混ぜない。
- `display_p4` の名前がログの検査 (例: `display_p4: cursor:` を監視する文書・道具・MCP のツール) に使われていれば、それも合わせて直す。外から見える名前 (ログの行の頭など) を変えるときは一覧にして report に書く。
- 確かめ: Tab5・NARYAv4・AtomS3R・Retro・ブラウザ版・Linux (mruby と Spinel) のビルドが通り、ファームの大きさと静的な内蔵 RAM が変わらない (名前の文字列の差で数バイト変わるのは可。差を書く)。

## 2. M2: 端末の画面の口

- 描画の部品は「端末の画面の口」だけを呼ぶ: 初期化 (画面の大きさ・色の形を返す)、矩形の送り出し、背光の明るさ、I2C のポートの持ち主 (描画の側が持つか)、など。口の形 (関数の名前・引数) は今の作りに合わせて決め、ヘッダに書く。
- 端末ごとの違いは口の実装 1 つにまとめる: Tab5 / NARYAv4 (MIPI・PPA・LT8912B などの今の経路) と AtomS3R (`lgfx_atoms3r.hpp`) と、ブラウザ版・Linux の経路。どの実装をビルドに入れるかは CMake で端末から選ぶ。
- 描画の部品 (`display_local` の中) から `#if defined(FMRB_HW_ATOMS3R)` / `FMRB_HW_TAB5` などの端末名の分岐を無くす (端末名ではなく、できることのフラグ `FMRB_HAS_PPA` などで分けるのは可)。残す必要があるものは、理由を report に書く。
- 小さい画面の振る舞い (止まったときの画面・起動の画面) は、今と同じく画面の幅で決める。
- **振る舞いを変えない**。Tab5 と NARYAv4 は手元に無い (Tab5 は PC につながっていない) ので、ビルドの大きさと、描画の経路のコードの読み合わせで確かめ、実機で確かめることを report の「ユーザに確かめてほしいこと」に書く。AtomS3R は焼いて確かめる (画面の取り込み `/fb` で見る)。ブラウザ版と Linux の sim も動かして確かめる。

## 3. M4: CI

- `.github/workflows/build.yml` に `build-esp32 (ATOMS3R)` を足す (今の NARYAv3 / TAB5 のジョブと同じ形)。端末ごとの既定 (`BOARD_BUILD_DEFAULTS`) で `FMRB_ASTERISM=1` になるので、asterism の取得も含めて通ること。
- 手元で同じ手順を回して通ることを確かめる。CI は push のときに親が回す。

## 4. 受け入れ条件

1. `display_p4` の名前が、コードと文書と道具から無くなる (履歴の文書・archive は除く。残す場合は理由)。
2. 描画の部品の中に端末名の分岐が無い (または理由つきで最小)。
3. 全構成 (Tab5・NARYAv4・AtomS3R・Retro・ブラウザ版・Linux mruby・Linux Spinel) のビルドが通り、大きさと静的な内蔵 RAM の差が名前の分だけ。
4. AtomS3R で起動・画面・アプリ 1 本の起動・止まったときの画面が今と同じ (`/fb` の写しで比べる)。sim の標準構成でエディタの 1 打鍵まで。ブラウザ版が起動する。`rake test` が通る。
5. CI のジョブが足され、手元で同じ手順が通る。

## 5. 範囲・決まり

- **触ってよいもの**: fmruby-core の main/・components/ (submodule 以外)・CMake・Rakefile・rakelib・`.github/workflows/build.yml`・doc (参照の直し)・tools、親リポジトリの tools/ (MCP のツールがログの名前に頼っていれば)。fmruby-core はブランチ `feature/display-local` を develop から。親リポジトリは develop にコミット。どちらもコミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` は変えない)。
- **触らないもの**: sdkconfig / defaults の中身、submodule、fmruby-graphics-audio、公開済みの gem。
- **端末**: AtomS3R (`/dev/ttyACM1`、WiFi、ip は mDNS 名から) を焼いてよい (焼いた後は serial_stop → serial_start)。P4-Nano (`/dev/ttyACM0`) は触らない。
- **終わったら**: `build/` を Linux の標準構成 (x86-64) に戻し、ブラウザ版のビルドが通る状態に戻す (`rake clean_all` は wasm の libmruby を消す。emsdk の環境を読み込んでから作り直す)。
- **止まる条件**: 振る舞いが変わる直しが要るとき、Tab5 / NARYAv4 の経路を実機なしでは安全に動かせないと判断したとき (案を書いて返す)、仕様の選択。
- **report**: `doc/headless_s3/report/m1_m2.md`、Japanese plain style (常体)。
