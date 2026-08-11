# 完了報告: Spinel 再インスタンス化時の stale 定数 static の根本修正

対象指示書: `doc/spinel_aot/instruction_stale_statics.md`。
発端は `doc/editor_ja/report/ja2.md` の「エディタを 2 回目に開くと SEGV」。

| | |
|---|---|
| fork 修正 | `c7de66c` (kishima/spinel, fmrb-dev) **未 push (要確認)** |
| SPINEL_PIN | `7b1feb75...` → `c7de66ca687b5029f51a614a5cbee5e66a42fd27` |
| fmruby-core 側 | 本コミット (pin + IMPORT_INFO + アプリ側回避の撤去 + doc) |

---

## T1: 全域調査 — クリア対象の列挙

判定基準は指示書どおり「**instance_end 後も値が残り、次の begin 時に mark
され得るか / 参照され得るか**」。

### クリア対象 (生成 TU)

| 種別 | 生成される static | 危険な理由 | 件数 (editor) |
|---|---|---|---|
| 定数 | `cst_<NAME>` | 文字列/配列/Hash 等はインスタンスヒープを指す。**エントリが再代入するのはソース順で、mark フック登録より後**なので、その途中の GC で未代入分が前インスタンスの値のまま mark される | 定数含め計 88 スロット |
| グローバル変数 `$x` | `gv_<name>` | 同上 (editor には無し) | 0 |
| クラス ivar | `civ_<Class>_<ivar>` | 同上。FmrbApp / FmrbGfx 等の共有基盤に多い | (上記 88 に含む) |
| singleton reader | `sg_<Class>_<name>` | 同上 | (同上) |
| **オブジェクトプール** | `sp_<Class>_pool_head` / `_pool_count` (`SP_POOL_DEFINE`) | **mark とは別系統の、より危険な経路**。`SP_POOL_NEW` が前インスタンスのヒープにあるオブジェクトを pop して `sp_gc_pool_relink` で現ヒープに繋ぎ直す = **解放済みメモリの再利用**。GC の NULL ガードでは防げない | 17 クラス |

**mark 一覧 = クリア一覧**にした。生成器の同じループで両方を組み立てるので、
片方だけ増えて乖離することがない (88 行が 1 対 1 で対応)。

### クリア対象外 (根拠つき)

| 対象 | 除外の根拠 |
|---|---|
| 文字列リテラル static (`_slit_NNN`) | 先頭マーカが `0xff` の**不滅リテラル**で、静的記憶域にある。`sp_mark_string` は `0xfe` (ヒープ文字列) のときしか触らない。インスタンス跨ぎで安全 |
| 整数・真偽の定数 (`cst_MENU_ID_FILE` 等 `mrb_int`) | ヒープを指さない純データ。エントリが毎回代入し直す |
| `sp_init_in_progress_<name>` | `int` フラグ。ヒープ非参照 |
| ランタイム (`lib/sp_*.c`) の global | SP_MULTI_CTX では `sp_ctx` フィールドへのマクロ。`sp_gc_old_heap` / `sp_gc_mark_stack` 等の file-scope static は **`#ifndef SP_MULTI_CTX` の中**にあり、multi-ctx ビルドには存在しない |
| `sp_gc_mark_stack` (multi-ctx 側) | ctx フィールド。かつ malloc された C ヒープで、インスタンスプールではない |
| `sp_re_init` / `sp_tu_ctx_init` が登録するフック類 | **関数ポインタ**であり、しかも登録先は `sp_ctx` (per-instance)。プロセスグローバルなのは指す先の関数 (静的コード) だけ |
| `__thread sp_ctx *g_sp_ctx` | 現在インスタンスの指し先。begin/end で張り替わる |

### 指示書の前提のうち、実物と違った点

指示書は「**mark フック側も NULL を skip することを保証する** (0x1 を mark
したのは skip が無い証拠)」としていたが、**生成済みの mark には既に
NULL ガードがある** (`if (cst_X) sp_gc_mark(...)`、`sp_mark_string` は
`if (!s) return`、`sp_gc_mark` も `if(!obj)return`)。バックトレースの `0x1` は
root そのものではなく、**stale なポインタを辿った先で壊れたヘッダを読んだ
結果**。つまり足りなかったのは skip ではなく**クリア**だけなので、
0 クリアのみを入れた (NULL skip は既存のまま使える)。

---

## T2: 修正 (Spinel フォーク `c7de66c`)

`src/codegen.c` / `codegen_util.c` / `codegen_internal.h`:

1. **reset 一覧の生成**: `sp_mark_user_globals` を組み立てるループで、
   同時に reset 行を `rs` バッファへ積む
   (`const char*` → `NULL`、`sp_RbVal` → `sp_box_nil()`、
   オブジェクトポインタ → `NULL`)。結果を `g_tu_reset_globals` に持つ。
2. **プール一覧の収集**: `SP_POOL_DEFINE` を出す 3 箇所で
   `note_pool_class(ci->c_name)` を呼び、`NameSet` に貯める
   (`nameset_add` が重複を除くので、collect-mode の二度打ちでも安全)。
3. **`sp_reset_tu_statics()` の出力**: クラス本体を出し終わった後、エントリの
   直前に `#ifdef SP_MULTI_CTX` 付きで定義。全 88 スロット + 17 プール
   (`pool_head = NULL; pool_count = 0;`) をクリアする。
4. **呼び出し**: エントリ冒頭、`sp_tu_ctx_init()` の**直後**、
   `sp_re_init()` (= mark フック登録) の**前**。

```c
int editor_entry(void){
    SP_GC_SAVE();
#ifdef SP_MULTI_CTX
    sp_tu_ctx_init();
    sp_reset_tu_statics();   /* ← 追加 */
#endif
    sp_re_init();
```

- **既定ビルド (単一インスタンス) の出力は不変**。`#ifdef` の外には 1 行も
  出ないので、プログラムを 1 回しか動かさない構成には影響しない。
- TU ごとに独立した static なので、kernel / desktop / editor それぞれで
  独立に効く。動作中の他インスタンスには触れない (「これから begin する TU」
  の static だけを触る、という指示書の注意を満たす)。

---

## T3: 検証

### 再現手順 (決定的)

エントリ中に GC を必ず走らせるのが条件。JA2 で入れたアプリ側回避
(`EditorStrings.install` を `on_create` から呼ぶ) を**撤去して
`FmrbI18n.add` をトップレベル = エントリに戻す**と、100% 再現する:

```
デスクトップで e (エディタ起動) → Ctrl+Q → e   ← 2 回目で SEGV
```

修正前のバックトレース (gdb):

```
#1 sp_gc_mark_all () at sp_gc.c:129
#2 sp_gc_collect () / #3 sp_gc_collect_retune () / #4 sp_gc_alloc ()
#5 sp_PolyArray_new () / #6 sp_poly_each_elem ()
#7 sp_FmrbI18n_s_add (editor_combined.rb:1933)
#8 editor_entry ()
#0 0x0000000000000001 in ?? ()
```

### 修正後

| 確認 | 結果 |
|---|---|
| **開閉 10 回連続 (gdb 監視下、アプリ側回避なし)** | **SIGSEGV 0 件 / "EditorApp created successfully" 10 件** |
| 標準構成の kernel / desktop 起動 | OK (ブート〜デスクトップ、ランチャー) |
| JA2 受け入れ: 折り返し・クリック・日本語表示 | OK (`rg1.png`: 折り返された行の継続区分をクリック → 行 14 桁 45) |
| 200KB 編集で OOM が出ない (閾値は pool/32 のまま) | OK `edit_lat: n=100 mean=797us / 917us p99<=5ms over25ms=0` |
| 互換構成 (mruby エディタ) | OK (開閉 3 回、SEGV 0 件。Spinel 側のみの変更なので当然だが確認した) |
| S3 / P4 ビルド | OK (残 16% / 29%) |

---

## T4: 後始末

- **アプリ側回避は撤去した**。`editor/i18n.rb` は元どおりトップレベルで
  `FmrbI18n.add` する。理由: 修正が入った以上この間接は不要で、しかも
  **エントリで登録する形に戻しておけば、エディタを開くたびに毎回この修正が
  実際に効いていることを確かめ続けられる** (回避を残すと、修正が将来壊れても
  誰も気づかない)。
- `ruby_writing_constraints.md` の B 表に「**同じプログラムを 2 回目に起動
  すると SEGV**」を **fork 修正済み `c7de66c`** として追加した
  (制約として書かれる前に解消したので、履歴として残す形にした)。
- `reports/fork_pr_candidates.md` に「修正済み」節を追加。
- `doc/editor_ja/report/ja2.md` の該当節に本レポートへの参照を追記。

## upstream 適用可否の所見

**単独で提出できる**と考える:

- 追加は `#ifdef SP_MULTI_CTX` の内側だけで、**既定ビルドの生成 C は 1 バイトも
  変わらない**。既存の利用者への影響が無い。
- 変更は codegen のみ (ランタイム `lib/` は無改変)。よって
  `import_from_fork.rb` が入れ替える snapshot の中身も変わらず、差分は
  IMPORT_INFO のメタデータだけ。
- SP_MULTI_CTX は「1 プロセスで同じプログラムを複数回/複数インスタンス動かす」
  ための機能なので、**この修正が無いと機能自体が 2 回目から壊れる**。
  upstream にとっても素直なバグ修正のはず。
- ただし PR 化の判断はユーザに委ねる (fork 運用方針どおり)。

## 未実施 (ユーザ確認待ち)

- **fork の push**: `c7de66c` はローカルコミットのまま。SPINEL_PIN はこの
  commit を指しているので、**push するまで他のマシンで `rake spinel:setup`
  が失敗する**。push してよいか確認したい。
- 実機 (S3 / P4) での再オープン確認。
