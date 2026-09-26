# 実装指示書 P2c: 内蔵 RAM の増分をゼロにする

対象: 実装担当のサブエージェント。前提: plan.md、report/p2b1.md (特に 4 節の
内蔵 RAM の表)、report/p2a.md (P-11 の見立て)。report は `report/p2c.md` へ。

## 目標 (ユーザ決定、2026-09-26)

**最新上流への載せ替えで増える内蔵 RAM は 0 バイトにする**。+54KB は許容しない。
P2b-1 の実測 (同じコミットの基準との差): DIRAM が S3 +54,048 / P4 +53,844
(.data 約 +30K、.bss 約 +24K)。これを両機種で 0 (以下) にする。
原理: 増えたものはどれも内蔵 RAM に置く必然性が無い。書き込まれない
ものは flash へ、書き込まれるが速さの要らないものは PSRAM へ、使わない
ものは外す。

パーティションは自由に変えてよい (ユーザ決定)。

## 作業場所と git

- fmruby-core の本体 checkout、作業ブランチ `feature/spinel-upstream` (P2b-1 の続き)。
  コミットしてよい。develop へのマージと push はしない。
- フォークの変更は clone `/home/kishima/fmrb/wt/spinel-rebase/` の `fmrb-next`
  にコミットする (push はしない)。取り込みは P2b-1 と同じ
  (`SPINEL_DIR`、`import_from_fork.rb`)。
- `.env` はユーザの未コミットの変更 (TAB5 向け) を保ち、コミットに含めない。
  ターゲットは P2b-1 と同じくコマンドラインの環境変数で渡し、`.env` は書き換えない。
  最後に `git diff .env` が
  `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/env_before_p2b1.diff`
  と一致することを確かめる。
- 実機への書き込みとシリアルは禁止 (実機の確認は P2b-2)。
- `/home/kishima/fmrb/wt/spinel-pr/` と `~/dev/spinel` は触らない。
- sdkconfig / sdkconfig.defaults は編集禁止 (fmruby-core/CLAUDE.md)。必要なら提案として report に書く。
  パーティション表 (`config/partitions_*.csv`) は編集してよい。

## T1: 1 バイト単位の棚卸し

基準 (develop、P2b-1 の T1 と同じ) と今のブランチの ESP32 ビルドの map ファイル
(と `idf.py size-components`、`size-files`) を比べ、**内蔵 RAM に載るシンボルの
増減を全部表にする** (シンボル、所属 (生成プログラム名 / ランタイムのファイル /
その他)、区分 .data / .bss / IRAM、バイト数)。**合計が P2b-1 の差
(S3 +54,048 / P4 +53,844) と一致するまで**。S3 と P4 の両方。
IRAM (命令) の増減もあれば含める。

## T2: 凍結リテラルを flash に置く (約 30KB)

- 凍結リテラルの静的オブジェクト (`_fzl_N`) を `const` にし、.rodata (flash) に
  置けるようにする。今は `sp_str_hash_miss` が実行時にヘッダへハッシュ値を
  書くので `const` にできない (report/p2a.md「P-11 の見立て」)。**変換時に
  ハッシュ値を計算してヘッダに入れておく** (ハッシュ関数と BINARY の扱いを
  コンパイラ側で正確に再現する。ずれると Hash のキーが一致しなくなる)。
- 実行時にリテラルのヘッダへ書く箇所が他に無いかを洗い出す (GC の印、
  凍結の旗、長さのキャッシュなど)。GC がリテラルを印付けしようとしないことも。
- **検出器**: ホストで、リテラルを読み取り専用の領域に置いた構成で
  `make test` / `make bench` / `make test-multi-ctx` を回す。書き込みが残っていれば
  SIGSEGV で見つかる。
- 既定 (上流の動作) を変えるかどうか: 上流にも意味のある変更 (リテラルが
  .rodata に行く) なので、既定で有効にして `make test` の全通過を確かめるのが
  本筋。駄目なら `-D` の口にする。report に書く (PR 候補 P-11)。

## T3: 残りの .data / .bss

- Integer 定数の `SP_INT_NIL` 初期化 (.data): 静的な初期値をやめ、TU の init で
  代入して .bss にする。`SP_TU_BSS` で PSRAM に置ける形に。
- `sp_hdr_char_cache` (16KB): PSRAM (`EXT_RAM_BSS_ATTR` 相当の口) か、
  インスタンスのプールから確保する形に。多重インスタンスでの共有の可否も
  確かめる (共有してよい読み取り専用の表か、インスタンスごとか)。
- `sp_bt_buf` (生成プログラムごと 1KB): `SP_TU_BSS` を付ける。
- `sp_slab_wk` (2KB): `SP_NO_SLAB` では外す。
- T1 の表に出たその他の増分も全部、flash / PSRAM / 外す / 本当に内蔵 RAM が
  要る、のどれかに分類して処置する。**「本当に内蔵 RAM が要る」ものが残るなら
  理由を書く** (DMA、ISR、速度の実測など)。
- ランタイムの口はフォークに足し (既定の動作は変えない)、fmrb 側は
  `fmrb_sp_tu_bss.h` と同じやり方で値を与える。

## T4: 速度の確認

PSRAM に移したもののうち、よく通る経路 (1 文字の文字列の表など) について、
sim の打鍵遅延 (`edit_lat` / `spx: hid_lat`) と raycaster の `cast` 時間を
移す前と後で比べる。sim では PSRAM の遅さは出ないので、実機で見るべき
項目として report に列挙する (実機の計測は P2b-2)。

## T5: パーティション

- Tab5 / NARYAv4 (`config/partitions_p4.csv`) のアプリ区画を 6M → 7M に広げる
  (storage は 8M のまま。16MB に収まることを確かめる)。S3 (`partitions_n16r8.csv`)
  は変えない (空き 28%)。
- storage の開始位置がずれることの影響 (既存の端末の更新手順、インストーラ、
  `rake flash` / app_only の扱い) を調べて report に書く。

## T6: 仕上げの計測

S3 と P4 を `rake clean_all` → `rake build:esp32` し、T1 と同じ基準と比べる。

- **静的な DIRAM の増分が両機種で 0 以下**であること (表で)
- アプリが区画に入ること (P4 は 7M の区画で)
- sim (標準構成) で P2b-1 と同じ操作を通す: 起動、エディタの起動・打鍵・
  閉じる・再起動・打鍵、raycaster / spinel_hello / fft_bench、設定ダイアログ。
  FrozenError・abort 0

## 止まる条件

- ハッシュ値を変換時に計算する方式で、実行時の値と一致させられない
  (Hash の動作が変わる) と分かったとき
- 「本当に内蔵 RAM が要る」増分が残り、どう削っても 0 にできないと分かったとき
  (内訳と理由を書いて止まる。親とユーザで決める)
- sdkconfig の変更が要るとき (提案を書いて止まる)
- sim で原因の分からない落ち方をしたとき (再起動の前に gdb で全スレッドの backtrace)

## report/p2c.md に書くこと

- T1 の棚卸し表 (処置前) と、処置後の同じ表
- 各処置 (フォークのコミット、fmrb 側のコミット、既定の動作が変わらないことの確かめ方)
- T2 の検出器の結果
- T4 の比較と、実機で見るべき項目
- T5 の影響の調査
- T6 の表と sim の結果
- P2b-2 (実機) の指示書に入れるべきこと (待機時の内蔵 RAM 空きの比べ方など)
- 見立てと違った点・撤回した仮説
- **PR 候補**節 (台帳は編集しない)
- `.env` が作業前と同じことの確認

## 受け入れ条件

- S3 と P4 の静的な DIRAM の増分が、同じ手順の基準に対して 0 以下
- P4 のアプリが区画に入る
- 標準構成の sim で P2b-1 と同じ操作が通る
- `make test` / `make bench` / `make test-multi-ctx` が fmrb-next で退行なし
- `.env` が作業前と同じ
