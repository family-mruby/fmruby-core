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

残る仮説は排他な 2 つ:

- **(a) 画像経路**: デコード結果が canvas 2 に書けていない
- **(b) 合成経路**: 画像は書けているが canvas 2 が合成されていない

**まず (a)/(b) を 1 回のビルドで切り分ける**。`draw_background` の画像描画を
一時的に「目立つ色のベタ塗り」に差し替える:

```ruby
# system_desktop.app.rb draw_background 内、create_image/draw_image の代わりに
@bg_gfx.fill_rect(0, 0, @window_width, @window_height, 0x1C)  # 例: 緑
```

- **色が出る** → canvas 2 の合成は正常。**(a) 画像経路**が犯人。T6-1a へ
- **色が出ない** → canvas 2 が合成されていない。**(b) 合成経路**が犯人。T6-1b へ

#### T6-1a: 画像経路を追う場合

`CREATE_IMAGE_FROM_FILE` は寸法を正しく返しているので、
ヘッダの parse までは成功している。疑うのはピクセル実体:

- `display_p4` の PNG デコード先スプライトの色深度 / パレット。
  8bpp (RGB332) canvas に対してデコード結果がどう変換されるか
- `pushSprite` の透過色設定。ソーススプライトに透過色が設定されていて、
  デコード結果が全面それに一致すると全画素スキップされる
- **mruby との差分の探し方**: mruby desktop 構成
  (`FMRB_APP_ENGINE_DESKTOP=mruby`, カーネルは spinel のまま) でビルドし、
  `CREATE_IMAGE_FROM_FILE` / `DRAW_IMAGE` 前後の
  display_p4 側の状態 (スプライトの色深度、パレット有無、先頭数画素) を
  両構成でログに出して突き合わせる。表示側は同一コードなので、
  差が出るならコマンドのパラメータか、その時点の canvas の状態しかない

#### T6-1b: 合成経路を追う場合

- canvas 2 (z=0, 不透明) が PPA blend の合成対象に入っているか。
  `display_p4` の合成ループで z 順に走査している箇所を確認
- canvas 1 (z=254, 透過色 1) が全面不透明として扱われていないか。
  `@gfx.clear(0x01)` 後の canvas 1 の内容と、合成時の
  `use_transparency` / `transparent_color` の実効値をログに出す

**注意**: 合成は `present` 時に走る (描画コマンド単体では画面に出ない)。
`@bg_gfx.present` が canvas 2 を対象に発行されているかも確認対象。

#### 参考: 疑わしいが未確定の構文

`system_desktop.app.rb` の boot アニメーションに
`tx, ty = @boot_tiles[@boot_anim_idx]` がある。これは **poly 配列要素からの
多重代入**で、Phase 4 で「Spinel 不可」と判明した構文
(`due, rest = @_timers.partition{}` を明示ループに書き換えた) と同型である。
ただし直後の `@gfx.clear(0x01)` が canvas 全面を透過色にするため、
これだけでは症状を説明できない。切り分けの過程で boot アニメーション経路を
触るなら併せて確認する。

### T6-2: 例外スタックの計測と縮小 (~65 KB 回収)

**現状**: 内部 DRAM は mruby 比でまだ約 87 KB 多い。その内訳はほぼ
例外/catch スタックで、生成 TU 1 本あたり `sp_exc_*` 20,992 +
`sp_catch_*` 21,312 = 42,304 B、2 本で 84,608 B。
ユーザアプリ 2 つで IRAM free が 31 KB まで落ちるので、
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
**実機では未計測**。mruby 比で以下を取る:

- イベントレイテンシ (入力 → 描画反映)
- GC 停止時間の分布 (特に max)
- ブート時間 (`/app` スキャン 29 件が支配的だった)
- フラッシュ使用量

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
- **headless で再現しない負荷がある**。実マウスのドラッグが流す
  イベントレートは入力注入では出ない。実機でしか出ない不具合を
  headless の結果で否定しない。
- **ノブは Linux にも一律適用する**。dual build に容量差を作ると
  実機まで問題が持ち越される。Phase 5 の例外スタックの件は
  一律適用していたおかげで headless で露見した。
- sdkconfig / sdkconfig.defaults は編集禁止 (提案のみ)。
- Tab5 は DTR/RTS が効かない。書き込み後は物理ボタンでリセット。
