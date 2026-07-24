# Spinel メモリモデル調査 (T4-5 launcher OOM が発端)

調査日: 2026-07-24
対象: vendor/spinel `5129af9` 系 / 生成 desktop (SP_MULTI_CTX)
計測環境: **Linux headless (64-bit)**。SYSTEM_APP プール 800KB (PSRAM)。
発端: system_desktop の boot (on_create → ensure_icon_sprites, 29 icon) で OOM。

## 結論

- **live (保持メモリ) が mruby 比 1.9x** の主因は、**Spinel の per-object GC ヘッダが太い**
  こと (64-bit で 48B/object) ＋ **object を 1 個ずつ個別確保** すること。データ量では
  なく**簿記コスト**。mruby は均一スロットをアリーナから切り出すため object 単位の
  固定 overhead がほぼ無い。
- **churn (使い捨てゴミ) が mruby 比 11x**。OOM の直接の引き金はこちら (transient burst
  が薄い headroom を GC 回収前に超過)。live の 1.9x が headroom を薄くする素因。
- **この 1.9x / 11x は 64-bit Linux の数字**。32-bit ESP32 ではヘッダのポインタが半減し
  絵が変わる。**ESP32 のプール設計を 64-bit の比率で行ってはいけない** (要再計測)。

## T4-5 launcher OOM の計測 (churn / live 分離)

`ctx->est` の est_used を刻み、forced-GC 前後で true-live を分離:

| 指標 | mruby | Spinel |
|---|---|---|
| true live (29 icon 後・forced GC) | — | 678KB (平坦、+9KB のみ) |
| baseline live (非 forced) | 389KB | 728KB → **1.9x** |
| churn / GC サイクル | 6KB | 66KB → **11x** |
| pool (SYSTEM_APP, 共通) | 800KB | 800KB |
| OOM | なし (peak ~408KB) | あり (~icon 28) |

判定: **churn 支配**。live 678KB は平坦で pool に収まる (headroom ~122KB)。OOM は icon
parse/draw の transient garbage が薄い headroom を burst 超過するもの。
なお **true-live が 29 icon で +9KB のみ = icon sprite の実体は gfx 側メモリで、est(Ruby)
側は handle だけ**。678KB の live は「desktop エンジンの baseline footprint」であり
icon cache ではない (→ lazy icon load では live は減らない)。

## live 1.9x の root cause: object モデル (実測サイズ)

全 heap object が背負う `sp_gc_hdr` (`sp_string.h`):

```c
typedef struct sp_gc_hdr {
  struct sp_gc_hdr *next;              // 8   intrusive linked-list heap
  void (*finalize)(void *);            // 8
  void (*scan)(void *);                // 8
  size_t size;                         // 8   ("vestigial" for strings)
  unsigned marked:1; frozen:1;         // 4 (padded)
  void (*recycle)(struct sp_gc_hdr *); // 8
} sp_gc_hdr;                           // = 48 bytes/object (64-bit)
```

- poly 値 `sp_RbVal` = 16B (tag4 + cls_id4 + union8)。symbol キー Hash は常に poly。
- 文字列は別途 `sp_str_hdr` = ~24B/string。

| | mruby | Spinel |
|---|---|---|
| object 確保 | 均一 RVALUE スロットをアリーナ(ページ)から切り出し | **1 個ずつ est_calloc で個別確保** |
| per-object ヘッダ | RBasic ≒ 16B | **sp_gc_hdr = 48B** (64-bit) |
| アロケータ overhead | ページ一括 = object 単位ゼロ | **object ごとに estalloc ブロック header + align** |

固定 48B header × 多数の小 object が効く。live object 8,000 個なら header だけで
48B×8000 ≒ 384KB で、678KB の大半が説明できる規模 (要 object 数実測)。
[[spinel-stack-model]] の「object を個別確保・intrusive linked-list GC」のメモリ側の帰結。

## fundamental か reducible か

- **fundamental**: intrusive per-object GC (`next` リンク、per-object finalize/scan) は
  Spinel コレクタの根幹。
- **reducible (fork 最適化候補、高レバレッジ)**:
  1. `finalize` + `scan` の2ポインタ(16B)を **per-type ディスクリプタ1本**に集約
     (mruby 流に型ごと共有)。object ごとに関数ポインタ2本は無駄。
  2. `size` は文字列で vestigial。アロケータから引ければ per-object 8B 削減。
  3. `recycle` の要否を精査、`finalize` と統合できれば 8B。
  → **header 48B → ~24B に半減余地**。全 Spinel アプリの RSS を下げる。
  - 加えて estalloc を size-class/arena 化すれば個別確保 overhead も削減 (副次)。
- **churn 11x** は別軸: icon parse の中間 String/poly の使い捨て。est_calloc の zero-fill
  由来が大きければ、既出の **raw (非 zero-fill) alloc フック**の fork 起案材料。

## 64-bit vs 32-bit の注意 (重要)

sp_gc_hdr の大半はポインタ + size_t。**32-bit ESP32 では全部半分**:
- 48B → **24B** (4×5 + flags4)。mruby 側 RBasic も 16B→8B に縮む。
- `sp_RbVal` は 16B のまま (union が mrb_int/float の 8B)。

→ **ESP32 実機の live 比は 1.9x とは別物**。64-bit の 1.9x / 85%-pool を
ESP32 のプール設計にそのまま使わない。

## T4-5 の当面 fix と残課題

- **当面 fix (main 側、pool 非拡大)**: icon loop で毎 icon 明示 GC
  (`_force_gc`→`FmrbApp.gc`→`fmrb_spx_app_gc`→`sp_gc_collect`)。threshold 単独
  (pool/16→/32) や every-4 では tight FFI draw loop 中に自動 GC が間に合わず不十分。
  every-icon で boot clean。mruby は `FmrbApp.gc` 不在→rescue no-op (dual-safe)。
  ※ boot 時 one-time なら可。**interactive ループに forced GC を一般化しない** (frame hitch)。
- **残る構造リスク**: live 678KB = pool の 85% (mruby は 49%)。他操作が >122KB churn すれば
  同様に OOM しうる。

## 次にやること

1. **header 支配を1数字で確定**: forced-GC 後に `sp_gc_heap` を辿り live object 数を数え、
   `object数 × 48B` が 678KB の何割かを見る。
2. **ESP32 (32-bit) で live/churn 再計測**。これが本当のプール設計の根拠。
3. header 支配確定なら **sp_gc_hdr スリム化を fork 起案** (finalize/scan の per-type 化)。
   pool 拡大せず headroom を回復するのが本筋。churn の zero-fill 寄与が大なら raw-alloc も。
4. 上記が済んでから、必要なら **measured な** ESP32 側 SYSTEM_APP プール拡大を
   PSRAM 予算と併せて判断 (blind な 2x は避ける)。

## Linux dev のプール (ユーザ決定 2026-07-24)

64-bit Linux は object ヘッダ/ポインタが 32-bit ESP32 の倍なので、同じ desktop でも
Linux は本質的に ~2x の live を食う。よって **Linux 版のみ SYSTEM_APP を 1.5MB に拡大**し、
dev の安定を確保する (hack でなく 64-bit 分の正当な差別化)。ESP32 の 800KB は据え置き。
`CONFIG_IDF_TARGET_LINUX` ガードで分岐 (`fmrb_mem_config.h`):

```c
#ifdef CONFIG_IDF_TARGET_LINUX
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (1536*1024)  /* 64-bit object model ~2x + headroom */
#else
#define FMRB_MEM_POOL_SIZE_SYSTEM_APP (800*1024)
#endif
```

注意: これは dev 利便で、object-model 調査 (header 内訳確定・ESP32 再計測・sp_gc_hdr
スリム化) を止める理由にはしない (live/churn の数字は取得済で signal は失われない)。
every-icon GC も残す (ESP32 のタイトな pool で依然必要かもしれず、dual-safe)。
