# PicoRuby と MicroPython の移植方式の比較

同じ「fmrb_app の枠に入れたゲスト VM」でも、PicoRuby (mruby) と MicroPython の
移植は中身の作りがかなり違う。どこがなぜ違うのかを軸ごとに記録する。
今後 3 つ目の言語を足すときの判断材料でもある (Lua は両者の中間に
位置するので、要点だけ併記する)。

一言でいうと、**PicoRuby は「OS と一体化した主役の VM」、MicroPython は
「隔離された客人の VM」**という移植になっている。

## 早見表

| 軸 | PicoRuby | MicroPython | Lua (参考) |
|---|---|---|---|
| 本体の改変 | あり (lib/add / lib/patch / lib/replace を被せる) | ゼロ (port/mpconfigport.h のみ) | ゼロ |
| ビルド時生成 | 毎ビルド (mrbgems / picorbc) | オフライン生成をコミット (rake micropython:gen) | なし |
| 同時インスタンス | アプリごとに 1 VM、何本でも | システム全体で 1 本 (排他) | 何本でも |
| GC | 精密 (C スタックを見ない) | 保守的 (C スタック + レジスタを走査) | 精密 (Lua 内蔵) |
| メモリ | estalloc がプールを直接管理 | プールから 256KB を一塊で内蔵 GC へ | 割当関数差し替えでプール直結 |
| アプリ基盤の言語層 | mrblib をビルド時に bytecode 化 | prelude をソースのまま起動毎に compile | なし (薄い C バインディングのみ) |
| 停止 | Ruby レベルの協調 (stop メッセージ + @running) | VM フック + 非同期 abort | debug フック + エラー |
| スケジューラ連携 | mruby-task + FreeRTOS tick (深い) | なし (スレッド無効) | なし |
| カーネル/デスクトップ | 担える (主役) | 担わない (ユーザアプリ専用) | 担わない |

## 1. 本体の取り込み方と改変の吸収

PicoRuby は submodule (components/picoruby-esp32/picoruby) に対して
lib/add / lib/patch / lib/replace を Rakefile が被せる方式で、自前 gem の追加や
task_hal の差し替えなど**本体側への介入を前提に育ってきた**。介入の代償が
「submodule 直編集は上書きで消える」という運用上の罠で、編集場所の規約
(CLAUDE.md) で防いでいる。

MicroPython は本体無改変。設定は components/micropython/port/mpconfigport.h
1 ファイルに集約し、submodule にもコピー (mp_embed/) にも手を入れない。
これは MicroPython の embed port (ports/embed) が最初から「他人のビルド
システムに埋め込む」ために設計されているおかげで、生成 (qstr) を
rake micropython:gen に閉じ込めて生成物をコミットするため、日常ビルドは
素の C コンパイルだけで済む。

## 2. VM インスタンスの数 (最大の設計差)

- PicoRuby: `mrb_state` がインスタンス。**アプリごとに 1 VM、同時に何本でも**。
- MicroPython: VM 状態がグローバル (`mp_state_ctx`)。**システム全体で 1 本**。

このため MicroPython だけに acquire/release の排他 (fmrb_mp.c) と
「2 本目はエラーダイアログで拒否」という仕様上の妥協がある。強制 kill 経路でも
排他が解放されるようにしないと、応答しないアプリ 1 本で以後の Python アプリが
全部起動不能になる、という PicoRuby には無い故障モードも持つ。

## 3. メモリと GC の方式

PicoRuby は**精密 GC**。アロケータ (estalloc) がアプリのプールを直接管理し、
spawn は TLSF ハンドルすら作らない。GC はオブジェクトを正確に追跡するので、
C のスタックやレジスタを見る必要がない。

MicroPython は**保守的 GC**。プールから FMRB_MP_HEAP_SIZE (256KB) を一塊で
渡して内蔵 GC に管理させ、GC は **C スタックとレジスタを root として走査する**。
ESP32-S3 ビルドを唯一止めた GCREGS 問題 (レジスタ退避のアセンブラ実装が
Xtensa に存在せず、MICROPY_GCREGS_SETJMP で setjmp 方式に統一した。
report/phase4.md) はこの方式の固有問題で、PicoRuby では原理的に発生しない。

後始末も対照的で、mruby には「mrc_irep_free 後の mrb_close は二重解放になる
ためスキップする」という癖 (fmrb_app.c のコメント参照) があるが、MicroPython
は mp_embed_deinit + プール返却で素直に閉じる。

## 4. アプリ基盤 (言語層) の焼き込み方

- PicoRuby: FmrbApp / FmrbGfx (lib/add/picoruby-fmrb-app/mrblib/*.rb) は
  **ビルド時に bytecode 化されて gem に入る**。アプリ起動時のコストはゼロ。
- MicroPython: ファイルシステム import が無いので、prelude
  (components/micropython/prelude/*.py) を**ソースのまま C 文字列で焼き込み、
  アプリ起動のたびに compile + exec** する (S3 実機で 63-118ms、GC 8.5KB)。
  frozen bytecode (mpy-cross) を使えば PicoRuby 型に寄せられるが、
  道具立てが増えるので当面見送り (report/phase3.md, phase4.md)。

## 5. C バインディングの作り

PicoRuby の mrbgem C (lib/add/picoruby-fmrb-app/ports/esp32/) は IDF の中で
ビルドされるので **fmrb のヘッダを自由に include できる**。

MicroPython は qstr 抽出がホストの gcc でプリプロセスするため、
**ESP-IDF ヘッダを include すると生成が壊れる**。これが 2 ファイル分割
(modules/fmrb_module.c = qstr 対象でヘッダ制限あり / fmrb_bridge.c =
ファーム側) と、境界ヘッダ + 定数複製を `_Static_assert` で守る構造を生んだ
(report/phase3.md)。同じ理由で、mpconfigport.h から fmruby-core の関数を
参照するものを足したら、ホスト単体スモーク (port/test/main.c) にスタブが要る。

## 6. 停止とスケジューリング

PicoRuby は mruby-task による **VM 内スケジューラ + FreeRTOS tick 連携**
(task_hal) を持ち、OS との結合が深い。停止は stop メッセージと @running
フラグの協調が基本。

MicroPython はスケジューラ統合そのものが無い (スレッド無効、1 タスク 1 VM)。
停止は VM フック + mp_sched_vm_abort の**非同期 abort** で、ゲストの
`except:` に握り潰されない。代償として **abort 時は Python の on_destroy が
走らない** (資源は C 側 cleanup が回収する。known_limitations.md)。
tick のような OS 連携の複雑さが無いぶん、統合は PicoRuby よりずっと薄い。

## 7. 役割の違い

PicoRuby はカーネルとデスクトップまで担う主役の VM で、fmruby-core の
起動シーケンスと一体になっている。MicroPython (と Lua、BASIC) はユーザ
アプリ専用の客人で、fmrb_app の VM 種別 1 つとして完結する。この役割の差が
上の全部 — 改変の深さ、スケジューラ連携、後始末の癖 — の根にある。

## 8. なぜ MicroPython 側の移植が薄く済んだか

作業を通しての実感として、MicroPython は**言語として「移植される前提」で
設計されている**。本体無改変で済んだのは偶然ではなく、次の作りの結果だった。

- **ポートの契約が最初から切ってある**。コアは py/ に閉じ、ポート側は
  mpconfigport.h と数個の HAL 関数 (mp_hal_stdout_tx_strn 等) を書くだけ。
  embed port はその極端形で、「移植先のビルドシステムは知らない」前提の
  自己完結ソースを吐く仕組みが公式にある。
- **アーキテクチャ依存には必ず逃げ道がある**。NLR にも GC のレジスタ退避にも
  setjmp 版のフォールバックが用意されていて、Xtensa で止まったときの
  `#error` は解決策の設定名 (MICROPY_GCREGS_SETJMP) を名指しで教えてきた。
  エラーメッセージが移植者に向けて書かれているのは、移植が想定内の行為だと
  いう証拠。
- **機能が全部マクロで落とせる**。ROM レベルの段階制に加えて REPL・スレッド・
  コンパイラまで個別に外せるので、「ゲスト VM に要らないもの」は設定だけで
  削れた。
- **ヒープは一塊もらう設計** (gc_init に渡すだけ)。固定予算で動く組み込みの
  現実とそのまま噛み合い、割当関数の差し替えすら不要だった。

ただしこの移植しやすさは「単体で新しいチップに載せる」方向への最適化で、
「他の OS にアプリとして同居する」方向では妥協が出る。mp_state_ctx が
グローバルで多重インスタンスを作れないのが代表で、ここは lua_State /
mrb_state の設計が上。保守的 GC も同根で、「ポートが root 登録の面倒を
見なくていい」という移植の楽さと引き換えに、GCREGS のようなアーキテクチャ
依存を抱え込む — 楽さの代償は消えるのではなく場所を移す。また embed port は
本流ポートに比べると extmod (time / json 等) が付いてこないなど、
二級市民的な扱いも残っている。

PicoRuby は同じ組み込み向けでも最適化の方向が「小ささ」(mrbgems で機能を
刻む) にあり、「知らない環境に埋め込まれる」ことへの作り込みは MicroPython の
ほうが一枚上、というのが本移植での結論。

## 9. スレッドの実装方式 — ネイティブ直結か VM 内スケジューラか

並行処理の設計も両者で正反対で、fmruby-core との相性を分ける。

- **MicroPython の `_thread` はネイティブスレッドに 1:1**。FreeRTOS 環境では
  `_thread.start_new_thread()` のたびに本物の RTOS タスクが生まれ、自前の
  C スタックを持ち、スケジューリングは FreeRTOS のプリエンプションそのもの。
  ただしバイトコード実行は GIL で直列化されるので、得られるのは並列ではなく
  インターリーブ。GIL が外れるのはブロックする C 呼び出し (sleep / I/O) の間。
- **mruby (mruby-task) は VM 内スケジューラ**。Ruby レベルの Task が何本
  あっても、OS から見えるタスクはアプリタスク 1 本のまま。OS が管理する単位と
  VM 内の並行性の単位が分離されている。

「OS がタスクを所有する」fmruby-core の設計と相性が良いのは mruby-task 型で、
MicroPython の `_thread` 型は単体ファームウェア (MicroPython がチップの主人)
でこそ素直に働く。本移植で `_thread` を無効にした判断の詳しい理由は
known_limitations.md の「REPL なし / スレッドなし」を参照。
