# 実装指示書: Spinel 再インスタンス化時の stale 定数 static の根本修正

対象: 実装担当セッション。発端は doc/editor_ja/report/ja2.md の
「エディタを 2 回目に開くと SEGV」。アプリ側の回避 (i18n 登録を entry の
外へ) は入っているが、**ユーザ指示 (2026-08-11): 根本解決が必要**。
report は doc/spinel_aot/report/stale_statics.md へ。

> 後日談 (2026-08-14): この「entry 冒頭で毎回 reset」(`c7de66c`) は run-once の
> プログラム向け。**繰り返し呼ぶライブラリ用途では reset が前処理を毎回払わせる**
> ので、`cafe6595` で `--persistent-statics` (reset を初回だけにする opt-out) を
> 追加した。詳細は `stateful_library_entry.md` / `impl_plan_stateful_library_entry.md`、
> 書き方は `ruby_writing_constraints.md` (C 節)。

## バグの機序 (ja2.md で特定済み)

生成された Spinel プログラムは定数を**プロセスグローバルな C の static** に
持つ。エントリはそれをソース順に再代入するが、**GC の globals-mark フックは
最初の再代入より前に登録される** (sp_re_init がエントリ冒頭)。よって
2 回目以降のインスタンスで、エントリ実行中に GC が走ると**前のインスタンスが
残した無効ポインタを mark して SEGV** する。

- 再現条件: 同一 Spinel プログラムの再インスタンス化 + エントリ中の GC。
  エディタは「繰り返し起動される唯一の Spinel プログラム」なのでここでだけ
  顕在化した (kernel / desktop は 1 回きり)。
- 実測: エントリの割り当ては 16〜32KB。GC 閾値 pool/32=16KB がエントリ中に
  当たる。閾値を上げる対処は 200KB 編集で OOM になり**却下済み**。

## 修正方針 (ja2.md の引継ぎどおり、生成器側で)

**インスタンス生成時に、その TU の定数 static 群を 0 クリアする**。
mark フックが 0 を skip すれば、未代入の定数は単に「まだ無い」として扱われ、
エントリ中の GC が安全になる。

## タスク

### T1: 全域調査 (修正対象の列挙)

生成 TU + ランタイムの**プロセスグローバル static のうち、インスタンスの
ヒープポインタを保持し得るもの**をすべて列挙する。定数だけとは限らない:

- 定数 (今回の直接原因)
- グローバル変数 ($x) の格納先
- 文字列リテラル・frozen リテラルのキャッシュ static (あれば同じ危険)
- sp_re_init / sp_tu_ctx_init が登録・初期化しているもの一式
- インスタンス間で共有してよいもの (ヒープを指さない純データ) は除外し、
  その判断根拠を report に

「instance_end 後も値が残り、次の begin 時に mark され得るか」を基準に
判定する。

### T2: 修正 (Spinel フォーク側)

- 生成器が **TU ごとの「クリア対象 static の表」** (アドレス配列または
  1 個の struct 集約) を出力し、per-instance 初期化
  (sp_tu_ctx_init 相当。mark フック登録の**前**) で 0 クリアする。
- mark フック側も **NULL を skip する**ことを保証する (現状 0x1 のような
  ゴミを mark したのは skip が無い証拠。0 クリアと NULL skip の両輪)。
- 複数 TU (kernel / desktop / editor) それぞれで独立に効くこと。
- 作業リポジトリは **Spinel フォーク (vendor/spinel、kishima/spinel の
  fmrb-dev)**。反映手順は SPINEL_PIN のヘッダ記載どおり:
  fork へ push → SPINEL_PIN の commit 更新 →
  `ruby components/fmrb_spinel_rt/import_from_fork.rb vendor/spinel` 実行
  → **両方を同時に
  コミット** (rake spinel:gen が IMPORT_INFO との食い違いを警告するので
  clean を確認)。

### T3: 検証

1. **再現テストを先に作る**: 修正前の状態で「エディタ開閉 2 回で SEGV」を
   決定的に再現する手順を確立する (エントリ中に GC を必ず走らせる。
   一時的に閾値を下げる、または i18n の on_create 回避を一時的に entry へ
   戻すなど。手段は report に)。
2. 修正後、同条件で **開閉 10 回連続 SEGV なし** (Linux sim、gdb 監視下)。
3. 回帰確認: 標準構成の kernel / desktop 起動、エディタの JA2 受け入れ
   シナリオ一式 (折り返し・クリック・日本語表示)、200KB 編集で OOM が
   出ないこと (閾値は pool/32 のまま)。
4. S3 / P4 ビルド通過。互換構成 (mruby) に影響が無いこと (Spinel 側のみの
   変更のはずだが確認する)。

### T4: 後始末

- **アプリ側回避 (EditorStrings.install の on_create 呼び) は残して良い**
  (エントリを軽くするのは行儀として悪くない)。ただし修正の実証として、
  「entry に戻しても落ちない」ことを T3-1 の再現手順で確認してから戻すか
  残すかを決め、判断を report に書く。
- doc/spinel_aot/ruby_writing_constraints.md の
  「エントリで大きな割り当てをしない」制約 (B: fork 修正で消えるべきもの)
  を**解消済み**に更新する。
- fork_pr_candidates.md に本件の起票があれば解決済みへ、無ければ
  修正済み事項として記録する。
- doc/editor_ja/report/ja2.md の該当節に解決コミットへの参照を 1 行追記。

## 注意

- スタック上の値や実行中インスタンスには触らない修正であること
  (kernel は動きっぱなしで再インスタンス化されない。クリアは
  「これから begin する TU」の static に限る)。
- Spinel はプロトコル進化が速い fork 運用なので、修正は fmrb-dev に閉じ、
  upstream への PR 化判断はユーザに委ねる (report に upstream 適用可否の
  所見だけ書く)。

## 完了報告

report/stale_statics.md に: T1 の列挙表 (クリア対象/対象外と根拠)、
生成器の変更点、再現手順と修正前後の結果、回帰確認、SPINEL_PIN の
新旧 commit、upstream 適用可否の所見。
