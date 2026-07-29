# PicoRuby VM リモートデバッグ 動作検証手順

VM リモートデバッグ機能 (Phase 1/2) の動作検証手順をまとめる。検証は 2 層構成:

1. **ヘッドレス自律検証** — Claude Code / CI が GUI なしで実行できる。デバッグコア
   とプロトコル、DAP アダプタの E2E を機械的に確認する。
2. **VSCode GUI 手動検証** — エディタ余白の BP 設定・停止線表示・変数ペイン・
   ステップ操作といった GUI 依存部分。ヘッドレスでは確認できず、ユーザが VSCode
   GUI 環境で実施する。

関連ドキュメント:

- 設計: `doc/remote_debug/vm_remote_debug_design.md`
- 実装計画: `doc/remote_debug/vm_remote_debug_impl_plan.md`
- プロトコル仕様: `doc/remote_debug/vm_remote_debug_protocol.md`
- 進捗ログ: `doc/remote_debug/vm_remote_debug_progress.md`

---

## 前提条件

- 両リポジトリの Linux ビルドが済んでいること (`rake build:linux`)。
- `docker compose` スタックがビルド可能なこと。
- VSCode を動かすマシンに Python 3 + `msgpack` (`pip install msgpack`)。
- debugd が TCP 5555 で listen していること。
  `tools/dev_run_check.sh --keep` がルートの docker compose で `5555:5555` を公開する。

用語:

- **standalone アプリ**: `flash/app/**.app.rb` の単体ファイル。デバイスは basename で
  BP を照合する (マッピング不要)。例: `Kamon` (`demo/kamon.app.rb`)。
- **combined アプリ**: kernel / `system_*` など `subdir/*.rb` を連結してビルドするもの。
  `*_combined.map.json` で原本 `file:line` <-> combined 行を相互変換する。
  例: `system_desktop` (`clock_setting.rb:5` <-> `system_desktop_combined.rb:103`)。

---

## レイヤ1: ヘッドレス自律検証

いずれもスタックの起動 (`dev_run_check.sh --keep`) → Kamon 起動 → デバッグフロー実行
→ `docker compose down` までを自動で行う。リポジトリルート (`family-mruby`) から実行する。

### Phase 1: デバッグコア + プロトコル

```
fmruby-core/tool/debug/test_phase1.sh
```

`fmrb_dbg_client.py` で Kamon の pid を取得し、`test_phase1.py` が
attach → setBreakpoint → 停止 → stack_trace → frame_vars → step → continue → detach
を直接プロトコルで駆動する。BP は `/app/demo/kamon.app.rb:53`。

期待結果: 終了コード 0。BP ヒットで指定行に停止し、スタック/変数が取得でき、
step/continue が通り、detach で復帰する。

### Phase 2: DAP アダプタ + 行マップ

```
fmruby-core/tool/debug/test_phase2.sh
```

`test_phase2.py` が 2 部構成で検証する:

- **Part A (行マッパー単体)**: combined 往復 (`clock_setting.rb:5` <->
  `system_desktop_combined.rb:103`)、standalone の basename 変換。
- **Part B (アダプタ E2E)**: `fmrb_dap_adapter.py` を VSCode 相当で stdio DAP 駆動。
  initialize/attach/setBreakpoints/configurationDone/stackTrace/scopes/variables/
  continue/next/step*/pause/disconnect を通す。stackTrace の `source.path` が
  ホストパス (`.../flash/app/demo/kamon.app.rb:53`) に変換されることを確認する。

期待結果: 終了コード 0 (Part A / Part B とも PASS)。

### 手動でスタックを起こして個別確認する場合

```
# ルートで起動 (維持)
tools/dev_run_check.sh --keep

# 別ターミナルで生プロトコルを叩く
fmruby-core/tool/debug/fmrb_dbg_client.py localhost:5555 ps

# 片付け
docker compose down
```

---

## レイヤ2: VSCode GUI 手動検証 (ユーザ実施)

ヘッドレスで確認できない GUI 操作を確認する。WSL2 上のパスを扱うため、VSCode は
**WSL リモート接続**で開くこと (ウィンドウ左下に緑の `WSL: ...` 表示があること)。
表示が無い場合はコマンドパレット `WSL: Reopen Folder in WSL` 等で開き直す。

### 手順 0: 拡張のデバッグ起動 (Extension Development Host)

1. `vscode-fmrb-debug/` を VSCode (WSL リモート) で開く。
2. `F5` (Run Extension) で Extension Development Host を起動する。

補足: Dev Host で「ファイル → フォルダーを開く」から `family-mruby` を開こうとして
反応しない場合は、`vscode-fmrb-debug/.vscode/launch.json` の `Run Extension` の
`args` に開くフォルダを追加すると、F5 だけで対象フォルダを開いた Dev Host が起動する:

```json
"args": [
  "--extensionDevelopmentPath=${workspaceFolder}",
  "${workspaceFolder}/.."
]
```

これで `family-mruby` (= `vscode-fmrb-debug` の親, `fmruby-core/` を含む) を開いた
状態で Dev Host が立ち上がる。手動の「フォルダーを開く」は不要になる。

### 手順 1: launch.json 作成

Dev Host で開いた workspace (`fmruby-core/` を含むフォルダ) に `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "type": "fmrb",
      "request": "attach",
      "name": "fmrb: attach to Kamon",
      "host": "localhost",
      "port": 5555,
      "app": "Kamon"
    }
  ]
}
```

`app` はアプリ名 (`ps` と照合) または数値 pid。

### 手順 2: スタック起動 + アプリ起動

1. ルートで `tools/dev_run_check.sh --keep` を実行し debugd:5555 を公開する。
2. ランチャから対象アプリを起動する (headless なら `fmrb_input.py` でも起動可能だが、
   GUI 確認なので画面を見ながら起動してよい)。

### 検証 A: standalone アプリ (Kamon)

| # | 操作 | 期待結果 |
|---|------|----------|
| A1 | Kamon を起動 | ランチャからアプリが動作する |
| A2 | `demo/kamon.app.rb` を開き、53 行目付近の余白に BP を打つ | 赤丸の BP マーカーが表示される |
| A3 | `fmrb: attach to Kamon` でデバッグ開始 | アダプタが 5555 に接続し attach 成功 |
| A4 | BP 行が実行される | 該当行で停止し、停止線 (ハイライト) が表示される |
| A5 | コールスタックペイン | Ruby フレームが積まれ、最上位が停止行を指す |
| A6 | 変数ペイン (Locals) | ローカル変数が型別サマリで表示される |
| A7 | ステップオーバー/イン/アウト | 停止線が期待どおり移動する |
| A8 | Continue | 実行が再開する (ループ内 BP なら再ヒットする) |
| A9 | Disconnect (停止) | detach し、アプリが通常実行に復帰する |

### 検証 B: combined アプリ (system_desktop)

combined の原本ファイルに打った BP が、`*_combined.map.json` 経由で正しく
combined 行へ変換され、デバイス側でヒットすることを確認する。

| # | 操作 | 期待結果 |
|---|------|----------|
| B1 | `clock_setting.rb` の 5 行目に BP を打つ | BP マーカー表示 |
| B2 | system_desktop に attach し、その行を実行 | 原本 `clock_setting.rb:5` で停止 (内部的に combined:103 に変換されている) |
| B3 | 停止時の source.path | エディタ上は原本 `clock_setting.rb` が指される |

`combinedMaps` の既定 (`${workspaceFolder}/fmruby-core/main/prebuild_scripts/*/mrb`)
にビルドで生成された map.json が存在すること (`gen_combined_rb.py` が生成)。

### つまずきポイント

- **接続できない**: `dev_run_check.sh --keep` でスタックが起動し 5555 が公開されているか、
  `docker compose ps` で確認。`pythonPath`/`msgpack` の導入も確認。
- **BP がヒットしない (standalone)**: デバイスは basename 照合。開いているファイル名が
  デバイス上のファイル名と一致しているか確認。
- **BP がヒットしない (combined)**: map.json が生成されているか、`combinedMaps` の
  パスが実際の出力先と一致しているか確認。
- **Dev Host でフォルダが開けない**: 上記「手順 0 補足」の launch args 方式を使う。
  WSL リモート表示があるかも確認。

---

## 検証結果の記録

GUI 手動検証を実施したら、結果 (PASS/課題) を `doc/remote_debug/vm_remote_debug_progress.md` の
Phase 2 セクションに追記し、「ユーザ確認待ち」を解消する。
