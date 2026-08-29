# FM-EDITOR オンデバイスデバッガ検討・実装方針

作成日: 2026-07-18
更新: 2026-07-22 — 当初「Modern 限定」としていたが、Retro (S3, 320x240) でも
有効化する方針にユーザ決定で変更 (画面縮小を許容)。UI 設計は sec 4.5 参照。
ステータス: 検討完了・未実装 (実装は別途担当が行う。本書はその引継ぎ資料)

## 1. 目的

VSCode リモートデバッグ (doc/remote_debug/vm_remote_debug_design.md, Phase 0-2 実装済み) の
デバッグコアを流用し、**FM-EDITOR を拡張して実機上で完結するデバッガ**を実装する。
ホスト PC なしで、デバイス単体で「エディタでソースを開く → ブレークポイントを
置く → アプリを起動して止める → 変数を見る」までを可能にする。

## 2. 検討結果 (実現性評価)

結論: **性能・メモリともに実現可能**。むしろリモート版でホスト側に置いた機能
(DAP 変換、pathMappings、combined.rb 行補正) が FM-EDITOR ユースケースでは
不要になるため、残作業はリモート版より少ない。

### 2.1 構成上の適合性

- 重い処理 (BP 判定・step・パーク・stack_trace・frame_vars) は既に
  デバイス側 C 実装 (`main/drivers/debug/`) にある。ホスト側は表示と
  DAP 変換のみ。オンデバイス化で新規に必要なのは「接続経路」と「UI」だけ。
- パーク方式 (doc/remote_debug/vm_remote_debug_design.md sec 5.3) は「対象 VM タスク
  だけが hook 内で停止し、他アプリは動き続ける」設計。FM-EDITOR は別 VM・
  別 FreeRTOS タスクなので、**ターゲット停止中もエディタ UI は普通に動く**。
  デバッガ UI として成立する前提が最初から満たされている。

### 2.2 メモリ評価

- アプリプールは PSRAM 上 (`g_ctx_pool`)。現状でも kernel + desktop +
  一般アプリが同時稼働 (実測 ps: 512KB プールに対し used 245〜415KB 程度)。
  エディタへのデバッグ UI 追加 (BP リスト、スタック表示、変数ペイン) の
  Ruby ヒープ増は数十 KB オーダーでプール内に収まる。
- C 側 per-VM デバッグコンテキスト (BP 表・キュー) は数 KB。リモート版で
  既に払っているコストで増分なし。
- debug_info (`-g` 相当) による irep 肥大 (1〜2 割程度) はリモート版と共通の
  コスト。attach するアプリのみに限定可能。
- Modern (ESP32-P4) は PSRAM 32MB でさらに余裕がある。

### 2.3 性能評価

- `code_fetch_hook` は attach 時のみ armed。非デバッグ時のオーバーヘッドは
  ほぼゼロ (hook NULL チェックのみ、既存設計どおり)。
- デバッグ操作は人間ペース (数コマンド/秒)。ローカルのキュー往復は ms 未満で、
  BLE の帯域・遅延懸念がまるごと消える。変数展開・ログ表示はリモート版より速い。

## 3. 役割分担ポリシー (決定事項)

| 項目 | FM-EDITOR 拡張デバッガ | VSCode 連携 (既存路線) |
|---|---|---|
| 対応ハード | **全ターゲット** (Modern はペイン併置、Retro は下部分割 UI) | Linux sim (TCP) + 実機 (BLE) |
| デバッグ対象 | **一般アプリのみ** (flash/app 以下のオンデバイスコンパイル .rb) | 一般アプリ + **カーネル側機能** (kernel/default_app の combined ビルド含む) |
| ソースマッピング | 不要 (行番号がファイルと一致) | pathMappings + combined.map.json 補正 (実装済み) |
| トランスポート | ローカル (同一デバイス内) | TCP (実装済み) / **BLE (Phase 3、計画継続)** |

- **全ターゲット対応 (2026-07-22 変更)**: 当初は「画面が広い Modern のみ」と
  していたが、Retro でも画面縮小を許容して有効化する。Modern (Tab5 720x1280)
  はスタック/変数ペインを併置、Retro (320x240) は下部分割 + 表示切替の縮小 UI
  (sec 4.5)。エディタは 6x8 フォントで 320x240 でも 53 桁 x 30 行が取れるため、
  テキストベースのデバッグペインは成立する。HW ターゲットによるビルドゲートは
  行わず、レイアウトのみ画面サイズで切り替える。
- **一般アプリ限定の理由**: エディタ自身・system_desktop をパークすると UI /
  ウィンドウ管理が止まる。カーネル側 (system_desktop, 組み込みアプリ) の
  デバッグは従来どおり VSCode 連携で行う。
- **BLE (Phase 3) は引き続き必要**: 実機上のカーネル側デバッグ・実機検証は
  VSCode 連携で行うため、doc/remote_debug/vm_remote_debug_design.md の Phase 3 (BLE
  トランスポート) の計画は本機能によって置き換えられない。両方実装する。

## 4. アーキテクチャ方針

### 4.1 接続方式: mrbgem 直結 (推奨) vs ローカル transport

案A: ローカル transport 追加
: `fmrb_debug_transport_ops_t` (main/drivers/debug/fmrb_debug_transport.h) の
  3 本目としてインメモリのキューペア transport を実装し、debugd タスクの
  コマンドループをそのまま使う。エディタ側は msgpack ボディを組んで送る。
  プロトコル層 (fmrb_debug_proto.c) を完全流用できるが、Ruby 側に msgpack の
  組立/解析が必要になり、往復も一段増える。

案B: mrbgem 直結 (推奨)
: 新規 mrbgem (例: `picoruby-fmrb-debug`, Ruby API `FMRB::Debug`) が
  `fmrb_debug_ctx_*` (main/drivers/debug/fmrb_debug_ctx.h) を直接呼ぶ。
  msgpack もソケットも介さない。inspect 系の応答 (stack_trace / frame_vars /
  expand) は msgpack ボディで返る仕様なので、**gem の C 実装内で
  components/msgpack-esp32 を使ってデコードし、Ruby の Hash/Array にして返す**。
  Ruby 側は素のオブジェクトを受け取るだけになり最も軽い。

案B を推奨する。ただし以下のスレッド前提の整理が必須:

- `fmrb_debug_ctx_*` のコメントにある「called on the debugd task」は
  シングルクライアント前提 (inspect の内部バッファは次の inspect 呼び出しまで
  有効、`fmrb_debug_ctx_poll_event` はシングルコンシューマ) の意味。
  ctx 層自体はキューベースなので呼び出し元タスクがエディタ VM タスクでも
  動作するが、**リモート (debugd) とローカル (エディタ) が同時に触ると壊れる**。
- 対策: `fmrb_debugd` に**セッションオーナー概念** (none / remote / local) を
  追加する。local オーナー取得中は TCP/BLE の attach を拒否し、debugd タスクは
  event poll を止めて (またはローカルへ転送して) エディタ側 gem が
  `fmrb_debug_ctx_poll_event` を消費する。逆も同様。
  API 例: `fmrb_debugd_acquire_local() / fmrb_debugd_release_local()`。

### 4.2 Ruby API スケッチ (案B)

```ruby
FMRB::Debug.acquire            # session owner = local (失敗: リモート接続中)
FMRB::Debug.ps                 # [{pid:, name:, state:, mem_used:, ...}]
FMRB::Debug.attach(pid)
FMRB::Debug.bp_set(pid, "/app/usr/foo.app.rb", 42)  # -> bp_id
FMRB::Debug.continue(pid); FMRB::Debug.step_in(pid) # step_over/step_out/pause
ev = FMRB::Debug.poll_event(timeout_ms)  # {type:, pid:, reason:, file:, line:} or nil
FMRB::Debug.stack_trace(pid)   # [{frame:, file:, line:, method:}]
FMRB::Debug.frame_vars(pid, frame_idx)  # [{name:, value:, ref:}]
FMRB::Debug.expand(pid, ref)
FMRB::Debug.detach(pid); FMRB::Debug.release
```

- `poll_event` は必ずノンブロッキング〜短タイムアウトで呼ぶ。エディタの
  UI ループ内でのブロッキング待ちは禁止 (UI が固まる)。
- gem のビルド組み込みは components/picoruby-esp32/CMakeLists.txt の
  `PICORUBY_SRCS` 管理 + lib/add 配下 (CLAUDE.md の mrbgem 管理ルール準拠)。

### 4.3 エディタ UI 方針

対象: main/prebuild_scripts/default_app/editor.app.rb (現状約 1300 行)。

最小構成 (Phase E2 で成立させる範囲):

- BP ガター: 行番号横クリックでトグル。開いているファイルのパスと行番号を
  そのまま `bp_set` に渡す (マッピング不要、下記 4.4)。
- 実行制御バー: 対象アプリ選択 (`ps` の一般アプリのみ列挙) / attach /
  continue / step in/over/out / pause / detach。
- 停止表示: stopped イベント受信で該当ファイルを開き、停止行をハイライト。
- スタック/変数ペイン: 画面右または下に併置 (Modern の広い画面前提)。
  変数の `ref` 付き要素は展開操作で `expand` を呼ぶ。
- 対象アプリの起動はランチャー経由でもエディタからの spawn でも良いが、
  「BP を先に置いてから起動」を成立させるには attach を spawn 直後に行う
  導線が必要 (spawn 直後は state=init。attach タイミングは要検証。
  必要なら fmrb_app_spawn に suspended-start 相当の仕掛けを追加検討)。

### 4.4 ソースマッピングが不要な根拠

一般アプリ (flash/app/*.rb) はオンデバイスコンパイルで、
`mrc_ccontext_filename` 設定済み (リモートデバッグ Phase 0 対応済み) のため
irep の debug_info はデバイス上のファイルパス・行番号と一致する。
エディタが開いているファイル = デバイス上の実ファイルなので、リモート版で
必要だった pathMappings / combined.map.json 補正は一切不要。
(combined 補正が要るのは kernel/default_app の組み込みアプリのみで、
それは VSCode 連携の担当範囲)

### 4.5 Retro (320x240) 向け縮小 UI 設計

前提: エディタは 6x8 フォント、recompute_layout によるレイアウト再計算構造
(editor.app.rb) を持つため、下部ペインの挿入は既存の @edit_height /
@status_y の計算に 1 項足す形で実現できる。

- **下部デバッグペイン (分割方式)**: デバッグセッション中のみ、ステータス行の
  上に高さ 8 行程度のペインを挿入する (編集領域は約 28 行 -> 約 19 行)。
  ペインは 1 種類ずつ表示し、キーで切替する:
  - Stack 表示: 1 行 1 フレーム (`#0 method  file:line`)。上下キーで
    フレーム選択、選択フレームの変数表示へ切替可能。
  - Vars 表示: 選択フレームのローカル変数を `name = value` で列挙。
    ref 付きは選択 + Enter で expand (1 階層、ペイン内で戻る操作あり)。
- **BP 操作**: ガター表示の余白がないため、カーソル行に対する
  トグルをキー (例: F9) とメニュー項目「Debug > Toggle Breakpoint」で行う。
  BP 行は行頭 1 桁のマーカー文字 (例: `*`) + 行の色替えで示す。
- **実行制御**: DOS 系デバッガの慣例に合わせ F5=continue / F10=step over /
  F11=step in / Shift+F11=step out / F6=pause。メニューバーに「Debug」
  ドロップダウンを追加し、attach 対象選択 (ps 一覧) / detach もそこから行う。
- **停止表示**: stopped イベントで該当ファイルを開き停止行をハイライト
  (既存のカーソル行描画と同系の色替え)。停止行が BP 行と重なる場合は
  停止色を優先。
- **Modern との共有**: デバッグ状態管理・イベント処理・FMRB::Debug 呼び出しは
  共通コードとし、レイアウトのみ画面幅で分岐 (併置ペイン or 下部分割)。
  まず Retro の下部分割を実装し、Modern の併置はその拡張として後段で足すのが
  実装順として簡単 (Linux sim の既定解像度が 320x240 で自律検証できるため)。

## 5. 実装ステップ (フェーズ案)

### Phase E0: セッションオーナー排他

1. `fmrb_debugd` に owner (none/remote/local) を追加。remote attach と
   local acquire の相互排他、イベントコンシューマの切替。
2. Linux sim でリモートセッションとの排他を test_phase2.py 系に追加して検証。
   (この部分は Modern 限定ではなく共通コードで良い)

### Phase E1: mrbgem `FMRB::Debug`

1. 案B の C gem 実装 (ctx API 直結 + msgpack ボディの Ruby オブジェクト化)。
2. 全ターゲットに組み込み (Linux は開発検証用に必須。S3 は flash 残量
   約 35% あり、gem + UI 追加の増分は問題にならない見込み。ビルド後に
   残量を確認すること)。
3. シェル (shell.app.rb) から叩ける簡易コマンド or テストアプリで
   attach → bp → stopped → stack/vars → continue の一巡を headless 検証。

### Phase E2: エディタ UI 最小構成

- 4.3 の最小構成を **Retro 縮小 UI (4.5) で先に実装**する。Linux sim は
  既定で 320x240 のため、tools/dev_run_check.sh + fmrb_input.py +
  fmrb_screenshot.py による自律検証がそのまま Retro レイアウトの検証になる。
- Modern の併置レイアウトは共通ロジックの上に後段で追加し、Linux sim の
  解像度設定を Modern 相当に合わせて確認する。

### Phase E3: 変数ペイン強化・利便性

- expand の再帰展開 UI、値の安全フォーマッタ表示、BP の永続化
  (エディタ設定への保存)、停止中のログ表示連携。

### Phase E4 (後期): evaluate

- 停止コンテキストでの式評価 (リモート版 Phase 4 と共通のデバイスコード)。
  デバイス上に mrc があるため技術的には可能。リモート版で ctx 層に
  evaluate が入った後に UI を足すのが効率的。

## 6. 制約・注意点 (実装者向け)

- **全ターゲット対応** (2026-07-22 変更)。ビルドゲートはせず、UI レイアウトのみ
  画面サイズで切替 (320x240 は 4.5 の下部分割、広画面は併置)。S3 の flash
  残量はビルドごとに確認する。
- **デバッグ対象は一般アプリのみ**。エディタ自身・system_desktop・
  組み込みアプリは対象外。`ps` 一覧の時点でフィルタする
  (kernel pid=0 と system_desktop、および editor 自身の pid を除外)。
- **VSCode/BLE 計画は継続**。本機能は vm_remote_debug の Phase 3 (BLE) を
  置き換えない。debugd のコア (ctx 層・proto 層) は両者の共有資産なので、
  片方の都合で API を壊さないこと。
- パーク中アプリの reaper / 監視系誤検知対策 (DEBUGGING 状態の扱い) は
  リモート版と共通の課題。先に入っていなければ本実装で対応する
  (doc/remote_debug/vm_remote_debug_design.md sec 8 参照)。
- エディタ VM 側でのブロッキング呼び出し禁止 (poll ベースで UI ループを回す)。
- inspect 応答の内部バッファは「次の inspect まで有効」。gem 内で即座に
  Ruby オブジェクトへコピーし、バッファ参照を Ruby 側へ渡さないこと。
- mrbgem / picoruby 側の変更は lib/add 配下で管理 (submodule 直接編集禁止)。

## 7. 参照資料

| 資料 | 内容 |
|---|---|
| doc/remote_debug/vm_remote_debug_design.md | リモートデバッグ全体設計 (パーク方式の詳細は sec 5) |
| doc/remote_debug/vm_remote_debug_protocol.md | デバッグプロトコル仕様 (正) |
| doc/remote_debug/vm_remote_debug_impl_plan.md / _impl_plan2.md / _progress.md | 実装計画と進捗 |
| main/drivers/debug/fmrb_debug_ctx.h/.c | per-VM デバッグコンテキスト (本機能が直結する層) |
| main/drivers/debug/fmrb_debugd.c/.h | debugd タスク (オーナー排他を追加する場所) |
| main/drivers/debug/fmrb_debug_proto.c/.h | msgpack プロトコル処理 (案A の場合のみ関係) |
| main/drivers/debug/fmrb_debug_transport.h | transport 抽象 (案A の場合 3 本目を追加) |
| main/prebuild_scripts/default_app/editor.app.rb | FM-EDITOR 本体 (UI 拡張対象) |
| tool/debug/fmrb_dbg_client.py / test_phase2.py | 動作検証の参考 (コマンド一巡の手順) |
| doc/reference/support_esp32p4.md | Modern (ESP32-P4) 対応の設計 |
| tools/ (リポジトリルート) + ルート CLAUDE.md | headless 自律検証ハーネス |

## 8. 未決事項

- spawn 直後 attach の成立性 (BP を最初の行から効かせるための
  suspended-start の要否) — Phase E1 で実測して判断。
- Linux sim を Modern 解像度で動かす際の設定方法 (Modern 併置レイアウトの
  検証用。Retro レイアウトは既定解像度でそのまま検証可)。
- Retro での S3 flash 残量の実測 (現状約 35% 空き。gem + エディタ UI 追加後に
  再確認)。
- BP 永続化の保存先 (エディタ設定ファイルの形式)。
- evaluate の副作用制御 (リモート版 Phase 4 と共通検討)。
