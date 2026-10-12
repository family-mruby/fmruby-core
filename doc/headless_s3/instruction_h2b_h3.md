# ヘッドレス S3 H2b・H3 指示書: 落ちたアプリの扱い、画面の取り込み、Asterism を S3 に載せる

> 状態: 指示 | 更新: 2026-10-12 | H2b: 単一アプリの端末で、遠隔で起動したアプリが落ちたときに理由を画面に出し、次の起動で消す。可能なら S3 の遠隔操作に画面の取り込み口を足す。H3: picoruby-asterism-zenoh と asterism を S3 のビルドに入れ、AtomS3R を Asterism の網に入れる (Asterism の Z4)

前提: doc/headless_s3/plan.md、report/h1.md・h2.md (特に「単一アプリと launch の件」「画面の取り込み」「H3 の見積もり」)、instruction_h1.md の決まり、doc/ruby_asterism/plan.md (Z1-Z3、Z4)、doc/ruby_asterism/report/z1.md・z2.md (zenoh-pico を Linux と P4 に入れたとき、PSRAM だけで確保する作り)、doc/ruby_asterism/browser.md (端末の名前の決まり)、親の CLAUDE.md。作業はブランチ `feature/headless-h1` の続き。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 決まっていること (2026-10-12 ユーザ)

- AtomS3R の画面の表示はユーザが目で確かめた (H1b の目視は合格)。
- **落ちたアプリ** (h2.md の提案 1): `boot_mode = "app"` の端末で、遠隔 (`/app/launch`) で起動したアプリが落ちたら、止まったときの画面にそのアプリと理由を出す。次に何かを起動したら、止まったときの画面を消す。起動し直しはしない (開発の流れではそれでよい)。`boot_mode = "desktop"` の端末の振る舞いは変えない。
- **画面の取り込み口**: 無理せず、可能なら作ってよい。S3 の遠隔操作のサーバ (devctl) に、今の画面の画素を返す小さな窓口を足す (例: 128x128 の RGB565 をそのまま、約 32 KB)。PC の側は MCP の tab5_screenshot がそれを使えるようにする (ツールの変更が要るなら tools/mcp/ を直してよい。Tab5 の振る舞いは変えない)。内蔵 RAM を増やさない (取り込みの一時的な場所は PSRAM)。手間が大きい・内蔵 RAM が要るなら、作らずに理由を report に書く。

## 2. H3: Asterism を S3 に載せる

- picoruby-asterism-zenoh と asterism を S3 のビルド (ATOMS3R) に入れる。今は P4 のビルドだけに入っている。入れ方は P4 の入れ方に合わせ、全 S3 の端末で使える形にする (Retro に入れるかどうかは選べるようにし、既定では入れない。Retro の flash と内蔵 RAM を変えない)。
- zenoh-pico の確保は PSRAM だけ (Z2 の作り)。zenoh のタスクのスタックが内蔵 RAM に置かれるなら、その量を測る。
- `flash/app/test/asterism_demo.app.rb` を AtomS3R で動かし、asterism-console の図に ▣ のノード (`fmruby-XXXXXX`) として出ること、console と PC から呼べることを確かめる。ルータは今の手順 (`/home/zenoh_echo.txt` など) でつなぐ。
- 128x128 の画面で `asterism_demo` の表示が読めるか確かめる (読めなければ、小さい画面向けの表示の工夫を試しのアプリの側で行う)。
- 測る: ファームの大きさと残り、内蔵 RAM の空き (WiFi + zenoh)、呼び出しの往復時間、起動から網に入るまでの時間。

## 3. 受け入れ条件

1. 単一アプリの端末で、遠隔で起動したアプリが落ちると、止まったときの画面に理由が出る。次の起動で消える。sim の `desktop` の既定は変わらない (sim で確かめる)。
2. 画面の取り込み口: 作った場合は、tab5_screenshot で AtomS3R の画面が取れる。作らなかった場合は理由。
3. AtomS3R の `asterism_demo` が console の図に出て、呼べる。往復時間を書く。
4. Retro・Tab5 のビルドが通り、内蔵 RAM・flash が変わらない (Retro に Asterism を入れない既定で)。sim の標準構成が起動し、エディタで 1 打鍵できる。`rake test` が通る。
5. 内蔵 RAM の空き (WiFi + zenoh) と、H4 に回せる量を書く。

## 4. 範囲・決まり

- **触ってよいもの**: instruction_h1.md と同じ範囲、tools/mcp/ (画面の取り込みのため)、lib/add 配下の Asterism の取り込み。ブランチ `feature/headless-h1` に続けてコミット。push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` は変えない)。
- **触らないもの**: 既存の sdkconfig / defaults (ATOMS3R の defaults は直してよい)、submodule、fmruby-graphics-audio、公開済みの gem (picoruby-asterism-zenoh・asterism。直しが要るなら差分を書いて返す)、`config/wifi.toml` の中身。
- **端末**: AtomS3R は `/dev/ttyACM1` (ポートを明示する)。焼いてよい。焼くと MCP のシリアルの capture が切れるので、焼いた後は serial_stop → serial_start。P4-Nano (`/dev/ttyACM0`、192.168.10.10) は触らない。端末の IP・mDNS の名前・MAC はリポジトリに書かない。
- **docker**: zenohd と asterism-console を使ってよい。終わったら止める。
- **ビルド**: `rake clean_all` は wasm の libmruby を消す。終わったら `rake wasm:mruby` などで作り直し、ブラウザ版のビルドが通る状態に戻す (doc/wasm/)。`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**: ここに無い仕様の選択、Retro・Tab5 の内蔵 RAM の増加、既存の sdkconfig を変える必要、公開済みの gem の直し。
- **report**: `doc/headless_s3/report/h3.md` (H2b も含める)、Japanese plain style (常体)。
