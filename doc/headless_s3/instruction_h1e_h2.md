# ヘッドレス S3 H1e・H2 指示書: 5 MB に余裕を作り、WiFi で開発の流れを回す

> 状態: 指示 | 更新: 2026-10-12 | H1e: ATOMS3R の構成の既定を決め、日本語のフォントを外す一般のビルドの選択肢を足して、5 MB の区画に余裕を作る。H2: AtomS3R を WiFi につなぎ、遠隔のファイル転送とアプリの起動で、再 flash なしの開発の流れを回す

前提: doc/headless_s3/plan.md、report/h0.md、report/h1.md (特に「決めること」と「H2 への申し送り」)、instruction_h1.md の決まり、fmruby-core/CLAUDE.md、doc/dev_remote_ctl/plan.md、親の CLAUDE.md (Tab5 遠隔の MCP ツール、シリアル)。作業はブランチ `feature/headless-h1` の続き。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 決まっていること (2026-10-12 ユーザ、A + B)

- **A**: `FMRB_APP_ENGINE_EDITOR=mruby` と `FMRB_FFT_SPINEL=0` を、ATOMS3R の構成の既定にする (コマンドラインで渡さなくてよい形。ボードの表など、今の既定の決め方に合わせる。環境変数で上書きできることは保つ)。
- **B**: 日本語のフォント (efontJA 16 / 12 / 12 太字) を外す、**全端末で使える一般のビルドの選択肢**を足す。名前は `FMRB_FONT_JA` (既定 1、0 で外す)。ATOMS3R は既定で 0。
  - 外したとき、`set_font(:ja, …)` (大きさを問わず) は美咲 8 ドットに置き換わる。`set_font` は選ばれたフォントを返す (既存の約束) ので、置き換わったことはアプリから分かる。エラーにはしない。
  - エディタ・デスクトップ・ヘルプなど、日本語のフォントを前提にしている所が、外した構成で落ちないことを確かめる (ATOMS3R では使わないものも含め、ビルドが通り、使う所で例外にならない)。
- 止まったときの画面 (STOPPED・理由・Reset to try again.・暗い赤) は今のままでよい (ユーザから変更の指示なし)。

## 2. 作るもの

1. **H1e**: A と B。ATOMS3R のファームの大きさと残り、内蔵 RAM の変化を測る。Retro・Tab5 のビルドと大きさが変わらない (既定の `FMRB_FONT_JA=1`) ことを確かめる。sim の標準構成も起動とエディタの 1 打鍵まで確かめる。
2. **H2**:
   - ATOMS3R で WiFi を使う: `wifi_auto_start = true`、WiFi の設定は `config/wifi.toml` (gitignore 済み。ユーザの手元にある。中身をコミット・文書に書かない) を今の S3 の流儀で入れる。BLE が無いので排他の問題は無いはず。
   - mDNS の名前 (`fmruby-XXXXXX.local`) で見つかることを確かめる。
   - dev_remote_ctl の `/fs/*`・`/app/*` で、アプリを置いて起動し、差し替えて起動し直す流れを回す (MCP の tab5_fs / tab5_app / tab5_screenshot に ip を渡して使う。遠隔の画面の取り込みが 128x128 で動くかも確かめる)。
   - 単一アプリの扱い: report/h1.md の申し送り (kill の後に `/app/launch` したアプリは単一アプリとして扱われず、落ちても起動し直されない。止まったときの画面が後から起動したアプリを隠す) について、開発の流れで困るかを確かめ、困るなら直し方の案を書く (仕様の選択になるなら案だけ)。
   - WiFi を使った状態の内蔵 RAM と、遠隔の転送の速さを測る。

## 3. 受け入れ条件

1. ATOMS3R がコマンドラインの追加なしでビルドでき、5 MB の区画に十分な余裕がある (目安: 1 MB 前後。実測を書く)。
2. `FMRB_FONT_JA=0` で `set_font(:ja, 16)` が美咲 8 ドットを返し、例外にならない。Retro・Tab5 は既定で今と同じ (大きさの差を書く)。sim の標準構成が起動し、エディタで 1 打鍵できる。
3. AtomS3R が WiFi につながり、mDNS の名前で見つかる。
4. PC からアプリを置いて起動し、書き換えて置き直して起動し直せる (再 flash なし)。遠隔の画面の取り込みの可否を書く。
5. WiFi を使った状態の内蔵 RAM の空き (M1 の行と周期のダンプ) を書く。H3 (zenoh) に回せる量の見込みを書く。
6. `rake test` が通る。

## 4. 範囲・決まり

- **触ってよいもの**: instruction_h1.md と同じ。ブランチ `feature/headless-h1` に続けてコミット。push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` は変えない)。
- **触らないもの**: 既存の sdkconfig / defaults (ATOMS3R の defaults は直してよい)、submodule、fmruby-graphics-audio、`config/wifi.toml` の中身 (読むだけ。コミットしない)。
- **端末**: AtomS3R は `/dev/ttyACM1` (ポートを明示する)。焼いてよい。P4-Nano (`/dev/ttyACM0`) は触らない。シリアルの capture が `/dev/ttyACM1` に開いている (MCP)。使い続けてよい。
- **端末の IP・mDNS の名前・MAC はリポジトリに書かない** (`192.0.2.x`、`fmruby-aaaaaa`)。
- **終わったら**: `build/` を Linux の標準構成 (x86-64) に戻す。AtomS3R には最後のファームを残し、WiFi につながった状態にしてよい。
- **止まる条件**: ここに無い仕様の選択、Retro・Tab5 の内蔵 RAM の増加、既存の sdkconfig を変える必要。
- **report**: `doc/headless_s3/report/h2.md` (H1e も含める)、Japanese plain style (常体)。
