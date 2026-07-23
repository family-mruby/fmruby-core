# Phase 2 進捗レポート: カーネル VM の Spinel 化 (Linux)

作業ブランチ: fmruby-core `feature/spinel-aot`。進行中。

## サマリ (現時点)

- **T2-1 完了・検証・コミット済み** (3cc0419): 制御反転 poll 化。mruby 回帰なし。
- **T2-3 完了**: MessagePack (pure-Ruby) を spinel/ に配置。
- **T2-2 / T2-4 核心達成**: カーネル VM 全体 (FmrbKernelImpl + 全 mixin + Spinel
  ベース層 + msgpack + FmrbConst) が **Spinel でコンパイル成功、生成 C も gcc で
  実警告ゼロ**。Phase 2 最大リスク (実カーネルが Spinel 互換か) を退治。
- **残**: T2-5 (native task loading + 未実装 fmrb_spx C 関数 + ビルド統合),
  T2-6 (シナリオ回帰・性能・soak 検証)。

## T2-1: 制御反転 poll 化 (完了)

- `kernel.c`: `_spin` を `_poll_message(timeout_ms)` に置換 (1 件受信 -> hash|nil)。
- `fmrb_kernel.rb`: main_loop を poll ループ + per-message rescue +
  上限付き burst drain (MSG_DRAIN_BURST=64) に。C->Ruby コールバックを排し
  mruby/Spinel で main_loop ソースを共有。
- 検証: mruby エンジンで desktop 描画・メニュー・Launcher/Shell 起動、エラーゼロ。

## T2-2 / T2-4: Spinel ベース層 + combined コンパイル (核心達成)

### 成果物 (`main/prebuild_scripts/spinel/`)

- `fmrb_ffi.rb`: FmrbSpx FFI 宣言 (23 関数 + buffers)。send は `:str`+明示長
  (埋め込み NUL 保持、検証済)、recv は `:binstr` 戻り (byte-exact String、
  type/src は out-param、検証済)。
- `fmrb_kernel_base_spinel.rb`: `class FmrbKernel` を FFI で再実装 (~24 メソッド)
  + `Log` / `Machine` モジュール + `SpxBytes` バイト列パーサ。window record /
  app-info をバイト列から symbol-hash に組み立て。
- `msgpack_pure.rb`: `MessagePack` として pack/unpack (Phase 0 の 32bit 安全版)。
- `tool/spinel/gen_const_rb.rb`: C ヘッダ (fmrb_msg.h/task_config.h/status_led.h)
  から FmrbConst サブセットを生成 (drift-safe)。kernel が使うのは ~9 定数
  (THEME_/KEY_/PROC_STATE_ は desktop 専用で Phase 4)。
- `tool/spinel/gen_kernel_combined.rb`: FFI -> const -> msgpack -> base ->
  mixins (sort 順) -> fmrb_kernel.rb を連結。require_relative と
  `#:spinel-strip-begin/end` ブロックを除去。

### デュアルビルドのための Ruby 書き換え (すべて mruby 互換)

Spinel は全コードパスを型推論コンパイルするため、以下を調整 (1 ソース維持):

1. **bareword "Set" 回避**: Spinel はソース (コメント含む) に `Set` があると
   `require "set"` を自動 splice し、その set.rb 自体がコンパイル不能。
   コメントから "Set" を除去。
2. **`hash[k] ||= v` (IndexOrWriteNode) 未対応**: `x = [] unless x` 形へ。
3. **FFI :str 境界の poly**: `_send_raw_message` / `_spawn_app_req` の String
   引数を `.to_s` で concrete 化 (poly を FFI に持ち込まない原則)。
4. **RTC ハードウェア (ESP32 専用)**: I2C/RX8900/RX8130 (mruby 専用 C クラス) を
   `#:spinel-strip-begin/end` で囲み、combined 生成時に除去。Linux では platform
   チェックで到達しないので挙動不変。mruby は verbatim。
5. **poly レシーバの `Array#delete`**: Spinel が String#delete に誤 dispatch。
   PUB/SUB の 2 箇所を明示ループ (rebuild) に。
6. **`Integer#chr`**: ランタイムに sp_str_chr 無し。`"\x00"*len` + setbyte に。

### Spinel/fork への follow-up (今 Phase では回避、後で fork 修正候補)

- `source_references_set` がコメント/文字列内の "Set" も誤検出 (heuristic が
  コメントを飛ばすべき)。
- poly レシーバの `.delete(int)` が String#delete に誤 dispatch (Array#delete
  であるべき)。
- `Integer#chr` / sp_str_chr がランタイム未実装。
- bundled set.rb 自体が Set#& を poly `other` でコンパイル不能。

## T2-5: FFI シム実装 + native ロード + ビルド統合 (完了)

- **(a) fmrb_spx C 関数 7 本を実装** (`main/kernel/fmrb_spx_kernel.c`):
  set_ready / check_protocol_version / check_ga_version / set_error_led /
  sync_time_to_host / app_info_snapshot / last_error。既存 mruby binding
  (`kernel.c`) と同じ下層 API を呼ぶ (fmrb_transport_check_version,
  status_led_set_error, fmrb_kernel_set_ready, fmrb_app_get_last_error_*,
  fmrb_app_get_context_by_id, CONTROL SET_TIME)。
- **(b) kernel spawn の分岐** (`fmrb_kernel.c`): raw-task の hello_kernel bring-up
  を廃し、Spinel カーネルを **FMRB_VM_TYPE_NATIVE** として `fmrb_app_spawn` で
  起動 (native_func = fmrb_kernel_entry を呼ぶ wrapper)。PROC_ID_KERNEL の
  context / message queue / state / lifecycle は mruby 版と同一 =挙動パリティ。
  FMRB_VM_TYPE_NATIVE と execute_native_function は既存基盤をそのまま利用
  (LOAD_MODE 追加は不要だった)。
- **(c) ビルド統合**: `rake spinel:gen` を gen_kernel_combined.rb ->
  `spinel --entry fmrb_kernel_entry` の 2 段に (combined .rb / .c を gen/ に生成)。
  CMake の prepare/generate を hello_kernel から fmrb_kernel_combined に切替。

### バグ修正: ffi_buffer は getbyte を持たない (Phase 2 の実バグ)

初回 T2-6 起動で `NoMethodError: undefined method 'getbyte'` によりカーネルが
起動直後に終了 (`check_terminated_apps -> update_window_list ->
_get_window_list -> win_buf.getbyte`)。原因は **ffi_buffer が `:ptr` (生ポインタ)
を返す型で、getbyte 非対応** (読み出しは ffi_read_* のみ、per-byte reader は無い)。
recv が動いていたのは `:binstr` が実 String を返すため。

**修正**: windows_snapshot / app_info_snapshot / last_error の 3 関数を
**`:binstr` 戻り値**に変更 (recv と同じ機構、sp_net_bin_len でバイト長公開、
実 String なので getbyte 可)。fork 変更不要。win_buf / info_buf / msg_buf の
ffi_buffer は削除。Ruby 側 (_get_window_list/_get_app_info/_get_last_error) は
戻り String をそのままパース。

## T2-6: シナリオ回帰検証 (完了, Linux headless)

`rake build:linux` (FMRB_KERNEL_ENGINE=spinel) + dev_run_check.sh + fmrb_input.py。
Spinel カーネルが実カーネルとして全経路動作、エラー/例外ゼロ:

- ブート -> desktop 描画、時刻同期 (RTC skip, host SET_TIME)、boot marker
  `main_loop started` を Ruby (fmrb_kernel.rb) から出力。
- 入力ルーティング: メニューバー click -> `Desktop overlay: active` トグル、
  ドラッグ処理。
- ウィンドウ管理: `_get_window_list` (:binstr 修正) が周期 cleanup で 2 分以上
  無事故 (初回クラッシュ地点を通過)。
- 実ユーザーアプリ spawn: Launcher から GPIO Viewer をダブルクリック ->
  kernel `Spawn request` -> `_spawn_app_req` -> `_get_app_info` (:binstr 修正) ->
  `HID target set` -> 別プロセス RUNNING -> 専用 canvas 描画。全経路成功。
- `Resources cleaned up` = 0 (カーネル無終了)、spx エラーログ 0 件。

### 性能計測 (2026-07-23 実施, Linux headless)

計測コードを共有カーネルソースに追加 (mruby/Spinel 同一コード):
`input_router.rb` の handle_hid_event 末尾で 1 イベント処理時間
(Machine.board_millis, ms 分解能) を蓄積し、1000 イベントごとに
`hid_lat: n/sum_ms/max_ms/ge1/ge5/ge10/gt25` を Log 出力。負荷は
fmrb_input.py による決定的な合成イベント列 (両エンジン同一):
(a) 通常 move 洪水 + 100 move ごとに click (上流の合成で実効 ~30Hz)、
(b) タイトルバー drag 中の move 洪水 ~60 秒 (毎 move で
_update_window_position + find_window_by_pid が走る最重経路)。
対象アプリは FM-Shell を Launcher から起動しフォーカス。

結果 (1000 イベント窓、sum_ms = 窓内合計処理時間):

| シナリオ | エンジン | sum_ms (窓ごと) | max_ms | >=1ms 件数 | >25ms 警告 |
|---|---|---|---|---|---|
| move+click | mruby  | 162 / 153 / 163 | 2 | 152-161 | 0 |
| move+click | Spinel | 2 / 0 / 0       | 0-1 | 0-2   | 0 |
| drag 中    | mruby  | 298             | 3 | 295     | 0 |
| drag 中    | Spinel | 2               | 1 | 2       | 0 |

- **平均処理時間はおよそ 80-150 倍改善** (move: ~0.16ms -> ~0.001ms、
  drag: ~0.30ms -> ~0.002ms)。max も 2-3ms -> 0-1ms でテールが消えた。
  mruby 版は 15-30% のイベントが 1ms 以上かかっていた (アロケーション/GC
  churn を含む) のに対し、Spinel 版はほぼ全イベントが ms 分解能未満。
- `hid_event slow (>25ms)` 警告は両エンジンともゼロ (x86_64 Linux は
  十分速い。25ms 警告は ESP32 実機での現象であり、実機比較は Phase 5)。
- 実行全体でエラー/例外ログ 0 件 (両エンジン)。
- 計測コードは共有ソースに残置 (イベントあたり整数演算数個 + 1000 件に
  1 行のログで、常時有効でも無害。ESP32 実機計測にもそのまま使える)。

**未実施のまま残す項目**: 30 分 soak はユーザ判断でスキップ。ヒープ使用量
比較は Spinel 側の統計配線が Phase 3 の estalloc フック導入とセットのため
延期 (fmrb_app_ps の FMRB_VM_TYPE_NATIVE が統計 0 を返し Monitor の
カーネル行が消えている件も同時に解決予定)。

### 追加バグ修正: `SpxBytes.name` が Module#name に解決される

GUI 実機確認でユーザが 2 症状を報告: (1) アプリウィンドウをマウスでドラッグ
移動できない (メニューバーのウィンドウ切替は効く)、(2) 起動直後の最初の 1-2
キーが落ちる。両方とも mruby では発生しない Spinel 固有。

原因は **1 つ**: `SpxBytes.name(buf, off, width)` が、定義した `def self.name`
ではなく組み込みの **Module#name (モジュール名 "SpxBytes" を返す) に解決**され、
window / app-info / last-error の全名前フィールドが "SpxBytes" に化けていた。
結果 `_get_window_list` の app_name が全部 "SpxBytes" になり、
`find_window_at` の `next if win[:app_name] == "system_desktop"` が効かず、
z=254 で全画面のデスクトップが最前面ウィンドウ扱いに。→ y>=13 の全クリックが
デスクトップにヒットし、アプリのドラッグ不可 + クリックのたびに
`_set_hid_target` がデスクトップに飛んでキーボードフォーカスが乱れる (初期キー
落ち)。

修正: `SpxBytes.name` -> `SpxBytes.read_name` に改名 (全 5 呼び出し)。検証済:
ドラッグでウィンドウ移動 OK (`Start drag: PID 4`)、起動直後 `ABCDEFGH` 8/8 着弾、
HID target のデスクトップ誤切替なし。Spinel の命名衝突ガチャ (bareword "Set" /
poly Array#delete 等と同種) にもう 1 件追加。

## Phase 2 判定

カーネル VM の Spinel 化は **機能・性能とも完了**。Spinel コンパイル済み
カーネルが mruby カーネルの drop-in 置換として desktop / 入力 / ウィンドウ
管理 / アプリ spawn を駆動することを headless で実証し、HID 処理レイテンシは
mruby 比およそ 80-150 倍改善 (上記計測)。残るは ESP32 実機 (Phase 5)。
30 分 soak はユーザ判断でスキップ、ヒープ比較は Phase 3 の estalloc
フック導入後に実施。
