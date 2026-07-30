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

#### イベントレイテンシの計測方法 (2026-07-30、S3 実機)

再現と引用のために、何をどう測ったかを先に書く。

**何を測っているか**。`fmrb_kernel/input_router.rb` の `handle_hid_event`。HID
イベント 1 件のバイナリを解いた直後に `t0 = Machine.board_millis` を取り、
`subtype` ごとの処理 (3 = マウス移動 / 4 = ボタン押下 / 5 = ボタン離し) を抜けた
ところで差を取る。**カーネルの Ruby がイベント 1 件を処理し切るまでの時間**であって、
USB から画面反映までの端から端ではない。カーネルのエンジンを差し替えたときに
直接効く区間を狙って挟んである。

**経過時間であって CPU 時間ではない**。処理の途中でタスクが他へ譲れば、その待ちも
入る。mruby カーネルは仮想機械の中で動くので途中で譲る。

**窓の区切り**。1000 件ごとに 1 行出して、そこで全カウンタを 0 に戻す。だから
1 行 = 直前の 1000 件で、行が出た時点までに何をしていたかで中身が変わる。

**集計行の読み方**: `sum_ms` は 1000 件の合計 (÷1000 が平均)、`max_ms` は最大、
`geN` は N ms 以上の件数、`gt25` は 25 ms 超の件数。25 ms は「マウス移動が 30Hz
= 33 ms 間隔で届くので、それを超えるとカーネルの待ち行列に溜まりはじめる」という
意味のしきい値 (`hid_event slow` 警告と同じ基準)。

**ハードと構成**: ESP32-S3 (NARYAv3、N16R8、240 MHz)、画面 320x240、映像・音声は
子マイコン (ESP32-WROVER) に UART 921600 bps で委譲。desktop は**どちらのビルドでも
mruby**、差し替えたのはカーネルのエンジンだけ。

**ビルドの同一性**: ログの `App version` で Spinel 版が `1.0.0-266-gb516abc-dirty`、
mruby 版が `1.0.0-270-g3ac50e3-dirty`。この 2 コミットの差分は doc / .github / .env
だけで**ファームのコードは同一**。エンジンの選択は `FMRB_KERNEL_ENGINE`
(mruby 版は `FMRB_KERNEL_ENGINE=mruby rake build:esp32`)。

**操作手順** (両ビルドで同じ): リセット → デスクトップ表示 → ランチャーから
JA Text (`/app/demo/ja_text.app.rb`) → Monitor (`default/monitor`) → Piano
(`/app/game/piano.app.rb`) を起動 → ウィンドウをドラッグ → マウスを動かし続けて
集計行を出す。

**メモリの数値の出どころ**: 内蔵 RAM は `fmrb_task: IRAM free:` の行、カーネルの
プール使用量は `fmrb_app: --- VM Pools ---` の `fmrb_kernel` 行。どちらも同じ操作の
同じ時点 (desktop のみ / ユーザアプリ 3 本) で読んだ。

**Spinel カーネル**、アプリ 3 本の起動を含む窓:

```
spx: hid_lat: n=1000 sum_ms=414 max_ms=14 ge1=362 ge5=4 ge10=3 gt25=0
```

1 イベント平均 0.414 ms、max 14 ms、**25 ms 超えはゼロ** — Phase 5 が「実機での
本命確認」に挙げた基準は満たす。`ge10=3` はアプリ起動処理と競合した瞬間と見られる。

2 本目 (同日、RTC/ファイル同期の修正を入れた `1.0.0-273-g429aaaf-dirty`。デスクトップで
しばらく待ったあとランチャー → JA Text → Monitor を起動しウィンドウを 2 回ドラッグ):

```
spx: hid_lat: n=1000 sum_ms=602 max_ms=19 ge1=520 ge5=9 ge10=2 gt25=0
```

平均 0.602 ms、max 19 ms、**25 ms 超えは再びゼロ**。**アプリ起動とドラッグを含む窓で
2 回続けて 25 ms 超えが出ない**ことが確認できた。1 件 1 ms 未満が半分強
(`ge1=520`) という比率も 1 本目 (`ge1=362`) と同じ傾向。

3 本目、**アプリを 1 つも起動せずマウスを動かすだけの窓** (同じファーム):

```
spx: hid_lat: n=1000 sum_ms=287 max_ms=9 ge1=272 ge5=2 ge10=0 gt25=0
```

平均 0.287 ms、max 9 ms、1 ms 以上が 272 件。mruby の同条件の窓と直接比べられる。

**mruby カーネル**、生ログ 3 行 (出た順):

```
13:46:09 fmrb_kernel: hid_lat: n=1000 sum_ms=2622 max_ms=89 ge1=965 ge5=28 ge10=22 gt25=16
13:46:54 fmrb_kernel: hid_lat: n=1000 sum_ms=2758 max_ms=84 ge1=997 ge5=20 ge10=16 gt25=15
13:47:41 fmrb_kernel: hid_lat: n=1000 sum_ms=1155 max_ms=3  ge1=1000 ge5=0  ge10=0  gt25=0
```

1 行目と 2 行目がアプリ 3 本の起動とウィンドウのドラッグを含む窓、3 行目は起動が
済んだあとマウスを動かすだけだった窓 (この間 `hid_event slow` の警告も 1 件も
出ていない)。1 行目と 2 行目の間もドラッグを続けている。

参考として、その `hid_event slow` 警告の実物 (mruby 版、25 ms 超の 1 件ごとに出る):

```
W fmrb_kernel: hid_event slow: subtype=3 capture=drag 80ms
W fmrb_kernel: hid_event slow: subtype=3 capture= 77ms
```

| 区間 | Spinel カーネル | mruby カーネル | 比 |
|---|---|---|---|
| **静か**: 平均 | **0.287 ms** | 1.155 ms | **4.0x** |
| 静か: 最大 | 9 ms | **3 ms** | (mruby のほうが小さい) |
| 静か: 1 ms 以上の件数 | **272 / 1000** | 1000 / 1000 | |
| **負荷あり**: 平均 | **0.414 / 0.602 ms** | 2.62 / 2.76 ms | **約 4.5x** |
| 負荷あり: 最大 | **14 / 19 ms** | 89 / 84 ms | |
| 負荷あり: 25 ms 超え | **0 / 0 件** | 16 / 15 件 | |

「静か」= アプリを起動せずマウスを動かすだけ。「負荷あり」= アプリ 2〜3 本の起動と
ウィンドウのドラッグを含む。mruby 側は 3 窓、Spinel 側も 3 窓 (負荷あり 2 + 静か 1)。

- **素の 1 件あたりは 4 倍速い**。静かな条件で 0.287 対 1.155 ms、負荷ありでも同じ
  約 4 倍。**1 ms 未満の比率が決定的**で、Spinel は 73% が 1 ms 未満、mruby は
  1 件も 1 ms を切らない (`ge1=1000`)。仮想機械の解釈コストが床を作っている形。
  Linux の 2.0 対 4.8 ms と同じ桁。
- **静かな条件では最大値が逆転する** (Spinel 9 ms 対 mruby 3 ms)。mruby は全件が
  1〜3 ms に収まる代わりに床が高く、**Spinel は床が低いかわりに時々数 ms の山が出る**
  (`ge5=2`)。Spinel にもゴミ集めはあるので、その辺りが候補。**この逆転は隠さずに
  記録する** — 「AOT にすれば最悪値も必ず良くなる」とは言えない。
- **効くのは負荷がかかったときの差**。そこでは mruby の山が 90 ms まで伸びるのに
  対し Spinel は 20 ms 未満に収まり、25 ms 超えがゼロ。
- **操作感でも差が出る** (ユーザによる実機評価、2026-07-30): Spinel カーネルでは
  「マウス操作時の引っ掛かりがなくなってスムーズになった」。数字と符合する —
  30Hz でフレーム間隔 33 ms に対し 80〜90 ms は**フレーム 2〜3 個ぶんの停止**なので、
  カーソルが飛ぶ・追従が遅れるとして体感に出る。逆に静かな窓での最大値の逆転
  (9 ms 対 3 ms) は 1 フレーム未満なので体感には出ない。**主観評価だが、
  「平均 4 倍」よりも「25 ms 超えがゼロになる」ほうが利用者に届く差**である
  ことをこれが示している。
- **効いているのは山**。アプリ起動とドラッグを含む窓で mruby は 70〜90 ms の詰まりを
  15〜16 回起こし、Spinel は同じ操作を含む窓で 25 ms 超えがゼロ (max 14 ms)。
  Phase 5 が「実機での本命確認」に挙げたのはこの項目。
- **山の原因は未計測**。出るのがアプリ生成・ドラッグと確保が増える場面に限られ、
  静かな窓では 1 件も出ないので、**カーネル側の仮想機械のゴミ集めが有力**という
  見立て止まり。切り分けるには下の「GC 停止時間」の計装が必要。
- 30Hz (33 ms 間隔) に対して平均で見ればどちらも追いつく。**mruby も「追いつかない」
  わけではない**。山が 80 ms = フレーム 2.4 個ぶんなので、その瞬間だけ溜まる。
- 測っているのは経過時間なので、タスクが他へ譲った時間も含む。mruby カーネルは
  仮想機械の中で動き処理の途中で譲るため、山の一部は計算ではなく順番待ちの可能性。

**メモリの比較 (同じ 2 ビルド)**:

| | Spinel カーネル | mruby カーネル |
|---|---|---|
| 内蔵 RAM 空き (desktop のみ) | 82,848 | 101,008 |
| 内蔵 RAM 空き (アプリ 3 本) | 18,004 | 35,948 |
| カーネルのプール使用量 | 96〜136 KB / 512 KB | 171〜180 KB / 512 KB |

**S3 では Spinel のほうが内蔵 RAM を約 18 KB 多く使う** (うち 4 KB はカーネルの
タスク用領域の差 12288 対 16384)。上の P4 の測定 (「内蔵 RAM は実質同等、むしろ
2.5 KB 有利」) と食い違うが、チップも desktop 側のエンジンも違う測定なので
**上書きせず両方を残す**。プール使用量は Spinel のほうが少ないが、ゴミ集めの
起動条件が違うのでそのままの大小比較はできない (下の「live set の公平な比較」)。

同時に読めた実機の数値 (kernel-only Spinel、desktop + ユーザアプリ 3 本):

- Spinel カーネルの VM プール used 96〜136 KB / 512 KB、frag 3〜13% で安定
- 内部 RAM は desktop のみ 82.8 KB → アプリ 3 本で 18.0 KB。**アプリ 1 本あたり
  約 23 KB** 減るので、ユーザアプリ 3 本という上限はスロット数だけでなく
  内部 RAM 側からも妥当

**この計測で言えないこと** (発表などで引用するときの境界):

- 端から端の応答時間 (USB 受信 → 画面に反映) は測っていない。測ったのはカーネルの
  Ruby がイベント 1 件を処理する区間だけ
- 山 (70〜90 ms) の原因。ゴミ集めが有力という見立てまで
- Spinel 側の静かな窓。素の 1 件あたりを同条件で比べるには足りていない
- 操作は手で行っているので件数や速さは厳密に同一ではない。**窓ごとに 1000 件を
  平均しているので平均値は安定するが、`max` は 1 回の当たりで動く**
- S3 の 1 例のみ。P4 では別の結果 (上の起動時間・内部 RAM の行) が出ている

**残りの未計測項目**:

- GC 停止時間の分布 (特に max)。Linux では AOT が有利だったが実機は未確認。
  **mruby の 70〜90 ms の山と、Spinel が静かな窓でも出す数 ms の山 (`ge5=2`、
  max 9 ms) の正体を決められるのはこの計測**なので、未計測項目の中で最優先。
  両エンジンに入れれば「AOT でも山は消えないが桁が違う」を数字で言える
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

**時計とファイル同期は修正済み・実機確認済み (2026-07-30)**。以下は経緯の記録。
直し方は「RTC の読み出しを C に移す」で、`main/kernel/fmrb_rtc.c` の
`fmrb_rtc_sync_system_clock()` を `fmrb_kernel_start()` から 1 回呼ぶ。I2C1 は
`fmrb_hal_i2c_*` (hw_proxy 経由なので時計設定アプリの書き込みと同じ調停に乗る)、
レジスタ読みの手順は picoruby の `I2C#read(addr, len, reg)` から写した。先頭
レジスタだけがチップで違う (RX8900=0x00 / RX8130=0x10)。レジスタ値は Ruby 実装と
同じく **UTC として扱う**。年が 2020〜2099 の外や範囲外の値なら**時計を設定せず
警告を出す** (電池切れの誤った日付を入れないため)。Ruby 側の `sync_rtc` は時刻を
映像側へ渡すだけになり、`#:spinel-strip-*` は削除した = **エンジンによって中身が
変わる箇所が無くなった**。ファイル同期は C 側に既にあった
`fmrb_kernel_get_sync_files` / `fmrb_kernel_sync_file` へシム 3 本
(`fmrb_spx_sync_file_count` / `_entry` / `_sync_file`) を足して繋いだだけ。

実機ログ (`1.0.0-273-g429aaaf-dirty`):

```
I (13:57:57.000) fmrb_rtc: RTC read 2026-07-30 04:57:57 UTC (epoch=1785387477)
I (13:57:57.005) hw_proxy_i2c: Releasing I2C1 ownership (explicit, bus kept)
I (13:57:57.446) spx: File sync: 1 file(s) configured
I (13:57:57.494) spx: File sync [0]: /usr/share/sounds/test.nsf synced
I (13:57:57.500) spx: RTC sync: time sent to host
```

ログの時刻が `09:00:04` から `13:57:57` に切り替わっている (= 時計が設定された)。
エポック値も検算済みで 04:57:57 UTC = 13:57:57 JST。内蔵 RAM の増加は約 180 バイト
(desktop のみで 82,848 → 82,668)。

---

以下、修正前に何が起きていたかの記録。**Linux 前提の省略が実機で穴になる**という
形の失敗なので残す。
mruby カーネルは `RTC sync: 2026/7/30 3:8:16` を出してログの時刻が実時刻に飛ぶが、
**Spinel カーネルは RTC 関連の行を 1 行も出さず、ログの時刻が最後まで
`09:00:0x` のまま** = 1970-01-01 + 時差 9 時間で止まっている。
デスクトップの時計表示も誤ったままになる。

機序は `fmrb_kernel.rb` の `sync_rtc` そのもの:

```ruby
  unless FmrbConst::PLATFORM == "esp32"
    Log.info("RTC sync: skipped (not ESP32)")
    return
  end
  #:spinel-strip-begin
  ...I2C / RX8900 で RTC を読む処理...
  #:spinel-strip-end
```

`#:spinel-strip-*` の間は Spinel 変換時に削除される。コメントは「Spinel カーネルは
Linux 用なので上の判定で必ず戻る」という前提で書かれているが、**実機では
`PLATFORM == "esp32"` なので判定を通過し、その先が空**なので何もせず何も言わずに
終わる。ログが 1 行も出ないのはこのため (「skipped」も出ない)。Linux 前提の省略が
実機に載せた時点で穴になった形。

**同じ性質の穴がファイル同期にもある**。`fmrb_kernel_base_spinel.rb` の
`_get_sync_files` は `[]` を返す実装で、コメントは「実機では C 側が扱う」と
言っているが、実機ログには C 側の同期の記録も無い (mruby カーネルは
`File sync: data/test.nsf up-to-date` を出して実際に同期している)。

**当時の実用上の注意** (解決済み): `.env` の既定が `FMRB_KERNEL_ENGINE=spinel` に
なった直後だったので、この 2 件は「既定の構成で焼くと時計が合わずファイル同期も
行われない」という状態だった。**この構成を実際に焼いて動かすまで誰も気付かなかった**
のが要点で、Linux では両方とも「その経路を通らない」ので症状が出ない。

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
   いったんローカル変数に受けてから否定する形に書き換えて回避 (1 箇所)。
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
