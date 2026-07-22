# FM-EDITOR オンデバイスデバッガ 実装進捗 (Phase E0 / E1)

計画書: `doc/vm_editor_debug_impl_plan_e0e1.md`
設計の正: `doc/vm_editor_debug_design.md`
作業ブランチ: `feature/editor-debug` (fmruby-core, develop から分岐)
記録開始: 2026-07-23

## 状態サマリ

| フェーズ | 実装 | ビルド | 検証 |
|---|---|---|---|
| E0 セッションオーナー排他 | 完了 | linux/esp32 OK | 回帰 PASS |
| E1 mrbgem `picoruby-fmrb-debug` | 完了 | linux/esp32 OK | 一巡 PASS |
| E2 エディタ内蔵デバッガ (Retro 下部分割) | 完了 | linux OK | 一巡 PASS |
| テスト資材 (app / harness) | 完了 | - | PASS |

## 検証結果 (2026-07-23, Linux sim headless)

- **5.1 ビルド**: `rake build:linux` OK (`build/fmruby-core.elf`)。
  `rake clean_all` 後 `rake build:esp32` (NARYAv3/S3) OK。バイナリ 0x1f9b20
  (~2.07MB)、app パーティション 3MB 中 **0x1064e0 (34%) free**。gem+テスト追加の
  増分は小さくフラッシュに余裕あり。P4 は未実施 (任意)。
- **5.2 リモート回帰**: `tool/debug/test_phase1.sh` RESULT: PASS、
  `tool/debug/test_phase2.sh` (DAP) RESULT: PASS。E0 で remote 経路の挙動は不変。
- **5.3 ローカル一巡 + 排他**: `tool/debug/test_e1.sh` exit 0。
  - ログ: `E1 TEST: PASS frames=7 vars=11` (compute() 停止で 7 フレーム、
    ローカル 11 個、`doubled`/`label` を確認)。
  - 画面: dbg_e1_test ウィンドウに緑 "PASS" 描画を確認 (320x240 キャプチャ)。
  - 排他: dbg_e1_test が LOCAL 保持中にリモート attach が BUSY 拒否されることを
    確認 (E0)。
- 実機 (5.4) はユーザ作業として残す (下記「残作業」)。

備考: 検証環境の Docker Desktop WSL 統合が当初未配線で
`/var/run/docker.sock` が無かったため、`docker-desktop-user-distro proxy` 起動を
契機に root:docker 所有のソケットが生成され疎通。以降のビルド/検証はコンテナ経由
で正常実行。

## E0: セッションオーナー排他 (実装内容)

対象: `main/drivers/debug/fmrb_debugd.h` / `fmrb_debugd.c`

- `fmrb_dbg_owner_t` (NONE/REMOTE/LOCAL) と 3 API を追加:
  `fmrb_debugd_acquire_local` / `fmrb_debugd_release_local` / `fmrb_debugd_owner`。
- owner は debugd タスクと VM タスク双方が触るため `int s_owner` を `__atomic_*`
  (ACQ_REL/ACQUIRE) でアクセス。遷移は `owner_cas()` ヘルパ (CAS)。
- `acquire_local`: NONE->LOCAL の CAS。既に LOCAL なら冪等に OK、REMOTE なら BUSY。
- `release_local`: `fmrb_debug_ctx_detach_all()` を先に呼んでから LOCAL->NONE。
- `handle_attach`: ctx_attach 成功時に NONE->REMOTE を CAS で取得。
- `dispatch`: hook 系コマンド (attach/detach/bp_set/bp_clear/pause/continue/
  step_*/stack_trace/frame_vars/expand) は owner==LOCAL の間 `reply_ok(BUSY)` で
  拒否 (`is_hook_cmd()` 判定、先例 handle_app_ctl と同形)。非 hook 系
  (version/ps/log_read/spawn/app-ctl) は許可。
- `forward_events`: owner==LOCAL の間は早期 return (イベントキューの単一
  コンシューマを gem 側に譲る)。
- 切断処理: owner!=LOCAL のときのみ `detach_all` + REMOTE->NONE。owner==LOCAL の
  間はスキップ (remote は attach していないので外す物が無く、呼ぶと local の
  対象を巻き込む)。

## E1: mrbgem `picoruby-fmrb-debug` (実装内容)

構成 (モデル: picoruby-fmrb-app):

```
lib/add/picoruby-fmrb-debug/
  mrbgem.rake              # 依存: picoruby-fmrb-msgpack
  include/picoruby_fmrb_debug.h
  src/picoruby_fmrb_debug.c   # gem_init/final -> _impl (rake ビルド)
  ports/esp32/debug.c         # FMRB::Debug 実体 (CMake ビルド、main ヘッダ直結)
  mrblib/debug.rb             # Ruby ラッパ (msgpack デコード / event 整形)
```

組み込み (計画 4.1 の 3 箇所):
1. `Rakefile`: app コピー直後に picoruby-fmrb-debug を submodule mrbgems へ cp。
2. `lib/add/family_mruby.gembox`: `conf.gem core: "picoruby-fmrb-debug"` (app と
   picorabbit の間、msgpack より後)。
3. `components/picoruby-esp32/CMakeLists.txt`: linux/esp32 両ブロックに
   `FMRB_DEBUG_PORTS_SRCS` glob + PICORUBY_SRCS 追加、INCLUDE_DIRS に include/。

C 実装 (ports/esp32/debug.c): `fmrb_debug_ctx_*` / `fmrb_debugd_*` の薄い
バインディング。inspect 応答 (stack_trace/frame_vars/expand) は C でデコードせず
msgpack ボディをそのまま `mrb_str` にコピーして返し、Ruby 側で
`MessagePack.unpack`。バッファは「次の inspect まで有効」なので即コピー必須
(設計 doc sec 6 準拠)。`_poll_event_raw` は固定 Array `[type,pid,reason,bp_id,
line,file]` で返す。全 hook 系は `require_local()` で owner==LOCAL を要求
(違反時 false/nil + FMRB_LOGW)。attach は自 pid == 対象 pid を拒否
(パーク待ちの自己デッドロック回避、`fmrb_current()->app_id` 使用)。

Ruby ラッパ (mrblib/debug.rb): `stack_trace`/`frame_vars`/`expand` が
`::MessagePack.unpack` して `"frames"`/`"vars"` を返す。`poll_event` が
`_poll_event_raw` の Array を Hash (`type:` はシンボル化) に整形。picoruby の罠
(`::MessagePack` 明示、unpack キーは String) に対応済み。

## テスト資材

- `flash/app/test/dbg_e1_test.app.rb` (+ .toml): 一巡テストアプリ。
  acquire -> `FmrbApp.ps` で "Debug Sample" の pid 検索 -> attach ->
  bp_set(line 60, compute 内) -> stopped(breakpoint) -> stack_trace/frame_vars
  (既知ローカル `doubled`/`label` を確認) -> step_over -> stopped(step) ->
  continue -> resumed -> detach。結果を Canvas 描画 + `Log.info("E1 TEST: PASS
  ...")`。PASS 後も owner=LOCAL を保持 (排他確認用)、on_destroy で release。
- `tool/debug/test_e1.sh`: headless ハーネス。dbg_sample と dbg_e1_test を
  debugd の spawn (TCP、非 hook) で起動し、ログの "E1 TEST: PASS" と、保持中の
  リモート attach が BUSY 拒否されること (E0) を確認。

## E2: エディタ内蔵デバッガ (Retro 下部分割 UI, 設計 doc sec 4.5)

対象: `main/prebuild_scripts/default_app/editor.app.rb` (ビルド時に bytecode 化)。
`::FMRB::Debug` (E1) を UI に接続。実装内容:

- **下部デバッグペイン (分割方式)**: デバッグセッション中のみ、ステータス行の上に
  高さ 8 行のペインを挿入 (`recompute_layout` に分岐)。Stack / Vars の 2 ビューを
  F4 で切替。停止時はヘッダに `stop ln<N>`、実行中は `run`。
- **BP マーカー (左ガター赤丸)**: デバッグモード中のみ編集領域の左に 8px のガター
  列を設け、BP 行に **赤丸** (`@gfx.fill_circle`) を表示 (VSCode 風)。BP を張ると
  丸が出て外すと消える。停止行は行全体を黄色ハイライトし、ガターに黄リング/黄丸
  (停止行が BP なら赤丸に黄リング重ね)。非デバッグ時はガター無しで従来レイアウト。
- **実行制御 (F キー)**: F5=continue, F6=pause, F10=step over, F11=step in,
  Shift+F11=step out, F7/F8=フレーム選択, F4=ペイン切替, F9=BP トグル (常時)。
- **Debug メニュー**: メニューバーに追加。非アタッチ時 = Attach.../Toggle BP、
  アタッチ時 = Continue/Step.../Pause/Toggle BP/Detach。Attach は ps から一般
  mruby アプリ (type==USER, vm==mruby, running, self 除く) を列挙するモーダル
  ピッカーで選択。ドロップダウンは画面右端に right-anchor (240px 窓に収める)。
- **停止表示**: stopped イベント (on_update で非ブロッキング poll) 受信時、該当
  ファイルを自動オープンし停止行へカーソル移動 + ハイライト、Stack/Vars を取得。
- **アタッチ時にソース自動オープン**: attach 成功時、対象の script パスを
  `::FMRB::Debug.source_file(pid)` で取得し (E1 gem に追加、ctx->filepath を返す)、
  未保存編集がなければ load_file する。停止前に BP を置く導線が成立。
- **セッション後始末**: on_destroy で `end_debug_session` (detach + release)。

### E2 検証 (Linux sim headless, 実行結果)

`dbg_sample` を spawn 後、エディタを起動し入力注入で一巡を実行、各段でスクショ確認:
1. Ctrl+D で Debug メニュー → Attach → ピッカーに "Debug Sample (pid 4)" 表示
2. Enter で attach (ログ `FM-Editor: Editor debug: attached pid=4`)、下部ペイン出現
3. F6 pause → 停止。エディタが `dbg_sample.app.rb` を自動オープン、停止行 (ln27)
   をハイライト、Stack ペインに 7 フレーム (`#0 on_update ...:27` ～ top) 表示
4. F4 → Vars ペイン (`value = nil` 等 frame0 ローカル表示)
5. F10 step over → 停止行 ln28 (`@tick += 1`) へ前進、ハイライト移動
6. F5 continue → resume、ペイン `run / (not stopped)`
7. Ctrl+D → Detach → ペイン消滅、セッション解放 (ログ `detached pid=4`)

備考 (実装・検証で判明した事項):
- **Ctrl+D を Debug メニューのアクセラレータに追加**。設計は Alt+D だが、Linux sim
  の入力経路は SDL KMOD の ALT ビット (0x100) が下位バイト切り捨てで復元不可
  (`usb_task_linux.c` の注記どおり)。実機は USB HID 経由で Alt が届くため Alt+D も
  有効。Ctrl 系は 0x40 で経路を通る。
- **Enter/ESC の cross-platform バグを修正**: `handle_menu_key` 等が `ev[:keycode]`
  で Enter=40/ESC=41 (HID Usage ID) を判定していたが、Linux 経路では keycode に
  SDL keysym (Enter=13/ESC=27) が載るため一致しなかった (矢印キーは keysym==Usage
  ID で偶然一致していた)。`ev[:scancode]` (両経路で HID Usage ID) 判定に変更し、
  メニュー/ピッカーの Enter/ESC が sim でも動くようにした。
- 検証補助として root の `tools/fmrb_input.py` に `ctrl+`/`alt+` 修飾子を追加
  (alt は sim では no-op、注記済み)。ヘッドレスでエディタにキーを送る前に一度
  ウィンドウ本体をクリックしてフォーカスを与える必要がある。

## 計画からの逸脱 (理由付き)

1. **gem に spawn を追加せず**、テストは debugd の既存 `spawn` コマンド (proven:
   test_phase1.sh と同経路) で対象と試験アプリを起動し、試験アプリは `FmrbApp.ps`
   で対象 pid を得る方式にした。理由: 一般アプリ向けの Ruby spawn API が存在せず、
   計画 5.3 が「ランチャー起動 + ps で pid」方式を明示的に許容しているため。
   `FMRB::Debug` を純粋なデバッグ API に保てる利点もある (spawn は E2 のエディタ
   側で扱う)。
2. **`FMRB::Debug.owner`** (`:none/:remote/:local`) を API 表に無いが追加。owner
   検査がテスト・デバッグに有用で実装コストが極小のため。
3. 試験アプリは PASS 後にセッションを保持し、同一起動内で排他確認 (リモート
   attach->BUSY) を可能にした (計画 5.3 の自動化を 1 起動で満たす)。

## ctx 層のスレッド前提レビュー (計画 リスク項目 7)

`fmrb_debug_ctx.c` を通読。呼び出し元タスク固有の隠れた仮定は無し:
- `s_event_q` は単一コンシューマ前提 -> E0 の owner ゲートで担保 (LOCAL 中は
  debugd の forward_events を止め gem が消費)。
- `s_dctx[]` は `__atomic_*` と in_use release/acquire で保護済み。
- inspect の `out_body` は per-ctx `park_buf` を指す (共有 static ではない) が
  「次の inspect まで有効」。gem 側で即 `mrb_str` コピーするため問題なし。
- 自 VM attach のデッドロックのみ新規リスク -> gem 側 self-pid ガードで対処。

## 残作業

- **E2 の esp32 ビルド + flash 残量再測** (5.1): E2 で editor bytecode が増えたため
  `rake clean_all && rake build:esp32` (NARYAv3/S3) で残量を再確認する。bytecode は
  ターゲット非依存で linux ビルドでコンパイル済みのため増分は小さい見込み。
- **E2 Modern (併置ペイン) レイアウト**: 設計 doc sec 4.5 のとおり Retro 下部分割を
  先行実装済み。広画面 (Tab5) の右併置ペインは共通ロジック (状態管理/イベント/
  FMRB::Debug 呼び出し) の上に後段で追加する。
- **E2 の残利便性 (E3)**: 変数 `expand` の UI (ref 付き要素の展開)、BP 永続化、
  ファイルオープンダイアログ経由の「BP を先に置いてから起動」導線。
- **S3 実機テスト** (ユーザ作業、5.4): dbg_e1_test の緑 "PASS" 確認に加え、
  エディタで dbg_sample を Attach -> Pause -> Stack/Vars -> Step -> Continue -> Detach
  が実機で動くこと。音声/NTSC/操作感は本フェーズ対象外。
