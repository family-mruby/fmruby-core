# MicroPython ゲスト VM 取り込み計画

.py スクリプトを Lua / BASIC と同列のゲスト VM (`FMRB_VM_TYPE_MICROPYTHON`) として
fmruby-core 上で実行できるようにする。本フォルダはその実装計画である。
実装は phase0.md から順に進める。各フェーズは単独でビルド可能・検証可能な
状態で完了させ、次フェーズへ先回りしない。phase0-4 (第一段階) は完了済みで、
いまは phase5-9 (第二段階) を進めている。

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
- import できるのは組み込みモジュールと、**アプリ自身のディレクトリ
  および `/usr/lib/python` に置いた .py** (phase5 で対応)。それ以外の
  場所は探さない。共通層 (prelude) は module ではないので、import した
  ファイルからは `FmrbApp` などが見えない。
- REPL なし。スレッド (\_thread) なし。
- 標準ライブラリの範囲は mpconfigport.h の ROM レベル設定に従う
  (初期値 CORE 相当。不足があればフェーズ 4 までに調整)。
- ROM レベルによらず、**extmod/ のモジュールは既定では入らない**
  (embed port の生成物に extmod/ の .c が含まれないため。phase0 で確認)。
  py/ にあるもの (array, collections, math, struct, io, gc, sys,
  micropython) は使える。**必要なものは 1 つずつ取り込める**:
  `random` はそうやって入れてある (extmods/micropython.mk)。

## 実測で確定した資源コスト (phase4)

| 項目 | S3 (Retro) | P4 (Modern) |
|---|---|---|
| フラッシュ増加 | 127,912 B | 149,992 B |
| 内蔵 RAM (静的) | 445 B | 445 B |
| アプリパーティション残量 | 22% | 36% |

見込み (フラッシュ +200-300KB) を下回り、Lua とほぼ同じ規模に収まった。
GC ヒープ 256KB はアプリ用プール (PSRAM) から取るので内蔵 RAM に効かない。
詳細と測り方は [phase4.md](phase4.md)。

S3 実機で追加確認した値 (phase4.md「実機確認の結果」):

| 項目 | 値 |
|---|---|
| タスクスタックのピーク | 約 4,720 B (アプリタスク 16,384 B の 29%) |
| アプリ基盤の読み込み | 63-118 ms |
| GC ヒープの空き (基盤読み込み後) | 約 244 KB |

スタックは 16KB のままで足りる。起動・描画・排他・終了・再起動・停止応答・
GC のルート走査まで実機で確認済み。

## 第二段階 (phase5-9): 音付きのゲームが書けるところまで — 完了

**目標だった「Python で音付きのゲームが書けること」に到達した。**
見本は `flash/app/game/breakout/` のブロック崩しで、スプライト・日本語・
曲と効果音・タイマ・全画面切り替えを全部使っている。実機での確認だけが
残っている (書き込みが要るため、ESP32 の資源計測とまとめて行う)。

第一段階で後回しにしたもののうち、そこに要るものを順に埋めた。**実装の重い
部分は共通 C 層 (components/fmrb_gfx/fmrb_gfx_cmd.h) に揃っていた**ので、
主な仕事は `_fmrb` への接続と Python 層の記述だった。graphics-audio 側にも
音声タスクにも手を入れていない。

| フェーズ | 内容 |
|---|---|
| [phase5](phase5.md) | 土台: 性能の実測、時計・ファイル読み・メッセージ組み立ての拡張、ライフサイクルの残り、タイマ、pub/sub |
| [phase6](phase6.md) | 絵: 日本語表示 (`set_font`)、画像、スプライト。タイルは命令だけ |
| [phase7](phase7.md) | **アプリ間通信の実証**: Python 版 RoboPilot。世界 (Ruby) を操縦 (Python) が動かす |
| [phase8](phase8.md) | 音: BGM と効果音。音符はごみを出さない C 直行路で送る |
| [phase9](phase9.md) | 見本のゲーム (ブロック崩し) と文書の締め |

各フェーズでやってみて分かったことは [report/](report/README.md) にある。

**phase7 がこの段階の主眼**だった。Ruby のアプリ (robo_explorer) には
手を入れず、操縦側だけを Python で書き直して、同じ topic で会話できることを
示した。命令から結果までの時間は Ruby 版と差が無かった (3.2ms / 3.5ms)。

### この段階で測った数字 (シミュレーション)

| 項目 | 値 |
|---|---|
| Python の命令 1 つ | 30-200 ns |
| 描画命令 1 本 (fill_rect) | 45 us |
| 提示 (present) 1 回 | 33 us |
| スプライト 8 個の移動 + 提示 | 333 us/フレーム |
| ブロック崩しの 1 フレーム (最悪) | 1 ms (予算 33 ms) |
| 音符 1 個の割り当て | 0 バイト |
| アプリ間通信の往復 | 3.2 ms |

**フレームの予算を決めるのは Python の行数ではなく描画命令の本数**で、
動くものをスプライトに任せると 1 フレームは数百マイクロ秒で済む。
実機の数字は未測定 (ESP32 の計測時にまとめて採る)。

## 将来課題

第二段階でも実装しないと決めたもの。仕様上の制限は
[known_limitations.md](known_limitations.md) にまとめてある。

- タイルマップのクラス。地図を使うアプリは Ruby 側に既にあり、Python へ
  写すと共通層の読み込み時間を押し上げるだけになる。命令 (`draw_tile`) は
  phase6 で通すので、必要ならアプリ側に書ける。
- **共通層の事前コンパイル (frozen bytecode) はやらない** (2026-08-17 決定)。
  共通層は毎回そのまま解析されるので、phase5-8 で育つ分だけ Python アプリの
  起動は遅くなるが、道具立てを増やしてまで詰める場所ではないと判断した。
  代わりに**共通層に大きなものを置かない** (地図クラス等はアプリ側へ)。
- `GfxBlock` (描画のまとめ送り)、composite region、viewport。
- 共通層 (prelude) を import できる module にすること。**ユーザアプリの
  import は phase5 で入れる**が、共通層は今までどおりアプリの名前空間で
  実行する。import した側から共通層のクラスが見えないのはそのため。
- ファイルへの書き込み (得点の保存などができない)。読み込みは phase5 で通した。
- マイク入力、外部への MIDI 送出。
- ランチャー用のアイコン画像 (現状は文字 "P" のフォールバック)。
- REPL。
- 描画コマンド組み立ての共通化 — 現在 mruby / Spinel / Lua / Python の
  4 箇所に同じコードがある (report/phase3.md 参照)。

## フェーズ一覧 (第一段階。すべて完了済み)

| フェーズ | 内容 | 完了条件の要旨 |
|---|---|---|
| [phase0](phase0.md) | submodule 追加と embed port 生成の道具立て | rake micropython:gen が再現可能に動き、生成物がコミットされ、ホスト単体で hello が動く |
| [phase1](phase1.md) | ESP-IDF コンポーネント化と fmrb_mp ラッパ | rake build:linux で未定義参照ゼロ、自己診断で print 動作をログ確認 |
| [phase2](phase2.md) | fmrb_app 統合 (.py 起動・停止・排他) | headless で .py が起動・実行・終了し、2 本目が拒否される |
| [phase3](phase3.md) | FmrbApp / FmrbGfx バインディング (Ruby 版準拠) とデモアプリ、ランチャー対応 | Ruby アプリ同等のウィンドウ・文字・図形が描画され、閉じるボタンで終了できる |
| [phase4](phase4.md) | ESP32 ビルド・資源実測・制限事項文書 | 両 esp32 ターゲットでビルド成功、サイズ実測値を記録、実機確認 (ユーザ) |

各フェーズの実装レポート (気づき・実測値・申し送り) は
[report/](report/README.md) に置く。

PicoRuby の移植と何がどう違うか (改変の深さ、インスタンス数、GC 方式、
言語層の焼き込み方など) は [porting_comparison.md](porting_comparison.md) に
まとめてある。次の言語を足すときの判断材料もそちら。

## 生成物を作り直すタイミング

components/micropython/ には日常のビルドが触らない生成物が 2 つある。
どちらも `rake micropython:gen` が両方まとめて作り直すので、下記を編集したら
gen -> ビルド -> 生成物も一緒にコミット、の順で進める。

| 編集したもの | 作り直る生成物 | 理由 |
|---|---|---|
| port/mpconfigport.h | mp_embed/ (+ mp_embed_srcs.cmake) | 機能の有無が qstr 表と生成ヘッダに影響する |
| modules/fmrb_module.c | mp_embed/genhdr/ | 新しい qstr とモジュール登録を取り込む |
| prelude/*.py | prelude/fmrb_prelude.h | Python 層をファームに焼き込むため |

prelude だけなら `rake micropython:prelude` で足りる (gen は make と python3 を
要求するが、prelude は Ruby だけで済む)。
modules/fmrb_bridge.c は生成に関係しないので、編集してもビルドし直すだけでよい。

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
- それ以外の実装中の気づき・実測値・次フェーズへの申し送りは
  report/phase<N>.md に書く。計画文書は「何をやるか」、report は
  「やってみて何が分かったか」で分ける。仕様として確定した制約は
  本 README の「受け入れる制約」に上げる。
