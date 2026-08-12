# 実装指示書 P7: WebConsole のブラウザ内型支援 (WASM 経路)

対象: 実装担当セッション。前提: P4/P4.5/P4.6 完了 (sig/ に API と二国語 doc、
gen_help、engine の 3 エントリが揃っている)。plan.md と report/p4*.md を先に
読むこと。report は doc/editor_ti/report/p7.md へ。タスクごとにコミット。

## ゴール

tool/web の簡易エディタに、**ブラウザ内で完結する補完・ホバー・診断
(+ F1 ヘルプ)** を足す。エンジン + db を emscripten で WebAssembly 化して
ブラウザで走らせる。**実機も BLE も不要** (実行時も開発時も)。source of
truth は sig/ の 1 箇所で、二国語 (P4.6 の `<<en>>`) もそのまま効く。

## この経路が firmware より単純な理由 (前提の確認)

- **WASM ビルドに mruby VM は入らない**。prism の allocator は standalone
  分岐で `xmalloc = malloc` になる (prism_xallocator.h)。**P2 の global_mrb /
  B 案 / fmrb_mem / プールは一切不要**。engine.c + prism.c + 生成 db.c を
  emcc でそのままリンクするだけ。
- engine は純 C で、`rake ti:test` が gcc で native ビルド済み。emcc は
  「もう一つの C ターゲット」。
- **fork は無改修** (engine をそのまま WASM 化するだけ)。wrapper は
  fmruby-core 側 (tool/web/wasm/) に置く。

## 前提ツール (2026-08-12 導入・動作確認済み)

- **emscripten** `emcc 6.0.6` (emsdk。各シェルで `source ~/emsdk/emsdk_env.sh`
  してから使う — PATH に載る)。
- **Playwright + headless Chromium** 導入済み
  (`node -e "require('playwright')"` OK、chromium executablePath 解決済み
  = `~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome`)。
- 念のため実装冒頭で `emcc --version` / `node -e "require('playwright')"` を
  再確認する。

## 確認済みのパス (調べ直し不要)

- **engine**: `vendor/picoruby-ti/` (rake ti:setup 済み)。src/**/*.c、
  include/ に 4 エントリのヘッダ:
  `ti_fill_suggestions_at_cursor` (picoruby_ti_suggest.h) /
  `ti_find_hover_at_cursor` (picoruby_ti_hover.h) /
  `ti_fill_diagnostics` (picoruby_ti_diagnostic.h) /
  `ti_find_call_context` (picoruby_ti_call_context.h)。生成 db は
  src/generated/ (rake ti:gen で sig/ から出す)。
- **prism**: `components/picoruby-esp32/picoruby/mrbgems/mruby-compiler/lib/prism/`
  (src/*.c、include/)。**PICORB_VM_MRUBY を定義しなければ xmalloc=malloc**
  (prism_xallocator.h の standalone 分岐、確認済み) = WASM は malloc で完結。
- 既存の native ビルド手本: `rake ti:test` / `ti:probe` (Rakefile 598-)。
  engine を gcc でリンクして走らせている。emcc 版はこれのターゲット違い。

## 自律検証の方針 (ユーザはブラウザを操作しない)

3 段で、下ほど強い保証。ユーザのクリックは一切要らない。

1. **型推論の正しさ**: `rake ti:test` (native、既存)。WASM と無関係にここで
   担保。ブラウザゼロ。
2. **WASM モジュールの動作**: **Node で自律**。emcc が吐いた .wasm/.js を
   Node で読み、sample ソースに suggest/hover/diagnose を呼んで期待値を
   assert する Node テストを書く (tool/web/wasm/test_wasm.mjs)。ブラウザ不要。
3. **ページ/UI**: **Playwright headless で自律**。ローカル静的サーバで
   tool/web を配信 → headless Chromium でページを開き、エディタに打鍵を
   注入 → 補完ドロップダウンの DOM を assert + スクリーンショット。
   sim + fmrb_screenshot のブラウザ版。ユーザ操作ゼロ。

## T1: WASM ビルド (rake ti:wasm)

- Rake タスク `ti:wasm`: engine の src/**/*.c + prism の src/*.c + 生成 db.c を
  emcc でリンクし、`tool/web/wasm/ti.js` + `ti.wasm` を出す。
  - db は `rake ti:gen` と同じ tidbgen を使い、WASM 用の出力先に生成
    (sig/ から。firmware と同じ db)。
  - prism は firmware と同じ vendor コピーを使う (mruby 無しでコンパイル =
    PICORB_VM_MRUBY を定義しない → xmalloc=malloc)。
  - emcc フラグ: `-O2 -sMODULARIZE -sEXPORT_ES6 -sALLOW_MEMORY_GROWTH`
    `-sEXPORTED_RUNTIME_METHODS=ccall,cwrap` +
    `-sEXPORTED_FUNCTIONS=@wasm/exports.txt` (T2 の export 一覧)。
- **生成物 (ti.js/ti.wasm/db) は gitignore** (flash/help や型 db と同じ
  方針。source は sig/ + engine)。

## T2: emscripten wrapper (tool/web/wasm/ti_wasm.c)

engine の C API (ti_fill_suggestions_at_cursor / ti_find_hover_at_cursor /
ti_fill_diagnostics / ti_find_call_context) を、JS から呼べる薄い関数で包む。
editor_ti_bridge.c の「fill してから index で get」の形を踏襲するが、
**プール/ロック/prism-hook は不要** (malloc で足りる):

- `int tw_suggest(const char *src, int len, int cursor)` -> 候補数。
  内部で TiSourceList を組んで ti_fill_suggestions_at_cursor。結果を static
  配列にコピー。
- `const char *tw_suggestion(int i, int field)` (label/detail/doc) 等の getter。
- 同様に `tw_hover(src,len,cursor)` + getter、`tw_diagnose(src,len)` +
  getter (診断は行/桁に変換した値を返す)、`tw_call_context` (signature help)。
- 文字列は UTF-8 のまま返し、JS 側で `UTF8ToString`。
- EMSCRIPTEN_KEEPALIVE を付け、exports.txt に列挙。

## T3: JS モジュール (tool/web/js/ti.js — WASM ラッパ)

- `ti.wasm` をロードし、`suggest(text, byteOffset)` /
  `hover(text, byteOffset)` / `diagnose(text)` / `callContext(...)` を
  Promise で返す ES モジュール。
- **二国語 split を JS に移植** (P4.6 と同じ `<<en>>`): doc/要約を表示する前に
  現在言語で割る。言語源はブラウザ側 (下記 T4 のトグル or navigator.language)。
- F1 ヘルプ本文は T5 のバンドルから引く。

## T4: エディタ UI (tool/web/js/app.js の textarea エディタに追加)

現在の textarea + editorHighlight に足す:
- **補完**: 識別子/`.`/`::` の後で Ctrl+Space (ブラウザは Tab を奪えないので
  Ctrl+Space、かな衝突は無い) → ドロップダウン。Enter/Tab 確定、Esc 閉じ。
  選択候補の doc を下部に (二国語 split 後)。
- **ホバー**: 語にマウスを重ねる or キーで型/シグネチャをツールチップに。
- **診断**: 入力が落ち着いたら (debounce) diagnose を呼び、該当範囲に波線 +
  一覧。ブラウザなので保存を待たず入力追従でよい (実機と違い速い)。
- **言語トグル**: ja/en の切替 UI (または navigator.language 初期値)。
  P4.6 と同じ体験をブラウザでも。
- F1 (or ボタン) でヘルプパネル (T5)。

## T5: ヘルプのブラウザ用バンドル (任意、後回し可)

- gen_help と同じ入力 (sig/ の doc 長文) から、ブラウザ用に
  `tool/web/wasm/help.json` (`{name: markdown}` + index) を生成する
  小タスク (gen_help を JSON 出力にする派生 or 別スクリプト)。生成物は
  gitignore。
- UI はパネルに markdown をレンダリング。二国語 split 適用。
- **v1 では省略可**: 補完/ホバー/診断が本体。ヘルプは次段でよい。

## T6: 自律検証

1. **Node (WASM ロジック)**: `node tool/web/wasm/test_wasm.mjs` が
   `n = 42` の `n.` で Integer メソッド、`class MyApp < FmrbApp` 内の
   `@gfx.` で draw_*、誤引数で診断 1 件、を assert して pass 表示。
2. **Playwright (UI)**: `node tool/web/test/ui.mjs` (or Playwright test) が
   - ローカル静的サーバ (python3 -m http.server or node) で tool/web を配信
   - headless Chromium でページを開き、エディタに `s = 1\ns.` を打鍵注入 →
     Ctrl+Space → 補完ドロップダウンに `abs` 等が出ることを DOM で assert +
     スクショ (report に貼る)
   - 言語トグル en → doc 行が英語になることを assert
3. `rake ti:test` green (native、回帰)。

## 受け入れ条件

1. `rake ti:wasm` が ti.js/ti.wasm を吐く (emcc 前提)。
2. T6-1 (Node) と T6-2 (Playwright headless) が**ユーザ操作なしで** pass、
   スクショが report にある。
3. `rake ti:test` green。fork 無改修 (git diff が vendor/picoruby-ti や
   tidbgen に触れない。変更は tool/web/ + Rakefile のみ)。
4. 生成物 (wasm/db/help.json) は gitignore。

## やらないこと (P7 の範囲外 / 別判断)

- **debugd over BLE 経路** (実機診断相乗り)。WASM 全部入りで不要にした。
  必要なら別途 (sim の TCP debugd で試作可)。
- WebConsole の既存機能 (ファイル転送・sprite/map エディタ) への影響は
  最小に (エディタモーダルにだけ足す)。
- 実機 (Tab5/S3) 側の変更ゼロ (ブラウザ内完結)。
- 3 言語目以降。
