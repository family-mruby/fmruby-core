# MicroPython ゲスト VM 取り込み計画

.py スクリプトを Lua / BASIC と同列のゲスト VM (`FMRB_VM_TYPE_MICROPYTHON`) として
fmruby-core 上で実行できるようにする。本フォルダはその実装計画である。
実装は phase0.md から phase4.md を順に進める。各フェーズは単独でビルド可能・
検証可能な状態で完了させ、次フェーズへ先回りしない。

## 取り込み方式 (結論)

MicroPython 公式リポジトリ (micropython/micropython) の組み込み用ポート
`ports/embed` を使う。これは MicroPython を単体ファームウェアとしてではなく、
他のアプリケーションに埋め込むための自己完結した C ソース一式
(qstr 等の生成済みヘッダを含む) を生成する仕組みで、生成後は普通の C ソースとして
ESP-IDF コンポーネントにできる。`examples/embedding` に手本がある。

esp32 向け公式ポート (`ports/esp32`) は使わない。あちらはチップ全体
(FreeRTOS メインタスク・ヒープ・ペリフェラル) を専有する設計で、
fmruby-core のような「OS 側がタスクとメモリを管理し、VM は 1 アプリとして動く」
形に合わない。

## 既存 Lua 統合との対応

| 既存 (Lua) | MicroPython |
|---|---|
| components/lua/ (本体 submodule + wrapper + extension) | components/micropython/ (本体 submodule + 生成物 + port + modules) |
| lua_newstate(lua_fmrb_alloc, ctx) で割当関数差し替え | fmrb_malloc でヒープを一塊確保し mp_embed_init に渡す (GC は MicroPython 内蔵) |
| FMRB_VM_TYPE_LUA (fmrb_app.h) | FMRB_VM_TYPE_MICROPYTHON を追加 |
| create_vm_lua / execute_lua_script / fmrb_lua_close (fmrb_app.c) | create_vm_micropython / execute_micropython_script / fmrb_mp_close |
| lua_sethook による終了要求チェック | MICROPY_VM_HOOK_LOOP で ctx->should_exit を監視し脱出 |
| fmrb_lua_gfx.c (gfx/app モジュール登録) | MP_REGISTER_MODULE によるユーザ C モジュール |
| fmrb_app_spawner.c の ".lua" 判定 | ".py" 判定を追加 |

## 主要な設計判断

1. **同時実行は 1 本まで**。MicroPython の VM 状態はグローバル変数
   (mp_state_ctx) にあり、lua_State / mrb_state のような多重インスタンスを
   本家はサポートしない。Python アプリの 2 本目の起動は spawn 時点で拒否する。
   本体に手を入れて多重化することはしない (改造量が割に合わない)。
2. **ヒープはアプリ用メモリプールから一塊で確保**。アプリごとのプール
   (FMRB_MEM_POOL_SIZE_USER_APP = 500KB) から固定サイズ (初期値 256KB、
   define で調整) を fmrb_malloc し、MicroPython の GC に管理させる。
   プール破棄で確実に回収されるので、異常終了時のリークも既存機構で吸収される。
3. **停止は VM フックで**。Lua の lua_sethook と同型に、VM ループのフックで
   ctx->should_exit を監視し、例外 (SystemExit 相当) または vm abort 機構で
   実行を巻き戻して mp_embed_exec_str から戻す。
4. **qstr 等の生成物はコミットする**。生成 (make + python3 が必要) は
   rake タスクに閉じ込め、日常のビルド (rake build:linux / build:esp32) では
   生成済みソースをコンパイルするだけにする。バインディング用 C モジュールを
   変更したときのみ再生成が要る。
5. **NLR は setjmp 実装に固定** (MICROPY_NLR_SETJMP)。linux (x86_64) と
   esp32 (Xtensa / RISC-V) の両ターゲットで同一挙動にするため。

## 受け入れる制約 (仕様として明記するもの)

- Python アプリは同時に 1 本のみ。
- import できるのは組み込みモジュールのみ。ファイルシステムからの import は
  当面サポートしない (フェーズ外の将来課題)。
- REPL なし。スレッド (\_thread) なし。
- 標準ライブラリの範囲は mpconfigport.h の ROM レベル設定に従う
  (初期値 CORE 相当。不足があればフェーズ 4 までに調整)。
- ただし ROM レベルによらず、**extmod/ に実装のあるモジュールは使えない**
  (time, json, os, re, random, binascii 等)。embed port の生成物には
  extmod/ の .c が含まれないため (phase0 で確認)。py/ に実装のあるもの
  (array, collections, math, struct, io, gc, sys, micropython) は使える。

## リスク・実測で確定する項目

- フラッシュ増加は 200-300KB 程度の見込み。phase4 で実測して本書に記録する。
- 静的 RAM (bss/data) の増加も phase4 で実測する。内蔵 RAM が逼迫している
  ため、増加が大きい場合は ROM レベルを落とすか置き場所を調整する。
- パーサ/コンパイラが C スタック再帰を使うため、タスクスタック量の見直しが
  要る可能性がある (phase2 で確認)。

## フェーズ一覧

| フェーズ | 内容 | 完了条件の要旨 |
|---|---|---|
| [phase0](phase0.md) | submodule 追加と embed port 生成の道具立て | rake micropython:gen が再現可能に動き、生成物がコミットされ、ホスト単体で hello が動く |
| [phase1](phase1.md) | ESP-IDF コンポーネント化と fmrb_mp ラッパ | rake build:linux で未定義参照ゼロ、自己診断で print 動作をログ確認 |
| [phase2](phase2.md) | fmrb_app 統合 (.py 起動・停止・排他) | headless で .py が起動・実行・終了し、2 本目が拒否される |
| [phase3](phase3.md) | gfx/app バインディングとデモアプリ、ランチャー対応 | ランチャーから Python デモが起動し描画がスクリーンショットで確認できる |
| [phase4](phase4.md) | ESP32 ビルド・資源実測・制限事項文書 | 両 esp32 ターゲットでビルド成功、サイズ実測値を記録、実機確認 (ユーザ) |

## 実装時の全般ルール (実装担当への申し送り)

- fmruby-core/CLAUDE.md を厳守する。特に:
  - submodule (components/micropython/micropython を含む) は直接編集禁止。
    設定・移植の差分はすべて components/micropython/port/ 側のファイルで吸収する。
    どうしても本体側の変更が要る場合は実装せずに報告する。
  - main/ 以下の戻り値は fmrb_err.h、ログは fmrb_log.h、メモリは fmrb_mem.h。
    素の malloc 禁止 (MicroPython 生成物内部の実装は除く。ただし GC ヒープが
    fmrb_malloc 由来であることは保証する)。
  - コード内コメントは英語。Legacy コードは残さない。
  - 問題回避のためにソースをビルド対象から外すことは禁止。
  - git 操作 (submodule 追加を含む) はユーザに確認してから行う。
- 検証はリポジトリルート (family-mruby) の自律検証ツールで headless に行う:
  tools/dev_run_check.sh (起動+画面撮影) / tools/fmrb_screenshot.py /
  tools/fmrb_input.rb (入力注入)。使い方はルートの CLAUDE.md 参照。
  シミュレーションを再起動するときは 3 コンテナまとめて再起動する
  (core だけの再起動は framebuffer が壊れる)。
- lib/ 以下を触ったら rake clean、linux/esp32 の切り替え時は rake clean_all。
  .env の FMRB_HW_TARGET が環境変数指定を上書きする点にも注意。
- 各フェーズの「未確定事項」は実装時に実地で確定し、結果を該当フェーズの
  文書に追記して育てる。本家ドキュメントの URL やページ番号などの出典情報は
  文書に書かない。
