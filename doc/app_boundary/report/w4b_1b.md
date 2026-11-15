# W4b-1b W4b-1 の後に決めた 3 つと、呼び出しの経路の小さな直し

> 親: plan.md の「W4b-1 の後に決めたこと」、指示: instruction_w4b_1b.md | 2026-11-15 | 本体の checkout (develop) と
> tmp/mruby-checkpoint。開始 14:02、終了 14:40 (`date`)。

## 1. mruby の `present` を C に戻した

- `lib/gems/picoruby-fmrb-app/ports/esp32/gfx.c` の `mrb_gfx_present` (`present([x, y])`、`MRB_ARGS_OPT(2)`)。中で
  境界の実体 `fmrb_bnd_gfx_present_raw` を呼ぶ。引数の形は Ruby の包みと同じ: `x` と `y` がどちらも真なら位置つき、
  それ以外 (省略・片方 nil) は Window の位置。返り値は self。失敗は生成したつなぎと同じ `fmrb_gen_mrb_check`。
- mrblib の `present` は消した (コメントで C にあることを書いた)。`present_raw` (生成) はそのまま。
- 道具・sig との関係: 道具は「internal = 境界に載せない」だけを見て、包みをどこに書いたかは見ない
  (`check_raw_names` は `_raw` と同名の包みが境界に無いことしか調べない)。よって sig の `present` は
  `%a{fmrb.internal}` のまま、道具の変更は無い。例外の書き方は 2 か所に書いた: `sig/README.md` (包みは各エンジンの
  Ruby で書くのが決まりで、例外は mruby の `present` だけ。印は同じ internal) と `tools/boundary/gen.rb` の冒頭。
- 記録係 (`tools/regress/recorder.rb`) は mrblib を読み込んで C の半分を差し替える作りなので、`present` を足した。
- report/w4b_1.md 4 章の表の扱い: 表は W4b-1 の時点の測定として残し、この段の値を下の 5 章に書いた。

## 2. 描き手の上限 (9 個目) を全エンジンで例外に

| エンジン | 前 | 後 |
|---|---|---|
| mruby | `RuntimeError: no room for another FmrbGfx (8 at most)` | 同じ |
| MicroPython | `RuntimeError: no room for another FmrbGfx` | `RuntimeError: no room for another FmrbGfx (8 at most)` (`mp_raise_msg_varg` で `FMRB_GFX_DRAWERS_MAX`) |
| Spinel | 番号 -1 のまま黙って描かない | `FmrbGfx#initialize` で `raise RuntimeError, "no room for another FmrbGfx (8 at most)"` |
| Lua | — | 当たらない: Lua のアプリの FmrbGfx は殻の layout が開く `gfx` 1 つだけで、Lua から FmrbGfx を作る口が無い (`fmrb_lua_gfx.c`)。直していない |

- 文言は 3 つで同じ。Spinel は C の定数を読めないので 8 を文字列に書き、`fmrb_gfx_drawer.h` の名前をコメントで示した。
- 確認: sim (Retro) で 9 個作る一時のアプリ (`/home` に置き、確認の後に消した) を mruby と MicroPython で流し、
  ログが `drw9 mruby: 8 made, then RuntimeError: no room for another FmrbGfx (8 at most)`、
  `drw9 python: 8 made, then RuntimeError no room for another FmrbGfx (8 at most)`。Spinel は実行時に試すアプリが
  作れない (AOT) ので、`rake regress:spinel` の生成した C (`editor_combined.c`) で `if (self->iv_d < 0LL)` の後に
  `sp_raise_cls("RuntimeError", ... "no room for another FmrbGfx (8 at most)")` があることを確かめた。

## 3. 版をまたぐ checkpoint: mruby-checkpoint で断る

### 3.1 調べて分かったこと (report/w4b_1.md 1.2 の訂正)

- report/w4b_1.md は「mrblib のフレームは、読み手に同じ中身が無ければ運んだ中身をそのまま動かす」と書いたが、
  **C の読み手はそうしていない**。フレームの Proc は種類 15 (`GK_PERM_BODY`) として名前で読み手の Proc に結ばれ、
  フレームは**読み手の Proc の irep を書き手の pc から**動かす (`r_frame`: `ci->pc = ci->proc->body.irep->iseq + pc`)。
  運んだ中身 (`GR_PERM_CARRIED`) はフレームには使われない。format_v1.md 7 章の「新しい Proc を作ってフレームに使う」は
  C の読み手には実装されていなかった。
- よって中身の違う mrblib のメソッドの中で止まった像は、復元の後に**別のコードを書き手の pc から**動かす。
  NoMethodError で済むとは限らない。さらに、運んだ irep はどの Proc にも持たれないので復元の途中で解放され、
  `DBG0` の読み込みがその解放済みの irep を読む (ASan で heap-use-after-free、普通のビルドで segfault を見た)。
- mrblib の中で作られたブロック (実行中に作られた Proc) は、運んだ中身を本体に持つ Proc として作られるので、
  運んだ古いコードがそのまま動く (こちらは report/w4b_1.md の書いたとおり)。

### 3.2 断る判定 (gem の作り)

既存の判定 (表 TBL0 は版で断る、SND0 は名前で見て断らない、`requires` は名前で断る) に合わせ、復元の時に読み手が
自分で判定する。イメージの形式は変えていない (新しいセクション無し、古い書き手の像にも効く)。

| 何を | 判定 | 結果 |
|---|---|---|
| フレームが mrblib のメソッドの中 (種類 15) で、運んだ irep の中身のハッシュが読み手の基準に無い | 中身のハッシュ (今ある `checkpoint_perm_irep_by_hash` と同じ照合) | **断る**: `restore failed: a frame is inside 'M:CkLib#wrap' of the writer's mrblib, and this build has another version of that method (its code differs here)` |
| mrblib の中で作られたブロック (`GR_PERM_CARRIED` で、読み手に同じ中身が無いもの、とその子) | ブロックの `OP_SEND` 系 6 つのシンボルのうち、復元した後の状態のどのクラス・モジュール (読み手の基準とプログラムの定義) も持たない名前 | 1 つでもあれば**断る** (名前を挙げる)。全部あれば運んだブロックを動かす (今までどおり) |

- ブロックの判定は SND0 と同じ名前だけの見方 (受け手のクラス・`super`・VM が扱う演算子・計算した名前の `send` は
  見ない)。SND0 と違って断るのは、プログラムが書いていないコードで、`respond_to?` などで備えようがないため。
  format_v1.md 6.2 と 7 に書いた。
- 黙って通る経路について: フレームは中身で見るので、版の違う mrblib のフレームは全部断る。ブロックは名前の
  見方の限界 (別のクラスに同じ名前がある、など) が SND0 と同じく残る。
- **古い版の読み手**はこの判定を持たないので、古いファームへ運ぶ向きは今までどおり (ピンを上げたファーム同士から効く)。
- 運べる範囲への影響: 断るようになるのは、今まで復元の後に別のコードを動かしていた像だけ。アプリの主の Task は
  `FmrbApp#main_loop` (mrblib) のフレームの中で止まるので、**`main_loop` の中身が違うファームの間の移動は断られる**
  (ハッシュは中身だけで、行番号やファイルの他の所の変更は入らない)。FmrbGfx の包み (`draw_text` など) の中で
  止まった Task も同じ。今の regress の場面 (同じソースのビルドの間) は当たらない。

### 3.3 試験 (gem の側)

- 試験用のドライバに `CHECKPOINT_FIXTURE_LIB=1|2|3` を足した: 基準の前に読む小さなライブラリ `CkLib` の 3 つの版
  (1 と 3 は `wrap`・`make` のブロックの中身が違うが呼ぶ名前は同じ、2 は呼ぶ先を `_old_step` から `step_raw` に
  改名)。変数が無ければ基準は今までと同じ (ほかの試験に影響しない)。
- `tools/test_inspect.rb` に「mrblib of another version」の 15 個: フレームの像 (版 1 で `wrap` の中で止める) は版 1 で
  続き (`11`)、版 3・版 2 では `'M:CkLib#wrap'` を挙げて断る。ブロックの像 (版 1 の `make` が作った Proc を持って
  止める) は版 1・版 3 で続き (`2`、版 3 は運んだブロックが動く)、版 2 では `_old_step` を挙げて断る。逆向き (版 2 の像を
  版 1 で) も `step_raw` で断る。64・32 ビットの両方。
- `rake test` (gem): run_tests 507/507 を 3 通り、表の試験 87、inspect 3499、fuzz の差 0。全部通った。

### 3.4 gem のコミット (ピンを上げるときに要る情報)

- `tmp/mruby-checkpoint` の main に手元のコミット 1 つ (push していない):
  `b0ca8c0c6c16ecb9c57849da3a2ad6b2d9df31be fix(restore): refuse mrblib code of another version that cannot run here`
  (親は今のピン `47fce2a`)。
- 変えたもの: `src/checkpoint_restore.c` (判定)、`tools/checkpoint/checkpoint.c` (試験用のドライバの `CkLib`)、
  `tools/test_inspect.rb`、`doc/format_v1.md`、`README.md`・`README.ja.md`。公開の API (`include/`) は変えていない。
  表の版 (`MRB_CHECKPOINT_TABLE_VERSION`)・形式の版も変えていない。
- 内蔵 RAM: 増えない。読み手の作業域 (`ck_*`、復元の間だけ) に、版の違うブロックがあるときだけ名前の表が増える。
  `irep_perm` の値を 1 (自分の) と 2 (運んだ中身から作った) に分けただけで、配列は増やしていない。
- ピンを上げるには: gem の main を push し、`lib/upstream/MRUBY_CHECKPOINT_PIN` の commit を書き換え、`rake setup`。

## 4. 呼び出しの経路の小さな直し

- 記録の詰め込み (`fmrb_boundary_mrb.inc.erb`): 欄の鍵を、型紙が欄ごとに書く `mrb_intern_lit(mrb, "src_x")` で渡す
  (前は `mrb_intern_cstr(key)`、欠けたときは 2 回)。欄の名前の文字列は文言のためだけに残した。
- 選択肢の引数 (`set_font_raw` など): `mrb_sym_name_len` を 1 回と、長さで分けた `memcmp`。毎回の `mrb_intern_lit` を
  やめた (前の形 `sym2name` + `strcmp` より軽い)。
- 中間の Hash: **Spinel** は包み `set_composite_regions` が区画から直接 13 バイトの記録に詰める形にした (区画ごとの
  Hash と配列を作らない。欠けた欄の補いは包みのまま)。**mruby** はできなかった: 生成した `set_composite_regions_raw`
  は欄のそろった Hash の配列を受ける約束で、包みが補った値を渡すには Hash が要る。避けるには raw の約束を変える
  (欠けた欄を許す) か手書きの C が要り、決めたことの外なので入れていない。
- TLS: `fmrb_gfx_submit_from(ctx, cmd)` (`components/fmrb_gfx/fmrb_gfx_cmd.h`) を足し、`fmrb_gfx_submit` はそれを
  `fmrb_current()` で呼ぶだけにした。`fmrb_boundary_gfx.c` の `TARGET` が引いた ctx を持ち、`submit` と `get_pixel`・
  `present_raw` はそれを使う。描く命令 1 つあたりの `fmrb_current()` は 3 回 → 1 回、`present_raw` は 4 回 → 1 回。

## 5. 計測 (mruby、ホストの命令数)

- report/w4b_1.md 4.1 と同じ測り方 (本体の mruby をホスト向けに `-O2`、何もしない平らな関数、callgrind、
  `ghost-valgrind`、1 回あたり = (n = 20,000 − n = 0) / 20,000、空のループ 1 周 135 込み)。道具は scratchpad の
  `w4b1b/` (hotpath の bench を写し、この段の形を足した)。

| 呼び方 | 前 | 後 | 参考 |
|---|---|---|---|
| `present` | 1,342 (Ruby の包み) | **644** (C) | W4a の前の C 620、`present_raw` 直接 790 |
| `present(3, 4)` | 1,371 | 755 | |
| `set_font_raw(:ja, 12)` | 1,421 | **766** | W4a の前の `_set_font` 839 |
| `set_font_raw(:ja_bold, 12)` | 1,844 | 771 | |
| `set_composite_regions` (3 区画) | 38,393 | 37,692 (−1.8%) | W4a の前 24,031。残りの差は区画ごとの Hash (4 章) |

- `present` は 620 前後に戻った (+24 は省略できる引数を `|oo` で受けて nil を見る分)。
- TLS の 1 回化は、このホストの bench (平らな関数が何もしない) では測れない。実機の `fmrb_current()` は FreeRTOS の
  TLS の読み出し 1 回 (`fmrb_task_get_tls(fmrb_task_get_current(), ...)`)。

## 6. 確かめたこと

| 確認 | 結果 |
|---|---|
| `rake build:linux` (`rake clean` から) | 通った |
| `rake regress` | ホスト 29 件・描画の記録 229 件 全部通った (37 秒) |
| `rake regress:spinel` | 6 本 全部生成できた (19.6 秒) |
| `ruby tools/boundary/gen.rb --check` | SDK と qstr が型定義と一致 (46 関数・記録 3) |
| 9 個目の描き手 | mruby・MicroPython は sim で例外、Spinel は生成した C、Lua は当たらない (2 章) |
| gem の `rake test` | 全部通った (3.3) |
| `MRUBY_CHECKPOINT_DIR=tmp/mruby-checkpoint rake regress:checkpoint` | standard・mruby・web 全部 ok (7.2 分)。ネイティブ版の gem に判定が入っていることを見た |
| `rake regress:screens ONLY=retro` | 9/9 (デスクトップは mruby で毎フレーム `present`、2.3 分) |
| `rake regress:run` | ok (4.5 分)。Retro・Modern とも揺れの幅の中 (デスクトップ 794K→794K、カーネル・アプリも基準の中) |
| 最後の Linux のビルド | `MRUBY_CHECKPOINT_DIR` なし (固定の版 `47fce2a` の gem) で `rake clean` から作り直した。`file build/fmruby-core.elf` は x86-64。picoruby の中の gem の写しも固定の版に戻った (判定は入っていない)。ネイティブ版の `native/build` は regress:checkpoint が手元の gem で作ったまま |

## 7. コミット

fmruby-core (develop):
- aade542e feat(gfx): the ninth FmrbGfx of an app raises in every engine
- 98767d4c perf(gfx): mruby's FmrbGfx#present is a C method again
- 08b24082 perf(boundary): fewer lookups on the drawing call path
- この report と plan.md (docs)

mruby-checkpoint (手元の main): `b0ca8c0c fix(restore): refuse mrblib code of another version that cannot run here`

## 8. 親とユーザに確かめてほしいこと

- **3.2 の運べる範囲**: `FmrbApp#main_loop` や FmrbGfx の包みの中身が違うファームの間では、止めた Task がその中に
  いる限り移動を断るようになる (前は別のコードを動かしていた)。ピンを上げる前に、この扱いでよいか。
- **mruby の `set_composite_regions` の中間の Hash** は残した (4 章)。消すなら raw の約束を変えるか手書きの C。
- format_v1.md 7 章の「新しい Proc を作ってフレームに使う」は C の読み手に無い (SabiRuby の読み手がどうかは見ていない)。
  C の読み手で実装すれば、版の違うフレームも断らずに運んだ中身で動かせる (呼ぶ名前の判定はブロックと同じものが使える)。
