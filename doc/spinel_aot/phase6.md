# Phase 6 指示書: 実機の残課題と仕上げ

前提: **Phase 5 完了**。Spinel カーネル + Spinel desktop の 2 インスタンスが
ESP32-P4 (Tab5) 実機で安定動作している (`reports/phase5_report.md`)。
Phase 6 は「動いた」から「実用」へ持っていくフェーズで、
機能欠落 1 件、メモリ回収 1 件、Phase 5 で先送りした計測群、の 3 系統からなる。

fork = `origin/fmrb-dev = 94c2f89a`。

## タスク

### T6-1: 画像が 1 枚も描画されない問題の解決 (最優先)

**症状**: Spinel desktop × P4 でのみ、壁紙も起動ロゴも表示されず
`clear` の色だけが見える。mruby desktop では表示される。
Linux の Spinel desktop でも表示される。リモートデスクトップ (ブラウザ) でも
出ないので、DSI パネル固有ではなく合成結果に入っていない。

Phase 5 でエンジン間の差分は潰し済み (canvas 作成パラメータ、`present`、
`TRANSPARENT_COLOR`、`clear` のコマンド割り当て、`CREATE_IMAGE_FROM_FILE` の
コマンド構築、desktop の Ruby ソース)。詳細は phase5_report.md。

### 切り分け済み: 表示側は無罪

**混成ビルド (`FMRB_KERNEL_ENGINE=spinel` + `FMRB_APP_ENGINE_DESKTOP=mruby`) で
壁紙も起動ロゴも正常に表示された**。同一実機・同一の display_p4・同一の画像ファイル・
同一のカーネルで desktop エンジンだけが違うので:

- PNG デコード、`pushSprite`、PPA 合成、DSI 出力は**すべて正常**
- canvas 2 の合成機構そのものも**正常に機能している**
- **問題は Spinel desktop が送るコマンド列、またはその時点の状態にある**

したがって「表示側の合成が canvas 2 を無視している」という筋は消えた。

### 次の一手: `present` の対象 canvas を疑う

両ビルドのログで `DRAW_IMAGE` の行は**完全に同一**
(`id=1 -> canvas=2 (113,20) 200x200`)。**描画先は正しい**。
つまり「画像が canvas 2 に届いていない」のではなく、
**canvas 2 の内容が画面に出ていない**可能性が高い。合成は `present` 時に走るので、
疑うべきは `@bg_gfx.present` である。

具体的な確認項目:

1. **`@bg_gfx` が canvas 2 を指しているか**。mruby 版のログには
   `app: Created background canvas 2 for app system_desktop` と
   `gfx: FmrbGfx.new called: canvas_id=2` があるが、Spinel 版には無い。
   `fmrb_spx_app_init` が背景 canvas の id を Ruby 側へ正しく引き渡しているか、
   `_init` の戻り値レコードを確認する
2. **`present` が canvas 2 に対して発行されているか**。
   `display_p4` 側に present の canvas_id をログする一行を足せば即座に分かる
3. **present の座標**。`fmrb_spx_gfx_present` は `explicit_pos` が 0 のとき
   `ctx->window_pos_x/y` を使う。背景 canvas は全画面 (0,0) 想定なので、
   ここに desktop のウィンドウ座標が入ると位置がずれる。
   mruby 側の present が同じ値を渡しているかを突き合わせる

いずれも Spinel desktop 側 (`fmrb_spx_app.c` / `fmrb_spx_gfx.c` /
`fmrb_app_base_spinel.rb`) の問題であり、fork の変更は要らない見込み。

### それでも見つからない場合

`draw_background` の画像描画を「目立つ色のベタ塗り」に一時差し替える:

```ruby
@bg_gfx.fill_rect(0, 0, @window_width, @window_height, 0x1C)  # 例: 緑
```

- **色が出る** → present は正常で、画像経路 (デコード結果の中身) が犯人
- **色が出ない** → present か `@bg_gfx` の指す canvas が犯人 (上記 1〜3 を深掘り)

#### 参考: 疑わしいが未確定の構文

`system_desktop.app.rb` の boot アニメーションに
`tx, ty = @boot_tiles[@boot_anim_idx]` がある。これは **poly 配列要素からの
多重代入**で、Phase 4 で「Spinel 不可」と判明した構文
(`due, rest = @_timers.partition{}` を明示ループに書き換えた) と同型である。
ただし直後の `@gfx.clear(0x01)` が canvas 全面を透過色にするため、
これだけでは症状を説明できない。切り分けの過程で boot アニメーション経路を
触るなら併せて確認する。

### T6-2: 例外スタックの計測と縮小 (~65 KB 回収)

**現状**: 例外/catch スタックが内部 DRAM を、生成 TU 1 本あたり
`sp_exc_*` 20,992 + `sp_catch_*` 21,312 = 42,304 B、**2 本で 84,608 B** 占めている
(map からの実測)。ユーザアプリ 2 つで IRAM free が 31 KB まで落ちるので、
3 つ目 (`FMRB_MAX_USER_APPS = 3`) を実用にするには回収が要る。

**Phase 5 で 32 に下げて失敗している**。begin フレーム push
(`sp_exc_top++`) に境界チェックが無く、溢れた書き込みが隣接 `.bss` の
`sp_exc_rootmark` (GC ルート管理) を破壊して偽の OOM になった。
**見積もりで決めてはいけない**。手順:

1. **fork: push を fail-loud にする**。codegen が
   `sp_exc_msg[sp_exc_top] = 0; sp_exc_obj[sp_exc_top] = 0; sp_exc_top++;`
   を出している 7 箇所 (`codegen_iter.c` / `codegen_expr.c` /
   `codegen_stmt.c` ×3 / `codegen_call.c` ×2) を、`sp_runtime.h` の
   インラインヘルパ呼び出しに置換する。ヘルパは `sp_rescue_push` と同型で、
   溢れたら明示的に落とす。**生成 C の再生成が必要**
2. **同じヘルパで high-water を記録**し、`fmrb_app_dump_vm_pools()` の行に
   出す (計装の受け皿は Phase 5 で作成済み)
3. 実機を通常操作し、両インスタンスの最大値を読む
4. **実測値 + マージン**で `SPINEL_RT_EXC_STACK_MAX` を決める。
   16 まで下げられれば約 65 KB、32 でも約 42 KB 回収

ノブ自体は fork `b8e5a02c` で用意済み。値を変えるだけなら
生成 C の再生成は不要 (マクロは `sp_runtime.h` にあり、
配列は生成 TU にある)。ただし 1. の fail-loud 化には再生成が要る。

**併せて**: `SP_GC_MARK_STACK_MAX` も現在 8192 (32 KB/本) だが、
実測でプール使用率が 17〜40% と判明したので、
必要ならさらに詰められる。こちらは溢れても再帰に落ちるだけで安全。

### T6-3: 性能計測 (Phase 5 受け入れ基準 2 の積み残し)

Linux では計測済み (Spinel 2.0ms/draw vs mruby 4.8ms、max 3ms vs 14ms)。

**起動時間は実機で計測済み** (混成ビルドとの比較、phase5_report.md):
`/app` スキャン 3.5x、スプライト生成 3.3x、カーネル起動→壁紙描画 3.0x
(17.17 s → 5.73 s)。**内部 RAM は実質同等** (Spinel の静的 .bss 43 KB は
mruby VM の実行時内部 RAM 消費で相殺され、むしろ 2.5 KB 有利)。

**イベントレイテンシは Spinel カーネル側だけ実測済み** (2026-07-30、S3 実機、
kernel-only Spinel = desktop は mruby)。`input_router.rb` の `hid_lat` ログ、
アプリ 3 本の起動を含む操作区間:

```
spx: hid_lat: n=1000 sum_ms=414 max_ms=14 ge1=362 ge5=4 ge10=3 gt25=0
```

1 イベント平均 0.414 ms、max 14 ms、**25 ms 超えはゼロ** — Phase 5 が「実機での
本命確認」に挙げた基準は満たす。`ge10=3` はアプリ起動処理と競合した瞬間と見られる。
**ただし比較の相方 (同一コミットの mruby カーネル) が未取得なので、mruby 比で
速いかはまだ言えない**。同じ操作を `FMRB_KERNEL_ENGINE` 未設定でビルドして並べれば
この項目は埋まる。

同時に読めた実機の数値 (kernel-only Spinel、desktop + ユーザアプリ 3 本):

- Spinel カーネルの VM プール used 96〜136 KB / 512 KB、frag 3〜13% で安定
- 内部 RAM は desktop のみ 82.8 KB → アプリ 3 本で 18.0 KB。**アプリ 1 本あたり
  約 23 KB** 減るので、ユーザアプリ 3 本という上限はスロット数だけでなく
  内部 RAM 側からも妥当

**残りの未計測項目**:

- イベントレイテンシの mruby カーネルとの比較 (上記の相方)
- GC 停止時間の分布 (特に max)。Linux では AOT が有利だったが実機は未確認
- フラッシュ使用量
- **live set の公平な比較**。VM プールの used は GC トリガ条件が違うため
  そのまま比較できない (mruby 381 KB 一定 vs Spinel 205〜325 KB 変動)。
  両方で強制 GC 直後を採る必要がある。64bit Linux で測った
  「Spinel は mruby の 1.9 倍」が 32bit 実機でも成り立つかは**未確認**で、
  現状のデータはむしろ成り立たない可能性を示している

計測は `board_millis` による dual-build 一時計装で行う (Phase 4 と同じ手法)。
カーネルと desktop のエンジンを独立に切り替えられるので、
片方を mruby 固定にして対象を隔離できる。

### T6-4: soak (Phase 5 受け入れ基準 4 の積み残し)

2 インスタンス同居の長時間動作。最低 30 分、できれば数時間。
`fmrb_app_dump_vm_pools()` の used が単調増加しないこと (リーク検出) と、
IRAM free が定常であることを確認する。
ユーザアプリの起動 → 終了を繰り返し、
Phase 4 で mruby について確認済みの「終了後にプールが完全復帰する」性質が
Spinel でも成り立つことを見る。

### T6-5: `spinel:doctor` と RTC (Phase 5 受け入れ基準 6 の積み残し)

- `rake spinel:doctor` を clean にする
- RTC (RX8900/RX8130) は ESP32 専用 mrbgem のため Spinel desktop の TU 外。
  Phase 4 で「cross-gem 課題」として棚上げした。FFI 化するか、
  desktop から切り離すかを決めて実装し、根拠をレポートに記録する
- 実機ログに `RTC sync: failed to read time` が出ている件も併せて解決

### T6-6: ESP32-S3 実機検証

Phase 5 の指示書は S3 前提だったが、実機検証は P4 で行った。
**S3 実機は未検証**。ビルドとリンクは通っている (Phase 5 の T5-2)。
32bit ゲートが幅の問題をカバーしているので大きな驚きは想定しないが、
Xtensa の `__thread` と `SP_NO_MMAN` 周りのヘッダ検出は
アーキ依存なので実機で確認する。

### T6-7: 小さな残件

- **`m5gfx_task.cpp` の `send_ack` 固定バッファ化が未コンパイル検証**。
  ATOM_DISPLAY 専用ソースのため今回のビルドに含まれていない。
  `.env` の `FMRB_HW_TARGET=ATOM_DISPLAY` + `rake clean_all` + `rake build:esp32` で確認
- **`picoruby-esp32` の msgpack gem が libc malloc を参照したまま**。
  submodule 配下なので `lib/add|patch|replace` 経由で対処する
- **`fmrb_app_dump_vm_pools()` の frag 列が 100% を超える**ことがある
  (mruby VM で 123% を観測)。estalloc の統計計算側の疑い。
  この列は現状信用できない

### T6-8: kernel-only Spinel 構成 (2026-07-29 Linux / 2026-07-30 S3 実機)

desktop の Spinel 化は工数の割に高速化への寄与が薄い可能性が高いため、
**カーネルだけ Spinel** (`FMRB_KERNEL_ENGINE=spinel`、desktop は mruby) で
先に動きを見る、という方針で通した構成。Linux headless で
**デスクトップ起動・壁紙描画・アプリ起動/終了とも正常、カーネルログにエラーゼロ**。

この構成を初めて通したことで不具合が 3 件出た。いずれも修正済み。
1 と 2 は mruby 構成では出ないもの、3 は共有 C 側で**エンジンに依らない**もの。

1. **生成 C が構文エラー** — 条件式の単項 `!` の下の呼び出しで前置き文が
   式の中に落ちる fork の codegen バグ。`ruby_writing_constraints.md` B と
   `reports/fork_pr_candidates.md` B-1 に登録。カーネル Ruby 側は
   ローカルへホイストして回避 (1 箇所)。
2. **パスが 32 バイト以上のアプリを起動できない** — `fmrb_spx_spawn_app_req`
   が、パスを表示名用の `FMRB_MAX_APP_NAME` (32) で値域チェックしていた。
   受け側の `fmrb_app_spawn_app` はパスとして扱い下流は `FMRB_MAX_PATH_LEN` (128)。
   mruby バインディングは長さ制限を持たないため**この構成でしか出ない**。
   `FMRB_MAX_PATH_LEN` へ修正し、拒否時のログを追加 (無言で失敗していたので
   ログ上は「spawn 要求 → 失敗」の 2 行だけで原因が見えなかった)。
3. **ユーザアプリ 4 本目がコンテキストプールの外へ書く** (`main/app/fmrb_app.c`)。
   スロット探索が `PROC_ID_MAX` まで走るのに配列は `FMRB_MAX_APPS` 個で、
   3 枠埋まると配列外のゼロ領域を空きスロットと誤認していた。**Spinel 固有では
   なく**、エディタの RUN で踏んだ。修正は上限の一致 (`PROC_ID_USER_APP_END`) と
   static assert 2 本、および余っていた `PROC_ID_USER_APP3..5` の削除。
   併せて起動失敗の理由をカーネル経由でダイアログまで通した (以前は原因を問わず
   「.toml が要る」と表示しており、1 と 3 の切り分けを二度誤らせた)。

**S3 実機でも確認済み** (2026-07-30、ユーザ実施)。Spinel カーネル + mruby desktop で
ブートし、ユーザアプリ 3 本 (ファイル起動と built-in の両方) を起動・操作。
3 件目の修正が効いていることも実機で確認できた: 3 枠埋めた状態で 4 本目
(built-in の editor) を要求すると

```
E fmrb_app: No free context slots available for app_type=2
E fmrb_default_apps: Failed to spawn built-in app: default/editor (error=-9)
```

で拒否され、その後もタスク一覧・VM プール・z-order 変更が続いて**死なない**。
ESP32 には fortify が無いので、修正前はここで PSRAM の .bss を黙って壊していた。
計測値は T6-3 に記載。

**未実施**: mruby カーネルとの比較計測 (T6-3)。

## 受け入れ基準

1. 壁紙と起動ロゴが実機で表示される (T6-1)。
2. 例外スタックの high-water が実測され、その値に基づいて
   サイズが決定されている。決定の根拠がレポートに残っている (T6-2)。
3. 実機の性能数値が mruby 比で表になっている (T6-3)。
4. soak で VM プールの used が単調増加しないことが確認されている (T6-4)。
5. `rake spinel:doctor` clean、RTC の方式が決定・実装済み (T6-5)。
6. S3 実機でブートし、desktop が描画される (T6-6)。
7. mruby 構成に回帰がない。

## 落とし穴・注意

- **見積もりでバッファサイズを決めない**。Phase 5 で
  「タスクスタックが 24 KB だからネストは数段」という推論が外れ、
  GC ルートを破壊した。計測してから決める。
- **エラーコードの意味を確認する**。Phase 5 の "msgpack unpack failed" は
  パースエラーではなく NOMEM で、症状名から推定すると誤診する。
- **OOM は「総量不足」と「連続領域が取れない」を区別する**。
  Phase 5 の GC クラッシュはプール使用率 20% で起きていた。
- **`fmrb_spx_*` シムと mruby バインディングの契約差は、その構成を通すまで出ない**。
  同じ `fmrb_*` 関数を呼んでいても、シム側が独自に付けた値域チェックや
  バッファ幅が mruby 側と食い違いうる (T6-8 の 2 件目)。エンジンを
  切り替えたら、そのエンジンでしか通らない経路を必ず一度動かす。
- **`spinel:doctor` は生成 C をコンパイルしない**。source-level leg
  (unsupported/unresolved) だけなので、**codegen が壊れた C を吐く類は
  doctor を通っても実ビルドで初めて出る** (T6-8 の 1 件目、T4-3 の sprintf も同様)。
  doctor clean をビルド可能性の保証と読み違えない。
- **headless で再現しない負荷がある**。実マウスのドラッグが流す
  イベントレートは入力注入では出ない。実機でしか出ない不具合を
  headless の結果で否定しない。
- **ノブは Linux にも一律適用する**。dual build に容量差を作ると
  実機まで問題が持ち越される。Phase 5 の例外スタックの件は
  一律適用していたおかげで headless で露見した。
- sdkconfig / sdkconfig.defaults は編集禁止 (提案のみ)。
- Tab5 は DTR/RTS が効かない。書き込み後は物理ボタンでリセット。
