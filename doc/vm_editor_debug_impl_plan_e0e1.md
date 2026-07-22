# FM-EDITOR オンデバイスデバッガ 実装計画書 Phase E0 + E1 (handoff)

作成: 2026-07-22。実装担当 AI への引き継ぎ資料。
設計の正: `doc/vm_editor_debug_design.md` (2026-07-22 更新版。Retro 含む全ターゲット対応)。
本書のスコープ: **Phase E0 (セッションオーナー排他) + Phase E1 (mrbgem FMRB::Debug)**。
エディタ UI (Phase E2) は含まない。E1 完了時点で「Ruby からデバッグ一巡
(attach -> bp -> stopped -> stack/vars -> continue -> detach)」が Linux sim の
headless 検証で PASS していることがゴール。

作業ブランチ: fmruby-core の `develop` 上でそのまま作業する (ユーザ決定)。
ルートリポジトリの変更は無い見込み (あれば記録して報告)。

## 1. 必読資料 (実装前に読む)

| 資料 | 目的 |
|---|---|
| doc/vm_editor_debug_design.md | 全体方針。特に sec 4.1 (案B), 4.2 (API), 6 (制約) |
| main/drivers/debug/fmrb_debug_ctx.h | 直結する C API とスレッドモデルのコメント |
| main/drivers/debug/fmrb_debugd.c | E0 の変更対象 (約 360 行、全読み推奨) |
| doc/vm_remote_debug_protocol.md | inspect 応答の msgpack 形式 (frames/vars/複合型) |
| lib/add/picoruby-fmrb-app/ | 新 gem の構造モデル (mrbgem.rake / ports/esp32 / mrblib) |
| lib/add/picoruby-fmrb-msgpack/ | MessagePack.pack/unpack (E1 で応答デコードに利用) |
| CLAUDE.md (fmruby-core) | mrbgem 管理・ログ・メモリ関数などの必須ルール |
| ルート CLAUDE.md | headless 検証ハーネスの使い方 |

## 2. 現状コードの要点 (2026-07-22 develop)

- debugd (fmrb_debugd.c) は単一タスク。`s_tp->poll` でリクエスト受信 ->
  `dispatch()`、ループ末尾で `forward_events()` が
  `fmrb_debug_ctx_poll_event()` を **単一コンシューマとして** drain し、
  切断検出時に `fmrb_debug_ctx_detach_all()` を呼ぶ (fmrb_debugd.c:327-350)。
- ctx 層 (fmrb_debug_ctx.c) はキューベースで、attach/bp/flow/inspect の
  呼び出し元タスクは本質的には固定でない。ただし:
  - inspect 応答バッファは「次の inspect 呼び出しまで有効」(単一クライアント前提)
  - イベントキューは単一コンシューマ前提
  - ヘッダコメントの「called on the debugd task」はこの前提の表明
- したがって **remote (debugd タスク) と local (エディタ/アプリ VM タスク) が
  同時に ctx 層を触ると壊れる**。これを防ぐのが E0。
- `handle_app_ctl` は attach 中の kill/stop/suspend を BUSY で拒否する
  先例あり (fmrb_debugd.c:118-127)。
- gem の C コードから main/ のヘッダは直接 include できる
  (components/picoruby-esp32 は PRIV_REQUIRES main。例:
  lib/add/picoruby-fmrb-app/ports/esp32/gfx.c が fmrb_app.h を include)。
- デバッグ対象のサンプルアプリ flash/app/debug/dbg_sample.app.rb が既にある
  (リモート版の検証用)。E1 の検証ターゲットに流用する。

## 3. Phase E0: セッションオーナー排他

### 3.1 設計

fmrb_debugd に owner 概念を追加する。

```c
typedef enum {
    FMRB_DBG_OWNER_NONE = 0,
    FMRB_DBG_OWNER_REMOTE,   // TCP/BLE クライアント (既存経路)
    FMRB_DBG_OWNER_LOCAL,    // オンデバイス (FMRB::Debug gem)
} fmrb_dbg_owner_t;

// fmrb_debugd.h に追加
fmrb_err_t fmrb_debugd_acquire_local(void);   // NONE -> LOCAL。REMOTE 中は FMRB_ERR_BUSY
void       fmrb_debugd_release_local(void);   // LOCAL -> NONE (detach_all も行う)
fmrb_dbg_owner_t fmrb_debugd_owner(void);
```

排他規則:

| 状況 | remote 側 (dispatch) | local 側 (gem) |
|---|---|---|
| owner=NONE | attach で REMOTE を取得 | acquire で LOCAL を取得 |
| owner=REMOTE | 通常動作 | acquire は FMRB_ERR_BUSY |
| owner=LOCAL | hook 系コマンド (attach/detach/bp/pause/continue/step/inspect) を FMRB_ERR_BUSY で拒否。version/ps/log_read/spawn/kill 等の非 hook 系は許可 | 通常動作 |

- REMOTE の取得: `handle_attach` で ctx_attach 成功時に owner=REMOTE。
  解放: 「attach 中 pid が 0 になった時」を都度判定するのは複雑なので、
  **クライアント切断時の detach_all (fmrb_debugd.c:347) で NONE に戻す**
  単純化を採る。明示 detach で全て外れても切断までは REMOTE を維持して良い
  (VSCode セッションの実態と一致し、local との競合も安全側)。
- LOCAL 中のイベント消費: debugd ループの `forward_events()` は
  owner==LOCAL の間スキップする (gem の poll_event が単一コンシューマになる)。
- LOCAL 中の切断処理: owner==LOCAL の間は `detach_all` を呼ばない
  (was_connected の分岐に owner ガードを追加。remote は attach していないので
  外すものが無く、呼ぶと local のデバッグ対象を巻き込んで外してしまう)。
- release_local は内部で `fmrb_debug_ctx_detach_all()` を呼び、gem 側の
  解放漏れでも VM がパークしたまま残らないようにする。
- スレッド安全: owner は debugd タスクと VM タスクの双方から触るため、
  ctx 層のフラグと同様に `__atomic_*` (release/acquire) で読み書きする。
  acquire_local の CAS は `__atomic_compare_exchange_n`。

### 3.2 実装手順

1. fmrb_debugd.h/.c に owner 変数 + 上記 3 API を実装。
2. dispatch() の hook 系 case に owner==LOCAL ガードを追加 (reply は
   FMRB_ERR_BUSY。先例 handle_app_ctl と同じ形)。
3. debugd ループ: forward_events() と detach_all の owner ガード。
4. handle_attach 成功時の owner=REMOTE 設定、切断時 detach_all 後の NONE 戻し。

### 3.3 E0 単体の検証 (Linux)

- 既存回帰: `tool/debug/test_phase1.sh` / `test_phase2.sh` が PASS すること
  (owner=REMOTE 経路が従来と同一挙動である確認)。
- 排他の自動テストは E1 完了後の統合テストで行う (5.3。local 取得は gem 経由が
  最も簡単なため)。E0 単体では C レベルの挙動をコードレビューで担保する。

## 4. Phase E1: mrbgem `picoruby-fmrb-debug` (Ruby API `FMRB::Debug`)

### 4.1 gem 構成 (モデル: lib/add/picoruby-fmrb-app)

```
lib/add/picoruby-fmrb-debug/
  mrbgem.rake            # 依存: picoruby-fmrb-msgpack
  include/picoruby_fmrb_debug.h
  ports/esp32/debug.c    # C 実装 (linux/esp32 共通。glob で両ビルドに入る)
  mrblib/debug.rb        # Ruby ラッパ (FMRB::Debug)
```

組み込み (3 箇所、いずれも lib/add 配下と components/picoruby-esp32/CMakeLists.txt):

1. `lib/add/family_mruby.gembox` に `conf.gem core: "picoruby-fmrb-debug"` を追加
   (全ターゲット共通。ターゲット別 rb ファイルではなく gembox に置く)。
2. components/picoruby-esp32/CMakeLists.txt: linux / esp32 両ブロックに
   `file(GLOB FMRB_DEBUG_PORTS_SRCS ".../picoruby-fmrb-debug/ports/esp32/*.c")`
   + PICORUBY_SRCS への追加。INCLUDE_DIRS に include/ を追加。
3. サブモジュール直接編集は禁止。lib/add に置けば Rakefile が反映する
   (既存 gem と同じ扱い。反映を確認すること)。

### 4.2 C 実装 (ports/esp32/debug.c)

方針: `fmrb_debug_ctx_*` / `fmrb_debugd_*` を直接呼ぶ薄いバインディング。
**inspect 応答は C でデコードせず、msgpack ボディをそのまま mrb_str に
コピーして返し、Ruby 側で MessagePack.unpack する** (設計 doc 4.1 案B の
変形。picoruby-fmrb-msgpack が既にあるため C デコーダを書く必要がない。
バッファは「次の inspect まで有効」なので mrb_str へのコピーが必須 —
これで設計 doc sec 6 の「バッファ参照を Ruby に渡さない」も満たす)。

| Ruby (mrblib ラッパ経由) | C 関数 | 備考 |
|---|---|---|
| FMRB::Debug.acquire -> bool | fmrb_debugd_acquire_local | false = リモート使用中 |
| FMRB::Debug.release -> nil | fmrb_debugd_release_local | detach_all 込み |
| FMRB::Debug.attach(pid) -> bool | fmrb_debug_ctx_attach | |
| FMRB::Debug.detach(pid) -> bool | fmrb_debug_ctx_detach | |
| FMRB::Debug.attached?(pid) -> bool | fmrb_debug_ctx_is_attached | |
| FMRB::Debug.bp_set(pid, file, line) -> Integer/nil | fmrb_debug_ctx_bp_set | 戻り bp_id。失敗 nil |
| FMRB::Debug.bp_clear(pid, bp_id=-1) -> bool | fmrb_debug_ctx_bp_clear | -1 で全消し |
| FMRB::Debug.pause(pid) / continue(pid) -> bool | fmrb_debug_ctx_pause/continue | |
| FMRB::Debug.step_in/step_over/step_out(pid) -> bool | fmrb_debug_ctx_step | FMRB_STEP_* |
| FMRB::Debug._stack_trace_raw(pid, max) -> String/nil | fmrb_debug_ctx_stack_trace | msgpack ボディ |
| FMRB::Debug._frame_vars_raw(pid, frame) -> String/nil | fmrb_debug_ctx_frame_vars | 同上 |
| FMRB::Debug._expand_raw(pid, ref) -> String/nil | fmrb_debug_ctx_expand | 同上 |
| FMRB::Debug._poll_event_raw(timeout_ms) -> Array/nil | fmrb_debug_ctx_poll_event | 下記 |

- `_poll_event_raw` は fmrb_dbg_event_t を **C 側で Ruby Array**
  `[type, pid, reason, bp_id, line, file]` にして返す (msgpack 不要の固定形。
  Hash 組み立てより C コードが短い)。mrblib で Hash に整形する。
- 事前条件ガード: acquire 前に attach 系を呼んだら false/nil を返す
  (owner!=LOCAL のとき C 側で FMRB_ERR_BUSY 扱い。デバッグしやすいよう
  FMRB_LOGW を出す)。
- メモリ: mrb_str 生成は mruby のアロケータ任せで良い (VM プール内)。
  fmrb_malloc 等の直接使用は不要のはず。C 側 static バッファも不要。
- ログは fmrb_log.h (FMRB_LOGx)。esp_log 直接使用は禁止 (CLAUDE.md)。

### 4.3 mrblib/debug.rb (Ruby ラッパ)

```ruby
module FMRB
  module Debug
    # stack_trace(pid, max=16) -> [{idx:,func:,file:,line:}] or nil
    def self.stack_trace(pid, max = 16)
      raw = _stack_trace_raw(pid, max)
      raw ? ::MessagePack.unpack(raw)["frames"] : nil
    end
    # frame_vars(pid, frame=0) -> [{name:,type:,value:,...}] or nil
    # expand(pid, ref) -> 同上
    # poll_event(timeout_ms=0) -> {type:, pid:, reason:, bp_id:, line:, file:} or nil
    #   type は :stopped/:resumed/:exited のシンボルに変換
    # ps はここでは追加しない (プロセス一覧は既存 API / shell の ps 相当を利用。
    #   無ければ E2 で検討し、本フェーズのテストは spawn した pid を直接使う)
  end
end
```

注意 (picoruby の既知の罠、メモリ済みの実測事項):
- クラス/モジュール内の bare `MessagePack` はトップレベル解決されないことが
  ある。`::MessagePack` と書く。
- `defined?` は無い。存在確認は const_defined? か begin/rescue。
- unpack 結果のキーは String ("frames" 等)。シンボル化しない。

### 4.4 応答フォーマット (unpack 後に期待される形)

protocol.md sec 複合型より:
- stack_trace: `{"frames" => [{"idx"=>i,"func"=>s,"file"=>s,"line"=>i}, ...]}`
- frame_vars / expand: `{"vars" => [{"name"=>s,"type"=>s,"value"=>s, ...}, ...]}`
  (expand 用の `ref` キーが入る。protocol.md の表が古い場合は実測に合わせ、
  ずれていたら protocol.md を直すこと)

## 5. 検証 (完了条件)

### 5.1 ビルド

1. `rake build:linux` 成功 (graphics-audio 側は既ビルドの前提)。
2. `rake clean_all` 後 `rake build:esp32` を NARYAv3 (S3) で成功させ、
   flash 残量を記録 (.env の FMRB_HW_TARGET 上書きに注意)。
   P4 ビルドは任意 (通れば記録)。

### 5.2 リモート回帰 (E0 で挙動が変わらないこと)

- `tool/debug/test_phase1.sh` / `test_phase2.sh` PASS。

### 5.3 ローカルデバッグ一巡 + 排他 (新規、Linux sim headless)

テストアプリ `flash/app/test/dbg_e1_test.app.rb` を新規作成する:

1. `FMRB::Debug.acquire` (false なら FAIL 表示)
2. dbg_sample を spawn (既存の FMRB::App 系 spawn API を利用。無ければ
   デバッグ対象をランチャー起動し ps 相当で pid を得る方式に変更して良い)
3. `bp_set(pid, "/app/debug/dbg_sample.app.rb", <ループ内の行>)`
4. `poll_event` ループ (Machine.delay_ms ベース、ブロッキング長時間待ち禁止)
   で stopped を受信
5. `stack_trace` / `frame_vars` を取得し、frames が 1 個以上・vars に
   既知の変数名が含まれることを確認
6. `step_over` -> stopped(step) -> `continue` -> resumed
7. 結果を画面 (Canvas への文字列描画) と Log.info の両方に出す
   ("E1 TEST: PASS/FAIL" の1行を必ず出す)
8. detach -> release

実行手順 (ルートリポジトリの自律検証ツール):
```
tools/dev_run_check.sh --keep
python3 tools/fmrb_input.py <ランチャーから dbg_e1_test を起動する操作>
python3 tools/fmrb_screenshot.py e1_result.png   # PASS 表示を確認
docker compose logs | grep "E1 TEST"             # ログでも確認
```

排他確認 (同じ起動のまま):
- dbg_e1_test が acquire 中 (テストアプリに「hold モード」を付けて BP 停止を
  維持するなど) に、ホストから
  `python3 tool/debug/fmrb_dbg_client.py localhost:5555 attach --pid <pid>` 相当を
  実行し **BUSY で拒否されること**。
- 逆方向: リモートで attach 中に dbg_e1_test を起動し acquire が false に
  なること。
- この 2 ケースを `tool/debug/test_e0_owner.py` (+.sh) として自動化できるなら
  する。困難なら手動手順を進捗ログに記録して省略可 (判断を記録すること)。

### 5.4 実機 (ユーザ作業、計画書の範囲外)

S3 実機でのテストアプリ実行はユーザに依頼する (手順を進捗ログに 1 段落で
書く)。実装 AI のゴールは 5.1-5.3。

## 6. 制約・ルール (fmruby-core CLAUDE.md より、違反しやすいもの)

- サブモジュール (components/picoruby-esp32/picoruby 以下) 直接編集禁止。
  gem は lib/add 配下のみ。反映は Rakefile 経由。
- lib/ 以下を編集したら `rake clean` してからビルド。linux/esp32 切替は
  `rake clean_all`。
- main/ および gem C コードのログは fmrb_log.h ラッパ経由。
- C コメントは英語。絵文字・チェックマーク等の非 ASCII 記号は使わない。
- ソースコードのファイル削除・ビルド対象からの除外による問題回避は禁止。
- コミットはユーザ (レビュー AI) の指示があるまで行わない。作業完了時に
  変更ファイル一覧と数行の英文コミットログ案を提示する。

## 7. リスク・注意点

| 項目 | 内容 | 対応 |
|---|---|---|
| ctx 層の隠れた debugd タスク前提 | fmrb_debug_ctx.c 内に呼び出し元タスク固有の仮定が残っている可能性 | 実装前に fmrb_debug_ctx.c を通読し、キュー/バッファ以外のタスク仮定 (例: static 変数の非同期アクセス) を洗い出す。見つけたら計画からの逸脱として記録し対処 |
| poll_event の競合窓 | owner 遷移の瞬間に debugd と gem が同時に poll する窓 | owner を先に確定してから消費を始める順序にする (acquire 成功後に gem が poll、debugd は owner==LOCAL を見たループ周回から skip)。イベントを 1 個取り逃しても次周回で拾える設計なら許容 |
| VM タスクからの inspect ブロッキング | inspect はパーク中 VM の応答待ちでブロックする | gem 呼び出しは attach 対象とは別 VM で行われるため成立するが、**自 VM を attach 対象にした場合デッドロック**。C 側で「呼び出し元 VM の pid == 対象 pid」を拒否する (fmrb_app の自 pid 取得 API を利用) |
| spawn 直後 attach | 設計 doc 未決事項。state=init 中の attach 可否 | dbg_sample は起動後ループし続けるアプリなので、spawn 後に短い delay + リトライで attach する。最初の行から止める要件は E1 では扱わない (E2 以降) |
| flash 残量 (S3) | gem + テストアプリ追加分 | 5.1 で実測記録。増分は小さい見込み (数十 KB) |
| MessagePack.unpack の対応範囲 | picoruby-fmrb-msgpack が全型をデコードできるか | 検証 5.3 が実質のテスト。欠けがあれば lib/add 内で拡張し記録 |

## 8. 成果物とコミット分割案 (レビュー後にユーザ指示でコミット)

1. E0: fmrb_debugd owner 排他 (+ 回帰 PASS 記録)
2. E1: picoruby-fmrb-debug gem + gembox/CMake 組み込み
3. テスト: dbg_e1_test.app.rb + (可能なら) test_e0_owner.py + 進捗ログ更新
   (doc/vm_remote_debug_progress.md ではなく新設の
   doc/vm_editor_debug_progress.md に E0/E1 の実施記録を書く)

各コミットは独立ビルド可能な単位とし、英文数行のログ案を添える。
計画から逸脱した点は必ず進捗ログに理由付きで記録すること。
