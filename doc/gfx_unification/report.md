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
