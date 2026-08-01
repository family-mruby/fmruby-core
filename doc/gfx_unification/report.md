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

### Phase A 以外で見つかった問題

- **ランチャーに Python アプリが出ない** (別途修正済み。下記参照)。
  Phase A の検証では Shell の `run /app/demo/python.app.py` で起動した。

## Phase B: コマンド組み立ての共通化 (2026-08-01)

### 実装

- `fmrb_gfx_cmd.h` に `fmrb_gfx_cmd_*` コンストラクタを 30 個定義した。
  対象は 5 バインディングが組んでいた gfx_cmd_t の和集合 (mruby 版が全集)。
  すべて `void fmrb_gfx_cmd_XXX(gfx_cmd_t *cmd, fmrb_canvas_handle_t
  canvas_id, ...)` の形で、引数は対応する FmrbGfx メソッドの並び、型は
  格納先フィールドの幅に合わせた (キャストは呼び出し側 1 回で済む)。
- 文字列を持つ 2 つ (draw_text / load_sprite_image_bmp) は長さ指定版
  `fmrb_gfx_cmd_text_n` / `fmrb_gfx_cmd_load_sprite_image_bmp_n` を本体に
  し、NUL 終端版はその薄い皮にした。境界チェックは `cmd_copy_bytes` 1 箇所。
- 全コンストラクタは `cmd_begin` で `memset(cmd, 0, sizeof(*cmd))` してから
  埋める。
- 5 バインディング + `main/app/fmrb_app.c` の gfx_cmd_t 組み立てを置換。

### 気づき

1. **Spinel の draw_text だけ NUL 終端でない**。FFI は Ruby 文字列を
   (ptr, len) で渡すので、コンストラクタを NUL 終端前提にすると
   Spinel だけ独自に組み立てが残る。長さ指定版を本体にして解決した。
   BMP パスも同じ理由で `_n` 版が要る。
2. **`main/app/fmrb_app.c` に 6 番目の組み立て箇所があった** (README の
   5 実装に入っていない)。ウィンドウ移動時に PRESENT を送るカーネル側の
   処理で、`g_ctx_lock` を保持したままカーネルタスクから他アプリの
   canvas に送るため、**fmrb_gfx_submit は使えない** (アプリ用フロー
   セマフォで待つとロックを持ったままブロックする、src_pid も
   PROC_ID_KERNEL)。組み立てだけコンストラクタに置き換え、送出は
   その場に残してコメントで理由を書いた。
3. コンストラクタが全体を memset するので、`get_pixel` 以外で `sync` に
   ゴミが載る事故が構造的に起きなくなった。以前は指定初期化子に頼っていた。

### 検証 (headless, Linux ターゲット)

変更前 (Phase A 時点) と変更後で同じデモを撮って比較した。

| 対象 | 差分 |
|---|---|
| mruby Shapes | アプリ描画領域は完全一致 (差分はカーソル位置・時計・背後の別ウィンドウのみ) |
| BASIC basic.app.bas | 画面全体が 1 ピクセルも違わない |
| Lua lua.app.lua | 枠・文字とも一致 (経過秒表示のみ変化) |
| Python Shapes ページ | 完全一致 |
| Python Lines ページ | 完全一致 |
| Spinel デスクトップ + Shapes | 一致 (差分はカーソル位置のみ) |

Python は閉じるボタンで終了 (Reaped ログ確認)、Spinel 構成の Shapes も
同様。`grep -rn "GFX_CMD_\|\.params\."` の残りは host_task / fmrb_gfx 側の
実装と file_cmd_t (対象外) のみで、バインディングには 0 件。

### ビルド

| 構成 | 結果 | サイズ |
|---|---|---|
| linux (mruby デスクトップ) | OK | - |
| linux (spinel デスクトップ) | OK | - |
| esp32s3 (`rake build:esp32`) | OK | 0x255EC0 (2,449,088 B) |
| esp32p4 (`FMRB_HW_TARGET=TAB5 rake build:esp32`) | OK | 0x3DBCD0 (4,046,032 B) |

S3 は Phase A 時点 (0x256160) から **672 バイト減**った。5 バインディングに
散っていた組み立てコードがコンストラクタ 1 本に集約されたぶんが、関数呼び出しの
増加を上回っている。P4 は Phase A では測っていないので比較値は無い。

## 付随修正: ランチャーがアイコン無しアプリを落とす (2026-08-01)

GFX の一本化とは独立した不具合。Phase A の検証中に見つけたもの。

### 症状

`/app/demo/python.app.py` があり `flash/data/launcher_index` にも載って
いるのに、ランチャーに Python アプリが出ない。ファイルマネージャや Shell の
`run` からは起動できる。

### 原因

`main/prebuild_scripts/kernel/system_desktop/launcher.rb` の
`load_launcher_cache` がキャッシュ行を `next unless f[3]` で捨てていた。
キャッシュ 1 行は `label\tpath\ticon_char\ticon_file` で、`save_launcher_cache`
はアイコンファイルが無いアプリに空文字を書く (行がタブで終わる)。Ruby の
`String#split` は末尾の空要素を落とすので、この行は 3 要素にしかならず
f[3] が nil になって行ごとスキップされていた。

アイコンファイルを持つのは `VM_ICON_FILES` にある rb / lua / bas だけなので、
**アイコンを持たない VM (現状 py) のアプリが必ず全部消える**性質の不具合で、
Python 固有ではない。

### 修正

必ず在る 2 つのフィールドだけを要求し、アイコン欄は「無い = 空」と同じ扱いに
した。書き出し側の形式は変えていないので、既存の `/data/launcher_index` は
そのまま読める (再スキャン不要)。

### 確認

headless でランチャーに "Python" が P の文字アイコンで並び、ダブルクリックで
起動して Shapes ページを描画、閉じるボタンで終了することを確認した。
アイコンを持つ既存アプリの表示に変化は無い。

## 付随修正2: グラフィックス側のアセットが更新されない (2026-08-01)

上の付随修正でランチャーに Python が出るようになったあと、アイコンを描き
直しても古い絵が出続けることが分かった。原因は別で、影響範囲も広い。

### 原因

「グラフィックス側にファイルを置く」処理が 2 通りあった。

- **正しい方**: `fmrb_kernel_sync_file()` (main/kernel/fmrb_kernel.c)。
  ローカルの size と CRC32 を計算し、FILE_STATUS の応答と **exists・size・
  checksum が 3 つとも一致したときだけ**転送を省略する。graphics-audio 側の
  `handle_status` が `file_crc32()` を返すのはこのため。起動時の
  `[[sync_files]]` はこれを使っている。
- **各所に手書きされていた方**: `@gfx.file_status(dest)[:exists]` で存在確認
  して、無ければ `transfer_file`。**中身が変わっても差し替わらない。**
  launcher.rb / fmrb-gfx.rb#load_image / flappy / tile_map_test /
  draw_tile_test / nsf_player / sprite_editor の 7 箇所。rpg_demo だけは
  この欠陥に気づいていて、無条件転送で回避していた。

checksum は host_task の `file_status_response_cb` まで来ていたが、Ruby に
出す Hash が `{exists:, size:}` だけを組んでいたため、アプリ側からは CRC を
比較しようがなかった。

### 修正

操作を 1 つに寄せた。

- `fmrb_kernel_sync_file()` を唯一の実体として残し、`FmrbGfx#sync_file(src,
  dest:)` として mruby (gfx.c の `_sync_file`) と Spinel (FFI
  `fmrb_spx_gfx_sync_file`) の両方に出した。`FmrbKernel` 経由の起動時同期も
  同じ関数のまま。
- 手書きの 7 箇所と rpg_demo の回避策を `sync_file` に置換。
- `file_status` は残した。fmrb-tilemap.rb がサイズからタイル行数を出すのに
  正当に使っている。

### 途中で直した 2 つの問題

1. **`fmrb_kernel_sync_file` が非リエントラントだった**。結果構造体が関数内
   `static` (遅延応答で死んだスタックを踏まないための意図的な設計) で、
   カーネルタスク専用の前提だった。アプリからも呼ぶので mutex で直列化した
   (`init_file_sync`)。static のままなので元の安全性は保たれる。
2. **P4 では CRC が常に 0 だった**。host_task.c の Modern 経路が
   `checksum = 0` を返しており (コメントは "parity with WROVER (no CRC)"
   だが WROVER は現在 CRC を返す)、比較が必ず外れて **P4 だけ毎回全再転送**に
   なる。アセット系を全部 sync に寄せた今回で実害が出るところだった。
   `host_file_local_status` に CRC32 を計算させて解消した。

### 確認

graphics 側のキャッシュを**同じサイズの別画像** (ruby.bmp / lua.bmp /
basic.bmp、いずれも 1654 バイト) で汚してから起動し、CRC 不一致で再転送されて
正しい絵に戻ることを mruby 構成と Spinel 構成の両方で確認した。他のアイコンは
`up-to-date` でスキップされる。サイズが同じなので、効いているのが CRC だと
確定できる。

ビルド: linux (mruby / spinel デスクトップ)、esp32s3 (2,449,456 B)、
esp32p4 (4,046,608 B) すべて成功。P4 は今回直した Modern 経路が実際に
コンパイルされる唯一の構成なので必ず通した。

## Phase C: App サービス層の共通化 (2026-08-01)

### C1: canvas サービス

`components/fmrb_gfx/fmrb_app_canvas.{h,c}` を新設した。

- `fmrb_app_canvas_init(ctx, *canvas, *bg)` — window canvas と (条件付き)
  背景 canvas を作って ctx に登録。headless は何も作らず FMRB_OK を返すので、
  呼び出し側に headless 分岐が要らない。背景 canvas の生成失敗はログのみで
  致命にしない (壁紙が無くてもアプリは動く)。
- `fmrb_app_canvas_create_main(ctx, w, h, z, ...)` — スクリプトが寸法を
  決める形 (Lua の FmrbApp.create_canvas、BASIC コンソール) 用。
- `fmrb_app_canvas_create_extra` / `delete_extra` — extra_canvas_ids の
  スロット管理込み。
- `fmrb_app_canvas_release_all(ctx)` — main / bg / extra を解放。**フィールド
  を消してから削除する**ので再入安全。バインディングの cleanup、カーネルの
  通常 reap、強制 kill の 3 経路すべてがこれを呼び、最初の 1 回だけが働く。

置換したのは 5 バインディング (mruby / Spinel / Python / Lua / BASIC) と
`fmrb_app.c` の 2 経路。`fmrb_gfx_create_canvas` / `fmrb_gfx_delete_canvas` の
直接呼び出しはサービス外に 0 件。

**置換で消えたバグ**: BASIC は `console->canvas_id` を直接削除しながら
`ctx->canvas_id` を残していたため、カーネルの reap が同じ id を 2 度削除して
いた。グラフィックス側が既に別アプリへ配り直した id を消しうる。release_all
はフィールドを先に消すので構造的に起きない。

### C2: HID イベント解読

`components/fmrb_msg/fmrb_hid_event.{h,c}` に正規化構造体
`fmrb_hid_event_t` と `fmrb_hid_event_decode(data, size, *out)` を置き、
mruby app.c と Python fmrb_module.c は「正規化構造体 -> hash / dict」だけに
なった。サイズ検証もデコーダ 1 箇所。

**計画からの逸脱 (1)**: 計画では「Python は qstr 制約で fmrb のヘッダを
引けないので構造体を fmrb_mp_bridge.h へ写し `_Static_assert` で守る」と
していたが、fmrb_module.c は既に `fmrb_hid_msg.h` を include していた。
制約はファームウェアのヘッダを引くヘッダに対するもので、stdint だけの
plain なヘッダには当てはまらない。デコーダの引数を `fmrb_msg_t*` ではなく
`(data, size)` にして plain さを保ち、写しは作らなかった。

**計画からの逸脱 (2)**: 置き場所は fmrb_common ではなく fmrb_msg にした。
デコード対象のワイヤ形式 (fmrb_hid_msg.h) がそこにあり、両バインディングとも
既に fmrb_msg を REQUIRES している。

**副産物 2 つ**:
- Python に gamepad イベントが届くようになった。mruby は 5 種類すべて
  扱っていたが Python は key / mouse だけで、デコーダ共通化で自動的に揃った。
- `fmrb_hid_mouse_motion_event_t` を削除した。HID_MSG_MOUSE_MOVE の実際の
  ワイヤ形式は host_task.c が書く 6 バイト
  `[subtype, button=0, x(2), y(2)]` で、button イベントと同一。この構造体は
  5 バイトで一致しておらず、mruby と Python の両方が「この構造体を信じるな」
  というコメント付きで手動パースしていた。デコーダは button イベントの
  レイアウトで move も読む。

### C3: 見送り

計画の「差分が残るなら見送ってよい」に従って見送った。2 つの `_spin` の差は
言語への呼び出しだけではない。

1. **待ち方**: mruby は残り時間ぶん `fmrb_msg_receive` を 1 回ブロックする。
   Python は 100ms (FMRB_MP_SPIN_SLICE_MS) に刻んで毎回 `should_exit` を
   見る。stop メッセージはアプリのキューが満杯だと落ちるので、Python には
   このポーリングが要る。
2. **stop のラッチ**: Python は dispatch 前に
   `fmrb_mp_bridge_note_control` で stop を latch する (ハンドラが例外を
   投げても残るように)。mruby の `_spin` は `should_exit` を一切見ない。
3. **resize**: mruby は C から Ruby の @window_width / @user_area_* を
   直接書く。Python は `_handle_resize` に委譲し、更新はアプリ側。

共通化すると mruby の待ち方を変える (デスクトップを含む全 Ruby アプリの
挙動変更) か、待ち方自体をコールバック表に持たせることになり、共通部分は
receive を囲む while だけになる。器を作る価値が無いと判断した。

### 検証 (headless)

- 4 言語: mruby (Shapes の描画 + ページ切替クリック、Shell のキーボード)、
  Lua、BASIC、Python (Shapes/Lines ページ切替) の起動・描画・入力・終了。
- Spinel デスクトップ構成: 起動、ランチャー、Shapes の起動・描画・終了。
  `fmrb_spx_app_cleanup` が release_all で canvas を 1 回だけ解放。
- suspend / resume: フルスクリーンの BASIC アプリで desktop が
  SUSPENDED -> RUNNING と往復し、壁紙ごと正しく再描画される。
- canvas の対応: 上記すべてで作成と削除が 1 対 1。バインディングの cleanup が
  解放した後、カーネルの reap は何もしない (2 回目の削除ログが出ない)。

**kill 経路**: debugd (DBG_CMD_SPAWN / DBG_CMD_KILL) 経由で spawn -> kill を
5 回繰り返し、canvas のリークが無いことを確認した。

ただし**強制終了経路 (force_release_resources) は実行できていない**。
出荷されている 4 ランタイムはすべて 1 秒の猶予内に協調終了する
(`Exited on request`)。意図的にビジーループする Lua アプリを一時的に作って
試したところ、`fmrb_app_kill` が猶予ループを抜ける前に固まり、
`No response in 1000ms, forcing termination` のログにすら到達しなかった。
Phase C の変更とは無関係 (fmrb_app.c の差分は canvas 解放ブロック 2 箇所
だけで、固まる箇所より下流)。ただし**現状このコードは事実上到達不能**という
別の問題なので申し送る。再現手順は「canvas を作ってから
`while true do end` する Lua アプリを spawn して kill」。

### ビルド

| 構成 | 結果 | サイズ |
|---|---|---|
| linux (mruby デスクトップ) | OK | - |
| linux (spinel デスクトップ) | OK | - |
| esp32s3 | OK | 2,448,128 B (前コミットから -1,328) |
| esp32p4 | OK | 4,045,440 B (前コミットから -1,168) |
