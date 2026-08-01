# GFX 一本化 実装レポート

作業計画は [README.md](README.md)。

## Phase A: 送出の一本化 (2026-08-01)

### 実装

- 新設 `components/fmrb_gfx/fmrb_gfx_cmd.{h,c}`
  - `fmrb_err_t fmrb_gfx_submit(const gfx_cmd_t *cmd)`
  - `void fmrb_gfx_set_flow_semaphore(fmrb_semaphore_t sem)`
  - 挙動は Python 版 (fmrb_bridge.c) をそのまま正とした。
    fmrb_current() -> セマフォ take -> fmrb_msg_send(PROC_ID_HOST, 5000) ->
    失敗時のみ give で返却。リトライループは持たない。
- `main/kernel/host/host_task.c`
  - `fmrb_host_task_init()` の**成功パス末尾**で
    `fmrb_gfx_set_flow_semaphore(g_host_gfx_queue_semaphore)` を呼ぶ。
  - `fmrb_host_task_deinit()` で削除前に `fmrb_gfx_set_flow_semaphore(NULL)`。
  - `fmrb_host_get_gfx_queue_semaphore()` は利用者が消えたので削除
    (host_task.h の宣言も削除)。
- 5 実装の送出関数を削除し `fmrb_gfx_submit` 呼び出しに置換
  (mruby / Spinel / Python / Lua / BASIC)。`grep -rn
  "send_gfx_command\|spx_gfx_send"` は 0 件。
- 依存の整理
  - `components/micropython/CMakeLists.txt` の `PRIV_REQUIRES main` を削除
    (host_task.h を参照しなくなったため)。
  - gfx.c / fmrb_spx_gfx.c / fmrb_bridge.c から `host_task.h` を削除。
  - 送出関数と一緒に不要になった include (Lua/BASIC の fmrb_msg.h、
    BASIC の fmrb_rtos.h、Spinel の fmrb_log.h + TAG) も削除。

### 気づき

1. **セマフォ登録は init の末尾でないと危険**。
   セマフォ生成直後に登録すると、その後のキュー生成やタスク生成が失敗した
   エラーパスで `fmrb_semaphore_delete` されたハンドルが fmrb_gfx 側に
   残る。成功パス末尾に置き、deinit で NULL に戻す形にした。
2. **fmrb_gfx.h には置けなかった**。`fmrb_gfx_msg.h`
   (gfx_cmd_t の定義元) が `fmrb_gfx.h` を include しているため、逆向きの
   include は循環になる。宣言は新ヘッダ `fmrb_gfx_cmd.h` に分けた。
   Phase B のコンストラクタ群もここに置く前提の名前にしてある。
3. **Lua/BASIC のリトライ方式は本当に迂回だった**。両者とも
   `fmrb_msg_send` を 3 回 + 100ms 待ちで叩くだけで、HID 予約枠
   (FMRB_HOST_HID_RESERVED_SLOTS = 32) の保護を受けていなかった。今回で
   5 実装すべてが同じバックプレッシャに乗った。

### 検証 (headless, Linux ターゲット)

デスクトップ = mruby 構成:

| 言語 | アプリ | 起動 | 描画 | 終了 |
|---|---|---|---|---|
| mruby | /app/demo/shapes.app.rb | OK | OK (図形ページ) | OK (閉じるボタン -> Reaped) |
| Lua | /app/demo/lua.app.lua | OK | OK (テキスト + 経過秒) | 注記参照 |
| BASIC | /app/demo/basic.app.bas | OK | OK (PRINT 出力全文) | OK (END -> Reaped) |
| Python | /app/demo/python.app.py | OK | OK (Shapes/Lines ページ) | OK (閉じるボタン -> Reaped) |

- Lua デモは `while running do ... end` に終了条件が無く、閉じるボタンを
  描くだけで on_event を処理しない (flash/app/demo/lua.app.lua)。
  本作業以前からの デモ側の作りで、送出経路の問題ではない。

**入力枠の保護**: BASIC アプリ (グリフシート構築で GFX を連続発行、
host 側計測で 283 cmds/s) の描画中にマウス移動を注入し、350ms 後の
スクリーンショットでカーソルが指定座標に追従していることを 8 点で確認。
Lua/BASIC がセマフォ方式に変わったことによる入力の詰まりは無い。

**Spinel デスクトップ構成**: `FMRB_APP_ENGINE_DESKTOP=spinel rake build:linux`
でビルドし headless 起動。壁紙・メニュー・ランチャー・タスクバー
(すべて fmrb_spx_gfx.c 経由) が描画され、BASIC アプリと mruby Shapes の
起動・描画・終了まで確認。この構成でしかコンパイルされない経路も動作。

**ESP32 ビルド**: Phase A の完了条件には無いが CMakeLists を触ったので
確認した。`rake build:esp32` (esp32s3) 成功、`fmruby-core.bin` は
0x256160 バイト (パーティション 0x300000 の 78%)。

### Phase A 以外で見つかった問題 (未修正、申し送り)

- **ランチャーに Python アプリが出ない**。
  `main/prebuild_scripts/kernel/system_desktop/launcher.rb` の
  `load_launcher_cache` が `next unless f[3]` でアイコン列の無い行を捨てて
  いる。`flash/data/launcher_index` の Python 行は末尾のアイコン欄が空
  (`Python\t/app/demo/python.app.py\tP\t`) で、Ruby の `split("\t")` は
  末尾の空要素を落とすため f[3] が nil になり、行ごとスキップされる。
  今回の検証では Shell の `run /app/demo/python.app.py` で起動した。
  Phase A の範囲外なので触っていない。
