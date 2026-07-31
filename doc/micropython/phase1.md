# Phase 1: ESP-IDF コンポーネント化と fmrb_mp ラッパ

## 目的

phase0 の生成物を ESP-IDF コンポーネントとしてビルドに組み込み、
fmruby-core 側から MicroPython を起動・実行・破棄する C API (fmrb_mp) を作る。
fmrb_app への統合はまだ行わない。

## 作業項目

1. **CMakeLists.txt**: components/micropython/CMakeLists.txt を作成。
   components/lua/CMakeLists.txt を手本に、
   - SRCS: mp_embed/ 以下の生成 C ソース一覧 + fmrb_mp.c
   - INCLUDE_DIRS: include, mp_embed, port
   - REQUIRES: fmrb_mem fmrb_common log
   で idf_component_register する。コンパイルフラグの追加が要る場合
   (警告抑制など) は target_compile_options で該当ソースに限定して付ける。
   生成ソースは GNU C 方言前提 (gchelper_generic.c が `register ... asm` で
   レジスタを固定する。report/phase0.md 気づき 4)。IDF 既定の gnu17 で通る
   見込みだが、通らない場合も -std を厳密 C に寄せる方向ではなく、該当
   ソース限定のオプションで解決する。

2. **fmrb_mp ラッパ**: components/micropython/include/fmrb_mp.h と
   components/micropython/fmrb_mp.c を作成。components/lua/fmrb_lua.c と
   同じ粒度の API にする:
   - `fmrb_err_t fmrb_mp_init(void)` — サブシステム初期化 (ログのみで可)
   - `fmrb_err_t fmrb_mp_acquire(fmrb_app_task_context_t* ctx)` —
     単一インスタンス排他の獲得。使用中なら FMRB_ERR_BUSY 系を返す。
     static な使用中フラグ + fmrb_mutex で守る。
   - `fmrb_err_t fmrb_mp_start(fmrb_app_task_context_t* ctx)` —
     ctx->mem_handle から GC ヒープ (FMRB_MP_HEAP_SIZE, 初期値 256KB) を
     fmrb_malloc し、mp_embed_init 相当でランタイムを立ち上げる。
     直後に mp_stack_set_limit で C スタック上限 (現在のタスクスタック残量
     から余裕を引いた値) を**必ず**設定する。省略するとすべてのスタック
     チェックが失敗し、最初の raise が nlr_jump_fail の無限ループに落ちて
     無反応ハングになる (report/phase0.md 気づき 1。スモークテスト
     port/test/main.c に再現コメントあり)。
   - `fmrb_err_t fmrb_mp_exec(fmrb_app_task_context_t* ctx, const char* src, size_t len, const char* path)` —
     ソース文字列を実行。未捕捉例外は traceback をログ (FMRB_LOGE) に出して
     エラー戻り値。正常終了は FMRB_OK。
   - `void fmrb_mp_close(fmrb_app_task_context_t* ctx)` —
     ランタイム終了 (mp_embed_deinit 相当)、ヒープ解放、排他解放。
     途中失敗のどの段階から呼ばれても安全にする。
   - 実際の embed port API 名 (mp_embed_init / mp_embed_exec_str /
     mp_embed_deinit 等) と引数は生成物のヘッダを読んで合わせる。

3. **標準出力の経路 (確認のみ)**: 生成物 mp_embed/port/mphalport.c の
   mp_hal_stdout_tx_strn_cooked は printf そのものなので、print の出力先は
   追加実装なしで Lua の print と同じ挙動 (プロセスの標準出力 = docker ログ /
   UART) になる見込み (report/phase0.md 気づき 5)。自己診断のログで実際に
   出力先を確認するだけでよい。期待どおりでない場合のみ port 層で対処する。

4. **自己診断 (暫定)**: FMRB_MP_SELFTEST を define したときだけ、起動
   シーケンスの一箇所から `fmrb_mp_acquire → start → exec("print('mp:', 1+1)")
   → close` を一度実行するコードを入れる (置き場所は fmrb_lua_init の
   呼び出し元に並べる)。phase2 で正式経路ができたら削除する。
   これは検証用の追加コードであり、既存コードをビルドから外すものではない。

## 注意

- グローバル状態が生きたまま二重に start されないよう、acquire を通らない
  start は assert で落とす。
- fmrb_mp.c 内のログタグは "fmrb_mp" に統一。
- mpconfigport.h の調整 (足りない機能・余計な機能) はこのフェーズで行い、
  変更したら rake micropython:gen で再生成してからビルドする。

## 完了条件

- rake build:linux が通り、未定義参照ゼロでリンクできる。
- FMRB_MP_SELFTEST 有効ビルドで tools/dev_run_check.sh を実行し、core の
  ログに `mp: 2` が出る (docker compose logs で確認)。
- SELFTEST を無効に戻した通常ビルドでも従来どおり起動する
  (dev_run_check.sh のスクリーンショットにデスクトップが出る)。
- 実測した GC ヒープ初期消費量 (start 直後の gc 空き) を記録。

判定結果・実測値・実装中の気づきは [report/phase1.md](report/phase1.md)。
