# VT1・VT2 指示書: console の Terminal (読むだけ → 操作・権限・記録)

> 状態: 実装中 | 更新: 2026-11-08 | 正は plan.md (目的・方針・決定事項 (2026-11-08 ユーザ))

## 範囲 (asterism-console、要れば asterism gem)

- **VT1**: console のページに Terminal (xterm.js を vendor に同梱)。asterism gem の CLI と同じ
  コマンドのうち読むだけのもの (node list、status、values など、CLI の help の分類に従う) を打てる。
  Tab の補完、↑↓ の履歴、Ctrl+C で止める、CLI の色をそのまま。コマンドは **bridge の中で**、
  今の接続のまま動かす (ページ ↔ bridge は console の既存の経路 (solid_cable など) に載せる)。
  流れ続けるコマンド (watch・echo など) は bridge が抱え、1 人あたり・全体の数に上限、
  ページを閉じたら止める。サーバの OS のシェルは出さない (コマンドは CLI の分類表から選ぶだけで、
  任意の文字列を shell に渡さない)。サインインした人だけ。
- **VT2**: メソッドを呼ぶコマンド (`call` など) を、ページからの呼び出しと同じ Call permissions の
  表で許す (許していなければ理由つきで断る)。ファイルを送るコマンドがあるなら Members の印
  (T4b) に従う。打ったコマンドと結果の要約を **今の Call log** に残す (だれ・いつ・何・結果)。
  一覧から消せない。残す期間は Call log と同じ。
- CLI の中身を console のために作り直さない。gem の CLI を bridge から呼べる形 (出力の行き先・
  止め方を外から渡す) にする必要があれば gem を変えてよい (次の版の Unreleased)。

## 確認

- `bin/rails test` (コマンドの分類と許可、記録、上限、止め方、ページ)。gem を変えたら gem の試験
  (`ASTERISM_CREDENTIALS` を外して)。
- 手元 (WSL) で zenohd・console (`bin/dev`)・相手 (CRuby の Asterism の小さな相手か、ネイティブ版の
  Family mruby) を動かし、ヘッドレスの Chromium で Terminal を操作した画面を撮る:
  読むコマンド、Tab 補完、履歴、watch と Ctrl+C、許した call・許していない call、Call log の行。
- 版は console 0.3.15 (CHANGELOG の Unreleased、docs/install.md の更新の節)。

## 触ってよい範囲

asterism-console と asterism gem の作業用 checkout (どちらもコミットまで)。fmruby-core は
`doc/console_terminal/` の report だけ (コミットしない、親がする)。push・タグ・公開・VPS はしない。

## 止まる条件

- 仕様 (plan.md の決定) と違う動きが要る。既存の権限・記録の作りを大きく変える必要が出た。
- 任意のシェルを出さずには作れない、など安全の前提が崩れる。

## report

`fmruby-core/doc/console_terminal/report/vt1_vt2.md` (作り、コマンドの分類表、上限、試験の結果、
VPS に要る設定、残件)。
