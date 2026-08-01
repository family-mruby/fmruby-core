# GFX 送出・組み立ての一本化 (App/Gfx 実装分散の解消)

言語バインディングごとに重複している描画コマンドの送出とコマンド組み立てを
components/fmrb_gfx に集約する。Phase A -> B の順に実施し、各フェーズ完了時に
報告する。Phase C (App サービス層) は A+B 完了後に本書へ詳細を追記してから
着手する。実装レポート (気づき・実測・申し送り) は
[report.md](report.md) に置く。

## 背景: 現状の分散

送出関数 send_gfx_command が 5 箇所にコピーされ、しかも 2 つの流儀に
割れている (2026-07-31 調査。発端は doc/micropython/report/phase3.md)。

| 実装 | ファイル | 流儀 |
|---|---|---|
| mruby | lib/add/picoruby-fmrb-app/ports/esp32/gfx.c | セマフォ方式 |
| Spinel | main/app/fmrb_spx_gfx.c | セマフォ方式 |
| Python | components/micropython/modules/fmrb_bridge.c | セマフォ方式 + 失敗時返却 |
| Lua | components/lua/extension/fmrb_lua_gfx.c | **セマフォ無し** 3 回リトライ + 100ms 待ち |
| BASIC | components/basic/extension/fmrb_basic_gfx.c | **セマフォ無し** 3 回リトライ + 100ms 待ち |

セマフォ方式は「HOST キューのうち HID 用の予約枠
(FMRB_HOST_HID_RESERVED_SLOTS) を GFX が侵食しない」ためのバックプレッシャ。
Lua/BASIC のリトライ方式はこれを迂回しており、**重い描画がキューを HID
予約分まで埋めて入力を取りこぼす**余地がある。本作業は整理であると同時に
この修正でもある。

さらにプリミティブごとの gfx_cmd_t 組み立て関数群も 5 ファイルに重複している
(mruby が全集、他は部分集合)。今後 Python の API を Ruby 版に追いつかせる
たびに重複が増えるので、その前に一本化する。

## ゴール

- 送出とコマンド組み立ての実体は components/fmrb_gfx に 1 つだけ。
- 言語バインディングは「言語から引数を取り出す -> コンストラクタ ->
  submit」の薄い皮になり、gfx_cmd_t のフィールドを直接触らない。
- 新しいプリミティブの追加は「fmrb_gfx に 1 箇所 + 各言語 1 行」になる。

## Phase A: 送出の一本化

1. **fmrb_gfx_submit の新設**: components/fmrb_gfx に
   `fmrb_err_t fmrb_gfx_submit(const gfx_cmd_t *cmd)` を実装する。
   正とする挙動は Python 版 (fmrb_bridge.c) のもの:
   - fmrb_current() で ctx を取り src_pid にする (5 実装とも同じ処理)
   - フローセマフォを take してから fmrb_msg_send(PROC_ID_HOST, ..., 5000)
   - 送信失敗時はセマフォを give で返却 (成功時は host_task が消費時に give)
   - リトライループは持たない (バックプレッシャで待つのが正)
2. **セマフォの注入**: セマフォの実体は host_task (main/) にあるので、
   fmrb_gfx 側に `void fmrb_gfx_set_flow_semaphore(fmrb_semaphore_t sem)` を
   用意し、host_task がキュー生成後の初期化で登録する。
   fmrb_gfx から main/ への依存は作らない (PRIV_REQUIRES main は使わない)。
   注入前に submit が呼ばれた場合はセマフォ無しで送る (現 Lua/BASIC と同等の
   挙動に落ちるだけで、初期化順の事故がハングにならない)。
3. **5 箇所の差し替え**: 各 send_gfx_command / spx_gfx_send を削除し、
   fmrb_gfx_submit の呼び出しに置き換える。
   fmrb_host_get_gfx_queue_semaphore の呼び出し元が消えるので、
   他に利用者がいなければ accessor 自体も削除する (Legacy を残さない)。

### Phase A の完了条件

- `grep -rn "send_gfx_command\|spx_gfx_send"` で実装の重複が 0 件
  (fmrb_gfx_submit のみが残る)。
- headless で 4 言語のデモ (.rb shapes / .lua / .bas / .py) が起動・描画・
  閉じるまで動く。
- **入力枠の保護の確認**: 描画の重いアプリ (BASIC か Python のビジー描画) を
  動かしながらマウス入力が遅延なく効くことを確認する
  (Lua/BASIC がセマフォ方式に変わったことの回帰確認を兼ねる)。
- **Spinel デスクトップ構成の実走**: fmrb_spx_gfx.c はこの構成でしか
  コンパイルされないので、`FMRB_APP_ENGINE_DESKTOP=spinel rake build:linux`
  でビルドし、デスクトップの描画とアプリ起動を headless で確認する
  (この構成しか通らない経路を必ず動かす)。

## Phase B: コマンド組み立ての共通化

1. **コンストラクタ集約**: fmrb_gfx.h に `fmrb_gfx_cmd_*` 群を定義する。
   手本 (全集) は mruby 版 lib/add/picoruby-fmrb-app/ports/esp32/gfx.c。
   まず 5 実装が組んでいる gfx_cmd_t の和集合を洗い、シグネチャは mruby の
   C 実装に合わせる。draw_text 系の文字列コピーの境界チェックも共通化する。
2. **バインディングの置換**: 5 実装のコマンド組み立てをコンストラクタ +
   fmrb_gfx_submit の呼び出しに置き換える。言語からの引数取り出し
   (mrb_get_args / lua_tointeger / mp_obj_get_int / BASIC の引数評価) は
   各バインディングに残る (そこが言語の皮の仕事)。
3. BASIC の screen_ops 経由の console 描画は対象外。gfx_cmd_t を組んでいる
   箇所だけを置き換える。

### Phase B の完了条件

- 各言語バインディングのソースに gfx_cmd_t のフィールド代入が残っていない
  (grep で確認。コンストラクタ呼び出しのみ)。
- Phase A と同じ 4 言語 + Spinel 構成の headless 検証が通る。
- 見た目の回帰が無いこと: 変更前後で同じデモのスクリーンショットを撮り
  比較する (python.app の Shapes ページ、shapes.app.rb など)。
- rake build:esp32 が S3 / P4 両方で成功する (サイズの増減も一言記録)。

## Phase C: App サービス層 (A+B 完了後に詳細化)

対象候補: canvas の生成/削除の共通サービス化 (所有権登録込み)、
HID イベントのメッセージ解読の正規化 (現状 mruby app.c と python
fmrb_module.c が fmrb_hid_msg.h の構造体を各自パースしている)。
gfx ほどの重複密度ではないため、A+B の結果を見てから範囲を決め、
本書に詳細を追記してから着手する。**A+B の作業中に先回りしない。**

## 実装ルール (この作業に固有の注意)

- mruby 側の編集は **lib/add/picoruby-fmrb-app/** の下で行う
  (components/picoruby-esp32/ 以下のコピーは rake setup で上書きされる)。
  lib/ を触ったら rake clean。
- Python 側で触るのは components/micropython/modules/fmrb_bridge.c のみの
  はず。fmrb_module.c (qstr 生成対象) に fmrb_gfx.h を include してはならない。
  fmrb_bridge.c は生成に無関係なので rake micropython:gen は不要
  (fmrb_module.c を触った場合のみ必要)。
- fmruby-core/CLAUDE.md の一般規約 (fmrb_err.h / fmrb_log.h / fmrb_mem.h、
  英語コメント、Legacy 残さない、ビルド対象から外す禁止、git 操作は
  ユーザ確認) はすべて適用。
- 検証はリポジトリルートの自律検証ツール (dev_run_check.sh /
  fmrb_screenshot.py / fmrb_input.rb) で headless に行う。
