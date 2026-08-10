# 実装指示書 P5: エディタ全体の Spinel 化 (段階 6)

対象: 実装担当セッション。前提: P1-P4 完了 (report/p1.md〜p4.md)。
report は report/p5.md へ。タスクごとにコミット。

## 前提の確認結果と決定事項

- **B-3 (sp_io VFS フック) は実装済みと判明** (2026-08-10 確認)。Spinel
  フォーク側の backend スロット (sp_io.h) + fmruby 側の fmrb_hal_file_*
  配線 (fmrb_spinel_host.c の hal_open 群) が現ピンに入っており、P4 Spinel
  desktop の実機 /app スキャンが実証。esp32_host_deps_sweep.md は訂正済み。
  **P5 に FS まわりの前提作業は無い**。
- **ユーザ決定 (2026-08-10): v1 の Spinel 版エディタは on-device デバッガを
  省く**。デバッグ作業時は .env で mruby 版エディタに切り替えて使う
  (互換構成は全機能維持)。デバッガの将来形 (別アプリ化等) は後続フェーズで
  別途設計する。
- 到達点は plan.md 6 章のとおり: **単一ソース二重バックエンド**。エディタの
  ソースは 1 本のまま、`FMRB_APP_ENGINE_EDITOR=spinel` で Spinel ビルド、
  未設定で従来の mruby バイトコード。両方が動く状態を維持する。

## ゴール

標準構成 (kernel=spinel, desktop=mruby, **editor=spinel**) で、P1-P4 の
全機能 (デバッガ除く) が動き、edit_lat: が mruby 版から改善すること。

## T1: デバッガ部の分離 (ソース分割、機能は不変)

Spinel ビルドから FMRB::Debug 依存 (19 呼び出し) を外すための下準備。
**mruby ビルドの動作は 1 ビットも変えない**こと。

- editor.app.rb からデバッガペイン関連 (breakpoint 管理 @bp、@dbg_*、
  ペイン描画、F5 デバッグ起動系、FMRB::Debug 呼び出し) を
  `editor/debug_pane.rb` (組み込みアプリの `<name>/*.rb` 連結機構が
  既にある: compile_ruby_to_bytecode.cmake の gen_combined_rb 経路) へ
  移動する。
- 本体側との接点は少数のメソッド呼び出しに絞る (例: dbg_active?,
  dbg_draw_pane, dbg_handle_key, dbg_line_background)。本体には
  **無効時の no-op スタブ**を置き、debug_pane.rb が再定義する形にする
  (Spinel ビルドはスタブのまま = 参照が静的に消える)。
- 分離後、mruby ビルドで P2 のデバッガ受け入れシナリオ (下分割ペイン表示、
  ブレークポイント、ステップ) が退行していないことを確認してから次へ。

## T2: ビルド配線 (desktop の型をなぞる)

- Rakefile: `FMRB_APP_ENGINE_EDITOR` 環境変数 + spinel:gen のエディタ分
  (gen_app_combined.rb の APPS 表に "editor" を追加。連結対象は
  fmrb_app_ffi.rb + 生成 consts + msgpack_pure.rb +
  fmrb_app_base_spinel.rb + editor.app.rb 本体。debug_pane.rb は含めない)。
- main/CMakeLists.txt: desktop の FMRB_APP_ENGINE_DESKTOP_SPINEL ブロックを
  なぞって editor 分を追加 (-DSP_MULTI_CTX + sp_mem_override.h)。
- spawner: `default/editor` と `default/editor_fs` の組み込みテーブルを
  #ifdef で FMRB_VM_TYPE_NATIVE + native_func に切り替え
  (spinel_desktop_native の型)。editor_fs の属性 (fullscreen /
  fullscreen_switchable / resizable / min_window_*) は NATIVE 側でも
  同一であること。
- .env は触らない (既定は mruby のまま。有効化はユーザが行う)。

## T3: editor-core gem の Spinel バインディング

P4 の editor-core は文書スロットを **mrb_state キー**で持っている。Spinel
側には mrb_state が無いので、ここを整理する:

- gem の C 実装を「スロット番号 (int) キーの純 C API (ec_*)」に再編し、
  mruby バインディングは「mrb_state → スロット番号」の解決だけを行う
  薄い層にする (P4 の外部挙動は不変)。
- Spinel 側は fmrb_ffi.rb に ec_* を ffi_func 宣言 (:int / :str /
  :binstr のみ。P4 の 18 API はこの型制約で設計済み)。スロット番号は
  アプリ初期化時に確保して ivar に持つ。
- syntax-highlight は gem 内 C 直呼びなので追加作業なし。

## T4: 基底クラスと本体の Spinel 適合

- **fmrb_app_base_spinel.rb の API 追随**: P1-P3 で mruby 側 FmrbApp に
  増えた分を揃える — on_quit_request (既定 stop)、request_fullscreen /
  toggle_fullscreen、resize 制御の窓ジオメトリ復元、draw_window_frame の
  frame block 遅延生成 (P3)、edit_lat 計測が使う時刻取得。desktop が
  使っていない API が漏れている前提で、editor.app.rb が呼ぶ FmrbApp API を
  総当たりで突き合わせること。
- **editor.app.rb の書き方制約への適合** (ruby_writing_constraints.md):
  文字列リテラル frozen (可変は +"" / String.new / dup)、defined?(@ivar) の
  静的解決、escaping proc の局所捕捉禁止、symbol キー Hash の
  poly-dispatch 回避 (デバッガ分離で大半は消える見込み)、
  Array#delete 等の既知の弱点。**dual-build 安全が絶対条件** — Spinel の
  ためだけの分岐をソースに書かない (書き方自体を両対応にする)。
- rake spinel:doctor をエディタ分に通し、unsupported/unresolved ゼロに。

## T5: 検証

すべて Linux sim。実機はユーザ確認待ちに積む (実機無しの前提)。

1. **標準構成** (kernel=spinel, editor=spinel): P1-P4 の受け入れシナリオを
   一通り — 窓/全画面起動、F11 トグル、編集・選択・複数行ペースト・検索、
   HL 既定 (P3)、53KB/200KB ファイル (P4)、保存ダイアログ (park 経由)、
   Ctrl+Q (quit_request、未保存確認)、F5 で窓/全画面アプリ、Ctrl+Tab。
   デバッガペインは**出ないこと** (省いた確認)。
2. **互換構成** (全 mruby): 同シナリオ + デバッガペインが従来どおり
   動くこと (T1 の分離が退行していない再確認)。
3. **性能**: edit_lat: を mruby 版と Spinel 版で同条件比較 (小ファイル
   HL on / 10.9KB HL on / 200KB)。期待は解釈部分の短縮 (カーネルの実測では
   ~4 倍) だが、P4 で描画・文書操作は既に C なので**改善幅は小さくても
   退行していなければ可** — 数字を正直に report へ。`ps` の VM 列が
   spx になっていることも確認。
4. ビルド: Linux / S3 / P4 の 6 通り (各 x editor エンジン 2)。
   S3 は flash 残量も report に記録 (Spinel 化でサイズが増える方向のはず)。

## 受け入れ条件

- 標準構成で 1 のシナリオ全通過 (スクリーンショット添付)。
- 互換構成で機能退行ゼロ (デバッガ含む)。
- edit_lat: の比較表 (退行なし。改善はあれば記録)。
- doctor クリーン、S3/P4 ビルド通過。
- .env 既定は mruby のまま。

## 範囲外

- デバッガの Spinel 対応・別アプリ化 (後続フェーズで設計)
- 640x360 (段階 5。実機計測が前提なので実機確認とセットで)
- editor-core の追加最適化 (単一 arena 化等)

## 進め方の約束

P1-P4 と同じ。kernel Ruby は触らない見込みだが、触る場合は両エンジン検証
(instruction_p2.md の注意)。Spinel フォーク側 (vendor/spinel) に手を
入れる必要が出た場合は、SPINEL_PIN の運用 (fork へ push → pin 更新 →
import_from_fork.rb → 同時コミット) に従い、その旨を report に明記する。

## 完了報告

report/p5.md に: T1 分離の切り口、base API の追随一覧、制約適合で
書き換えた箇所の類型と件数、性能比較表、S3 flash 残量、
デバッガ将来形への引継ぎ (分離境界がそのまま別アプリ化の下地になるか)。
