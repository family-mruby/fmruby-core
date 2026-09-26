# 実装指示書 P2b-1: fmrb 側へ取り込み、sim で動かし、実機のサイズを測る

対象: 実装担当のサブエージェント。前提: plan.md、report/p2a.md (特に
「6. P2b の指示書に入れるべきこと」)、report/p1.md。report は `report/p2b1.md` へ。

P2b は 2 つに分ける。**P2b-1 (この指示書) は、手元の `fmrb-next` を参照して
fmrb に取り込み、sim で動作を確かめ、ESP32 のビルドサイズを実測するところ
まで**。実機への書き込み、`fmrb-next` の push、SPINEL_PIN の確定は P2b-2
(ユーザの判断の後)。

## 決定事項 (親、2026-09-26)

- 参照先は clone `/home/kishima/fmrb/wt/spinel-rebase/` の `fmrb-next`
  (`b259d0a3`、未 push)。`SPINEL_DIR=/home/kishima/fmrb/wt/spinel-rebase` で
  rake に渡す (Rakefile の最優先の経路)。`vendor/spinel` は触らない。
- `components/fmrb_spinel_rt/spinel_rt` のスナップショットは
  `ruby components/fmrb_spinel_rt/import_from_fork.rb /home/kishima/fmrb/wt/spinel-rebase`
  で取り直す。SPINEL_PIN は push 前なので**まだ変えない** (`rake spinel:gen` が
  出す食い違いの警告は想定内として report に書く)。
- **標準構成** (`.env` の今の値: kernel=spinel、editor=spinel、desktop=mruby) で検証する。
- `-ffp-contract=off` は**付けない** (今の fmrb の CMake のまま)。影響の見立てだけ書く。
- 凍結リテラルの `.data` (P-11) は**直さない**。実機ビルドの実数を測るのが目的。

## 作業場所と git

- fmruby-core の本体 checkout で作業する。**作業ブランチ `feature/spinel-upstream`
  を develop から切り**、そこにコミットしてよい (件名の規約は fmruby-core/CLAUDE.md。
  Co-Authored-By を付ける)。develop へのマージと push はしない。
- `.env` はユーザの設定 (`FMRB_HW_TARGET=TAB5` ほか) が**コミットされずに
  変更されている**。ブランチを切るときもこの変更を保つ (stash して捨てない)。
  `.env` をコミットに含めない。
- サイズ計測のために `FMRB_HW_TARGET` を一時的に切り替えてよい
  (S3 = `NARYAv3`、P4 = `TAB5`)。**最後に必ず今の値 (TAB5 と、TAB5 用の
  VIDPID の行) に戻し**、`git diff .env` が作業開始前と同じになったことを
  report に書く。
- fmruby-graphics-audio は、sim のための Linux ビルドだけしてよい (コードは変えない)。
- `fmrb-next` の clone でコミットしてよいのは、ESP newlib でのコンパイルに
  要る修正だけ (fopencookie、`sys/poll.h` など)。push はしない。
- 実機への書き込み (flash) はしない。シリアルも開かない。
- `/home/kishima/fmrb/wt/spinel-pr/` (PR2 が作業中) と `~/dev/spinel` は触らない。

## T1: 基準を先に測る (同じコミットで比べるため)

変更前の develop HEAD で、S3 (`NARYAv3`) と P4 (`TAB5`) の ESP32 ビルドをして
`idf.py size` と `idf.py size-components` を残す (`rake clean_all` → `rake build:esp32`)。
既存の build/ は別の時点のものなので使わない。

## T2: fmrb 側の変更

report/p2a.md 6 節のとおり。

1. スナップショットの取り直し (上記)。除外表の見直し。`sp_nosched.c` を入れる
2. `:binstr` の長さ: `sp_net_bin_len = n` と書いている所を全部
   `*sp_ctx_ffi_bin_len() = n` に。`sp_net_bin_len` の定義を消す
   (kernel、desktop、editor、gem の native すべて。grep で漏れなく)
3. `sp_instance_config` の新しい欄 (`remembered_entries` / `pinned_entries`) を
   fmrb_spinel_host.c で扱う。値は既定 (0 = 1024 / 256) で始め、プールの
   大きさとの関係を report に書く
4. その他、ビルドを通すのに要る変更。fmrb の Ruby を書き換える必要が出たら
   (凍結リテラルの FrozenError など)、変更箇所と理由を report に列挙する

## T3: sim で動かす

`rake clean_all` → `rake build:linux` (fmruby-core と graphics-audio の両方)。
`file build/fmruby-core.elf` で x86-64 を確認。MCP の sim_* ツールで:

- 起動してデスクトップが出る
- **エディタを起動して 1 打鍵する** (fmruby-core/CLAUDE.md の標準手順。
  ivar の食い違いで エディタだけ死ぬ壊れ方が実在した)
- **エディタを閉じて開き直し、もう一度打鍵する** (同じプログラムの再起動で
  前回の static が残らないか。report/p2a.md の「見るべきこと」)
- ランチャー、設定ダイアログ、raycaster (gem)、アプリの起動と kill を数回
- `docker logs` で FrozenError・`begin frames too deep`・abort が無いこと
- 周期ダンプの ExcHW (例外スタックの高水位) を記録
- 最後に sim_down

## T4: ESP32 のサイズを実測する

T2 の変更の上で、S3 と P4 をそれぞれ `rake clean_all` → `rake build:esp32`。

- ESP newlib でコンパイル・リンクが通るか。通らなければ fmrb-next 側を直す
  (fopencookie、`sys/poll.h` ほか)。直せないものは原因を書いて止まる
- `idf.py size` と `idf.py size-components` を T1 の基準と比べる
  (bin の差分でなく size-components で。memory の規約)
  - 内蔵 RAM: `.data` と `.bss` の増減 (DRAM / IRAM)。Tab5 の待機時空き
    152,612 バイトに対する見込み
  - flash: アプリのパーティションに対する使用率。**S3 に入るか**
- 生成プログラムごとの内訳 (kernel / editor / gem、ランタイム) を
  size-components か map ファイルから

## 止まる条件

- S3 のアプリがパーティションに入らないと分かったとき: S3 の構成は変えずに
  (mruby カーネルに戻す等はしない)、差の内訳を書いて、P4 の計測は続ける
- ESP newlib で fmrb-next 側を直せないとき
- sim で原因の分からない落ち方をしたとき: 状態を捨てる前に gdb で全スレッドの
  backtrace を取る (ルートの CLAUDE.md の手順) → report に書いて止まる
- sudo やパッケージの導入が要るとき

## report/p2b1.md に書くこと

- fmrb 側の変更の一覧 (ファイル、理由)、作業ブランチのコミット
- T3 の結果 (各操作の成否、スクリーンショットのパス、ログの要点、ExcHW)
- T4 の表: S3 / P4 × 基準 / 新 の内蔵 RAM と flash、生成プログラムごとの内訳
- ESP newlib で直したこと (fmrb-next のコミット)
- P2b-2 (実機・push・SPINEL_PIN) の判断に要る材料。とくに S3 をどうするか、
  Tab5 の内蔵 RAM の余裕
- 見立てと違った点・撤回した仮説
- **PR 候補**節 (台帳は編集しない)
- `.env` を元に戻したことの確認

## 受け入れ条件

- 標準構成の sim で、エディタの起動・打鍵・再起動・打鍵を含む操作が通る
- S3 と P4 の ESP32 ビルドのサイズが、同じ手順の基準と並べて表になっている
  (S3 がビルドできない・入らない場合は、その事実と内訳)
- `.env` が作業開始前と同じ
