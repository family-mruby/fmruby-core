# 実装指示書 P2a: フォークを最新上流へ載せ直す (フォーク側)

対象: 実装担当のサブエージェント。前提: plan.md、report/p0.md (特に
「4. 試し rebase」と「P2 (rebase)」)、report/p1.md。report は `report/p2a.md` へ。

P2 は 2 つに分ける。**P2a (この指示書) はフォークのリポジトリの中だけで
完結する**。fmrb 側 (spinel_rt スナップショットの取り込み、SPINEL_PIN の
更新、sim と実機での確認) は P2b として、P2a の検収後に別の指示書で出す。

## 決定事項 (親、2026-09-26)

- 基点は上流 `01521b1e`。新しいブランチ名は `fmrb-next`。
- **案 A で載せる** (report/p0.md の表)。落とすのは分類 6 のうち上流で不要な
  12 本と、`d9e363e` `1242ad3` の計 14 本。
  - `9aa7cdd` (`--no-main` / `--entry` / `--inject`) と `cafe659`
    (`--persistent-statics`) は**残す**。fmrb の生成手順を P2 で変えないため。
    VM 型と gem 型の ext への移行は P3 で行い、そのときに落とす。
  - `73a2083` (`make test32`) も残す。上流の `make test-corpus CC='cc -m32'`
    は、このホストでは i386 の libcrypt が無く回らないため。
  - 分類 6 の維持 5 本 (`9474d92` `8a298cb` `286de9b` `ca0709c` `622750c`) は残す
    (PR1 で上流 PR を準備中。取り込まれたら次の rebase で落ちる)。
- **衝突を解くより、移し漏れを探す作業だと思って進める**。上流の
  ランタイムの大域状態は 109 → 377 シンボルに増えた。`SP_MULTI_CTX` の
  ビルドでは、上流で増えた大域状態も `sp_ctx` (か TU ごとの static) に
  移すか、ポートでは使わない機能として外す。
- **slab アロケータ** (`sp_slab.c`): コンパイル時に切る口 (例 `SP_NO_SLAB`)
  を足す。既定 (何も `-D` しない) の動作と出力は変えない。`SP_MULTI_CTX` の
  ビルドでは slab を切り、確保はインスタンスのアロケータに流す。
- **mmap・fork/exec を使う新しいファイル** (`sp_iobuffer.c` の mmap、
  `sp_process.c` ほか): `SP_NO_MMAN` 等の既存の口に揃えて、MMU の無い
  ポートでコンパイルできるようにする。機能ごと外すなら、呼ぶと例外になる
  形にする (黙って何もしない形にしない)。
- 凍結リテラルの `.data` 増加 (P-11) は、この段階では**直さず記録する**。
  `SP_TU_BSS` と同じ口をリテラルの静的オブジェクトにも付けられるかの
  見立てだけ report に書く。

## 作業場所と触ってよい範囲

- 専用の clone `/home/kishima/fmrb/wt/spinel-rebase/` を作る
  (`git clone https://github.com/matz/spinel.git`、フォークは
  `fmruby-core/vendor/spinel` から `git fetch` で `fmrb-dev` を取る)。
  `~/dev/spinel`、`vendor/spinel`、`/home/kishima/fmrb/wt/spinel-pr/`
  (PR1 が作業中) は**触らない** (読むのはよい)。
- clone の中ではコミットしてよい。**push はしない**
  (`kishima/spinel` への push はユーザの指示を待つ)。
- fmruby-core / graphics-audio の作業ツリー、build/、sim、実機は触らない。
- コミットは元のコミットの分割を保つ (1 本ずつ載せ、件名は元のものを基本に。
  載せ直しで意味が変わったコミットは本文にその旨を書く)。新しく足す変更
  (slab の口など) は別コミットにする。コメントは英語。

## 進め方

1. 案 A の 34 本を順に載せる。衝突の解き方は、上流の今の構造に合わせる
   (フォークの古い形を押し戻さない)。
2. 多重インスタンス群を載せ終えた時点で、`test/multi_ctx/check_syms.sh`
   (nm の門) を動かし、移し漏れを数える。上流で増えた大域状態を表に
   する: シンボル、ファイル、`sp_ctx` へ移した / TU ごと / ポートでは外す /
   読み取り専用で共有してよい、の判定。**この表が P2a の中心の成果物**。
3. 門を通す: 移し漏れを 0 にするか、残すものに理由を付ける。
4. slab の口と、mmap・fork/exec を使う新しいファイルの口を足す。
5. 各段で次を回し、基点 (`01521b1e` の素の上流) と比べる。
   - `make` (自己ホスト一致)
   - `make test`、`make bench` (基点と同じ件数が通ること。基点でも落ちる
     ものは区別して書く)
   - `make test-multi-ctx` (smoke・estalloc・link2 を含む)
   - `make test32` (旧フォークの 1934 を下回らないこと。上流でテストが
     増減しているので、件数の比較は内訳つきで)
   - `-DSP_NO_MMAN` 等のポート向けの口を全部付けた構成でランタイムが
     コンパイルできること (ホストの gcc で。newlib の実機ビルドは P2b)
6. fmrb の生成手順がそのまま通るかを確かめる: fmruby-core の
   `main/prebuild_scripts/` の kernel / system_desktop / editor の Ruby と、
   gem 3 本 (fft / raycast / spinel_hello) の entry を、新しい `bin/spinel` に
   **今の rakelib/spinel.rake と同じオプション**で通し (出力は scratchpad)、
   C 生成と、`SP_MULTI_CTX` 付きのホスト gcc でのコンパイルまで通るか。
   combined Ruby の作り方は rakelib/spinel.rake を読んで同じことを
   scratchpad でする (fmruby-core の作業ツリーには書かない)。
   `rake spinel:doctor` 相当の警告の増減も記録する (U-13 の件を含む)。

## 止まる条件

- 移し漏れの棚卸しで、上流の設計 (`SP_THREADS` の TLS やスケジューラの
  状態) が `sp_ctx` の考え方と両立しないと分かったとき。両立させる案を
  1-2 個書いて止める (親とユーザで決める)
- 基点の上流で `make test` が大きく壊れていて、回帰の判定ができないとき
- sudo やパッケージの導入が要るとき
- fmrb 側の変更 (Ruby の書き換え、fmrb_spinel_host.c など) が無いと
  手順 6 が通らないと分かったとき。必要な変更を列挙して止める
  (fmrb 側は P2b で行う)

## report/p2a.md に書くこと

- 載せたコミットの対応表 (元 commit → 新 commit、衝突の有無と解き方の要約)
- 大域状態の棚卸し表 (手順 2)
- 各ゲートの結果 (基点との比較、内訳つき)
- 新しく足した口 (slab、mmap、fork/exec) と、既定の出力が変わらないことの確かめ方
- 手順 6 の結果 (ファイルごとに 通過 / 失敗と原因)
- P2b (fmrb 側の取り込み) の指示書に入れるべきこと。とくに
  import_from_fork.rb の除外表に足すファイル、`.data` / `.bss` の増減の
  見込み (内蔵 RAM の予算に効くもの)
- 見立てと違った点・撤回した仮説
- **PR 候補**節 (内容・最小再現・確認した上流 commit。台帳は編集しない)

## 受け入れ条件

- `fmrb-next` が `01521b1e` の上に載り、`make test` / `make bench` が基点と
  同じだけ通る (差は説明つき)
- `make test-multi-ctx` が通り、nm の門の移し漏れが 0 (または理由つき)
- `make test32` の結果が内訳つきで記録されている
- 手順 6 で、fmrb の 3 本の VM 型と gem 3 本の C 生成が通る (通らないものは
  原因と、P2b で要る fmrb 側の変更が書いてある)
