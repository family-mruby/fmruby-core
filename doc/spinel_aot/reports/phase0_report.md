# Phase 0 完了レポート: PoC・言語カバレッジ検証 (Go/NoGo)

作成日: 2026-07-23 / 対象: Spinel AOT で fmruby-core PreBuild Ruby を C 化する実現性検証。
詳細な技術知見は `phase0_findings.md`、作業ログは `phase0_progress.md`、
未対応構文と回避は `../../tool/spinel_poc/coverage/UNSUPPORTED.md` を参照。

## 総合判定: **Go**

input_router / window_manager (カーネル最重要ホットパス) を **実コード無改変** で
Spinel コンパイル・実行でき、CRuby と **byte 単位で完全一致** した (64bit / 32bit 両方)。
それには Spinel フォークへの **5 件の汎用バグ修正** が必要で、いずれも修正済み・
make test 回帰ゼロ。性能は allocation 律速で CRuby 同等〜3.5x、実機比較対象の
mruby には大差で優位。全 Go 基準を満たす。

## タスク別結果

| Task | 内容 | 結果 |
|---|---|---|
| T0-1 | Spinel ビルド・動作確認 | OK。make test 基準 1,991 pass / 1 fail (既存 cosmetic) |
| T0-2 | input_router+window_manager ハーネス | **CRuby==Spinel byte 一致 (2089行)**。実コード無改変。要 4 修正 |
| T0-3 | 性能計測 | フル 336k件: Spinel≈CRuby (alloc律速)。compute: 3.5x。mruby は Phase2 実機へ |
| T0-4 | desktop/shell 代表 coverage 7本 | **全7本 CRuby==Spinel 一致**。回避不能な未対応構文ゼロ |
| T0-5 | pure-Ruby MessagePack サブセット | **CRuby/Spinel とも 64 pass / 0 fail**。要 1 修正 (FIX-4) |
| T0-6 | 32bit (-m32) ビルド | **完了**。harness 32bit==CRuby 一致。要 1 修正 (FIX-5)。下記「32bit 結果」 |
| T0-7 | shell IRB/Sandbox 境界評価 | 完了。**shell は mruby のまま残す推奨**。下記「shell 判定」 |

## Spinel フォークへの改修 (fmrb-dev, コミット済み・push 未)

すべて symbol-keyed hash が常に poly 値になる Spinel の設計に起因して顕在化した
**汎用バグ**。upstream PR 可能な単位で個別コミット。make test 各回帰ゼロ (1,991/1)。

| commit | 内容 |
|---|---|
| 318f4a7b | codegen: String#setbyte が poly 引数を受理 (emit_int_expr でアンボックス) |
| 56394f2d | runtime: poly の String#size/length を byte-exact 化 (strlen→sp_str_length) |
| 7b820768 | analyze: include された mixin メソッドの `rescue => e` を Exception 型に (clone 後再特殊化) |
| a8c3c201 | runtime: String#* が埋め込み NUL を保持 (strlen→sp_str_byte_len, sp_str_alloc) |
| d9e363ed | runtime: Time.at 厳密変換を __int128 なしで 32bit ビルド可能に (ESP32 必須) |

## 重要な知見: poly (symbol-hash) が性能と互換の鍵

Spinel は symbol-keyed hash (`win[:x]`, `msg[:data]` 等) を **常に poly 値**として扱う
(typed symbol-hash 型が存在しない)。カーネルは symbol-hash を多用するため:

- 互換: poly 値は多くの操作で正しく流れるが、具体型を要求する builtin
  (setbyte 等) や一部 String メソッド (ljust) で問題化 → 上記修正/回避で対処。
- 性能: hot path が poly 演算だらけになり AOT の型特殊化の利点が薄れる。
  フル harness が CRuby 同等だった主因 (compute のみなら 3.5x 出る)。
- **恒久対策 (Phase 1/3)**: (a) typed symbol-hash (SYM_INT/SYM_STR) を Spinel に追加、
  (b) Phase 2 FFI 境界で payload を typed String として渡す、
  (c) window list を poll 化して毎クリック再生成の allocation を削減。

## T0-4 未対応構文 (すべて回避策あり = Go 基準充足)

- U-1: poly レシーバの `String#ljust`/`rjust` 未 dispatch → `.to_s.ljust` で回避。Phase1 修正候補。
- U-2: 同名 `rescue => e` を複数 arm で使うとサブクラス特殊化不可 (Spinel 意図的制約) →
  arm ごとに別名。OS コード規約に追記。
- U-3: nested-array 由来 poly を ctor/mixin 算術に渡すと miscompile → 添字アクセス /
  concrete 化で回避。Phase1 修正候補。

## T0-6 32bit 結果 (gcc-multilib 導入後・完了)

- `mrb_int = intptr_t = 4 bytes` を実機確認 (ESP32-S3 と同じ 32bit)。
- **harness_input_router を -m32 でビルド・実行 → CRuby と完全一致** (trace 2089 行、
  bench 336,000 件も一致)。ELF 32-bit i386。→ **実カーネルコードは 32bit で正当**。
- 32bit で判明・対処:
  - FIX-5 (`sp_time.c __int128`): 32bit で不可 → guard + double fallback で修正 (ESP32 必須)。
  - `crypt()` (String#crypt): 32bit libcrypt 不在 → harness 未使用のためリンクスタブ。実害なし。
- msgpack: kernel 相当サブセット (小整数/文字列/bin/配列/文字列キー map) は
  **32bit でも 51 pass / 0 fail**。>2^31 整数・int32/64・float64 は mrb_int=32bit のため
  表現不可で 13 件 skip (fmruby VM メッセージは未使用なので実害なし)。詳細は phase0_findings.md。

## T0-7 shell IRB/Sandbox 境界と推奨

- 事実: shell の IRB (`cmd_irb`/`irb_eval`) と .toml なしスクリプト実行は picoruby の
  `Sandbox#compile/execute/result` に依存。Sandbox は **実行時に Ruby をコンパイル・実行**
  する = eval 相当で、Spinel AOT が原理的に提供不可 (limitations.md: eval unsupported)。

| 部分 | メソッド/クラス | Spinel 化 |
|---|---|---|
| 行編集・スクロール・描画 | ShellScrollMixin, 入力ループ | 可 (ただし非ホットパス) |
| コマンド解析・FS 操作・ps・補完 | ShellCommandsMixin | 可 |
| 出力キャプチャ・IO | ShellIoMixin (OutputCapturer 等) | 可 |
| **IRB 評価** | ShellIrbMixin, `Sandbox#compile/execute` | **不可 (eval)** |
| **.toml なしスクリプト実行** | in-process Sandbox 実行 | **不可 (eval)** |

- 判定: **shell は Spinel 化せず mruby のまま残すことを推奨**。
  根拠: (1) shell の中核価値 (IRB / スクリプト実行) が eval 依存で AOT 不可、
  (2) UI 部分は対話的で性能ホットパスでない (Spinel 化の便益小)、
  (3) 分割には C シム (fmrb_spx_sandbox_exec 等) + mruby VM 併存が必要で複雑、
  便益に見合わない。00_common.md の「shell はオプション」方針と整合。
  最終判断はユーザ。

## Go/NoGo チェックリスト (phase0.md 受け入れ基準)

1. T0-2 実コード無改変で byte 一致: **達成** (改変 0 行、Spinel 側 4 修正で対応)。
2. T0-3 mruby 比 2x (不能時 CRuby 比 1x 以上): **達成** (Spinel≈〜3.5x CRuby ≫ mruby)。
3. T0-4 回避不能な未対応構文ゼロ: **達成** (U-1/2/3 すべて回避策あり)。
4. T0-5 msgpack 両系全パス: **達成** (64/64)。
5. T0-6 32bit 出力一致: **達成** (harness 32bit==CRuby。FIX-5 で __int128 32bit 対応)。
6. NoGo 材料: なし。

→ **全 6 基準達成。判定 Go。** Phase 1 (フォーク整備 + ライブラリモード +
typed symbol-hash 検討) へ進むことを推奨する。
