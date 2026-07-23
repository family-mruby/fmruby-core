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

## 残タスク

- **T2-5**: (a) 未実装/stub の fmrb_spx C 関数を実装 (app_info_snapshot,
  last_error, set_error_led, set_ready, check_protocol/ga_version,
  sync_time_to_host)。(b) `fmrb_app.c` に FMRB_LOAD_MODE_NATIVE 追加、kernel
  spawn を FMRB_KERNEL_ENGINE で分岐 (native + fmrb_kernel_entry)。
  (c) `compile_ruby_to_spinel.cmake` / `rake spinel:gen` を hello_kernel から
  fmrb_kernel_combined_spinel に切替。
- **T2-6**: dev_run_check.sh + fmrb_input.py でシナリオ回帰、性能計測
  (max/avg latency, ヒープ)、30 分 soak。

## Phase 2 判定の見通し

カーネル Ruby が Spinel でコンパイル可能と実証できたことで、残りは C 実装 +
ビルド統合 + 実機検証の「配線と計測」。設計上の未知は大きく減った。
