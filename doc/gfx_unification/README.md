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
  比較する (python.app の Shapes ページ、graphics.app.rb の Shapes
  セクションなど)。
- rake build:esp32 が S3 / P4 両方で成功する (サイズの増減も一言記録)。

## Phase C: App サービス層の共通化 (2026-08-01 詳細化)

A+B 完了後の調査で確認した App 側の重複は次の 4 つ。

1. **ウィンドウ canvas の初期化**: mruby (lib/add/.../ports/esp32/app.c の
   _init) と Python (fmrb_bridge.c の app_init) が同一手順の複製
   (透過キー 0x01、z_order、bg canvas の条件生成、ctx への登録)。
2. **追加 canvas の枠管理**: ctx->extra_canvas_ids のスロット確保 + 登録は
   現状 mruby のみだが、Python の API 拡充 (create_canvas_gfx 相当) で
   再複製が確定している。
3. **canvas の後始末が 4 箇所**: mruby _cleanup / python bridge cleanup /
   fmrb_app.c の通常・強制 kill の 2 経路。python bridge は bg canvas を
   消していない (C 側 cleanup が拾うので漏れはないが非対称)。
4. **HID イベント解読**: mruby app.c と python fmrb_module.c が
   fmrb_hid_msg.h の構造体をそれぞれサイズ検証・展開している。

### C1: canvas サービス

fmrb_gfx コンポーネント (fmrb_common に依存でき、main/ に依存しない) に
app canvas service を置く。ヘッダは fmrb_gfx_cmd.h と並べて新設でよい。

- `fmrb_app_canvas_init(ctx, ...)` — main + (条件付き) bg canvas を生成して
  ctx に登録。mruby _init / python app_init の複製部を置換する。
- `fmrb_app_canvas_create_extra(ctx, w, h, z_offset, transparent, color, *out)`
  — extra_canvas_ids のスロット管理込み。mruby _create_canvas の実体を移す。
- `fmrb_app_canvas_release_all(ctx)` — main / bg / extra を削除して 0 クリア。
  再入安全 (二重呼び出しで二重削除しない) にし、**fmrb_app.c の通常・強制の
  2 経路とバインディングの cleanup をすべてこれに畳む**。
- Lua の FmrbApp.create_canvas はスクリプト主導で main canvas を作る別形。
  実装時に登録先 (ctx->canvas_id か extra か) を確認し、同サービスに乗せる。

### C2: HID イベント解読の共通化

- fmrb_common に正規化構造体 (type / x / y / button / scancode / keycode /
  modifier / gamepad 系) と
  `fmrb_err_t fmrb_hid_event_decode(const fmrb_msg_t*, ...)` を追加。
  メッセージサイズの検証もここに一本化する。
- mruby app.c は「正規化構造体 -> mrb hash」、Python は「-> dict」だけになる。
- **Python 側の qstr 制約に注意**: fmrb_module.c から fmrb のヘッダは
  引けないので、正規化構造体は fmrb_mp_bridge.h に固定幅で写し、
  fmrb_bridge.c の `_Static_assert` で本体とずれたらビルドが落ちる形にする
  (phase3 の定数複製と同じ流儀)。
- Spinel はイベントを Ruby 側 (SpxBytes) で解読するので対象外。

### C3 (判断制): 受信ループ骨格の共通化

C1+C2 を終えた時点で mruby と python の _spin を並べ、差分が「言語への
呼び出しだけ」になっているかを見る。なっていれば
`fmrb_app_pump(ctx, timeout, ops)` (コールバック表) に畳む。差分が残る
(mruby は suspend/resume を Ruby 側で処理する等) なら**見送ってよい** —
その場合は理由を report に書く。C3 ありきで先に器を作らない。

### Phase C の完了条件

- fmrb_gfx_create_canvas / fmrb_gfx_delete_canvas の直接呼び出しが
  バインディングと fmrb_app.c から消え、サービス経由のみになる (grep 確認)。
- fmrb_hid_*_event_t のフィールド展開が mruby app.c / python fmrb_module.c
  から消える (grep 確認)。
- headless 検証: 4 言語の起動・描画・入力 (マウス/キー)・終了 + Spinel
  デスクトップ構成。
- **kill 経路の canvas 回収**: アプリ起動 -> タスクモニタから kill ->
  再起動、を 5 回繰り返し、canvas のリークが無いことをログで確認する
  (release_all の再入安全の確認を兼ねる)。
- mruby の suspend / resume (フルスクリーンアプリの切替) に退行が無い。
- rake build:esp32 が S3 / P4 両方で成功する。
- 結果・気づきは report.md に Phase C として追記。

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
