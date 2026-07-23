# Phase 0 指示書: PoC・言語カバレッジ検証 (Go/NoGo 判断)

先に `00_common.md` を読むこと。ここでの作業は Linux (WSL2) のみで完結する。
fmruby-core のビルドや firmware への組み込みは一切行わない。
成果物はすべて `tool/spinel_poc/` (新規ディレクトリ) と
`doc/spinel_aot/reports/phase0_report.md` に置く。

## 目的

1. 対象 Ruby コード (カーネル / desktop / shell の代表部分) が Spinel で
   コンパイル・正動作するかを実証する。
2. mruby (PicoRuby) 比の速度メリットを実測する。
3. 32bit ビルドの成否を確認する (ESP32-S3 は 32bit / Xtensa のため)。
4. shell の IRB/Sandbox 境界の設計方針を確定する。
5. 以上から Go/NoGo を判断する材料を揃える。

## 前提知識

- Spinel は `/home/kishima/fmrb/family-mruby/tmp/spinel` にある。
  README.md、docs/limitations.md、docs/FFI.md を読むこと。
  使い方: `./spinel app.rb` (実行ファイル生成) / `-c` (C 生成のみ) /
  `-E app.rb` (コンパイルして即実行)。テストは CRuby を oracle とした
  出力比較方式。
- 検証対象の Ruby ソース:
  - `main/prebuild_scripts/kernel/fmrb_kernel/input_router.rb` (~345 行、
    最重要ホットパス。HID バイナリイベントのデコード、ウィンドウ座標
    変換、ドラッグ/リサイズ状態機械)
  - `main/prebuild_scripts/kernel/fmrb_kernel/window_manager.rb` (~90 行、
    ウィンドウヒットテスト、z-order)
  - `main/prebuild_scripts/kernel/system_desktop/launcher.rb` (ソート・
    レイアウト処理の代表)
  - `main/prebuild_scripts/default_app/shell/shell_commands.rb` (文字列
    処理の代表)、`shell_io.rb` (`def puts(*args)` の splat、`$stdout`)
- これらは C バインディング (`FmrbKernel` のメソッド群、`Machine.board_millis`、
  `Log`、`FmrbConst`、`MessagePack`) を呼ぶ。PoC ではこれらを Ruby の
  スタブに置き換える。

## タスク

### T0-1: Spinel のビルドと現状確認 (半日)

1. `cd /home/kishima/fmrb/family-mruby/tmp/spinel && make deps && make`。
2. `make test` を実行し、パス数を記録 (基準値: 1,744 本)。
3. `./spinel -e 'puts 42'` で動作確認。
4. 失敗がある場合は環境要因 (cc、prism 取得) かを切り分け、レポートに記録。

### T0-2: input_router 検証ハーネス (1-1.5 日)

`tool/spinel_poc/harness_input_router.rb` を作る。

1. **スタブ層**: 実コードが参照する外部 API を Ruby で偽装する。
   - `FmrbKernel` 相当: `_get_window_list` はウィンドウ矩形のフィクスチャ
     (Hash の配列) を返す。`_set_hid_target` / `_send_raw_message` /
     `_try_send_raw_message` / `_update_window_position` 等は呼び出しを
     記録するだけのスタブ (後で出力する)。
   - `Machine.board_millis`: 単調増加カウンタ (イベントごとに +1ms など、
     **決定的に**進める。実時刻は使わない)。
   - `Log.info` 等: 標準出力へ `[I] ...` 形式で出す。
   - `FmrbConst`: 実物 (`lib/add/picoruby-fmrb-const/` の C ソース) から
     必要な定数値を読み取り、Ruby の定数定義に書き起こす。
2. **実コードの取り込み**: `input_router.rb` と `window_manager.rb` を
   できるだけ**無改変で** `require_relative` またはファイル連結で取り込む
   (Spinel は require_relative をパース時にインライン展開する)。
   mixin (`InputRouterMixin` 等) を include するテスト用クラスを定義し、
   必要な ivar (@windows キャッシュ等) をスタブ初期化する。
   改変が必要になった箇所は**全て記録**する (Go/NoGo 判断材料)。
3. **入力データ**: HID イベントの 6 バイトバイナリ形式 (実物の形式は
   input_router.rb のデコード部と `fmrb_hid_event.h` を読んで合わせる) を
   Ruby で合成する決定的ジェネレータを書く。シナリオ:
   move 連打 (マウス洪水)、click (ヒット/ミス)、ウィンドウドラッグ
   (down→move xN→up)、リサイズ、タイトルバー以外のクリック透過。
   合計 10 万イベント以上。
4. **出力**: 各イベント処理後の観測可能状態 (ヒットしたウィンドウ id、
   ドラッグ状態、スタブ呼び出しログの要約) を決定的な形式で puts する。
   末尾に集計 (イベント数、状態遷移数) を出す。
5. **一致確認**: 同一ハーネスを
   (a) CRuby: `ruby harness_input_router.rb`
   (b) Spinel: `./spinel harness_input_router.rb && ./harness_input_router`
   で実行し、標準出力が **byte 単位で一致**することを diff で確認。
6. **mruby でも実行**: picoruby のホスト向けバイナリで同じハーネスを流す。
   バイナリは `components/picoruby-esp32/picoruby/bin/` を確認し、
   なければ `rake host:build` (fmruby-core) で作れるか確認する。
   どうしても用意できない場合は、gem 等の一般 mruby ではなく
   「mruby 比較は Phase 2 の実機環境比較に委ねる」としてレポートに明記。

### T0-3: 性能計測 (半日)

1. 外部計測を基本とする: `time` コマンド (または `hyperfine` があれば
   それ) で CRuby / mruby / Spinel の同一ハーネス実行時間を 3 回計測し
   最良値を採る。出力の puts が支配的にならないよう、計測用モードでは
   出力を集計のみに切り替えるフラグをハーネスに持たせる (`ARGV[0]`)。
2. 言語内計測: 1 イベント処理あたりの最大時間 (max latency) を、
   CRuby では `Process.clock_gettime(Process::CLOCK_MONOTONIC)`、
   Spinel では `Time` で計測 (Spinel の Time 対応状況は README 参照。
   使えない場合は外部計測のみでよい)。
3. 記録する数値: 総実行時間 (3 実装)、イベント/秒、Spinel/CRuby 比、
   Spinel/mruby 比 (取れた場合)、最大レイテンシ。

### T0-4: desktop / shell 代表コードのコンパイル可否 (1 日)

`tool/spinel_poc/coverage/` に小さな検証スクリプト群を作る。それぞれ
実コードから該当ロジックを抜粋し (スタブ付き)、`./spinel -c` が通るか、
`-E` での実行結果が CRuby と一致するかを確認する。

1. `cov_launcher.rb`: launcher.rb のアイコンソート (`sort {|a,b| ...}`)、
   グリッドレイアウト計算。
2. `cov_shell_strings.rb`: shell_commands.rb 由来の文字列処理
   (split、コマンドライン解析、ljust 等の整形)。
3. `cov_shell_io.rb`: `def puts(*args)` の splat 定義、`$stdout` への委譲、
   `$stdout.respond_to?(:flush)` パターン。
4. `cov_i18n.rb`: i18n.rb の文字列テーブル参照 (Hash 定数 + シンボルキー)。
   ※ 日本語文字列 (UTF-8) を含むので、Spinel の UTF-8 前提で問題ないか
   出力一致で確認する。
5. `cov_binary.rb`: `String#setbyte/getbyte/<<` によるバイナリ組み立てと
   解析 (audio_handler.rb / input_router.rb のパターン)。
6. `cov_exceptions.rb`: begin/rescue/ensure/retry、カスタム例外クラス、
   ネストした rescue (fmrb_kernel.rb のトップレベル rescue パターン)。
7. `cov_mixin_const.rb`: mixin 内の定数参照 (input_router.rb 冒頭コメントに
   ある「mixin 内の裸定数はその mixin のスコープでしか解決されない」
   パターンを再現し、Spinel の定数解決が mruby と同じ結果になるか確認)。
8. 通らなかった構文は `tools/spinel-reduce` (Spinel 同梱) で最小再現に
   切り詰め、`tool/spinel_poc/coverage/UNSUPPORTED.md` に「最小再現 /
   エラー内容 / 回避書き換え案」を列挙する。

### T0-5: pure-Ruby MessagePack サブセット (1 日)

`tool/spinel_poc/msgpack_pure.rb` に `MessagePackPure` モジュールを実装。
将来カーネルの `MessagePack.pack/unpack` を置き換えるもの。

1. 対応型 (fmruby の VM 間メッセージで使う範囲):
   nil / false / true / 整数 (positive fixint, negative fixint, int8/16/32/64,
   uint8/16/32) / float64 / 文字列 (fixstr, str8/16/32) /
   バイナリ (bin8/16/32) / 配列 (fixarray, array16/32) /
   マップ (fixmap, map16/32)。**マップのキーは文字列のみ**
   (fmrb_kernel.rb:102 付近の「VM 間はシンボルでなく文字列キー」の規約)。
2. 実装は `String#getbyte` / `setbyte` / `<<` ベース。エンディアンは
   msgpack 仕様どおり big-endian。浮動小数は
   `[v].pack("G")` 相当が Spinel で使えるか確認し、不可なら
   ビット操作で実装 (Spinel の pack 対応は要確認)。
3. テスト `tool/spinel_poc/test_msgpack.rb`:
   - 固定テストベクタ (msgpack 仕様書の例 + 手計算のバイト列) との一致。
   - roundtrip (pack → unpack == 元データ) を代表 30 ケース以上。
   - CRuby に `msgpack` gem があれば相互検証 (pack 結果のバイト一致)。
     なければ固定ベクタのみでよい。
   - CRuby と Spinel の両方でテストを実行し全パス。
4. mruby (picoruby) でも同一ソースが動くか確認 (デュアルビルド原則)。
   実行環境が無ければ「構文上 mruby 互換の範囲のみ使用」を目視確認し
   レポートに明記。

### T0-6: 32bit (-m32) ビルド検証 (半日)

1. `gcc -m32` が使えるか確認 (`gcc -m32 -x c /dev/null -o /dev/null` など)。
   multilib 不足なら `sudo apt-get install gcc-multilib` をユーザに依頼
   するか、i386 の Docker コンテナ (例: `i386/debian`) で代替する。
2. Spinel のランタイムとハーネスを 32bit でビルド:
   - `./spinel harness_input_router.rb -c` で C を生成。
   - ランタイムを `-m32` で再ビルド (Makefile の CFLAGS を override
     できるか確認。できなければ手動で `gcc -m32 -O2 -c lib/sp_*.c` +
     `ar rcs`)。regexp/fiber/bigint 等ハーネスが使わないものは除外可。
   - 生成 C を `gcc -m32 -O2 -Ilib generated.c libspinel_rt32.a -lm`。
3. 32bit バイナリの出力が 64bit 版・CRuby と一致するか diff。
4. Spinel 自身のテストスイートの一部 (`test/` から整数演算・文字列・
   Hash・例外あたりを 30 本程度サンプル) を 32bit で回し、差異を記録。
   ※ 全 1,744 本を回せる仕組みが作れるならそれが望ましい。
5. 失敗した場合: 原因を特定し (`mrb_int` が intptr_t で 32bit になる
   ことに起因する桁あふれ、ポインタ/uint64 変換等)、修正パッチ案を
   レポートに書く (この Phase では Spinel を修正しない。記録のみ)。

### T0-7: shell IRB/Sandbox 境界の評価 (半日)

shell の Spinel 化は**オプション** (ユーザ決定: 難しければ対象から外し
mruby のまま残す)。このタスクの目的は「やるかどうか」の判断材料作りで
あり、実装コミットではない。

1. `main/prebuild_scripts/default_app/shell.app.rb`、`shell/shell_irb.rb`、
   Sandbox の C 実装 (`main/app/fmrb_app.c` の FMRB_LOAD_MODE 周辺と
   picoruby の Sandbox クラス) を読む。
2. 「Spinel 化する部分 (UI・行編集・スクロール・組み込みコマンド)」と
   「mruby に残す部分 (IRB 評価、.toml なしスクリプトの in-process 実行)」
   の境界を、実際のメソッド/クラス名レベルで列挙した表を作る。
3. C シムに必要な API 案 (例: `fmrb_spx_sandbox_exec(src, len)` +
   出力ストリームの poll 取得) と、概算工数・リスクを書く。
4. **推奨判定を書く**: 「shell を Spinel 化する価値がある / mruby のまま
   残すべき」のどちらかと根拠 (shell の性能課題の有無、境界の複雑さ)。
   最終判断はユーザが行う。
5. 成果は `doc/spinel_aot/reports/phase0_report.md` の一節として記載。

## 受け入れ基準 (Go/NoGo)

以下を全て満たせば Go:

1. T0-2: input_router + window_manager が (記録済みの軽微な書き換えの
   範囲で) Spinel でコンパイル・実行でき、CRuby と出力が完全一致する。
   「軽微」の目安: 変更行数が対象コードの 10% 未満、かつ意味を変える
   書き換え (アルゴリズム変更) がないこと。
2. T0-3: Spinel が mruby 比 2x 以上 (mruby 計測不能時は CRuby 比 1x 以上。
   mruby は CRuby より大幅に遅いため、CRuby 同等以上なら実用上 Go)。
3. T0-4: 検証 8 本のうち、回避不能 (書き換え案が出せない) な未対応構文が
   ゼロ。
4. T0-5: msgpack サブセットが CRuby / Spinel 両方で全テストパス。
5. T0-6: 32bit で出力一致 (フォークの fmrb-dev ブランチに修正をコミットして
   一致させた場合も可。または、失敗が「修正パッチ案を書ける具体的バグ」に
   限定されている)。
6. NoGo の場合: レポートに理由と、代替案 (ホットパスの手書き C 化) への
   引き継ぎ情報を書く。

## 落とし穴・注意

- ハーネスは**決定的**にする (実時刻・乱数を使わない)。CRuby/Spinel/mruby
  の出力 diff が判定手段なので、非決定性があると比較不能になる。
- `Hash#each` の列挙順は挿入順 (CRuby 準拠) を仮定してよいが、mruby でも
  同じか気になる箇所は sort してから出力する。
- Float の文字列化は処理系差が出やすい。出力には Float を直接 puts せず、
  整数化 (例: `(v*1000).round`) して出す。
- `tmp/spinel` はフォーク (kishima/spinel) であり改修可能。ただし Phase 0 の
  目的は Go/NoGo 判断なので、**コミットして良いのは PoC の完遂に必要な
  最小修正のみ** (例: 32bit で発覚した単発バグの修正)。作業ブランチ
  `fmrb-dev` (無ければ master から作成) にコミットし、`make test` を通し、
  レポートに全件列挙する。設計変更級 (ライブラリモード等) は Phase 1 で行い、
  ここでは必要性の記録に留める。
- fmruby-core 側の既存ファイルも変更しない (読むだけ)。新規ファイルは
  `tool/spinel_poc/` と `doc/spinel_aot/reports/` のみに置く。

## 完了レポートに含める事項

`doc/spinel_aot/reports/phase0_report.md` (日本語):
- 各タスクの結果 (パス/フェイル、数値、変更が必要だった箇所の一覧)
- 性能計測表 (CRuby / mruby / Spinel、絶対時間と倍率)
- UNSUPPORTED.md の要約と回避方針
- 32bit 検証結果と (あれば) 修正パッチ案
- IRB/Sandbox 境界表
- Go/NoGo の判定と根拠
