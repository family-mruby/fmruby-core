# Phase B0 指示書: 検証基盤とコーパス整備

前提: `00_common.md` と `family_basic_compat_plan.md` を読んでいること。
spec/ は整備済み (残りは core_spec sec 17 の資料待ち項目のみ。暫定値で
進める)。B0 の目的は「B1 以降を高速に回すための検証基盤」と
「技術リスクの早期判断」で、**言語機能の実装はまだ行わない**。

## タスク

### T0-1: C++ ビルド経路の開通 (コアの器)

- `components/basic/core/` を新設し、純粋 C++ のスケルトンを置く:
  - `core/basic_core.hpp` / `core/basic_core.cpp`: 空に近いインタプリタ
    クラス (状態構造 + `load()` / `run()` スタブ) と、ホストインタフェース
    構造体 `basic_host_t` (メモリ確保/解放、1 文字出力、行入力、tick、
    エラー通知のコールバック群) の定義。00_common の規約 7 に従い
    IDF・fmruby ヘッダを一切 include しない。
  - `fmrb_basic.c` から placement new でコアを生成して呼ぶ接続は
    B1 で行う。B0 では CMakeLists に .cpp を追加してビルドが通ることの
    確認まで。
- 検証: `rake build:linux` と `rake build:esp32` (NARYAv3) の両方が通る。
  linux target で .cpp をビルドした前例がないため、ここで問題が出たら
  (コンパイラフラグ、リンク) 解決して report に記録する。

### T0-2: ホスト・ゴールデンテストランナー

- `components/basic/test/` を新設:
  - `test/runner/main.cpp`: コア (core/ のみ) をホスト g++ でビルドし、
    引数の .bas を実行して結果をテキストで stdout に出す CLI。
    ホストインタフェースは「出力 = stdout、入力 = 引数ファイル or 固定、
    tick = no-op、メモリ = malloc」の素朴な実装でよい (ここだけは
    ホスト専用なので malloc 可)。
  - `test/golden/`: `NNN_name.bas` + `NNN_name.expected` の組。
    ランナー出力と expected の完全一致で合否判定。
  - `test/run_golden.sh`: 全ケースを回して合否集計。
- Rakefile に `rake basic:test` を追加 (g++ でランナーをビルドして
  run_golden.sh を実行。docker 不要、ホストの g++ を使う)。
- B0 時点のコアは何も実行できないので、ケースは「空プログラムで
  正常終了」「構文エラーで所定のエラー文言」程度の 2-3 本を置き、
  機構が動くことを示す。ケースの拡充は B1 の受け入れ基準側で行う。

### T0-3: テキスト画面ダンプの debug 経路の設計 (実装は B2)

- B2 以降の検証は「28x24 シャドウバッファのテキストダンプを取得して
  diff」で行う (compat_plan sec 6 の方針)。B0 では設計だけ確定する:
  - 取得手段の候補: (a) BASIC タスクがファイル (/home 以下) にダンプを
    書き出すデバッグ命令を持つ、(b) 既存の remote debug / debug 経路に
    問い合わせコマンドを足す、(c) linux sim 限定で stdout に吐く。
  - headless ハーネス (ルート tools/) から扱いやすいこと、実機でも同じ
    経路が使えることを評価軸に、report で 1 案を推奨して確定する。
- ダンプ形式も決める: 28x24 の文字コード表現 (カナを含むので 16 進 or
  透過表記の混在ルール) + カラー属性の有無。

### T0-4: PLAY 実装深度の技術判断

- 音源経路の現状を調査する:
  - S3 (Retro): WROVER 側 APU エミュレータ。既存経路は FMSQ シーケンス
    (`load_fmsq` / `play_slot`、`main/prebuild_scripts/kernel/fmrb_kernel/
    audio_handler.rb`) と NSF 再生。
  - P4 (Modern): ローカル apu_emu (`main/drivers/audio_p4/`)。
- FMSQ の表現力 (チャンネル数、音長・テンポ・音量・ループ) を仕様/実装
  から棚卸しし、spec の PLAY 節 (MML: 矩形波 2 + 三角波 1、O/T/V 等) を
  満たせるか判定する。
- 結論として compat_plan sec 5.4 の a 案 (MML -> FMSQ 事前変換) で
  足りるか、b 案 (リアルタイム経路新設) が要るかを report で推奨する。
  BEEP は短い固定シーケンスで代替可能かも確認する。
  **最終決定はユーザ**。

### T0-5: ターゲット作品の棚卸しテンプレート

- ターゲット作品リストはユーザが選定する (リポジトリに作品コードは
  収録しない)。実装側の準備として:
  - `reports/corpus_inventory.md` に棚卸し表の雛形を作る
    (作品名 / 使用ステートメント / 使用関数 / POKE・PEEK アドレス /
    PLAY 使用 / 直接モード依存 / 想定互換レベル)。
  - 自作サンプル (権利上問題のないコード) を test/golden/ とは別に
    `test/samples/` として 2-3 本書く (L1 範囲: 数値・文字列・制御構造。
    B2 以降のフェーズで画面・ゲーム機能サンプルを追加していく)。
- ユーザから作品リストが来たら棚卸しを実施し、POKE 頻出アドレスを
  B4 の仮想メモリマップ範囲の入力にする。

### T0-6: 既存デモの回帰基準の記録

- `flash/app/demo/basic.app.bas` の現状の画面出力 (headless ハーネスで
  スクリーンショット) を取得し、reports に「B1 置換後も維持すべき挙動」
  として記録する。使用している構文 (CLS/CIRCLE/PRESENT 等の gfx 拡張を
  含む) の一覧も書く。

## 受け入れ基準

1. linux / esp32s3 の両ビルドに components/basic の .cpp が含まれて通る
2. `rake basic:test` がホストで走り、機構確認ケースが green
3. テキストダンプ経路・PLAY 深度の 2 つの技術判断が report に
   選択肢 + 推奨付きで書かれている
4. 棚卸しテンプレートと自作サンプルが存在する
5. `reports/phase_b0_report.md` 完成

## 報告

`reports/phase_b0_report.md`。技術判断 (T0-3, T0-4) は判断根拠を含めて
必ず記載する。ユーザ判断待ち事項はリストにして明示する。
