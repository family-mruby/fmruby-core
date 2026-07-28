# FMRuby BASIC 実装プロジェクト 共通指示書

この doc/fmrb_basic/ 以下は、Family BASIC 互換の BASIC インタプリタを
fmruby-core に本格実装するプロジェクトの、実装担当 AI 向け指示書である。
全体の背景・方針検討は `family_basic_compat_plan.md` を必ず先に読むこと。
言語仕様は `spec/family_basic_core_spec.md` と `spec/family_basic_v3_spec.md`
を正とする (実装中に仕様の疑義が出たら spec を直すのではなく、まず
report に疑義として記録しユーザ判断を仰ぐ)。

## 決定済み事項

- 互換目標: **V3 準拠** (compat_plan sec 3)。互換レベル L1-L3 必須、
  L4 個別判断、L5 (マシン語 CALL / 実機タイミング) 対象外。
- 実装方式: **案 A** = components/basic を土台に言語コアを書き直す
  (compat_plan sec 4.1)。タスク統合・メモリプール・gfx 経路は流用する。
- 実行形態: 「.bas アプリ」(spawner 起動) を主形態とする。
  直接モードコンソールの要否は Phase B4 で判断する。
- **実装言語: C++** (2026-07-27 ユーザ決定)。規約は下記。

## C++ コーディング規約 (必須)

1. **例外・RTTI を使わない** (IDF デフォルトのまま)。エラーは BASIC の
   エラーモデル (エラーコード + 発生行) を内部で持ち、C 境界は
   `fmrb_err_t` を返す。
2. **暗黙に heap を使う STL は禁止**: `std::string` / `std::vector` /
   `std::map` / iostream 等は使わない。`std::array` / `std::span` /
   `<algorithm>` / `enum class` / `constexpr` テーブルは推奨。
3. メモリは per-task プール (`fmrb_malloc` 系、fmrb_basic.c の既存方式)
   から取得する。グローバル `operator new` は使わず、必要なら
   placement new + プールで統一する。
4. **C API 境界を維持する**: spawner / fmrb_app.c / gfx 拡張との接点は
   `extern "C"` のヘッダ (`include/fmrb_basic.h`) に閉じ、内部だけ
   C++ にする (main/drivers/display_p4 の .cpp 群と同じパターン)。
5. **非トリビアルなグローバルコンストラクタ禁止** (静的初期化順序の
   罠を避ける)。テーブル類は constexpr で持つ。
6. C++ 標準は IDF v5.5 コンテナのデフォルト設定の範囲で **C++20 まで**の
   機能を使う。コンポーネント側で -std を個別に上げない。
7. インタプリタコア (core/) は **IDF・fmruby ヘッダに依存しない
   純粋 C++** とし、メモリ・入出力・tick はホストインタフェース
   (関数ポインタ or 抽象構造体) 経由で受け取る。これによりホスト g++
   だけでゴールデンテストを回せる (Phase B0 で機構を作る)。

上記に加えて fmruby-core/CLAUDE.md の一般規則
(fmrb_err.h 標準、fmrb_mem 利用、ソースコメントは英語、絵文字禁止、
esp_log 直接使用禁止 = fmrb_log.h 経由、Legacy コードは残さない) に従う。
doc/ に書く文書に外部資料名・ページ番号・URL を記載しない。

## メモリ設計原則 (必須。2026-07-27 ユーザ指示)

S3 の内蔵 RAM は逼迫しており、タスクスタックは内蔵 RAM に置かれる
(PSRAM スタックは S3 で撤退済み)。したがって:

1. **C スタックを深くしない**。BASIC の制御構造・データを C の関数
   コール階層で表現しない:
   - 式評価は再帰下降ではなく、明示的なオペランド/演算子スタック
     (インタプリタ状態内、プールから確保) を使う反復実装とする。
     ネスト深さは上限値を決めてエラーにする (上限は BASIC 側の
     カッコネスト等として妥当な値を report で決める)。
   - FOR/GOSUB/エラー処理のネストも同様にインタプリタ状態内の明示的
     スタックで持つ (C の再帰呼び出しに写像しない)。
   - ステートメント実行は「1 ディスパッチ = 浅い呼び出し数段」を保つ。
     ハンドラからさらに実行エンジンを再帰的に呼ぶ構造を作らない。
2. **データは限界まで PSRAM (per-task プール = fmrb_malloc 系) に置く**。
   行テーブル、文字列プール、変数・配列、シャドウバッファ、
   スプライト/MOVE 状態、テンポラリはすべてプールから確保する。
   内蔵 RAM に置いてよいのは小さなホットな作業変数 (ローカル変数)
   のみ。ファイルスコープの static 配列バッファは原則使わない。
3. **不変テーブルは flash (rodata) へ**。キーワード表・フォント・
   タイル・色対応表は constexpr / const で持ち、RAM へコピーしない。
4. **計測を残す**: 各フェーズの report に BASIC タスクのスタック
   high-water mark とプール消費量を記録し、スタックサイズを必要以上に
   盛らない (逆に、深さ上限で守られている根拠を書く)。

## リポジトリ内の主要パス

| パス | 内容 |
|---|---|
| `components/basic/` | BASIC コンポーネント (置き換え対象の仮実装) |
| `components/basic/basic/` | 仮実装の lexer/parser/runtime (Phase B1 で撤去) |
| `components/basic/include/fmrb_basic.h` | C API (維持する境界) |
| `components/basic/extension/fmrb_basic_gfx.c` | gfx 拡張 + BASIC コンソール (流用・拡張) |
| `main/app/fmrb_app_spawner.c` (`.bas` 判定) | .bas アプリの起動経路 |
| `main/app/fmrb_app.c` (`execute_basic_script`) | ロード・実行・出力コールバック接続 |
| `flash/app/demo/basic.app.bas` | 既存デモ (互換維持が必要) |
| `doc/fmrb_basic/spec/` | Family BASIC 仕様 (正) |
| `doc/fmrb_basic/reports/` | フェーズ報告書の置き場 |
| リポジトリルート `tools/` (family-mruby 側) | headless 検証 (dev_run_check.sh / fmrb_input.py / fmrb_screenshot.py) |

## ビルドと検証

- `rake build:linux` / `rake build:esp32` (S3 = NARYAv3)。lib/ 以下を
  編集したら `rake clean`、linux/esp32 切替時は `rake clean_all`。
- ゴールデンテスト: Phase B0 で `rake basic:test` (ホスト g++ でコアのみ
  ビルドして .bas + 期待出力を比較) を新設する。以降の全フェーズで
  これを green に保つ。
- 画面が絡むフェーズ (B2 以降) はルートの headless ハーネスで検証する。
  スクリーンショット比較よりも「テキスト画面シャドウバッファのダンプを
  debug 経路で吐いて diff」を優先する (B0 で設計、B2 で実装)。
- 音声・実機の最終確認はユーザが行う (headless では検証不能)。

## フェーズ構成と報告

| フェーズ | 指示書 | 内容 |
|---|---|---|
| B0 | `phase_b0.md` | 検証基盤 (ゴールデンテスト・C++ ビルド経路・PLAY 技術判断・コーパス) |
| B1 | `phase_b1.md` | 言語コア再構成 (L1) |
| B2 | `phase_b2.md` | テキスト画面 (L2) |
| B3 | `phase_b3.md` | ゲーム機能 (L3) |
| B3.5 | `phase_b3_5.md` | 実行ループ基盤 (kill/フォーカス修正) + エディタ RUN |
| B4 | `phase_b4.md` | 周辺・体験 (L4) |
| B4.5 | `phase_b4_5.md` | 機能実装完了 (FILTER/2bpp 多色/LIST 忠実化 + 既知差異確定) |
| B5 | `phase_b5.md` | 実機調整・品質確定 |

- 各フェーズ完了時に `reports/phase_bN_report.md` を書く
  (実施内容、判断とその根拠、残課題、次フェーズへの引き継ぎ)。
  長丁場のフェーズは `reports/phase_bN_progress.md` に途中経過を残してよい。
- フェーズ指示書に書かれていない設計判断が必要になったら、報告書に
  選択肢と推奨を書いてユーザに確認する。**指示書と spec の範囲内は
  自律で進めてよい**。

## ユーザ判断待ちの事項 (実装をブロックしない形で扱う)

1. ターゲット作品リスト (B0。リスト確定まではコーパスを自作サンプルで進める)
2. タイルセット/フォント絵柄の制作分担 (B2/B3。実装はプレースホルダ絵柄で進めてよい)
3. PLAY の実装深度 (B0 で技術調査して推奨を出し、ユーザが決める)
4. 直接モードコンソールの要否 (B4 冒頭で判断)
