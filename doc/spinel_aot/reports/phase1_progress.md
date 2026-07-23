# Phase 1 進捗メモ (fork 側タスク: T1-1 / T1-1b / T1-1c / T1-2 / T1-3)

作業ログ。コミットしない (ユーザ指示)。作成: 2026-07-23。

## T1-1: フォーク利用準備の残作業確認

- `tmp/spinel` は branch `fmrb-dev` チェックアウト済み、working tree clean。
- Phase 0 の 5 修正 (318f4a7b, 56394f2d, 7b820768, a8c3c201, d9e363ed) は
  fmrb-dev の HEAD 側 5 コミットとして存在することを確認。
- `git status` で "Your branch is up to date with 'origin/fmrb-dev'" →
  **origin へ push 済み**。追加の push 作業は不要 (こちらから push はしない)。
- ベースライン再計測: `make` OK / `make test` = 1,991 pass / 1 fail
  (既存 cosmetic の nilclass_bool_ops_conversions のみ)。基準どおり。

### 指示書との差異

- phase1.md T1-1b は「最小再現は tool/spinel_poc/repro/ にある」とするが、
  repro/ にあるのは Phase 0 で修正済みの FIX-2/FIX-3 系
  (bug_poly_size_strlen.rb, bug_rescue_in_class_OK.rb, bug_rescue_in_mixin_FAIL.rb)
  のみ。U-1 / U-3 の最小再現は coverage/UNSUPPORTED.md 内のインラインコードが実体。
  → UNSUPPORTED.md のコードを正として作業する。

## T1-1b: U-1 / U-3 修正

(作業中)

## T1-1c: typed symbol-hash 設計評価

(未着手)

## T1-2: --no-main / --entry

(未着手)

## T1-3: --inject

(未着手)
