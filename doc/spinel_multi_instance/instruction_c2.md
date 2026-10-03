# 作業指示書 C2: エディタの文書の枠の回収を E2 の形に戻す

対象: 実装担当のサブエージェント。前提: plan.md、report/c1.md (判断を仰いだ 3)、doc/app_exit/report/e1.md (E2)・e3.md。
report は `report/c1.md` に節を足す (「C2」)。

## 決定 (ユーザ、2026-10-03)

- 同じ Spinel プログラムは同時に 1 つになった (C1) ので、エディタの文書の枠の回収を **E2 (a1b2ee50) の形に戻す**:
  `fmrb_spx_editor.c` は「今のエディタの枠」を 1 つだけ覚え (`s_slot_plus1` のような 1 変数、PSRAM)、新しいエディタが
  開くときに残っている枠を回収する (強制終了で返されなかった枠を、どのアプリの枠で起動しても回収できる)。
  E3 のアプリの枠ごとの表 (app_id で引く 5 要素) はやめる。
- `EditorCore.slot` を C に聞く形 (E3 の `fmrb_editor_ffi.rb`) は残す (クラスの ivar は次のインスタンスの開始で
  消えるので、C に持つ方が正しい。1 変数でも同じ口で答えられる)。
- 互換構成 (エディタも mruby) は 2 つ開けるまま (ユーザ確認済)。mruby のエディタの枠の扱い (VM ごと) は変えない。
- これ以外の動きは変えない。

## 検証

- 実機 P4-Nano: エディタの起動と終了 (Ctrl+Q / kill を混ぜて) を 15 回以上、「Doc full」0。kill の直後に、
  別のアプリの枠でエディタを開いても回収されること (5 回 kill 続けても Doc full にならない)。2 つ目は断られる (C1)。
  ミュートのまま。動いているアプリは止めてよい。
- `rake test`、NARYAv4 のビルド (静的な D/IRAM が 125,612 B を超えない)。sim は今は不安定 (graphics-audio の起動
  失敗、別件) なので、起動しなければ report にそう書いて実機の確認で代える。

## 作業の決まり

- 作業ブランチ `feature/spinel-multi-instance` (本体の checkout)。1 つのコミット。自分が変えたファイルだけを
  パスで指定。push とマージはしない。`.env` は書き換えずコミットしない (scratchpad の env_before_multi.diff と一致)。
  `rake clean_all` は、ほかの作業に影響しないので使ってよい (ユーザ許可)。
- sdkconfig、graphics-audio、submodule、Spinel のフォークは変えない。
- 最後にこの版を実機に焼いて残す。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 上に書いた以外に、利用者から見た動きを変える必要が出たとき (案として返す)。
