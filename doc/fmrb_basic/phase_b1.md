# Phase B1 指示書: 言語コア再構成 (互換レベル L1)

前提: Phase B0 完了 (core/ スケルトン、ホスト・ゴールデンテスト機構、
`rake basic:test`)。B1 は画面なしで動く言語コアを完成させ、ゴールデン
テストで固めるフェーズ。仕様の正は `spec/family_basic_core_spec.md`
(以下 core_spec) と `spec/family_basic_v3_spec.md` (以下 v3_spec)。

## 設計方針 (実装前に読み込むこと)

- **内部文字コードは Family BASIC のキャラクタコード (1 バイト/文字、
  core_spec sec 12 テーブル B) とする**。ASC/CHR$/SCR$/文字列比較が
  コード表準拠で自動的に正しくなる。UTF-8 との変換 (ソースロード時、
  出力時) はコアの外 (ホストインタフェース側) で行う。.bas ソース中の
  カナは UTF-8 で書かれたものをロード時に変換する。
- プログラムはロード時にクランチする: 行番号 (uint16) + トークン列
  (キーワード 1 バイト化、数値リテラルは変換済み 16bit 値、文字列は
  長さ付きバイト列) を行テーブルに格納し、実行はトークン列上で行う。
- 値は 16bit 符号付き整数 (-32768..32767) と文字列の 2 種のみ。
  演算・比較・オーバーフロー挙動は core_spec sec 2-3 に従う。
- 文字列は固定長プール + 単純 compaction (compat_plan sec 4.1-3)。
  テンポラリ文字列 (式評価中) の寿命管理に注意。
- エラーは core_spec sec 15 / v3_spec のエラー一覧の文言・コードで
  `?XX ERROR IN 行番号` 形式。エラー発生行 (ERL 相当) と種別 (ERR 相当) を
  状態に保持する (ON ERROR での捕捉自体は B4)。
- コアはホストインタフェース (B0 の `basic_host_t`) 経由でのみ外部と
  やりとりする。**コアに fmruby / IDF 依存を持ち込まない**。

## タスク

### T1-1: トークナイザ / クランチャ

- キーワード表 (ステートメント: core_spec sec 5-10 + v3_spec 追加命令、
  関数: core_spec sec 11) を constexpr テーブルで持つ。
- 行エディット系のコマンド (AUTO/RENUM/DELETE/FIND/LIST) はトークン
  定義のみ行い、実装は B4 (直接モード判断) まで「未対応エラー」でよい。
- 数値リテラル (10 進、16 進 &H)、文字列リテラル、変数名 (英字 +
  英数字、末尾 $ で文字列型。有効文字数は core_spec sec 2)、
  演算子・特殊記号 (core_spec sec 3-4) を処理する。
- クランチ結果はデクランチ (LIST 用の逆変換) 可能な形式にしておく
  (B4 の LIST/直接モードで必要になる。B1 では内部テスト用に
  デクランチ関数だけ用意)。

### T1-2: 行テーブルと実行エンジン

- 行番号 -> 行のソート済みテーブル + 二分探索。GOTO/GOSUB/RESTORE の
  行番号解決、実行中の行の挿入・削除 (NEW/ロード時のみ。実行中の
  自己書き換えは対象外)。
- ステートメントディスパッチ (トークン -> ハンドラ)。
- FOR/NEXT スタック (変数一致の NEXT、STEP 正負、ループ 1 回目の判定は
  core_spec sec 6 の挙動)、GOSUB/RETURN スタック (RETURN 行番号 =
  v3_spec)、ON n GOTO/GOSUB/RESTORE。
- STOP / END / CONT (CONT はコマンドとして。停止位置の保存)。
- 無限ループ対策: ホストインタフェースの tick コールバックを N
  ステートメントごとに呼ぶ (B3 の 1/60 tick 統合と、アプリ kill 応答の
  下地。N はパラメタ化)。

### T1-3: 変数・配列・代入

- 数値変数 (初期値 0)、文字列変数 (初期値空)、LET (省略可)、SWAP。
- DIM (最大 2 次元、添字範囲・省略時挙動は core_spec sec 2/6)、
  添字範囲外は所定のエラー。
- CLEAR (変数・文字列領域のクリア。パラメタは core_spec sec 5)。

### T1-4: 式評価

- 優先順位付き演算子 (core_spec sec 3): 算術、比較 (結果は -1/0)、
  論理 (AND/OR/NOT のビット演算意味)、単項。
- 数値関数: ABS / SGN / RND / FRE / PEEK (PEEK は B4 まで常に 0 +
  警告ログ相当のホスト通知)。RND の範囲・シード挙動は core_spec sec 11。
- 文字列関数: ASC / CHR$ / VAL / STR$ / HEX$ / LEFT$ / RIGHT$ / MID$ /
  LEN / INSTR (v3)。

### T1-5: 入出力 (画面なしの範囲)

- PRINT: `;` / `,` (8 文字カンマゾーン、4 ブロック) / 行末改行の規則。
  B1 では出力先はホストの 1 文字出力コールバック (ゴールデンテストは
  これを捕捉する)。ゾーン計算はカーソル桁を内部で持つ (B2 の 28 桁画面と
  同じ桁管理を先に作る)。
- INPUT / LINPUT: ホストの行入力コールバック経由。型不一致時の
  再入力挙動 (core_spec sec 6)。
- DATA / READ / RESTORE [行番号]。

### T1-6: fmruby 統合 (置き換え)

- 旧実装 `components/basic/basic/` (lexer.c / parser.c / runtime.c /
  basic_internal.h) を撤去し、`fmrb_basic.c` を新コアへの薄いアダプタに
  書き換える (`include/fmrb_basic.h` の API は極力維持し、fmrb_app.c の
  変更を最小にする)。メモリはこれまで通り per-task プール。
- gfx 拡張 (CLS/CIRCLE/PRESENT 等、`extension/fmrb_basic_gfx.c`) は
  **拡張ステートメントフック** (コアが未知キーワードをホストへ委譲する
  インタフェース) として接続し、`flash/app/demo/basic.app.bas` が
  従来どおり動くことを維持する (B0 T0-6 の記録と比較)。
  注: これら gfx 拡張は Family BASIC 外の fmruby 独自命令として残す。

### T1-7: ゴールデンテスト拡充

- 機能グループごとにケースを足す: 整数演算・オーバーフロー境界 /
  文字列関数 / 制御構造 (FOR 境界、GOSUB ネスト) / DATA / 配列 /
  PRINT ゾーン / エラー文言 (代表 5 種以上) / INPUT (入力はランナーに
  スクリプト供給機能を足す)。目安 30 ケース以上。
- spec の挙動と実装が食い違ったら、spec を確認し、spec 側の暫定値で
  疑義が残るものは report の疑義リストへ。

## 受け入れ基準

1. `rake basic:test` green (30 ケース以上)
2. `rake build:linux` / `rake build:esp32` 通過、旧 lexer/parser/runtime
   は削除済み
3. linux sim (headless ハーネス) で basic.app.bas デモが従来どおり動く
4. コア (core/) に IDF / fmruby ヘッダの include が無いことを確認
   (grep で機械的に検証して report に記載)
5. `reports/phase_b1_report.md` 完成 (仕様疑義リストを含む)

## 報告

`reports/phase_b1_report.md`。トークン形式・メモリレイアウト (行テーブル、
文字列プール) の設計サマリを図または表で残すこと (B2 以降の担当が
参照する)。
