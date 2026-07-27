# Phase 7 指示書: 実機デバッグと仕上げ

前提: **Phase 6 完了**。Spinel カーネル + Spinel desktop の 2 インスタンスが
ESP32-P4 (Tab5) と **ESP32-S3 (NARYA v3)** の両実機でブートし、desktop が動作する。
S3 では壁紙・起動ロゴを含めて描画も正常で、実マウス操作の体感も改善している
(`reports/phase6_report.md`)。

Phase 6 は「基本的な起動と動作確認」でスコープを切り、
機能欠落・計測・チューニングを本フェーズに送った。Phase 7 は
「実用として信頼できる」状態にするフェーズで、4 系統からなる。

1. 診断基盤 (T7-1) — これを先に整えないと以降の切り分けが汚染される
2. 機能欠落・不具合 (T7-2, T7-3)
3. 計測 (T7-4, T7-5)
4. 仕上げ (T7-6, T7-7, T7-8)

fork = `origin/fmrb-dev = 94c2f89a` (`components/fmrb_spinel_rt/SPINEL_PIN`)。

## タスク

### T7-1: 例外スタックの fail-loud 化と high-water 計測 (最優先)

**位置づけが Phase 6 から変わった**。Phase 6 では「約 65 KB のメモリ回収」
として扱っていたが、本フェーズでは**診断基盤**として最初に置く。
理由は、現状の begin フレーム push に境界チェックが無く、
溢れた書き込みが隣接 `.bss` の `sp_exc_rootmark` (GC ルート管理) を
**黙って破壊する**ため。Phase 5 ではこれが「偽の out of memory」として現れ、
原因究明を大きく迷わせた。T7-2 / T7-3 の未知の不具合を追う間、
この地雷が残っていると症状の解釈がすべて疑わしくなる。

現在値は `SPINEL_RT_EXC_STACK_MAX = 64`
(`components/fmrb_spinel_rt/CMakeLists.txt`)。upstream 既定のままで、
32 は Phase 5 で試して**危険と判明済み** (desktop の config ダイアログの
開閉で偽 OOM)。

手順:

1. **fork: push を fail-loud にする**。codegen が
   `sp_exc_msg[sp_exc_top] = 0; sp_exc_obj[sp_exc_top] = 0; sp_exc_top++;`
   を出している 7 箇所 (`codegen_iter.c` / `codegen_expr.c` /
   `codegen_stmt.c` x3 / `codegen_call.c` x2) を、`sp_runtime.h` の
   インラインヘルパ呼び出しに置換する。ヘルパは `sp_rescue_push` と同型で、
   溢れたら明示的に落とす。**生成 C の再生成が必要**。
2. **同じヘルパで high-water を記録**し、`fmrb_app_dump_vm_pools()` の行に
   出す (計装の受け皿は Phase 5 で作成済み)。
3. 実機を通常操作し、kernel / desktop 両インスタンスの最大値を読む。
   config ダイアログの開閉、launcher のスクロール、ユーザアプリの
   起動と終了、ウィンドウのドラッグを一通り通す。
4. **実測値 + マージン**で `SPINEL_RT_EXC_STACK_MAX` を決める。
   16 まで下げられれば約 65 KB、32 でも約 42 KB の内部 DRAM が戻る。
   決定の根拠 (実測 high-water と採用マージン) をレポートに残す。

ノブ自体は fork `b8e5a02c` で用意済み。値を変えるだけなら生成 C の
再生成は不要 (マクロは `sp_runtime.h`、配列は生成 TU)。1. の fail-loud 化には
再生成が要る。

**併せて**: `SPINEL_RT_GC_MARK_STACK_MAX` は現在 8192 (32 KB/本)。
プール使用率が 17〜40% と判明したのでさらに詰められる余地があるが、
こちらは溢れても再帰に落ちるだけで安全なので優先度は低い。

### T7-2: window order の異常 (新規)

**症状**: Spinel 版でウィンドウの重なり順がおかしくなる (ユーザ報告)。

**Spinel 固有と決めつけないこと**。同種の既知バグが 2 件あり、
どちらも Spinel とは無関係に存在する。Spinel は同じ処理を実機で
3.0〜3.5 倍速く流すため、**mruby の遅さで隠れていた race を踏みやすい**
という筋が十分にある。

既知の候補:

- **(A) SET_WINDOW_ORDER と canvas_sort_by_zorder の race**。
  `canvas_state_find` でポインタを取得 → render 側の qsort が canvas 配列を
  swap → 取得済みポインタ位置に z_order を書き込む、で**別の canvas に値が
  書かれて永続化**する。2026-05-17 に `graphics_handler.cpp` の mutex 漏れとして
  修正済みだが、**実機での解消確認は未**。症状の特徴は
  「一度崩れるとリセットするまで直らない」。
- **(B) window list の stale**。canvas 作成時に `mark_window_list_dirty` が
  呼ばれず、クリックのルーティングが古いリストを見る
  (`main/prebuild_scripts/kernel/fmrb_kernel/window_manager.rb`,
  `app_lifecycle.rb`, `input_router.rb`)。フルスクリーン時の
  `No window at (x,y)` として観測されている。
- **(C) Core 側 `fmrb_app.c` の z 採番**が誤った値を送り続けるパス。

手順:

1. **症状の型を記録する**。(a) リセットまで続くか一過性か、
   (b) どの操作で崩れたか、(c) 崩れ方 (overlay の下にユーザアプリが出るのか、
   ユーザアプリ同士の順序が入れ替わるのか)。(a) だけで (A) か否かがほぼ決まる。
2. **engine 軸で隔離する**。`FMRB_KERNEL_ENGINE=spinel` +
   `FMRB_APP_ENGINE_DESKTOP=mruby` の混成ビルドで同じ操作を行う。
   再現するなら既存バグ (A)/(B)/(C)、しないなら Spinel desktop 固有。
   これが最も安い一手で、以降の探索範囲を半分にする。
3. 既存バグ側なら、`Canvas allocated: ID=X ... z_order=Y` と
   `Canvas %u z_order updated to %d` のログで、
   意図した canvas に意図した z が入っているかを追う。
4. Spinel 固有なら、SET_WINDOW_ORDER の発行タイミングと発行元
   (kernel の window_manager か desktop か) を両エンジンで突き合わせる。

### T7-3: P4 で画像が 1 枚も描画されない問題

**Phase 6 から持ち越し。S3 の結果で切り分け表が埋まった**:

| desktop エンジン | 実機 / 表示経路 | 画像 |
|---|---|---|
| Spinel | Linux (SDL) | 出る |
| Spinel | S3 + WROVER (NTSC) | 出る |
| mruby | P4 (display_p4 / DSI) | 出る |
| Spinel | P4 (display_p4 / DSI) | **出ない** |

原因は「Spinel desktop のコマンド列」単体でも「P4 の表示側」単体でもなく、
**両者の組み合わせでのみ露見する**。S3 で同じ Ruby・同じ FFI・同じ
`fmrb_spx_app.c` が通って正常に描画される以上、Phase 6 で第一容疑者だった
**`_init` の canvas id 引き渡しと `@bg_gfx` の指す先は無罪**と見てよい
(P4 の canvas サイズによる分岐が無いことは要確認)。

仮説の優先順位 (S3 の結果で入れ替わった):

1. **タイミング / レース**。display_p4 側で PNG デコードや PPA 合成が
   非同期、あるいは `CREATE_IMAGE_FROM_FILE` と `DRAW_IMAGE` が別経路で
   処理されていると、Spinel の速度で順序依存が露見する。
   WROVER 経路と Linux はこの非同期性を持たないので出ない、という説明が
   上表の 4 行すべてと矛盾しない。
2. **P4 固有のパラメータ感度**。canvas サイズ・画像サイズ
   (P4 は壁紙 426x240、S3 は 320x240 系) が絡む分岐、PPA のアラインメント、
   透過処理など、P4 だけが持つ条件。
3. **present の座標**。`fmrb_spx_gfx_present` は `explicit_pos` が 0 のとき
   `ctx->window_pos_x/y` を使う。S3 でも同じコードが通って出ている以上、
   P4 で背景 canvas の window_pos が違う値になる理由がある場合に限られる。

手順 (安い順):

1. **P4 で GA 側のコマンドログを全量取り、mruby desktop 版と diff する**。
   Phase 5 では `DRAW_IMAGE` の行だけを突き合わせて「同一」と判断しているが、
   見るべきは**行の集合と順序と時刻**である。CREATE_CANVAS のパラメータ、
   clear、present の発行位置、各コマンド間の時間間隔。仮説 1 が正しければ
   時間間隔に明白な差が出る。ビルド不要で情報量が最大。
2. **`display_p4_vm: DEFINE_PROG ok: id=0/1 canvas=1` が Spinel 版に無い件**を
   確定させる。ログを出していないだけか、実際に登録が行われていないか。
   Phase 5 で挙げた他の 2 つのログ差分
   (`app: Created background canvas 2`, `gfx: FmrbGfx.new called: canvas_id=2`) は
   S3 の Spinel ログで `spxapp: created canvas ...` / `fmrb_gfx: Canvas created: ID=2`
   に対応しており、**ログ文の違いに過ぎない**ことが判明した。DEFINE_PROG だけが
   まだ説明されていない。
3. 差が見えなければ **`draw_background` の直前に 300 ms 程度の遅延を
   一時的に入れる**。出るようになれば仮説 1 で確定し、display_p4 の
   非同期処理を追う。出なければ仮説 1 は消える。
4. それでも残れば `draw_background` の画像描画を目立つ色のベタ塗り
   (`@bg_gfx.fill_rect(0, 0, @window_width, @window_height, 0x1C)`) に
   差し替え、present と画像経路を分離する。色が出れば画像経路、
   出なければ present 側。

#### 参考: 疑わしいが未確定の構文

`system_desktop.app.rb` の boot アニメーションに
`tx, ty = @boot_tiles[@boot_anim_idx]` がある。poly 配列要素からの多重代入で、
Phase 4 で「Spinel 不可」と判明した構文と同型。ただし直後の
`@gfx.clear(0x01)` が canvas 全面を透過色にするため、これだけでは
症状を説明できない。boot アニメーション経路を触るなら併せて確認する。

### T7-4: 性能計測 (Phase 5 受け入れ基準 2 の積み残し)

**計測済み**:

- Linux: Spinel 2.0 ms/draw vs mruby 4.8 ms、max 3 ms vs 14 ms
- P4 実機の起動時間 (混成ビルド比): `/app` スキャン 3.5x、
  スプライト生成 3.3x、カーネル起動 → 壁紙描画 3.0x (17.17 s → 5.73 s)
- 内部 RAM は実質同等 (Spinel の静的 `.bss` 43 KB は mruby VM の実行時
  内部 RAM 消費で相殺され、むしろ 2.5 KB 有利)
- **S3 実機の体感**: 実マウス操作で「つっかかり」が解消し安定
  (ユーザ確認、Phase 6)。Linux の max 3 ms vs 14 ms と整合する所見だが、
  **数値ではないので受け入れ基準は埋まらない**

**未計測**:

- **イベントレイテンシ** (入力 → 描画反映)。平均・max・p99。
  マウスドラッグ中が最も差が出る。S3 の体感所見を裏付ける本命の数値。
- **GC 停止時間の分布** (特に max)。fork に GC 計測フック (関数ポインタ) を
  追加する案が `reports/fork_pr_candidates.md` にある。
- **フラッシュ使用量**の mruby 比。
- **live set の公平な比較**。VM プールの used は GC トリガ条件が違うため
  そのまま比較できない (mruby 381 KB 一定 vs Spinel 205〜325 KB 変動)。
  両方で強制 GC 直後を採る必要がある。64bit Linux で測った
  「Spinel は mruby の 1.9 倍」が 32bit 実機でも成り立つかは未確認で、
  現状のデータはむしろ成り立たない可能性を示している。

計測は `board_millis` による dual-build 一時計装で行う (Phase 4 と同じ手法)。
カーネルと desktop のエンジンを独立に切り替えられるので、片方を mruby 固定に
して対象を隔離できる。**比較は必ず同一コミット・同一実機・同一操作**で行う。

### T7-5: soak (Phase 5 受け入れ基準 4 の積み残し)

2 インスタンス同居の長時間動作。最低 30 分、できれば数時間。**P4 と S3 の両方**で行う。

- `fmrb_app_dump_vm_pools()` の used が単調増加しないこと (リーク検出)
- IRAM free が定常であること
- ユーザアプリの起動 → 終了を繰り返し、Phase 4 で mruby について確認済みの
  「終了後にプールが完全復帰する」性質が Spinel でも成り立つこと
- T7-1 完了後であれば、例外スタックの high-water が soak 中に更新され続けないこと

### T7-6: S3 のメモリ余裕の確認 (新規)

Phase 6 の S3 ログから、S3 は P4 より余裕が小さいことが判明した。
どちらも現時点で問題は出ていないが、実用の上限を把握しておく。

| 項目 | S3 (NARYA v3) | P4 (Tab5) |
|---|---:|---:|
| アプリ実サイズ | 2,618,944 B | 4,265,216 B |
| factory パーティション | 3 MB (16.7% 空き) | 6 MB (32% 空き) |
| IRAM free (アイドル) | 61,328 B | 76,108 B |

- **フラッシュ**: S3 は空きが 16.7% しかない。Spinel 対象インスタンスを
  もう 1 つ増やす、あるいは生成 TU が増える変更を入れる前に、
  必ず実サイズを確認する。
- **IRAM**: P4 実測ではユーザアプリ 1 つで -25 KB、2 つで -45 KB だった。
  同じ傾向なら S3 は 2 つ目で 15〜20 KB まで落ち、
  `FMRB_MAX_USER_APPS = 3` は実質成立しない。**S3 でユーザアプリを
  2 つ、3 つと起動して IRAM free を実測する**。
- T7-1 の回収 (最大 65 KB) はこの項目に直接効く。T7-1 の前後で
  同じ測定を行い、効果を数値で示す。

### T7-7: `spinel:doctor` と RTC (Phase 5 受け入れ基準 6 の積み残し)

- `rake spinel:doctor` を clean にする。現在 `Rakefile:386` に
  `unresolved call 'write_time' on ... receiver` の暫定 allowlist がある。
- **RTC は FFI シム経由に決定済み (ユーザ決定 2026-07-24)**。
  現状 `clock_setting.rb` は `RX8900`/`RX8130` ドライバを直接インスタンス化して
  `write_time` を呼ぶ (ESP32 専用ブロック)。システムクロック設定は既に
  `FmrbApp.set_wallclock` FFI (`fmrb_spx_app_set_wallclock`, `fmrb_app_ffi.rb`)
  経由なので、RTC チップ書き込みも同経路に寄せる。
  - C 側 `fmrb_spx_app_set_wallclock` に RTC チップ書き込みを内包するか、
    兄弟 FFI `fmrb_spx_app_set_hwclock` を追加。チップ選択 (RX8900/RX8130,
    CHIP_MODEL) と I2C は C 側が持つ。
  - `clock_setting.rb` のドライバ生成ブロックを削除して FFI 呼びに置換。
  - **dual-safe**: mruby / Spinel の両 `set_wallclock` が同じ C 経路を通ること。
  - 完了後、allowlist を外して完全 clean を確認。
  - `sp_time` / `sp_random` の ESP32 backend もこの HW=FFI の流れで確定する。
- 実機ログの `RTC sync: failed to read time` も併せて解決する。

### T7-8: 小さな残件

- **`m5gfx_task.cpp` の `send_ack` 固定バッファ化が未コンパイル検証**。
  ATOM_DISPLAY 専用ソースのため現在のビルドに含まれていない。
  `.env` の `FMRB_HW_TARGET=ATOM_DISPLAY` + `rake clean_all` + `rake build:esp32` で確認。
- **`picoruby-esp32` の msgpack gem が libc malloc を参照したまま**。
  submodule 配下なので `lib/add|patch|replace` 経由で対処する。
- **`fmrb_app_dump_vm_pools()` の frag 列が 100% を超える**ことがある
  (mruby VM で 123% を観測)。estalloc の統計計算側の疑い。
  この列は現状信用できない。S3 でも desktop に 17% の瞬間が出ているが、
  この列を信用する前に計算側を直す。

## 受け入れ基準

1. 例外スタックの push が fail-loud になり、high-water が実測され、
   その値に基づいてサイズが決定されている。決定の根拠がレポートに残っている (T7-1)。
2. window order 異常の原因が特定され、既存バグか Spinel 固有かが確定し、
   修正されている (T7-2)。
3. P4 で壁紙と起動ロゴが表示される (T7-3)。
4. 実機の性能数値 (イベントレイテンシ、GC 停止分布、フラッシュ、live set) が
   mruby 比の表になっている (T7-4)。
5. P4 と S3 の両方で soak が通り、VM プールの used が単調増加しない (T7-5)。
6. S3 でユーザアプリ複数起動時の IRAM free が実測され、
   `FMRB_MAX_USER_APPS` の実用値が判断できる (T7-6)。
7. `rake spinel:doctor` clean、RTC が FFI 経由で実装済み (T7-7)。
8. mruby 構成に回帰がない (P4 / S3 とも)。

## 落とし穴・注意

- **見積もりでバッファサイズを決めない**。Phase 5 で
  「タスクスタックが 24 KB だからネストは数段」という推論が外れ、
  GC ルートを破壊した。計測してから決める。
- **エラーコードの意味を確認する**。Phase 5 の "msgpack unpack failed" は
  パースエラーではなく NOMEM で、症状名から推定すると誤診する。
- **OOM は「総量不足」と「連続領域が取れない」を区別する**。
  Phase 5 の GC クラッシュはプール使用率 20% で起きていた。
- **headless で再現しない負荷がある**。実マウスのドラッグが流す
  イベントレートは入力注入では出ない。実機でしか出ない不具合を
  headless の結果で否定しない。T7-2 の window order はまさにこの型。
- **速さが露見させる不具合がある**。Spinel は同じ処理を 3 倍速で流すので、
  mruby では踏まなかった race を踏む。「Spinel で出た = Spinel のバグ」
  ではない。engine 軸の切り替えで必ず隔離する。
- **ノブは Linux にも一律適用する**。dual build に容量差を作ると
  実機まで問題が持ち越される。Phase 5 の例外スタックの件は
  一律適用していたおかげで headless で露見した。
- **比較は同一コミットのビルド同士で行う**。別日・別コミットの記録と
  突き合わせると誤った結論になる (Phase 5 で実際に起きかけた)。
- **ターゲットを P4 / S3 で切り替えるときは `rake clean_all`**。
  `.env` の `FMRB_HW_TARGET` は環境変数より優先されるので、
  意図したチップがビルドされているかをブートログで必ず確認する。
- sdkconfig / sdkconfig.defaults は編集禁止 (提案のみ)。
- Tab5 は DTR/RTS が効かない。書き込み後は物理ボタンでリセット。

## 完了レポート

`doc/spinel_aot/reports/phase7_report.md`:

- 例外スタックの high-water 実測値と採用サイズ、回収した内部 DRAM の内訳
- window order の原因と修正 (既存バグだった場合はその旨と、
  Spinel が露見させた機序)
- P4 画像問題の原因と修正、切り分けの記録
- 実機計測表 (レイテンシ、GC 停止、フラッシュ、live set、mruby 比)
- soak 結果 (P4 / S3)
- S3 のメモリ余裕と `FMRB_MAX_USER_APPS` の実用値
- 残課題と、ユーザ確認が必要な項目のチェックリスト (音声、NTSC、操作感)
