# Spinel 上流の ext 機構でフォークを置き換えられるか

> 状態: 進行中 | 更新: 2026-09-26 | **P0・P1・P2a 完了**。フォークを上流 `01521b1e` に載せ直した `fmrb-next` (未 push) がテスト・多重インスタンスの門を全部通過。**ただし実機への影響の見込みが大きい: 内蔵 RAM の .data 約 +47KB、flash 約 +0.95MB**。次は P2b (fmrb 側の取り込みと実機サイズの実測)。PR1 完了 (6 件のブランチが提出待ち)

## 結論

上流 Spinel (matz/spinel master、2026-08-30 の一連のコミット) に「Ruby を
ホストから呼べるライブラリとして出力する」機構が入った。これで fmruby-core の
フォーク (kishima/spinel `fmrb-dev`) を丸ごと捨てられるかを検討した。答えは
**部分的に可、全体は不可**。

- **gem 型 (fft / raycast / spinel_hello) は置換できる**。しかも今より良くなる。
  エントリが型付きの C 関数になるので、値を FFI で往復させる今の作法
  (`:binstr` + `sp_net_bin_len` + 出力用 FFI) が不要になり、
  `--persistent-statics` も要らない (エントリは関数、トップレベルは init 時のみ走る)。
- **VM 型 (kernel / desktop / editor) は置換できない**。上流の ext は
  「1 プロセスに 1 ランタイム、1 イメージに 1 カーネル」の設計で、
  フォークの `SP_MULTI_CTX` (インスタンス分離、pool アロケータ、VFS フック) に
  相当するものが無い。実測で、ext カーネルを 2 本リンクすると 40 シンボルが重複する。
- **組み込み層のパッチは上流に無い**。`SP_NO_MMAN`、`SP_STACK_SCRATCH_MAX`、
  例外スタック段数の可変化、`SP_TU_BSS`、32bit の境界修正はいずれも上流に入っておらず、
  `sys/mman.h` は無条件 include のまま、`sp_time.c` は `__int128` で 32bit コンパイル不能。
- **Ruby 穴埋め (poly ディスパッチ等) は上流で全部通る**。フォークが足した
  6 本のテストを上流バイナリで走らせて全て一致した。rebase 時にこの群は落とせる。

したがって現実的な道は「フォークを最新上流に rebase し、ライブラリ化コミット
3 本を上流の ext 機構に置き換え、gem 型をそちらへ移す。多重インスタンスと
組み込み層のパッチは維持する」である。詳細は「移行方針の選択肢」。

## 目的

- フォークの維持コスト (上流 2,101 コミット分の乖離) を下げる。
- gem 型の境界を FFI 迂回から型付きエントリに直し、gem を書く手間と落とし穴を減らす。
- 上流に PR する価値のある差分を、今の上流の形に合わせて絞り込む。

## 前提: 現在の使い方は 2 形態

フォーク側の仕組みは `doc/spinel_aot/00_common.md` と
`components/fmrb_spinel_rt/` にある。使い方は 2 つに分かれる。

| 形態 | 対象 | 生成フラグ | ホストの呼び方 | 越境する値 |
|---|---|---|---|---|
| VM 型 | kernel / system_desktop / editor | `--no-main --entry X_entry` | タスク起動時に instance を作り `X_entry()` を 1 回呼び、Ruby 側が main_loop を回す | FFI (`fmrb_spx_*`) でメッセージを poll |
| gem 型 | fft / raycast / spinel_hello | `--no-main --entry X_entry --persistent-statics` | mruby タスクが instance を作り、呼び出しごとに `X_entry()` (引数無し) | 入力は FFI の getter、出力は FFI の setter。バイト列は `:binstr` |

どちらも `fmrb_spinel_instance_begin()` (estalloc pool 上の `sp_ctx`) の上で走り、
File/Dir は `sp_ctx` の VFS フックで fmrb HAL へ流している。

## 上流の ext 機構 (実測で確認した仕様)

上流 master `1a507623` (2026-09-05) を手元でビルドし、模擬カーネルで確認した。
再現手順は末尾。

- `spinel k.rb -c --ext-init NAME --ext-entry Mod.m,...` で、`main` の代わりに
  `void NAME(void)` を持つ C と、`k.h` (契約ヘッダ) が出る。init はランタイム初期化と
  トップレベルの実行 (`if __FILE__ == $0` ブロックは除く) を行う。
- エントリは `def self.m` のみ (インスタンスメソッドは拒否される)。ヘッダには実際の
  C 型で宣言される。例:

```c
const char * sp_K_s_greet(const char * lv_name);
sp_float     sp_K_s_sum_f(sp_FloatArray * lv_a);
sp_IntArray *sp_K_s_scale(sp_IntArray * lv_a, sp_int lv_k);
sp_int       sp_K_s_must_pos(sp_int lv_n);
```

- **Layer 1 (このヘッダ) は型を制限しない**。ユーザクラス (`sp_Core *`)、
  `sp_SymPolyHash *`、`sp_PolyArray *`、戻り `sp_RbVal` もそのまま出る。
  「Integer / Float / bool / String と型付き配列のみ」という制限は Layer 2
  (`--ext cruby` の CRuby シム) の話で、fmrb には関係ない。
- エントリの引数型は `if __FILE__ == $0` ブロックの呼び出しから推論する。
  省略可能引数とキーワード引数は位置引数に平坦化される。
- `NAME_try(fn, ctx, &cls, &msg)` で Ruby の raise を (クラス名, メッセージ) として
  受け取れる。`ArgumentError` が実際に越境した。
- String は埋め込み NUL を保ったまま越境する (`"a\0b"` を渡して bytesize 3、
  `"\x00\x01\x02" * 2` を受けて 6 バイト)。raycast の map や fft のサンプル列は
  `:binstr` 無しで普通の String 引数として渡せる。
- init を 2 回呼ぶとトップレベルがもう一度走る (定数の再代入)。ヒープは
  プロセスに 1 つで壊されないので、上流のモデルではそれで問題ない。
- ext TU が外に出す非 static シンボルは 46 個で、うち 40 個はランタイムのフック
  (`sp_sym_to_s`、`sp_class_to_s`、`sp_exc_*`、`sp_proc_call` 等)。
  **2 本の ext TU を 1 バイナリにリンクすると 40 個が multiple definition になる**。
  フォークの `e0fd181` (TU 関数をインスタンス経由に) が解いた問題がそのまま残っている。
- `-ffunction-sections -Wl,--gc-sections` でリンクすれば `crypt` への参照は落ちる
  (無い場合は `-lcrypt` が要る)。pthread は `SP_THREADS` を付けない限り出てこない。
- `spin ext build` は `lib/` の全ソース (65 ファイル、約 2.5MB) を gem の `ext/` に
  平置きでコピーする。fmrb の `import_from_fork.rb` と役割は同じだが、
  sp_net / sp_crypto を除く選別や `IMPORT_INFO` の hash は無い。

## フォークのパッチと上流の対応表

フォークは上流 `8c70d565` (2026-07-18) から 48 コミット。分類は
`tmp/blog/spinel_fork_patches.md` に従う。

| 分類 | フォークのコミット | 上流 master の状況 | 判定 |
|---|---|---|---|
| 1 ライブラリ化 | `9aa7cdd` `--no-main` / `--entry` / `--inject` | `--ext-init` が同じ役 (init = トップレベル)。`--ext-entry` は型付きエントリで上位互換。`--inject` は無いがホスト側 .c で足りる | **置換可** |
| 1 statics 再初期化 | `c7de66c` `sp_reset_tu_statics` | 無し。上流はヒープを壊さないので不要。fmrb の instance 破棄・再生成モデルを続けるなら必要 | 維持 (多重インスタンスとセット) |
| 1 永続 statics | `cafe659` `--persistent-statics` | 無し。ext モデルではエントリ間で状態が自然に残るので不要 | **不要になる** |
| 2 多重インスタンス | `1fea726` 〜 `e0fd181` (`sp_ctx`、alloc backend、VFS、TU 関数のインスタンス経由) | 無し。ランタイム状態はプロセス 1 つのグローバル。ヒープの閾値 (`sp_gc_threshold`) は触れるが pool 差し替え口は無い | **維持必須** |
| 3 例外スタック段数 | `b8b5a02` `SP_EXC_STACK_MAX` / `SP_CATCH_STACK_MAX` を `#ifndef` に | 名前は同じだが `#define ... 64` が無条件。`-D` で渡すと再定義になる | 維持 (小さい差分) |
| 3 GC mark 作業リスト | `94c2f89` `SP_GC_MARK_STACK_MAX` を可変に | 同名の無条件 `#define (1024*64)` が 2 か所 | 維持 (小さい差分) |
| 3 スタック一時領域 | `d0f0232` `SP_STACK_SCRATCH_MAX` | 無し。4KB / 8KB のスタックバッファが 25 か所 | 維持 |
| 3 push 境界検査と高水位 | `7b1feb7` | `sp_exc_check_depth()` はある (try ヘルパでも使用)。高水位の記録は無し | 一部維持 |
| 3 per-TU statics を PSRAM へ | `16333bf` `SP_TU_BSS` | 無し | 維持 |
| 4 32bit 境界 | `1242ad3` bigint の公開署名を `intptr_t` に | 未修正。ヘッダは `sp_int` (= `intptr_t`)、`sp_bigint.c` は `mrb_int` (= `int64_t`) のまま | 維持 (PR 候補) |
| 4 `__int128` 回避 | `d9e363e` | 未修正。`-m32` で `sp_time.c` がコンパイル不能 (実測) | 維持 (PR 候補) |
| 4 FFI 可変長引数の幅 | `53941f6` | 未確認 (要 32bit 実機で再確認) | 要確認 |
| 4 `make test32` | `73a2083` | 無し | 維持 |
| 5 MMU 無し / newlib | `4e33a00` `a03386b` `ac03888` `5221ee9` | `execinfo.h` だけは `__has_include` で自動判定済み。`sys/mman.h` は `spinel_rt.h` で無条件 include、`sys/ioctl.h` (`TIOCGWINSZ`) も無条件。`sys/wait.h` `sys/resource.h` `malloc.h` `fnmatch.h` `sys/file.h` も無条件 | 維持 (PR 候補) |
| 6 Ruby 穴埋め | poly ディスパッチ群、`Array#[]=` 拡張、NUL、`Set` 誤検出、他 | **全部通る**。フォークが足したテスト 6 本 (`poly_array_index` `poly_array_set_extend` `poly_int_to_s_base` `poly_str_byteslice` `poly_str_methods` `set_word_in_comment_and_string`) が上流バイナリで expected と一致 | **落とせる** |
| 6 struct 前提の検査 | `ca0709c` `622750c` | 未確認 (テスト無し) | 要確認 |

補足: フォーク時代のランタイムは `mrb_int` / `mrb_bool` を使い、mruby のヘッダと
衝突するので `fmrb_spinel_host.c` を分離 TU にしていた。上流は `sp_int` 系に改名済みだが
`mruby_shim.h` (bigint 用) は残るので、分離 TU の作りは続ける。

## 形態ごとの当てはめ

### gem 型: 上流 ext に乗せ替える (推奨)

raycast を例にすると、今の境界は FFI 関数 10 本 (map / w / h / gen / px / py / pa /
micros / output / log) で、エントリは引数無し。上流 ext では次の形になる。

```ruby
# lib/add/picoruby-fmrb-raycast/spinel/raycast_kernel.rb
require_relative "raycast_core"
module RaycastKernel
  def self.load_map(map, w, h)   # 型は下の driver ブロックから推論される
    $raycast = RaycastCore.new(map, w, h, 0)
    0
  end
  def self.cast(px, py, pa)
    $raycast.cast_packed(px, py, pa)   # 埋め込み NUL 込みの String がそのまま戻る
  end
end
if __FILE__ == $0
  RaycastKernel.load_map("\x01\x00" * 8, 4, 4)
  p RaycastKernel.cast(100, 100, 0).bytesize
end
```

```c
/* native/raycast_native.c: 生成ヘッダの契約に対してコンパイルする */
#include "raycast_kernel.h"          /* SPINEL_EXT_HOST を定義して spinel_rt.h を引く */
...
Init_raycast();                       /* instance 生成の直後に 1 回 */
sp_RaycastKernel_s_load_map(sp_str_from_bytes(map, w * h), w, h);
const char *buf = sp_RaycastKernel_s_cast(px, py, pa);
int len = (int)sp_str_byte_len(buf);
```

- FFI 宣言ファイル (`raycast_ffi.rb`) と、`:binstr` と `sp_net_bin_len` の流用が消える。
- `--persistent-statics` と「キャッシュのキーはオブジェクト自身にする」規約
  (`doc/spinel_aot/stateful_library_entry.md`) が不要になる。map の世代管理は
  `load_map` を呼ぶかどうかでホストが決める。
- raise は `Init_raycast_try` で受けられる。今は Ruby 側で握って log FFI に流している。
- `spin ext test` の差分ハーネス (純 Ruby と native の答え一致) は CRuby 前提だが、
  raycast が既にやっている「CRuby で総当たり比較」と同じ思想なので流用しやすい。
- 型推論の元になる `if __FILE__ == $0` ブロックは、mruby 側の `:ruby` バックエンドと
  同じ `raycast_core.rb` を読む構成を保てる (core は触らない)。

ただし **gem 型だけでも 1 イメージに 1 本まで** という制約が上流にはあるので、
fft と raycast と hello を同時に載せる今の構成は、多重リンクの解決 (下記) が先。

### VM 型: 上流 ext は代替にならない

- `--ext-init` は `--no-main --entry` の置き換えにはなる (init がトップレベル)。
- しかし kernel と desktop を別インスタンス・別 pool で同居させ、File/Dir を fmrb HAL
  に流し、例外スタックを PSRAM に置く、という土台は全てフォーク側の仕事で、上流に無い。
- 上流のランタイムは今も POSIX ホスト前提のヘッダを無条件に引く。
  ESP-IDF の newlib でどこまで通るかは、このセッションでは ESP ツールチェーンが
  ホストに無く未確認 (docker 内のみ)。フォークの `4e33a00` 系を当て直す前提で見るべき。

## 移行方針の選択肢

| 案 | 内容 | 得るもの | 失うもの / 費用 |
|---|---|---|---|
| A. 現状維持 | フォーク pin `622750c` のまま | 費用ゼロ | 上流の穴埋めと性能改善が入らない。乖離が広がり続ける |
| B. rebase + ext 採用 (推奨) | フォークを最新上流へ rebase。分類 6 を捨て、分類 1 の 3 コミットを捨てて ext 機構に乗り、分類 2〜5 を当て直す。gem 型を ext エントリに書き換える | 乖離が 48 から 30 前後に減る。gem の境界が型付きになる。上流の修正が入る | rebase 作業 (`sp_ctx` 群は 2,101 コミット分の衝突が予想される)。ext TU の多重リンク解決をフォークで再度行う必要 |
| C. 上流に PR して差分を消す | B の後、`SP_NO_MMAN` 等の port knob、`intptr_t` 境界、`__int128` 回避、ext TU のシンボル分離を PR | 長期的にフォーク不要に近づく | 上流の受け入れ次第。`sp_ctx` (多重インスタンス) は設計変更なので受からない可能性が高い |

B の中で「ext TU の多重リンク」をどう解くかが要。候補は 2 つ。

1. フォークの `e0fd181` 方式を ext TU にも適用する (TU 関数をインスタンス経由の関数ポインタに)。`SP_MULTI_CTX` の延長で、上流の ext と組み合わせる。
2. ext 出力に `--ext-prefix` のような「TU 内シンボルの改名」を足す (40 個を `NAME_` で修飾)。多重インスタンスとは独立で、上流に PR しやすい。ただしフォークの多重インスタンスとは別に必要。

fmrb は kernel / desktop / editor を別インスタンスで動かすので 1 が本線、2 は上流向け。

## 上流 PR の見込み (2026-09-06 検討)

上流の受け入れ状況: 直近 60 日でマージ 313 件、外部貢献者 12 人以上。README の
Contributing 節が作法を定める (焦点を絞った小さい PR、`make` の自己ホスト一致、
`make test` / `make bench` 通過、`test/` への回帰テスト、Issue 参照 trailer)。
CI は ubuntu の gcc / clang と macOS の clang のみで、README は「ランタイムは
POSIX 前提」と明言している。よって組み込み向けの変更は「既定では 1 バイトも
変わらず、Linux 上で `-D` を付けて検証できる」形が必須で、動機は
「32bit で壊れる」「ポートが値を決められる」のようにホストでも通る言い方に寄せる。

| 見込み | パッチ | 理由 |
|---|---|---|
| 高い | `__int128` 回避 (sp_time.c)、bigint の `intptr_t` 境界 | 32bit で純粋にバグ。上流に残っていることを実測済み。`-m32` の再現を添えれば Linux CI で検証できる |
| 高い | `SP_EXC_STACK_MAX` / `SP_CATCH_STACK_MAX` / `SP_GC_MARK_STACK_MAX` の `#ifndef` 化 | 上流自身が `SP_GC_STACK_MAX` と `SP_DYN_SYMS_MAX` で同じことをしている。数行 |
| 高い | ヘッダの存在判定 (`sys/mman.h`、`TIOCGWINSZ`、`ucontext.h`) | 上流は `execinfo.h` を `__has_include` で自動判定済み。同じ流儀に揃える |
| 中 | `make test32` | 単体では弱い。上の 2 件の回帰テストとして同梱する |
| 中 | ext TU のシンボル分離 (40 個の重複) | 上流自身の問題でもある (`spin ext` の gem を 2 つ同じ CRuby に load すれば同じ衝突のはず)。まず 2 gem で再現して Issue |
| 中 | `SP_STACK_SCRATCH_MAX`、push 高水位の記録 | 既定不変にはできるが触る箇所が多い。境界検査は `sp_exc_check_depth` が既にあるので高水位だけ小さく |
| 低め | `SP_TU_BSS` | 動機が PSRAM 一点。ヘッダ判定の PR に「配置属性の差し込み口」として相乗り |
| 低い | `SP_MULTI_CTX` 一式 (sp_ctx、pool アロケータ、VFS フック、malloc 付け替え) | 設計変更。上流はこの間に `SP_THREADS` の `__thread` TLS と M:N スケジューラへ進み、グローバル状態のモデルがさらに離れた。ext 設計も 1 プロセス 1 ランタイム前提。コードの前に Issue で設計提案 |

決定 (2026-09-06): 見込み「高い」は rebase 後に順次 PR する。「中」は今後検討。
**`SP_MULTI_CTX` 一式は fmrb として最も欲しい部分**であり、受からない前提でフォークに
残しつつ、Issue で構想を共有して反応を見る。受かる余地を作るなら、次の切り口が候補。

- 上流が `SP_THREADS` で作った「ワーカーごとの状態」(`SP_TLS` の例外スタック等) と
  `sp_ctx` を同じ器に寄せ、「ワーカー = インスタンス」と読める形にする。
- codegen に触らない最小 API (`sp_runtime_create(config)` にアロケータと I/O の
  フックだけ) を先に出し、`sp_ctx` への状態移設は後段に分ける。
- ext 機構と組み合わせて「ext gem を 2 つ同じプロセスに載せる」を動機にする。
  シンボル分離 (上の「中」) と地続きで、上流にも意味がある。

## 最新の上流に追従する (2026-09-26 開始)

ユーザ方針: **fmruby-core が参照する Spinel を最新の上流に揃え、以後も追従
できる形にする**。VM 型と組み込み層はフォーク維持が前提 (結論節) なので、
「最新を参照する」は**上流 HEAD の上にフォークの差分を載せ直した branch を
SPINEL_PIN が指す**ことを意味する。差分は小さく保ち、PR で減らしていく。

- 2026-09-26 時点: 上流 master は `01521b1e` (調査時の `1a507623` から
  1,270 コミット、フォーク基点 `8c70d565` から 3,371 コミット)。フォークの
  差分は 48 コミット。上流への PR / Issue はまだ 0 件。
- 上流の変化が速いので、上の対応表と実測は古くなっている前提で扱う。

**PR 候補の台帳は `pr_candidates.md`**。作業中に見つけた上流の不具合は、どの段階でも report の「PR 候補」節に書き、親が検収時に台帳へ移す。提出はユーザの判断。

段階 (推奨とスコープの 1-4 を次のように置き直す):

| 段階 | 内容 | 状態 |
|---|---|---|
| P0 | 最新上流で前提を取り直す (対応表・ext 仕様・多重リンク・32bit・newlib ヘッダ・試し rebase の衝突量)。コードは変えない | **完了** (report/p0.md) |
| P1 | 上流バイナリで gem 型を試作 (旧 段階 1)。fmrb のランタイムはフォーク版で上流と同居できないため、**sim ではなく fmrb のビルド外の試験用ホスト (tool/spinel_ext_poc/) で CRuby と一致を確かめる** | **完了** (report/p1.md) |
| PR1 | 上流 PR の下準備 第 1 陣 (U-6 U-10 U-11 U-12 P-9 P-3)。専用 clone でブランチと回帰テストまで、提出はユーザ判断 | **完了** (report/pr1.md)。6 件とも 1 コミットのブランチで make test / bench 通過、提出待ち |
| P2 | フォークを最新上流へ rebase し、SPINEL_PIN と spinel_rt スナップショットを更新 (旧 段階 2)。**ここで「最新を参照する」が成立**。**P2a** (フォーク側: 案 A で載せ直し、大域状態の棚卸し、slab 等の口) と **P2b** (fmrb 側: 取り込み・sim・実機) に分ける | **P2a 完了** (report/p2a.md)。P2b は **P2b-1** (取り込み・sim・ESP32 サイズ実測、指示書 instruction_p2b1.md) と **P2b-2** (実機・push・SPINEL_PIN、ユーザ判断の後) に分ける |
| P3 | ext TU の多重リンク解決と gem 3 本の移行 (旧 段階 3) | 未着手 |
| P4 | 追従の仕組み (乖離の表示と定期 rebase の手順) と PR の切り出し (旧 段階 4) | 未着手 |

P0 で確定したこと (上流 `01521b1e`、詳細と再現手順は report/p0.md):

- **上流の固定点は `01521b1e`**。P1 と P2 はこれに揃える。
- ext 機構 (`--ext-init` / `--ext-entry`、`_try`、NUL を含む String) は前回と
  同じで、32bit でも動く。ext の変換結果 2 本の同時リンクで 40 シンボル重複
  (F-2) と、ext ヘッダの include guard が固定名 (F-7) は未解決。
- **32bit は上流が対応済み**: `d9e363e` `1242ad3` は不要、`73a2083` は上流の
  `-m32` の仕組みに寄せて落とす。`53941f6` (FFI 可変長引数) は必要 (P-9)。
- **分類 6 は 12 本を落とし、5 本を残す** (`9474d92` `8a298cb` `286de9b`
  `ca0709c` `622750c`)。残す 5 本は上流で再現した (U-6 / U-10 / U-11)。
  「分類 6 は全部落とせる」は撤回。system_desktop は `8a298cb` 系が無いと
  上流でコンパイルできない。
- 組み込みの新しい論点: **slab アロケータ** (mmap の大領域予約、切るのは
  環境変数だけ、P-10) と、**凍結リテラルが `.data` を食う** (editor で
  24.6KB、ESP32 では内蔵 RAM、P-11)。リテラルを書き換える Ruby は
  FrozenError になる。
- 試し rebase: 不要な 14 本を落とした 34 本のうち 21 本が衝突 (117 か所・
  約 4,000 行、うち約 3,000 行が多重インスタンス群)。ただし**本当の費用は
  移し漏れの棚卸し**で、ランタイムの大域状態は 109→377 シンボルに増えた。
  import_from_fork.rb は `sp_*.c` を glob するので、上流の新しいファイル
  (`sp_slab.c` `sp_iobuffer.c` `sp_process.c` ほか) の除外表の見直しが要る。

P1 で確定したこと (report/p1.md、試作は tool/spinel_ext_poc/):

- raycast と fft は **core を 1 文字も変えずに ext 版にでき**、CRuby と
  バイト単位で一致した (64bit / 32bit、GC を強く回した場合と slab 無しでも。
  raycast は 2,685 回の cast と map 差し替え 5 回、raise 3 件)。
- ホストの書き方: `Init_<name>()` を 1 回、String 引数は
  `sp_str_from_bytes` で作る、**raise しうる呼び出しは全部 `_try` で包む**
  (包まないとプロセスが終わる。包むコストは揺れに埋もれる)。
- **init を再度呼んでもモジュールの状態は戻らない**。fmrb は pool を作り
  直すので、P3 では `c7de66c` (`sp_reset_tu_statics`) を ext の init にも
  効かせる必要がある。
- 撤回: 「ext にすると速くなる」。上流の既定 (`--int-overflow=raise`) の
  nil 検査で、ホストではフォーク比 15-45% 遅い。gem だけ wrap にするかは
  実機で測ってから決める (F-8)。
- 撤回: 「.data の増加は凍結リテラルだけ」。Integer 定数の分もある (P-11)。

P2a で確定したこと (report/p2a.md、clone `/home/kishima/fmrb/wt/spinel-rebase/`
の `fmrb-next` = `01521b1e` + 43 本、未 push):

- 案 A で 33 本を載せ直し (`8a298cb` は上流で空になり落ちた)、新規 10 本
  (slab の口 `SP_NO_SLAB`、mmap・fork/exec の口、上流で増えた状態の
  sp_ctx への移設、大域状態の門 `check-mc-globals`、sp_nosched.c ほか)。
- `make test` 4054 pass / 1 fail (基点と同じ。落ちる 1 本はホストの
  ネットワーク都合)、`make bench` 62/0、`make test-multi-ctx` 全通過、
  `make test32` 3916 / 1。fmrb の VM 型 3 本と gem 3 本の C 生成と、
  `SP_MULTI_CTX` + ポートの口でのコンパイルが 64bit / 32bit で通る。
- 移し漏れは予想どおり本当の費用だった: 載せた直後に大域が 307 個残り、
  2 インスタンス同時の GC で壊れるものを含んでいた。名前マクロ 102 個分を
  sp_ctx へ移し、残る 203 個は理由つきで許可表に分類した。
- 撤回: 「自動併合は安全」。衝突なしで入った `#if` が `--debug` の
  backtrace を黙って空にした (make test で発覚し、履歴を直した)。
- **実機への影響が大きい (ホストの `-m32 -O2` での見込み、実数は P2b)**:
  - 内蔵 RAM の `.data` が kernel + desktop + editor で **約 +47KB**
    (凍結リテラルと Integer 定数)。Tab5 の待機時空き 152KB に対して大きい
  - flash (.text + .rodata) が **約 +0.95MB** (生成 6 本 +580KB、ランタイム
    +377KB)。S3 の flash 残り 6% には入らない可能性が高い
- fmrb 側で要る変更: `sp_net_bin_len` → `*sp_ctx_ffi_bin_len() = n`、
  `sp_instance_config` の新しい欄、sp_nosched.c を入れる。
  ESP newlib での `fopencookie` と `sys/poll.h` は未確認。

## 推奨とスコープ

- 推奨は B。ただし一度に全部やらず、次の順で刻む。
  1. **上流バイナリで gem 型を試作** (Linux sim のみ、フォーク不変)。raycast を ext エントリ化し、`fmrb_spinel_host.c` 相当の分離 TU から呼べることを確認。多重リンクを避けるため、この段階では ext TU を 1 本だけ載せる。
  2. **フォークの rebase**。分類 6 を落とし、分類 2〜5 を当て直す。`make test` / `make bench` / `make test32` / `make test-multi-ctx` をゲートに。
  3. **ext TU の多重リンク解決** (候補 1)。kernel / desktop の VM 型はそのまま `--ext-init` に移し、gem 型 3 本を ext エントリに移す。
  4. PR 候補の切り出し (C)。
- スコープ外: VM 型の境界 (メッセージ poll の FFI) の見直し。ext 機構は C から Ruby を呼ぶ向きで、Ruby から C を呼ぶ FFI はそのまま使う。

## 受け入れ条件

- 段階 1: Linux sim で raycast が ext エントリ経由で `:ruby` バックエンドとピクセル一致。FFI 宣言ファイルと `sp_net_bin_len` の流用が gem から消える。
- 段階 2: rebase 後のフォークで上流 `make test` が全通し、`make test32` の通過数が rebase 前 (1934) を下回らない。
- 段階 3: kernel + desktop + gem 3 本を 1 イメージにリンクでき、Tab5 実機の待機時内蔵 RAM 空きが現状 (152,612 バイト) を下回らない。ExcHW の高水位が読める。
- 各段階で `doc/spinel_upstream_ext/report/pN.md` に結果を残す。

## 未確定事項

- 上流ランタイムが ESP-IDF newlib でどこまでコンパイルできるか (ホストに ESP ツールチェーンが無く未計測)。`sys/wait.h` `fnmatch.h` `malloc.h` の無条件 include は、フォークの `4e33a00` 時点では問題にならなかったのか、上流がその後に足したのか要確認。
- `53941f6` (FFI 可変長引数の幅) と `ca0709c` / `622750c` (struct 前提の検査) が上流で別の形で直っているか。テストが無いので 32bit 実機でしか確かめられない。
- ext エントリ引数の型推論が `if __FILE__ == $0` ブロック頼みなので、PolyArray に落ちた場合の性能。raycast / fft は Integer と String だけなので当面は問題にならない見込み。
- 上流の `SP_GC_MARK_STACK_MAX` が無条件 `#define` になった経緯。fmrb が 8192 に絞った理由 (512KB pool で 256KB の連続確保に失敗) は上流にも当てはまるので、`#ifndef` 化は PR に値する。
- gem を `spin ext` の gem レイアウト (spin.toml の `[ext]`、`lib/<name>/kernel.rb`) に寄せるか、fmrb の `lib/add/picoruby-fmrb-*` の作法のままにするか。fmrb は mrbgem なので後者のまま、生成だけ `spinel --ext-init` を直接叩くのが自然。

## 検証の記録 (再現手順)

上流の作業ツリー `~/dev/spinel` (master `1a507623`) で行った。フォークには触っていない。

```sh
# 上流のビルド
make deps && make -j8

# フォークが足したテストを上流で
git fetch <fmruby-core>/vendor/spinel fmrb-dev
for t in poly_array_index poly_array_set_extend poly_int_to_s_base \
         poly_str_byteslice poly_str_methods set_word_in_comment_and_string; do
  git show FETCH_HEAD:test/$t.rb > /tmp/$t.rb
  ./bin/spinel /tmp/$t.rb -o /tmp/$t && /tmp/$t | diff - <(git show FETCH_HEAD:test/$t.rb.expected)
done                                     # 6 本とも差分なし

# ext 出力とホストからの呼び出し
./bin/spinel k.rb -c --no-line-map --ext-init Init_k \
  --ext-entry K.greet,K.sum_f,K.scale,K.must_pos -o k.c   # k.h も出る
cc -O1 -w -Ilib -I. host.c k.c lib/libspinel_rt.a -lm -lcrypt -o host && ./host
#   toplevel ran / Hello w! / 2.985012 / 3 6 / raised ArgumentError: n must be positive
#   -- second init -- / toplevel ran / Hello again!

# 2 本の ext TU を同時リンク
cc -w host.o k.o k2.o lib/libspinel_rt.a -lm 2>&1 | grep -c 'multiple definition'   # 40

# 32bit
cc -m32 -O1 -w -Ilib -c lib/sp_time.c   # error: expected expression before '__int128'
cc -m32 -O1 -w -Ilib -c k.c             # ok
```

関連文書: `doc/spinel_aot/00_common.md` (フォーク方針の原点)、
`doc/spinel_aot/reports/fork_pr_candidates.md` (PR 候補の台帳)、
`doc/spinel_aot/stateful_library_entry.md` (persistent statics の経緯)、
`doc/raycast_spinel/plan.md` (gem 型の実例)。
