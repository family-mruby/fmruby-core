# エディタ型推論統合 (picoruby-ti) 計画

作成: 2026-08-11

## 目的

hamachan 氏の型推論エンジン picoruby-ti を FM-EDITOR に統合し、
補完・ホバー (型表示)・診断 (型エラー) をオンデバイスで提供する。
子供向けエディタとして「書きながら API を発見できる」ことが狙い。

- 上流: https://github.com/engneer-hamachan/picoruby-ti (MIT)
- fork: https://github.com/kishima/picoruby-ti (branch: fmrb-inheritance)
- 手元の作業ツリー: family-mruby/tmp/picoruby-ti

## picoruby-ti の概要

- 純 C の型推論エンジン。Ruby クラスは公開せず C API のみ
  (`ti_fill_suggestions_at_cursor` / `ti_find_hover_at_cursor` /
  `ti_fill_diagnostics`)。ソース文字列の配列 (先読み分 + 編集中) と
  カーソルのバイト位置を渡す。
- 構文解析は prism を直接呼ぶ (pm_parse)。mruby-compiler2 (= うちの
  mruby-compiler) に依存。
- 作業メモリは静的 arena 16KB のみ。リクエストごとに全ソースを
  再パース + 2 周評価 (キャッシュなし)。Cardputer (ESP32 RAM 512KB) の
  IDE で動作実績あり。
- 型データベースは sig/*.rbs から tidbgen (純 Ruby、rbs gem 不要) で
  C ソースを生成してビルドに含める (flash 側 rodata、RAM 消費なし)。
- 同梱の lsp/ (Go) で同じエンジンが PC 側 LSP/MCP サーバになる。

## 評価結果 (2026-08-11、実測)

### 互換性: 問題なし (実証済み)

- prism バージョンは双方 v1.9.0 で完全一致。うちの
  components/picoruby-esp32/picoruby/mrbgems/mruby-compiler/lib/prism から
  libprism.a を作り、picoruby-ti の全ホストテスト + FMRB 風デモが通った。
- prism はファームウェアに既在 (アプリの実行時コンパイルで使用中) なので、
  パーサ分の追加コストはゼロ。

### エンジンの守備範囲

効くもの:

- ローカル変数の型推論 (リテラル、メソッド戻り値、変数間の伝播、union)
- インスタンス変数の型追跡 (メソッドをまたいでも効く。2 周評価のおかげで
  前方参照も可)
- RBS 由来クラスのインスタンス/静的メソッド補完 (引数シグネチャと
  doc コメントつき)
- ユーザ定義クラスのメソッド補完 (別ソース先読みでも可)
- 引数の型不一致の診断、ホバー (変数の型、メソッドのシグネチャ)

上流に欠けていて fork で実装済みのもの (後述):

- 継承 (`class MyApp < FmrbAppBase` で親のメソッドが引けない)
- `self` の型 (self. で何も出ない)

制限として受け入れるもの:

- 変数の型は名前単位のグローバル管理 (クラスごとの ivar 分離はない)
- 必須引数のみ 8 個まで記録 (オプション引数・ブロックの型は未対応)
- ヒアドキュメント等の複数行構文や、上限超過時 (arena 16KB、候補 64 件、
  定義 64 件など) は静かに劣化する設計

## fork での継承 + self 対応 (実装完了)

branch fmrb-inheritance (deb9dae で継承+self、c1d2715 で RBS ivar 宣言)。
push 済み、上流 PR は未提出。統合ブランチ fmrb-dev の HEAD は 333beb1。

### RBS ivar 宣言対応 (2026-08-11 追加)

`@gfx: FmrbGfx` のような RBS のインスタンス変数宣言を tidbgen が db に
取り込み (親クラス/include を平坦化)、エンジンはソース中に代入が無い
`@名前` の読みを、外側クラスの継承チェーン上の db クラスから解決する。
これで `class MyApp < FmrbApp` の def 内の `@gfx.dr` が補完される
(attr_reader も前置きソースも不要になった)。ソース中の代入は宣言より
優先。クラス変数 (@@x) は対象外。上流 host_test の新テストは GPIO に
`@pin: Integer` / `@label: String` の宣言が要る。
既知の残り: hover.c はカーソルの外側クラスを context に設定しないので、
def 内 ivar のホバーには suggest と同じ処置が要る (P3 で fork に入れる)。

### 変更内容

- クラス定義の評価時に親クラス名を記録する (これまで `< Base` は
  読み捨てられていた)。TiDefineInfo に superclass_name_id を追加。
- メソッド解決 (レシーバあり/なし の 2 箇所) と補完候補列挙を、親クラスを
  たどる探索に変更。ユーザクラスの親をたどり、RBS 由来クラスに達したら
  そちらのメソッド表に接続する (循環対策で深さ 16 まで)。
- `self` をカーソル/評価位置を囲むクラスの型として評価するようにした。

これにより次が全部通る:

- `class MyApp < FmrbAppBase` 内の `gfx.dr` で draw_text / draw_rect が出る
  (レシーバなし `gfx` が継承チェーン経由で `() -> Canvas` に解決される)
- `self.on_` で継承メソッドが出る、レシーバなし呼び出しも親から引ける
- 継承したメソッドの戻り値型が変数に伝播し、診断まで効く

### 実装時の注意 (PR レビューや再作業で効く知見)

- 最上位 (クラス外) の def はクラス ID 0 (TI_CLASS_NONE) に登録される。
  チェーン探索のループ条件で ID 0 を「クラスなし」として弾くと既存テストが
  落ちる。「先に引いてから打ち切る」順序にする。
- 親指定のないクラスに暗黙の Object 継承は入れない。上流テスト
  test_user_class_only_suggests_its_methods が「ユーザクラスは自分の
  メソッドだけ出す」という作者の意図を示しているため。
- host_test は sig/ に GPIO と Enumerable の RBS がある前提
  (example/rbs には無い)。PR 説明に一言添えること。
- 残る磨き所: メソッドをオーバーライドすると自前と継承の同名候補が両方出る
  (シグネチャ文字列が違うので重複除去されない)。エディタ UI 側で名前で
  畳めば済むため PR には含めていない。

## 統合設計

### 配線 (fmruby-core)

- 取り込みは **Spinel 方式** (submodule にはしない): `vendor/picoruby-ti` に
  PIN ファイルでコミット固定した clone を置き、`rake ti:setup` で取得する。
  参照先は kishima fork の統合ブランチ (Spinel の fmrb-dev と同じ役割)。
  - FMRB 都合の変更 (mrbgem.rake の依存名 `mruby-compiler2` ->
    `mruby-compiler` の patch 等) は fork の統合ブランチに載せる。
    上流に返せるもの (継承対応など) はそこから PR に切り出す。
  - submodule にしない理由: lib/add/gembox の仕組み自体が「Rakefile で
    picoruby submodule 内へコピーする」方式でコピー元が submodule である
    利点が無い。また picoruby-ti は自分の中に lib/prism submodule を抱えて
    おり (うちの prism を使うので不要)、再帰 init や dirty 表示のノイズが
    増えるだけ。fork 追従の柔軟さも PIN 方式が上。
- ビルドへの接続は editor-core と同じ (family_mruby.gembox + Rakefile cp +
  components/picoruby-esp32/CMakeLists.txt の include/source 追加)。
  コピー元が lib/add ではなく vendor/picoruby-ti になるだけ。
- 型 db 生成 (tidbgen) は Rake のプリビルドに組み込む。sig/ は
  fmruby-core 側で管理し (これが FMRB API ドキュメントを兼ねる)、生成物は
  picoruby submodule 内のコピー先に出して vendor ツリーは汚さない。

### エディタ橋渡し

- editor-core (ec_*) に「文書全文の連続バッファ取り出し」と「カーソル
  (行,桁) -> バイトオフセット」の C API を追加。
- ti の結果 (構造体の配列) は flatten した count/get 形式の C シムで返し、
  mruby バインディングと Spinel FFI の両方から使う。FFI 宣言は共有 FFI と
  別ファイルにする (editor_serious_mode P5 の教訓)。
- 先読みソースとしてアプリ基底の Ruby (prebuild の fmrb_app_base 相当) を
  渡すか、基底 API を全部 RBS 側に寄せるかは P1 で判断する
  (RBS 側に寄せる方が診断のシグネチャが充実する)。

### UI (エディタ側)

- 補完: カーソル近くのドロップダウン。確定 = Enter/Tab、キャンセル = Esc。
  起動キーは Tab か Ctrl+N (Ctrl+Space はかな入力トグルに割当済みなので
  使えない)。キー判定は scancode で行う。
- ホバー: カーソル下の変数/メソッドの型をステータス行 or 小ポップアップに。
- 診断: 保存時 or 手動実行で該当行をマーキング + ステータス行に件数。
- 実行タイミングはキー要求時のみから始める (毎打鍵はしない)。全ソース
  再パース方式なので、大きいファイルでの所要時間を P2 で実測してから決める。

## 懸念と対処方針

1. arena 16KB が静的 BSS = ESP32 では内蔵 RAM を常時消費する。
   S3 の内蔵 RAM は逼迫しているので、EXT_RAM_BSS 属性化 (overlay patch) か
   「arena を外から渡す API」の上流提案で PSRAM に逃がす。
2. S3 の flash 残 6%。エンジン (~22 ファイル) + 生成 db の実サイズを
   idf.py size-components で測ってから S3 投入を判断。Modern (P4) 先行。
3. prism の確保が PRISM_XALLOCATOR 経由で mrb_malloc(global_mrb) に落ちる。
   エディタタスクから叩いたときのプールと、他タスクのコンパイルとの競合を
   P5 (esp32) で確認する。
4. Linux sim は 64bit でポインタが太る。arena あふれの出やすさが実機と
   違う可能性 (あふれても候補が出ないだけで壊れはしない)。

## 今後の作業 (段階)

- T0: 上流 PR — **スキップ決定 (2026-08-11)**。fork 追従で進める。
  fmrb-inheritance は PR 候補として温存し、出すかどうかは別途判断。
- P1: 取り込みと配線。**指示書 = instruction_p1.md 発行済み**。
  vendor/picoruby-ti (PIN + rake ti:setup) + gembox + Rakefile での db 生成、
  rake build:linux を通す。FMRB API の RBS 最小セット (FmrbAppBase / Canvas
  周辺) を書く。エンジン単体のホスト回帰 (host_test) を rake ti:test で回す。
- P2: エディタ補完 UI。**実装完了 (2026-08-11, report/p2.md)。レビュー指摘
  1 件が修正待ち: s_prism_scratch の __thread 化** (補完中の他タスク
  コンパイルが使い捨てヒープに迷い込む競合)。以下は発行時の記録。
  @gfx は fork の RBS ivar 宣言対応で解決 (PIN を 333beb1 に更新 +
  sig に @gfx: FmrbGfx。基底コード変更なし)。発火は Tab (文脈依存)。
  ti ブリッジ (et_*) は editor-core 内、結果はブリッジ側にコピー
  (arena は次呼び出しで無効)。prism アロケータ
  (PRISM_XALLOCATOR -> mrb_malloc(global_mrb)) の安全確認を T2 冒頭で行う。
  sim で自律検証 + ti_lat 常設計測。
- P3: ホバーと診断の UI。**指示書 = instruction_p3.md 発行済み**。
  fork 側の宿題 (hover.c の外側クラス解決 + 変数引きの
  ti_handle_identifier 化) は**実施済み** (fmrb-dev d40a9e6。@gfx の
  ホバーが Canvas を返すことをホスト確認、回帰テスト追加)。
  キーは Ctrl+T (ホバー) / Ctrl+E (診断、保存時自動 + 連打でジャンプ)。
  ステータス行は「右端バッジ常設 + 左は一時メッセージ (ヘルパ集約)」に
  整理して P2 のかなバッジ衝突を解消する。
- P4: RBS の充実。FmrbApp / GFX / Sprite / Sound / MIDI と進め、
  RBS を API ドキュメントの正とする運用に乗せる。
- P5: esp32 対応。Modern (Tab5) 先行。サイズ実測、arena の PSRAM 化、
  prism アロケータの競合確認。S3 はサイズ次第で判断。
- P6 (任意): PC 側 LSP/MCP。同じ sig から picoruby-ti-lsp を建てて
  VSCode/Vim/Claude から FMRB アプリの型支援を使えるようにする。
- P7 (任意): WebConsole (tool/web) の簡易エディタへの展開。2 経路:
  診断は debugd にコマンドを生やして保存時に相乗り (BLE は全文転送が
  遅く打鍵補完には不向き、保存はどうせ全文送る)。補完はエンジン + db を
  emscripten で WASM 化してブラウザ内で完結させる (エンジンは純 C 12KB +
  db 37KB なので現実的。db は同じ sig/ から生成して実機とずらさない)。

## 未確定事項

- 補完の起動キー (Tab か Ctrl+N か)。かな入力との干渉最終確認も含めて
  P2 で決める。
- アプリ基底を RBS に寄せるか Ruby 先読みにするか (P1)。
- S3 に載せるか Modern 限定にするか (P5 の実測後)。
- BASIC / エディタ以外 (shell 等) への展開は当面やらない。
