# 実装指示書 P1: picoruby-ti の取り込みと配線

対象: 実装担当セッション。前提: plan.md を読むこと。fork の継承対応
(kishima/picoruby-ti, branch fmrb-inheritance, commit deb9dae) は完了済み。
T0 (上流 PR) はスキップ決定 (2026-08-11)。fork 追従で進める。

report は doc/editor_ti/report/p1.md へ。タスクごとにコミット。

## P1 のゴール

型推論エンジンが firmware に**リンクされ、ホスト回帰が rake で回る**状態を
作る。エディタ UI には触らない (P2)。

- rake ti:setup だけで素の clone からエンジン取得までが揃う
- rake build:linux / build:esp32 (S3) が通り、ti のシンボルが入っている
- rake ti:test で picoruby-ti のホストテストが FMRB の sig で回る
- FMRB API の RBS 最小セット (sig/) が入る

## 方針 (決定済み)

- **submodule にしない**。Spinel 方式 (PIN 固定 clone) に揃える。
- FMRB 都合の patch は fork の統合ブランチ **fmrb-dev** に載せる。
- sig/*.rbs は fmruby-core 側で管理し、上流 host_test の期待と衝突しない
  よう**既存クラスの定義は追加のみ** (シグネチャ文字列の変更禁止、後述)。

## T1: fork に統合ブランチ fmrb-dev を作る

作業場所: family-mruby/tmp/picoruby-ti (現在 fmrb-inheritance が checkout
されている。remote kishima = git@github.com:kishima/picoruby-ti.git 追加済み)。

1. fmrb-inheritance から **fmrb-dev** を分岐。
2. mrbgem.rake の FMRB 適合 patch を 1 コミット:
   - `spec.add_dependency 'mruby-compiler2'` -> `'mruby-compiler'`
   - include path の `mrbgems/mruby-compiler2/lib/prism/include` ->
     `mrbgems/mruby-compiler/lib/prism/include`
   - それ以外は変更しない (上流に返さない差分を最小に保つ)。
3. `git push kishima fmrb-dev`。コミット hash を控えて T2 の PIN に使う。

役割分担: fmrb-inheritance = 上流 PR 候補 (機能のみ)、fmrb-dev = FMRB 統合用
(PR 候補 + 適合 patch)。今後の上流追従は fmrb-dev への rebase/merge で行う。

## T2: rake ti:setup (vendor + PIN)

Rakefile の Spinel 節 (SPINEL_PIN_FILE / spinel_pin / spinel:setup) を手本に:

- PIN ファイル: `lib/add/PICORUBY_TI_PIN`。書式は SPINEL_PIN と同じ
  (`repo:` と `commit:` の行。parser は spinel_pin を流用して共通化してよい)。
  repo は https://github.com/kishima/picoruby-ti.git (https にする。
  ti:setup だけで clone できることが目的なので ssh 鍵前提にしない)。
  commit は T1 の fmrb-dev HEAD。
- 取得先: `vendor/picoruby-ti` (vendor/ は spinel と同じ扱い。gitignore を
  確認し、漏れていたら追記)。
- 解決順も Spinel に合わせる: `PICORUBY_TI_DIR` env 上書き ->
  vendor/picoruby-ti -> ../tmp/picoruby-ti (開発中の作業ツリー)。
- **submodule (lib/prism) は init しない**。prism はうちの mruby-compiler
  同梱のものを使う。clone は `--recursive` を付けないこと。

## T3: ビルド配線 (gembox / Rakefile cp / CMakeLists / db 生成)

editor-core の配線 (Rakefile の cp 節、family_mruby.gembox、
components/picoruby-esp32/CMakeLists.txt の editor-core 該当行) を grep して
同じ形で足す:

1. Rakefile の gem コピー節に追加。コピー元だけ lib/add ではなく
   `#{PICORUBY_TI_DIR}` (T2 の解決結果):
   ```
   rm -rf <mrbgem_path>/picoruby-ti
   cp -rf <PICORUBY_TI_DIR> <mrbgem_path>/picoruby-ti
   rm -rf <mrbgem_path>/picoruby-ti/{.git,lib,lsp,host_test,images,example,tidbgen}
   ```
   (lib/prism・Go の lsp・テスト類はビルドに不要。コピー後に削るのが簡単)
2. **型 db 生成 (ti:gen)**: コピー直後に host の ruby で
   `vendor/picoruby-ti/tidbgen/main.rb --sig-dir sig --out
   <mrbgem_path>/picoruby-ti/src/generated` を実行する。
   生成先はコピー先のみ。vendor/ と sig/ は汚さない。
   esp32 ビルドは docker 内で走るが、この生成は spinel:gen と同じく
   **docker の外 (host) で先に**行う。
3. lib/add/family_mruby.gembox に `conf.gem core: "picoruby-ti"` を追加。
   位置は editor-core の近く (framework の後) で良い。依存の
   mruby-compiler は gembox 先頭で読まれている。
4. components/picoruby-esp32/CMakeLists.txt: editor-core の例に倣い、
   picoruby-ti の src/**/*.c (src/generated 含む) と include path を
   linux/esp32 両方の節に追加。prism の include
   (mruby-compiler/lib/prism/include) は既に入っているのでそのまま使う。

注意:

- lib/ を触ったら `rake clean`。linux と esp32 の切替は `rake clean_all`。
- .env の FMRB_HW_TARGET がターゲットを黙って上書きする。esp32 ビルドの
  前に .env を確認する。
- ビルド通過の確認は `file build/fmruby-core.elf` で x86-64 を見る
  (stale な esp32 build/ が残っていると偽グリーンになる)。

## T4: FMRB API の RBS 最小セット (sig/)

`sig/` を fmruby-core 直下に新設し、次を置く:

1. 基本型: vendor の example/rbs/*.rbs をコピー (エンジン必須の 15 クラス。
   MIT なのでそのまま取り込んで良い)。
2. `enumerable.rbs` と `gpio.rbs` を追加 (host_test が GPIO と Enumerable を
   前提にしている。tmp/picoruby-ti/sig/ に評価時に書いた最小定義があるので
   流用可)。GPIO は picoruby の実 API に合わせて書き直して良い。
3. `fmrb.rbs` (新規): FmrbAppBase (gfx / on_update / on_event / app_name あたり
   から。実際のメソッド一覧は main/prebuild_scripts/rb/lib/ のアプリ基底を
   読んで、**存在するものだけ**書く) と Canvas (draw_text / draw_rect /
   draw_line / present 等、picoruby-fmrb-app の GFX バインディングから
   シグネチャを拾う)。全網羅は P4 でやる。ここでは「P2 の補完デモが
   成立する最小セット」で止める。

**ルール: 上流 host_test が exact match で見るシグネチャ (String#tr 等、
example/rbs 由来のもの) は文字列を一切変えない**。FMRB の追加は必ず別
ファイルで行う (test_builtin がシグネチャ文字列 assert を持つため)。

## T5: rake ti:test (ホスト回帰)

- prism の静的ライブラリを components/picoruby-esp32/picoruby/mrbgems/
  mruby-compiler/lib/prism から一時ディレクトリにコピーして `make static`
  でビルドし (submodule を汚さないため直接 make しない)、
  `make -C host_test PRISM_ROOT=<そこ> test` を回す rake タスクを作る。
  db は sig/ から生成したものを使う。
- 全テスト green を受け入れ条件に含める。評価時 (2026-08-11) は
  example/rbs + enumerable + gpio + fmrb 追加で全 green を確認済み。

## 受け入れ条件

1. 素の状態 (vendor/ 無し) から `rake ti:setup` -> `rake build:linux` が通り、
   `nm build/fmruby-core.elf | grep ti_fill_suggestions_at_cursor` で
   シンボルが見えること。elf が x86-64 であること。
2. `rake clean_all` 後に esp32 (S3, .env=NARYAv3 相当) のビルドが通ること。
   flash 使用率の変化 (idf.py size の app 部分) を report に記録する
   (S3 残 6% 問題の判断材料。ここでは通れば良く、削減はしない)。
3. エンジン 2 構成 (標準 = Spinel 構成、全 mruby) の両方で linux ビルドが
   通ること (ti は C のみでエンジン非依存のはずだが、gembox を触るので確認)。
4. `rake ti:test` が全 green。
5. sim の起動確認 (tools/dev_run_check.sh) がこれまで通り通ること
   (ti はまだ誰も呼ばないので、リンクと起動が壊れていないことの確認)。

## report に書くこと

- fmrb-dev の HEAD (PIN に入れた commit)
- S3 / linux のサイズ実測 (エンジン + 生成 db でどれだけ増えたか)
- sig/ に置いたファイル一覧と、P4 に回した未定義 API のメモ
- 詰まった点・判断した点 (次段階の指示書に反映する)

## やらないこと (P1 の範囲外)

- エディタ UI・editor-core の全文取り出し API (P2)
- arena 16KB の PSRAM 化 (P5。S3 で内蔵 RAM が問題になったときに
  EXT_RAM_BSS 属性を fork 側で入れる)
- RBS の全網羅 (P4)
- 上流 PR (T0 スキップ中。fmrb-inheritance は PR 候補として温存)
