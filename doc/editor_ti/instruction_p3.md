# 実装指示書 P3: ホバーと診断の UI (picoruby-ti)

対象: 実装担当セッション。前提: P2 完了 (Tab 補完が sim で動作、
s_prism_scratch の __thread 化まで済んでいること)。plan.md と
report/p2.md を先に読むこと。

report は doc/editor_ti/report/p3.md へ。タスクごとにコミット。

## P3 のゴール

- **ホバー**: カーソル位置の変数/メソッドの型情報をキー一発で表示する
- **診断**: 型エラーを保存時と手動キーで検出し、行マーカー + ステータス行で
  見せる。エラー行へジャンプできる
- P2 で見つかったステータス行の奪い合い (候補 doc vs かなバッジ) を
  この機会に解消する

## 方針 (決定済み)

- fork 側の hover 対応は**実施済み** (ti_set_enclosing_class_at_cursor の
  共有化 + 変数引きを ti_handle_identifier 経由に。`@gfx` のホバーが
  `Canvas` を返すことをホストで確認済み、回帰テスト追加済み)。
  P3 は fmruby-core 側の作業のみ。
- ホバーのキーは **Ctrl+T** (type)、診断は **Ctrl+E** (error) + 保存時
  自動。いずれも scancode 判定。既存割当と衝突しないか実装時に確認し、
  衝突したら代替を選んで report に書く (Ctrl+Space / Tab / F3 / F4 / F5 /
  F11 / Ctrl+D / Ctrl+Q / Ctrl+Tab / Ctrl+S は使用済み)。
- ブリッジは P2 の et_* と同じ流儀: ロック + 使い捨てヒープ + 結果は
  ブリッジ側バッファへコピー。32KB 上限も同じ。

## T1: PIN 更新

1. `lib/add/PICORUBY_TI_PIN` を `d40a9e6e56388b6156e847e1e67548ee054d3bcb`
   (fmrb-dev、hover 対応入り) に更新し、ti:clean -> ti:setup。
2. sig の変更は不要。`rake ti:test` 全 green (hover の ivar テスト
   test_declared_instance_variable_type_at_cursor が増えている)。

## T2: ブリッジ拡張 (editor-core、et_*)

- `int et_hover(int slot, int y, int x)` -> found (0/1、負値エラー)。
  内部は P2 と同じ直列化 + (y,x)->バイト位置変換 + ti_find_hover_at_cursor。
  結果 (variable_name / type_name / method_signature / method_document /
  is_method) をブリッジのバッファへコピーし、読み出しは et_suggestion と
  同じ ptr+len 形式のアクセサで返す。
- `int et_diagnose(int slot)` -> 件数 (負値エラー)。TiDiagnostic の
  バイトオフセット (start/end) を**直列化と同じ行テーブルで (y,x) に
  逆変換**してからコピーする (message も含めて。TiDiagnostic の文字列は
  次の ti 呼び出しで無効になるため)。読み出しアクセサは
  行/桁 4 つ + message。件数上限は engine の 64。
- カーソルがメソッド名の上にあるときの hover は、ti 側がメソッド名の
  **末尾**基準で候補を探すので、(y,x) は「カーソルがその語の範囲内なら
  そのまま」で良い (エンジンが node の範囲判定を持っている)。特別な
  スナップ処理を書かないこと。

## T3: バインディング

P2 と同じ 3 点セット: editor_core_mrb.c (mruby) / fmrb_spx_editor.c +
fmrb_editor_ffi.rb (Spinel)。命名も P2 の et_* に揃える。

## T4: ステータス行の整理 (先にやる)

P2 の「候補の doc がかなバッジを隠す」の解消。ルール:

- **右端は常設バッジ専用** ([A]/[あ]/[ア]。将来ここに診断件数バッジを
  足せる幅を意識する)。バッジは常に最後に描く = 何があっても見える。
- **左〜中央は一時メッセージ領域**。ホバー結果・診断サマリ・補完候補の
  doc・「大きすぎる」等はすべてここを通す。表示は次のキー入力または
  一定時間 (目安 5 秒) で消え、消えたら通常のステータス表示 (行/桁等) に
  戻る。既存の表示コードを流用してよいが、**書き込み口を 1 つの
  ヘルパに集約**し、直接ステータス行を書く箇所を残さないこと。
- 一時メッセージがバッジ領域に届く長さなら切り詰める。

## T5: ホバー UI

- Ctrl+T でカーソル位置の et_hover を呼び、結果を一時メッセージ領域へ:
  - 変数: `@gfx : Canvas` / `msg : String` の形
  - メソッド: シグネチャそのまま (`draw_text: (Integer x, ...) -> nil`)。
    doc コメントがあり幅が許せば ` -- <doc>` を続ける
  - found=0 は「型情報なし」等の短い表示
- 表示だけの機能なのでモーダルにしない。カーソル移動や編集で消えるだけ。

## T6: 診断 UI

- 実行タイミング: (1) **保存成功直後に自動** (Ruby ファイルのみ。判定は
  HL と同じファイル種別規則)、(2) **Ctrl+E で手動**。
- 結果の見せ方:
  - エラー行に行マーカー (デバッガの停止行と同じ描画部品。色は赤系で
    区別)。範囲 (start_x..end_x) の強調までは不要、行単位で良い。
  - ステータス行に `2 problems: <最初の message>` の形で表示。0 件なら
    `no problems`。
  - **Ctrl+E の連打で次のエラー行へカーソルジャンプ** (末尾まで行ったら
    先頭へ戻る)。ジャンプのたびにその行の message を表示。
- **文書を編集したら診断マーカーは全て消す** (バイト位置が古くなるため。
  再表示は次の保存か Ctrl+E)。
- 32KB 超は補完と同じく断りメッセージ。診断 0 件と「実行できなかった」を
  ステータス表示で区別すること。

## T7: 検証 (sim 自律 + esp32 ビルド)

標準構成 (Spinel エディタ) で一巡、スクリーンショット付きで report へ:

1. `class MyApp < FmrbApp` の def 内の `@gfx` に Ctrl+T ->
   `@gfx : FmrbGfx` が出る。
2. `@gfx.draw_text(...)` の draw_text 上で Ctrl+T -> シグネチャが出る。
3. `@gfx.draw_text(10, "ten", "hi", 3)` を書いて保存 -> 行マーカー +
   `1 problem: type mismatch...`。Ctrl+E でその行へジャンプ。
4. 行を直して保存 -> `no problems`、マーカーが消える。
5. 編集した瞬間にマーカーが消えることの確認。
6. ホバー/診断の表示中もかなバッジが見えていること (T4 の確認。
   かなモード on で試す)。
7. Tab 補完の回帰 (P2 の受け入れ 1 と同じ操作が動くこと)。
8. 全 mruby 構成で 1 と 3 を再確認。
9. esp32 (S3/NARYAv3) ビルド通過 + サイズ記録 (診断/ホバーの追加で
   flash がどれだけ動いたか)。

## 受け入れ条件

1. T7 の 1-8 が通る (スクリーンショット付き)。
2. rake ti:test 全 green。
3. S3 ビルド通過 + サイズ記録。
4. ステータス行の書き込みがヘルパ 1 箇所に集約されている (grep で直接
   書いている残りが無いこと)。

## やらないこと (P3 の範囲外)

- 打鍵ごとの自動診断 (アイドル時診断含む)。範囲単位の下線・波線表示。
- RBS の網羅 (P4)。実機確認・arena PSRAM 化 (P5)。WebConsole 展開 (P7)。
- 定数補完など上流 db の機能拡張。
