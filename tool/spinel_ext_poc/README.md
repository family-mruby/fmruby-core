# spinel_ext_poc: 上流 Spinel の ext 機構で gem 型を試す

doc/spinel_upstream_ext の P1 (報告は `doc/spinel_upstream_ext/report/p1.md`)
の試作一式。raycast と fft の gem を上流 Spinel の `--ext-init` /
`--ext-entry` で型付きエントリにし、C の試験用ホストから呼んで、同じ Ruby を
CRuby で動かした結果とバイト単位で突き合わせる。**fmrb のビルドには配線して
いない** (Rakefile・CMake・components・main・lib から参照されない)。

## ファイル

| ファイル | 役割 |
|---|---|
| `raycast_kernel.rb` | raycast の ext 版エントリ (`RaycastKernel.load_map` / `cast`)。コアは gem の `mrblib/raycast_core.rb` を相対パスで読む |
| `raycast_host.c` | 試験用ホスト。`Init_raycast()` の後、ケースファイルの順にエントリを呼び、光線の出力バイト列をファイルへ書く |
| `raycast_run.rb` | raycast の実行スクリプト。ケース生成、CRuby 参照、64bit / 32bit のビルドと照合、大きさ、時間 |
| `fork_host.c` | 比較用。フォークの今の gem (`spinel/raycast_entry.rb`、FFI 10 本) を同じ条件で時間計測するホスト |
| `fft_kernel.rb` / `fft_host.c` / `fft_run.rb` | fft の同じ一式 (T4)。`level_db` は Float の越境を見るためのエントリ |
| `common.rb` | 実行スクリプトの共通部分 (フラグ取得、32bit ランタイムの構築、`size -A`) |

## 前提

- 上流 Spinel の checkout がビルド済みであること (`make deps && make`)。
  既定の場所は `~/dev/spinel`、P1 で固定した commit は `01521b1e`
  (違う commit だと警告を出して続行する)。checkout は読むだけで書き換えない。
- `cc` (gcc) と `-m32` 用の multilib (`gcc-multilib`)、`ruby` (3.x)、`git`、
  `make`、`size`。`taskset` があれば時間計測を 1 CPU に固定する。
- フォークとの比較に `fmruby-core/vendor/spinel` のビルド済み `bin/spinel` と
  `lib/libspinel_rt.a` を読む。無ければ `--no-fork` で省ける。

## 実行

生成物・バイナリはすべて `--out` の下に出る (既定は `$TMPDIR/spinel_ext_poc`)。
このディレクトリには何も書かない。

```sh
cd fmruby-core/tool/spinel_ext_poc
ruby raycast_run.rb --out /tmp/spinel_p1      # T1-T3
ruby fft_run.rb     --out /tmp/spinel_p1      # T4
```

どちらも最後に `RESULT:` 行を出し、全部一致なら終了コード 0。

- 初回は 32bit ランタイムを作る: `--out` の下に上流を `git clone` し
  (`up32/`)、同じ commit で `make CC='cc -m32' lib/libspinel_rt.a` だけを
  実行する (i386 の libcrypt が無くても通る)。2 回目以降は再利用する。
  既にあるなら `--rt32 <path>/libspinel_rt.a` で渡せる。
- フォークの 32bit ランタイムも同様に `fork32/` へ clone して作る。
- 主なオプション: `--spinel DIR` (上流の場所)、`--no-m32`、`--no-fork`、
  `--seed N` / `--random N` (乱数の姿勢・変換の数)、`--cpu N` (計測の CPU)。

## raycast_run.rb がすること

1. ケースを作る (`<out>/raycast/cases.txt`): 地図の読み込み前の `cast`
   (raise)、ゲームの地図 (`flash/app/game/raycaster.app.rb` の `WORLD_MAP`
   をその場で読む) で固定姿勢と乱数姿勢、手書きの 7x5 の地図への差し替え、
   不正な地図 2 件 (raise、前のコアが残ること)、ゲームの地図へ戻す。
2. CRuby で `raycast_kernel.rb` を直接動かし、参照の出力 (`ref.bin`) と
   イベント列 (`ref.events`) を作る。
3. x86_64 と i386 で ext の C を生成・コンパイル・リンクし、同じケースを
   3 通りの環境変数で回して照合する: そのまま / `SPINEL_GC_STRESS=1`
   (2KB ごとに回収) / `SPINEL_GC_SLAB=0` (slab アロケータ無し)。
4. `size -A` の表 (ext と、フォークで生成した `raycast_entry.c`)。
5. 時間の目安 (2 姿勢 x 1,000 回、20 巡の最小値。ホストでの相対比較で、
   実機の値ではない)。

## ホストの書き方 (P3 への見本)

- `Init_raycast()` を最初に 1 回。トップレベル (定数の設定) はここで走る。
- Ruby の String 引数は `sp_str_from_bytes(buf, len)` で作ったランタイムの
  文字列を渡す。素の C バッファを渡すと、長さを前置ヘッダから読むので壊れる。
  NUL を含むバイト列もそのまま渡せる。
- 戻りの String は `sp_str_byte_len()` で長さを取る。中身はコアが持つ
  バッファで、次の `cast` で上書きされる。
- raise しうる呼び出しは `Init_raycast_try(fn, ctx, &cls, &msg)` で包む。
  包まずに raise が出るとプロセスが終了する (終了コード 1)。
