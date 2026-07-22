# Spinel による PreBuild コード AOT コンパイル化の実現性検討と実装計画

日付: 2026-07-18 (改訂: フォーク方針・Desktop/Shell 対象化・ランタイム
マルチインスタンス化を計画に組み込み)
対象: Spinel (Ruby AOT コンパイラ、フォークして改修) を用いた fmruby-core
PreBuild コード (カーネル / system_desktop / shell) の C 言語化による
高速化・GC 硬直の緩和

方針 (決定事項):
- Spinel は **フォークして自前で改修する** (ライブラリモード、マルチ
  インスタンス化、ESP32 対応)。汎用性のある変更は upstream へ PR する。
- 対象はカーネル VM に加えて **system_desktop を正式対象**とする。
  そのためランタイムのグローバル状態排除 (複数 Spinel プログラムの同居) を
  必須課題として計画に含める。
- **shell はオプション扱い**。IRB / Sandbox (動的評価) との連携が必要で
  ハイブリッド構成のコストが高いと判明した場合は Spinel 化の対象から
  外し、mruby のまま残してよい (2026-07-18 ユーザ決定)。
- **OS 側 PreBuild Ruby (カーネル / desktop 等の Spinel 対象コード) は
  メタプログラミング禁止を正式なコーディング規約とする** (eval、動的名の
  send、define_method、method_missing、ObjectSpace、動的リフレクション、
  Fiber、Thread)。性能を優先する (2026-07-18 ユーザ決定)。

## 1. 背景と目的

- Family mruby では、Window マネージャ・イベントハンドリング等の OS 機能を
  PicoRuby (mruby VM) 上の PreBuild コードで実装しているが、GC 硬直などの
  性能課題がある。実測でも input_router で 1 イベント 25ms 超の警告
  (`input_router.rb:299-302`)、desktop の `draw_foreground` が 30-50ms かかる
  ことが分かっており、resize プレビューを 10Hz に律速するなどの構造的な回避を
  行っている。
- Spinel は Ruby ソースを型推論付きで単一 C ファイルへ AOT コンパイルする
  コンパイラで、CRuby+YJIT 比で幾何平均 ~5.8x の高速化実績がある。
  PreBuild コード (カーネル系) を Spinel で C 化できれば、mruby VM の
  インタプリタコストと GC 硬直を大幅に削減できる可能性がある。

## 2. 調査結果サマリ

### 2.1 fmruby-core 側 (PreBuild コードの実態)

- カーネル VM 本体は `main/prebuild_scripts/kernel/` の約 1,160 行
  (fmrb_kernel.rb + window_manager / input_router / app_lifecycle /
  audio_handler mixin)。system_desktop を含めると約 5,400 行。
- 各 mruby プログラムは「専用 FreeRTOS タスク + 専用 VM + 隔離ヒープ」で動作し、
  VM 間通信は C の FreeRTOS キュー経由のバイナリメッセージ (msgpack / 独自
  バイナリ) のみ。Ruby オブジェクトの共有はない。
  → **カーネル VM は境界が明確で、タスクごと差し替え可能**。
- カーネル Ruby が呼ぶ C API は固定的な少数セット:
  `FmrbKernel` (~24 メソッド)、`MessagePack.pack/unpack`、`Log`、`FmrbConst`、
  `Machine.board_millis`、`I2C`/RTC。desktop 側はさらに `FmrbApp`/`FmrbGfx`
  (計 ~70 メソッド)。
- **言語機能の使用は極めて静的**。eval / send(動的名) / method_missing /
  define_method / ObjectSpace / Fiber / case-in はカーネル・PreBuild コードに
  一切存在しない (grep で確認済み)。使用しているのは: クラス + module include
  mixin、ブロックイテレータ (each/map/select/times/sort)、例外
  (begin/rescue/ensure)、シンボル、文字列のバイト操作 (setbyte/getbyte/<<)、
  キーワード引数、グローバル変数、リテラル名の respond_to? のみ。

### 2.2 Spinel 側 (組み込み適合性)

適合する点:

- 上記の言語機能はすべて Spinel のサポート範囲内。リテラル名の
  `respond_to?` はコンパイル時に解決される (`src/analyze_pass.c:4715`)。
  `setbyte`/`getbyte` は char 配列直接アクセスに最適化される。
- GC は精密型 (root は `__attribute__((cleanup))` による明示 root スタック) で
  **C スタックを走査しない** → FreeRTOS の小さいタスクスタックと相性が良い。
- 32bit 対応は設計済み: `mrb_int` は `intptr_t` (32bit ターゲットでは int32)
  (`lib/sp_types.h:65-77`)。ただし 32bit CI 実績はなく要検証。
- 単一スレッドモードでは pthread / スケジューラ / SIGURG プリエンプションを
  一切リンクしない (Thread 未使用ならランタイムはシングルスレッド構成)。
- ランタイムは ar アーカイブ + `--gc-sections` で未使用分が除去され、
  必須コア (sp_gc/sp_alloc/sp_core/sp_string 等) は小さい。

ギャップ (対応が必要な点):

| # | ギャップ | 影響 | 対応方針 |
|---|---|---|---|
| G1 | ライブラリモードがない: 常に `int main()` を生成 (`src/codegen.c:5561`)、メソッドは file-static | 組み込み不可 | **フォークに `--no-main --entry <name>` を実装**。ブートストラップ列 (SP_GC_SAVE 等) は init 関数へ移す。汎用機能なので upstream PR 候補 |
| G2 | C→Ruby コールバック非対応 (FFI は Ruby→C のみ, `docs/FFI.md:199`) | `_spin` が C から `msg_handler` を mrb_funcall する現構造が使えない | **制御反転**: Ruby 側メインループが FFI でメッセージを poll し、Ruby 内でディスパッチする構造に変更 (mruby 版と両立可能な書き方にする) |
| G3 | mruby 連携機構なし (`mruby_shim.h` は bigint 借用シムであり相互運用ではない) | Spinel 化した VM と mruby VM は完全に独立したランタイム | 現状の設計がそもそも「VM 間はバイナリメッセージのみ」なので問題にならない。境界はバイト列で維持 |
| G4 | `MessagePack` C バインディングが使えない (FFI は構造化値を返せない) | msg ペイロードの pack/unpack | pure-Ruby msgpack サブセットを書いて Spinel でコンパイル (poly Hash / バイト列操作はサポート範囲)。mruby 側でも同一ソースを使えるとなお良い |
| G5 | Xtensa 向け Fiber/Thread 非対応 (asm は x86-64/arm64 のみ、fallback は ucontext + mmap) | Fiber/Thread/Enumerator.new 使用不可 | カーネルコードは未使用なので回避可能。**Spinel 化対象コードでは Fiber/Thread/Enumerator.new を禁止** |
| G6 | ランタイム状態がプロセスグローバル (sp_gc_roots、ヒープ管理、例外スタック等) | 1 ファームウェア内で Spinel プログラムは事実上 1 個。Desktop/Shell まで対象にするなら**必須で解決** | **フォークでランタイムをマルチインスタンス化** (詳細は 2.4 節)。方式は「グローバルを `sp_ctx` 構造体へ集約 + カレントコンテキストポインタ 1 本」を本命とし、暫定として objcopy シンボルプレフィックス方式 (ランタイム複数リンク) も用意 |
| G6' | Shell の IRB / Sandbox は「実行時に Ruby ソースをコンパイル・評価」する機能であり、AOT では原理的に実現不可 | shell を Spinel 化すると in-process の IRB / .toml なしスクリプト実行が動かなくなる | 案はハイブリッド構成 (shell 本体は Spinel 化、IRB/スクリプト評価は C シム経由で mruby Sandbox に委譲)。ただし **shell はオプション**: ハイブリッドのコストが見合わなければ shell は mruby のまま残す (ユーザ決定済み)。判断は Phase 0 の境界設計と Phase 2 の性能データで行う |
| G7 | ESP32 非対応箇所: 必須ファイル `sp_gc.c:93` の `malloc_trim(0)`、既定 `SP_GC_STACK_MAX=65536` (32bit で 256KB BSS)、GC トリガ既定 256KB、アロケータフックなし (calloc/free 直叩き) | ESP-IDF ビルド不可 / RAM 超過 | malloc_trim ガード、`-DSP_GC_STACK_MAX=2048~8192`、閾値の縮小、calloc/free を fmrb_mem (heap_caps, PSRAM) へリダイレクトするビルドシム |
| G8 | リモートデバッガ (feature/vm-remote-debug) は mruby VM 前提 | Spinel 化したカーネルはリモートデバッグ対象外になる | Spinel 化部分は生成 C + gdb (Linux) / JTAG でのデバッグに移行。mruby 版とのデュアルビルドを維持してデバッグ時は mruby 版を使う運用も可 |

### 2.3 期待できる効果と限界

- input_router / メッセージディスパッチのような「Ruby 実行が支配的」なパスは
  大幅な短縮が見込める (Spinel のベンチではインタプリタ比 10-50x のものもある。
  mruby VM は CRuby インタプリタよりさらに遅いため、伸び代は大きい)。
- GC 硬直: Spinel も mark-sweep STW だが、(1) 値型昇格・文字列最適化で
  割当量自体が激減する、(2) マーキングがネイティブで速い、ため停止時間は
  桁で縮む見込み。ただしゼロにはならないので Phase 2/3 で必ず実測する。
- desktop の `draw_foreground` 30-50ms は描画転送 (ホスト/子マイコン側) の
  コストを含むため、Ruby の AOT 化だけでは全部は解決しない可能性がある。
  → desktop / shell は正式対象とするが、着手順はカーネルで基盤・効果を
  実証してからとし、Phase 2 の計測で描画系の Ruby 比重を定量化しておく。
- 代替案との比較: input_router 等のホットパスだけを手で C (mrbgem) に
  書き直す方が初期コストは低い。ただし Ruby を正とした保守性・今後の
  機能追加を考えると、Spinel パイプラインを一度整備する価値はある。
  Phase 0 の PoC 結果が悪ければ手書き C 案へフォールバックする。

### 2.4 ランタイムマルチインスタンス化の設計方針 (G6)

カーネル / desktop / shell の 3 プログラムを同一ファームウェア内の別
FreeRTOS タスクとして同居させるための改修。フォークで実施する。

- **本命: コンテキスト構造体化**
  - ランタイムのグローバル状態 (オブジェクトヒープ・文字列ヒープの管理変数、
    `sp_gc_roots[]`、GC 閾値、例外 jmp_buf スタック、シンボル動的プール、
    乱数状態等) を `sp_ctx` 構造体に集約し、アクセスは
    `SP_CTX()->field` 形式のマクロ経由に機械的に置換する。
  - カレントコンテキストの取得は 1 本のポインタで行い、プラットフォームで
    実装を切り替える:
    - Linux: `__thread sp_ctx *` (pthread TLS)。
    - ESP-IDF: `vTaskSetThreadLocalStoragePointer` /
      `pvTaskGetThreadLocalStoragePointer` (FreeRTOS タスクローカル
      ストレージ)。**`__thread` の大きな配列は使わない**。ESP-IDF は
      .tbss/.tdata の合計サイズを全タスク (Wi-Fi や idle 含む) の TLS 領域
      として複製するため、ポインタ 1 本以外を TLS に置くと全タスクの RAM を
      浪費する。
  - `sp_ctx` と各ヒープ・root スタックはタスク起動時に
    `sp_instance_create(config)` で確保 (サイズはプログラムごとに設定可能:
    kernel は小さく、desktop は大きく)。
  - この改修は upstream の「複数インタプリタ埋め込み」ニーズにも合致するので
    PR 候補。ただし upstream 側の受け入れに依存しないよう、フォークを正とする。
- **暫定: シンボルプレフィックス方式** (コンテキスト化完了までのつなぎ)
  - プログラムごとにランタイム `.a` を `objcopy --prefix-symbols` で
    複製リンクする。改修ゼロで同居できるが、フラッシュにランタイムが
    複数載る。必須コアは小さいため 3 部でも増分は許容範囲の見込みだが、
    生成 C 側のシンボル (SPS_ シンボル定数等) との整合に注意。
  - Phase 2 (カーネル単独) はこの方式すら不要 (1 インスタンス)。

## 3. 結論 (実現性評価)

**条件付きで実現可能。カーネル → desktop の順で段階導入する。
shell はオプション (難しければ mruby のまま残す)。**

- 言語機能面の適合性は非常に高い (動的機能を使っていないため)。
  desktop も同じ規律で書かれており対象にできる。唯一の例外は
  shell の IRB / Sandbox (動的評価) で、shell を対象化する場合は
  mruby 側に評価を残すハイブリッド構成が必要 (G6')。コストが
  見合わなければ shell 自体を対象から外す。
- 主な工数は、フォークでのライブラリモード追加 (G1)、ランタイムの
  マルチインスタンス化 (G6)、FFI シム層と制御反転 (G2)、ESP-IDF/Xtensa への
  ランタイム移植 (G5/G7) にある。
- 最大の技術リスクは「Spinel の 32bit/Xtensa 実績のなさ」と「コンパイラ自体の
  成熟度 (silent miscompile)」。前者は Phase 0 の -m32 検証で先行確認し、
  後者は mruby 版とのデュアルビルド + 出力比較テストで抑える。
- desktop の描画コスト (draw_foreground 30-50ms) は転送側の比重が不明なため、
  Phase 2/3 のプロファイルで Spinel 化の効果を確認してから desktop 実装に
  入る (効果が薄い場合も入力・レイアウト処理の高速化と GC 硬直低減は残る)。

## 4. 実装計画

実装担当 AI 向けの詳細指示書は `doc/spinel_aot/` にある
(00_common.md が共通ルール、phase0.md - phase5.md が各フェーズの
作業手順・受け入れ基準・検証方法)。本節はその要約である。

対象範囲: カーネル VM → (マルチインスタンス化) → system_desktop
(+ オプションで shell) → ESP32-S3 実機、の順に進める。カーネルで基盤と
ツールチェーンを確立してから規模の大きい desktop に進むことで、
リスクを前倒しで消化する。

### Phase 0: PoC・言語カバレッジ検証 (Linux のみ、目安 3-5 日)

ゴール: 「対象 Ruby コードが Spinel でそのまま (または軽微な書き換えで)
コンパイル・正動作するか」「速度メリットが実測できるか」の Go/NoGo 判断。

1. `tmp/spinel` で `make deps && make && make test` (この時点ではフォーク
   作成前でよい)。
2. input_router + window_manager のロジックを、FmrbKernel API をスタブした
   単体ハーネス (合成 HID イベント列を流す) に切り出し、
   (a) CRuby、(b) mruby/picoruby、(c) Spinel でそれぞれ実行して
   出力一致とベンチマーク (イベント/秒、最大停止時間) を取る。
3. **desktop / shell の代表コードもカバレッジ検証に含める**:
   launcher のソート/レイアウト処理、shell_commands の文字列処理、
   `def puts(*args)` の splat、`$stdout` 等のグローバル変数、i18n の
   文字列テーブル参照あたりを抜粋してコンパイル可否を確認する。
4. pure-Ruby msgpack サブセット (nil/bool/int/float/str/bin/array/map) を
   書き、Spinel でコンパイルして往復テスト。
5. 生成 C + ランタイムを `-m32` でビルド・テスト実行し、32bit 問題を洗い出す。
6. shell の IRB / Sandbox 境界の設計スケッチ (どの機能が mruby 側に残るかの
   線引き) を作る。
7. Go/NoGo: コンパイル不可の言語機能が残る、または速度メリットが 2x 未満なら
   中止し、ホットパスの手書き C 化へ方針転換。

### Phase 1: フォーク整備 + 組み込み基盤 (Linux、目安 1-2 週)

1. **Spinel をフォーク**: GitHub 上に fork リポジトリを作成し、family-mruby
   側からはサブモジュールまたはツールとして参照する (リポジトリ追加・
   サブモジュール登録の git 操作はユーザ承認の上で実施)。upstream を
   remote に保持し、汎用的な変更 (ライブラリモード、マルチインスタンス化、
   32bit 修正) は upstream へ PR できる粒度でコミットを分ける。
2. **ライブラリモードをフォークに実装**:
   - `--no-main --entry <name>` を追加。`main()` の代わりに
     `int <name>(void)` を生成し、ブートストラップ (SP_GC_SAVE、
     必要なら sp_re_init 等) をその先頭で行う。
   - 生成 TU に利用側 C を差し込むための `--inject <file.c>` 相当
     (static メソッドが見える位置に bridge を置く) か、最低限
     エントリ 1 本のみ extern 化する方式で開始。
3. **spinel ランタイムコンポーネント** を fmruby-core に追加
   (components/ または main/ 配下、Linux ターゲット先行):
   - 必要メンバのみ (sp_gc/sp_alloc/sp_core/sp_string/sp_str/sp_array/
     sp_inspect/sp_format/sp_cold/sp_io 最小構成)。
   - `-ffunction-sections -fdata-sections` + `--gc-sections` を再現。
4. **FFI シム層 (fmrb_spx)**: カーネルが使う C API を素の C 関数として
   エクスポートするラッパを実装 (`_spin` 相当の poll 型 receive、
   `_send_raw_message`、window list 取得、`_set_hid_target` 等)。
   既存実装 (lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c) の
   mrb 依存部分を剥がして共通化する。バイナリは `:binstr`/`ffi_buffer`、
   構造体は個別 getter 関数で渡す。
5. ビルド統合: `rake` / CMake に「.rb → spinel -c → .c → コンパイル」の
   PreBuild ステップを追加。picorbc パスと並存させる
   (`FMRB_KERNEL_ENGINE=mruby|spinel` の切り替えフラグ)。

### Phase 2: カーネル VM の Spinel 化 (Linux で動作、目安 1-2 週)

この時点では Spinel インスタンスは 1 個 (カーネルのみ) なので、
マルチインスタンス化 (Phase 3) を待たずに進められる。

1. カーネル Ruby の再構成:
   - `_spin` の制御反転 (Ruby 側 poll ループ + Ruby 内ディスパッチ)。
     mruby 版でも同じ構造で動くよう、C バインディング側にも poll API を
     追加して**ソースは単一に保つ** (デュアルビルド原則)。
   - MessagePack 呼び出しを Phase 0 の pure-Ruby 実装へ置換。
   - FmrbConst 等の定数は Spinel 用に ffi_const / 生成定数ファイルで供給。
2. カーネルタスク起動を差し替え: `fmrb_app_spawn` のカーネル起動経路に
   「ネイティブエントリ呼び出しモード」を追加。
3. 検証: リポジトリルートの自律検証ツール (dev_run_check.sh +
   fmrb_input.py) で起動・ウィンドウ操作・ドラッグ/リサイズの回帰確認。
   mruby 版とのイベントレイテンシ / 最大停止時間 / メモリ使用量を比較計測。
   **この計測結果が desktop / shell の効果見積りの基礎データになる。**

### Phase 3: ランタイムのマルチインスタンス化 (フォーク改修、目安 1-2 週)

Desktop / Shell 対象化の前提となる G6 の解決。設計は 2.4 節のとおり。

1. ランタイムのグローバル状態の洗い出し: `lib/sp_alloc.c` のヒープ管理、
   `lib/sp_gc.c`/`sp_gc.h` の root スタック、例外 jmp_buf スタック、
   シンボル動的プール、乱数状態、`$` グローバル変数の実体、生成 C 側の
   定数キャッシュ等を列挙し、状態を持つものを `sp_ctx` 構造体へ移す。
   読み取り専用テーブル (クラステーブル、SPS_ シンボル定数等) は
   グローバルのまま共有してよい。
2. `SP_CTX()` アクセサ導入: Linux は `__thread sp_ctx *`、ESP-IDF は
   FreeRTOS タスクローカルストレージポインタ。`sp_instance_create(config)` /
   `sp_instance_destroy()` を追加し、ヒープ閾値・root スタックサイズを
   インスタンスごとに設定可能にする。
3. 検証: Linux 上で「同一プロセス内の複数スレッドがそれぞれ独立の Spinel
   インスタンスを実行する」テストを Spinel のテストハーネスに追加し、
   ヒープ隔離・GC 独立性・例外独立性を確認。既存テスト (1,744 本) が
   シングルインスタンスで劣化しないことも確認する。
4. つなぎとして必要なら objcopy シンボルプレフィックス方式を先に動かし、
   Phase 4 の着手をブロックしない。

### Phase 4: system_desktop の Spinel 化 (+ オプション: shell) (Linux、目安 2-4 週)

1. **FFI シム拡張**: FmrbApp (~24) / FmrbGfx (~47) メソッドの C 関数
   ラッパを追加。描画系は既存 C 実装の薄いラッパなので機械的作業。
   `set_timer(&blk)` のようなブロック保持はコールバック不可 (G2) のため、
   Ruby 側でブロックを保持して poll ループから期限判定して呼ぶ構造に変える。
2. **system_desktop**: mixin 13 本 + 本体 (~4,200 行) を Spinel サブセットで
   コンパイル可能に調整し、フラグで mruby 版と切り替え可能にする。
   カーネルと合わせて 2 インスタンス同居の初実証。
3. **shell (オプション)**: 着手前にユーザと判断する。実施する場合は
   ハイブリッド構成 (UI・行編集・スクロール・組み込みコマンドを
   Spinel 化し、IRB / .toml なしスクリプトの in-process 実行は C シム
   経由で mruby Sandbox に委譲)。コストが見合わなければ shell は
   mruby のまま残す (共存は Phase 3 の成果でそのまま成立する)。
4. 検証: 自律検証ツールでデスクトップ操作 (メニュー、Launcher、ドラッグ、
   アプリ起動) の回帰確認と、mruby のまま残る shell / default_app が
   従来どおり動く共存確認。draw_foreground / on_update の実測比較で
   効果を定量化。

### Phase 5: ESP32-S3 ポート (目安 2-3 週 + 実機検証)

1. ランタイムの Xtensa ビルド: malloc_trim ガード、root スタック縮小
   (インスタンス設定で 2048-8192)、GC トリガ閾値縮小、calloc/free →
   fmrb_mem 系 (必要に応じ PSRAM/heap_caps) へのリダイレクト。
2. ESP-IDF コンポーネント化 (リンカフラグメントで --gc-sections 相当を確保)。
   ビルドは既存方針どおり lib/add・Rakefile 経由で管理。
3. まずカーネルのみ Spinel で実機起動 → 安定後に desktop (および
   対象化した場合は shell) を順次有効化。インスタンスごとのヒープ配置
   (内部 SRAM vs PSRAM) を調整。
4. 実機計測: イベントレイテンシ、GC 停止時間、内部 SRAM / PSRAM 使用量、
   フラッシュ増分。mruby ヒープが不要になる分の RAM 回収も確認。
5. 安定性: 長時間 soak (input_router のマウス洪水、アプリ起動/終了の
   繰り返し。shell を対象化した場合は shell/IRB 往復も)。

## 5. 主要リスクと緩和策 (再掲・優先順)

1. **Spinel の成熟度 / silent miscompile** → デュアルビルド維持、
   ハーネスで mruby 版との出力一致テストを CI 化。tools/ の
   spinel-doctor / spinel-reduce を活用。
2. **32bit/Xtensa 実績なし** → Phase 0 で -m32 先行検証、Phase 5 で
   実機検証。修正はフォークに取り込み、汎用分は upstream へ報告/PR。
3. **マルチインスタンス化の改修規模** → ランタイム全域に触るため、
   フォークの既存テスト全通過を必須ゲートにする。遅延した場合も
   objcopy プレフィックス方式で Phase 4 を先行できる逃げ道を確保。
4. **フォーク保守 (upstream 追従)** → 変更を「upstream PR 可能な汎用
   コミット」と「fmruby 固有コミット」に分離し、定期的に upstream を
   マージする。upstream は活発なため放置すると乖離コストが増える。
5. **GC 停止が期待ほど縮まない** → Phase 2 で必ず実測。改善が薄ければ
   ホットパス限定の手書き C 化へ切り替え (FFI シム層は流用可能)。
6. **保守の二重化 (サブセット制約)** → rubocop_spinel でサブセット逸脱を
   lint。制約対象はカーネル / system_desktop / shell 等の PreBuild 系のみ
   とし、ユーザアプリの Ruby には制約を課さない。
7. **shell のハイブリッド境界 (IRB/Sandbox)** → shell はオプション対象。
   Phase 0 で境界設計を先行し、コストが高ければ shell を対象から外して
   mruby のまま残す (この場合リスク自体が消滅する)。対象化する場合も
   mruby Sandbox は C 側サービスとして残り、ユーザスクリプトの互換性は
   維持される。
8. **デバッグ手段の変化** → mruby 版デュアルビルドを常に残し、
   リモートデバッガでの調査は mruby 版で行う。Spinel 版は生成 C を
   gdb (Linux) / JTAG (実機) でデバッグする。

## 6. 参考 (調査で確認した主な根拠)

- Spinel: README.md、docs/limitations.md、docs/FFI.md、docs/thread.md、
  `src/codegen.c:5561` (main 生成)、`src/codegen.c:746` (static 化)、
  `lib/sp_gc.h:62` (SP_GC_STACK_MAX)、`lib/sp_gc.c:93` (malloc_trim)、
  `lib/sp_fiber_ctx.h:26` (arch ゲート)、`lib/sp_types.h:65-77` (32bit)、
  `src/analyze_pass.c:4715` (respond_to? のコンパイル時解決)。
- fmruby-core: `main/prebuild_scripts/` 一式、
  `main/prebuild_scripts/compile_ruby_to_bytecode.cmake`、
  `main/app/fmrb_app.c` (bytecode ロード)、
  `lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c` (_spin / msg_handler)、
  `main/prebuild_scripts/kernel/fmrb_kernel/input_router.rb:299-302`
  (ホットパスの実測警告)。
