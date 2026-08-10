# 実装指示書 P4: editor-core gem (文書モデルの C 化)

対象: 実装担当セッション。前提: P1-P3 完了 (report/p1.md〜p3.md)。
report は report/p4.md へ。タスクごとにコミット。

## 決定事項 (ユーザ決定 2026-08-10)

- **実装方式は C 直書き** (syntax-highlight と同型の native mrbgem)。
  「Ruby で書いて Spinel で C 変換」案は採らない — Spinel オフロード基盤
  (mruby からの同期呼び出し経路 / sp_export / プール切り出し) は未構築で
  あり、その第 1 号は別題材 (raycaster 等) に譲る。
- 実行時の切り替え・純 Ruby 版文書モデルの並走維持はしない (plan.md 6 章)。
- gem は将来 (段階 6) Spinel 版エディタからも FFI で呼ばれる共通実装になる。

## 目的

1. **50KB 級ファイルでエディタ VM が死ぬ問題の解決** (p1.md。20.7KB 正常 /
   53.4KB で例外ログ無くタスク消滅)。
2. **全画面エディタ + 14KB 窓アプリ同時で NoMemoryError の解決** (p2.md)。
3. 文書データを mruby GC ヒープから追い出し、編集時のゴミ発生を減らす。

## 最重要の設計要件: 確保元は専用プール新設

**文書 arena は新設の専用プール `POOL_ID_EDITOR_DOC` (PSRAM 静的、1MB) に
置く。`mrb_malloc` も `fmrb_sys_malloc` も使わない。** 理由:

- `mrb_malloc` は VM を開いた 500KB アプリプールそのものから出る
  (mrb_open_with_custom_alloc)。C の平坦バッファ化だけでも消費は大きく
  減るが、(1) 固定 500KB の天井に文書サイズが乗り続ける (200KB 級の要件を
  満たせない)、(2) malloc_increase として GC 発火の会計に乗る、
  (3) アプリの churn と混ざって断片化する、の 3 点が残る。
- `fmrb_sys_malloc` は **POOL_ID_SYSTEM = 500KB の共有プール**から出る
  (fmrb_alloc.c の fmrb_sys_mem_init)。ここはドライバ等のシステム機能の
  財布であり、1MB 級の文書は物理的に入らないし、入る大きさに拡張しても
  「共有財布を文書が食う」構造の問題が残る (ユーザ指摘 2026-08-10)。

実装:

- `FMRB_MEM_POOL_SIZE_EDITOR_DOC (1*1024*1024)` を fmrb_mem_config.h に
  追加し、fmrb_mempool.c のプール配列 (EXT_RAM_BSS_ATTR) と get_size に
  同期させる (2 ファイル同期。アプリスロットではないので
  fmrb_task_config.h は触らない)。PSRAM 余裕は S3 実測 3.2MB /
  P4 20MB 超なので 1MB は両機種とも安全。
- gem 側は `fmrb_get_mempool_ptr(POOL_ID_EDITOR_DOC)` +
  `fmrb_mem_create_handle` で自分のハンドルを作り、`fmrb_malloc(handle,..)`
  で確保する (既存 TLSF 機構の再利用。`ps` の VM Pools 一覧にも載り
  観測できる)。
- arena はセッション単位 (open 時に確保、close/次 load で解放または再利用)。
  **枯渇時は負のエラーコードを返して mruby 側でメッセージ表示**
  (「ファイルが大きすぎる」) とし、**絶対に firmware を落とさない**こと。
- Linux ターゲットでも同経路でビルド・動作すること (fmrb_mem は両対応)。

## gem の作り

- 置き場所: `lib/add/picoruby-fmrb-editor-core/` (mrblib の薄いラッパ +
  src/ の C 実装 + include/)。gembox (lib/add/family_mruby.gembox) に追加。
  ESP-IDF ヘッダ (fmrb_mem 等) を使うので `components/picoruby-esp32/
  CMakeLists.txt` の PICORUBY_SRCS への配線も忘れない (CLAUDE.md の規約)。
- 文書の内部表現は実装に任せる (行ポインタ配列 + 行バッファが最も素直)。
  1 文書のみ対応で良い (ハンドル不要。エディタは 1 インスタンス 1 文書)。
- **列の意味は現行踏襲 = UTF-8 の文字数**。現行コードは mruby String の
  文字単位スライス (`line[0,@cx]`) で動いているので、C 側も cursor 列は
  文字インデックスとして扱う (バイトではない)。CJK の 2 桁幅表示は現行にも
  無いので範囲外。
- **ハイライトは gem 内で完結させる**: picoruby-syntax-highlight の
  トークナイザを C レベルで呼べるよう関数を 1 本公開し (現在は mruby
  バインディング経由のみ)、editor-core が行バッファ上で直接呼ぶ。
  これで「1 行描くたびに mruby ヒープへ hl 用 String が 1 個増える」現状の
  ゴミ発生も消える。行単位トークン結果のキャッシュ (dirty 行だけ再計算) を
  gem 内に持つこと (report/p3.md に残した改善案の実装位置がここになる)。

## 境界 API (18 本、実コードの 60 箇所から導出)

引数・戻り値はスカラーと String のみ。複合の戻り値は packed binstr
(固定レイアウト) で返す。将来 Spinel FFI (:int/:str/:binstr) にそのまま
乗る形を保つこと。

```
# 読み (描画・ナビゲーション)
line_count()                              -> Integer
line_length(y)                            -> Integer   # UTF-8 文字数
render_row(y, col0, max_cols)             -> binstr    # 行テキスト断片 + hl マップを
                                                       # 1 レコードで (境界越え半減)
char_at(y, x)                             -> Integer   # カーソル下 1 文字 (draw_cursor 用)

# 編集 (戻り値で新カーソル位置と dirty_from を返す)
insert_text(y, x, str)                    -> Integer   # 新 x
split_line(y, x)                          -> nil
join_line(y)                              -> Integer   # 結合前の前行長 (= 新 x)
delete_char(y, x)                         -> nil
delete_range(sy, sx, ey, ex)              -> nil
insert_multiline(y, x, str)               -> binstr    # packed [new_y, new_x]

# I/O (mruby 側に全文 String を一切作らない)
load_file(path)                           -> Integer   # 行数 / 負のエラー
save_file(path)                           -> Integer   # 書いたバイト数 / 負のエラー
doc_bytesize()                            -> Integer

# 検索 (join("\n") を gem 内に閉じ込める)
find(query, from_y, from_x, after)        -> binstr    # packed [found, y, x]

# クリップボード (文書コピーを mruby ヒープに出さない)
copy_range(sy, sx, ey, ex)                -> Integer   # gem 内クリップボードへ。長さ
paste_at(y, x)                            -> binstr    # packed [new_y, new_x]
clipboard_length()                        -> Integer

# 計測
mem_used()                                -> Integer   # arena 使用バイト
```

- 行数が変わる操作 (split/join/delete_range/insert_multiline/paste) は
  アプリの dirty 管理 (`mark_dirty_from`) と接続する。戻り値に dirty_from を
  含めるか、アプリ側で操作種別から導くかは実装に任せる (ハイライトの
  複数行構文対応を将来やるなら gem が dirty_from を返す形が伸びる)。
- undo/redo は**範囲外** (現行にも無い)。ただし操作が全部 gem を通るので、
  将来 gem 内ジャーナルで実装できる。API を閉じておくこと (アプリが行内容を
  直接書き換える裏口を残さない)。

## アプリ側の書き換え

- `@lines` を全廃し、上記 API の薄いラッパ経由に置き換える (対象 22
  メソッド、report/p1 の差分描画・P3 の HL 既定はそのまま活かす)。
- カーソル・選択・スクロール・検索 UI・デバッガ・メニュー等の UI 状態は
  従来どおりアプリ側 (選択は delete_range/copy_range に 4 スカラーを渡す
  だけ。selected_text をアプリに持ってこない)。
- `load_file` / `save_file` / `find` の全文 join 経路を削除する。

## 性能の注意 (ここだけは計測しながら)

全面再描画 1 回で render_row が可視行数ぶん (27〜45 回) 呼ばれる。mruby の
メソッド呼び出し + binstr 生成のコストが乗るので、**T1 基準値 (p1.md) から
の退行が無いことを edit_lat: で確認**する。行テキストと hl を 1 レコードに
畳むのはそのため。もし退行するなら、可視範囲をまとめて返す
`render_rows(y0, n, col0, max_cols)` に畳んで呼び出し回数を 1/行数にする。

## 受け入れ条件 (数値で)

計測は `load_file` 直後に `FmrbApp.pool_usage` と `mem_used` を 1 行ログに
出して行う (sim と実機で同じ行が取れる)。

1. **53.4KB のファイルが開けて編集・保存できる** (現状: VM 消滅)。
   さらに 200KB 級の生成ファイルでも開けること (arena 上限までは動く)。
   上限超過時はエラーメッセージ表示で、エディタも firmware も生きている。
2. **pool_usage: 20KB ファイルを開いた状態で従来より大幅に低い**こと
   (T1 比。従来値と新値を report に併記)。目安: 文書サイズに比例する
   消費が mruby プール側からほぼ消える。
3. **全画面エディタから 14KB 窓アプリ (tetris) を F5 → NoMemoryError に
   ならない** (p2.md の再現手順)。
4. **edit_lat: が T1/T4 基準から退行しない** (p99 < 33ms、25ms 超ゼロを維持。
   小ファイル HL on / 10.9KB HL on の 2 条件で比較表を report に)。
5. 機能退行なし: 編集・選択・コピー/カット/ペースト (複数行含む)・検索・
   置換相当の操作・HL (P3 の既定含む)・デバッガペイン・保存/名前付け保存。
   sim スクリーンショットで代表ケースを report に。
6. Linux / S3 / P4 全ビルド通過。標準構成 (Spinel カーネル) で一通り、
   互換構成 (mruby カーネル) でスモーク。実機確認はユーザ確認待ちで明記。

## 範囲外

- undo/redo (将来 gem 内ジャーナルで)
- ハイライトの複数行構文対応 (行キャッシュの土台までは可)
- Spinel FFI バインディング (段階 6 で追加)
- 行折り返し (現行に無い)

## 進め方の約束

P1-P3 と同じ。加えて:

- lib/ を触るので **ビルド前 rake clean** を忘れない。
- gem 追加時は submodule 側にコピーされる構成 (lib/add → コピー) を確認
  してから編集する (feedback: mrbgem 編集は lib/add 側)。
- 最後に doc の整合を 1 箇所直す: doc/spinel_aot/selective_offload.md の
  「editor-gem が単一ソース二重バックエンドの利用者第 1 号」の記述に、
  2026-08-10 の方針変更 (editor-core は C 直書き、オフロード基盤第 1 号は
  別題材に譲る) の注記を日付つきで追加する。

## 完了報告

report/p4.md に: 内部表現の選択と理由、API の最終形 (差分があれば)、
受け入れ条件 1-6 の実測値 (前後比較表)、性能の考察 (境界越えコスト)、
段階 6 (Spinel 版エディタからの FFI 利用) への引継ぎ事項。
