# 設計検討: P4 表示ちらつきの本質と正しい直し方(commit-on-present)

Mic Spectrum + デスクトップ時計の同時描画で残る「数秒に一回・時刻表示に連動」
するちらつきについて、Retro/sim と P4 の合成アーキテクチャを比較した結論。
**本書は検討のみ(実装は別途)**。調査は 2 リポジトリ横断で実施(Retro/sim =
fmruby-graphics-audio、P4 = fmruby-core)。

## 結論(一行)

P4 は**キャンバスの「確定(present 済み)状態」を持たず**、`present` が**グローバル
な全再合成**を誘発するため、別アプリが描画途中のキャンバスを合成が拾う。Retro/sim
は**キャンバスごとに作業用/確定用の二重バッファ**を持ち、合成器は**確定側だけ**を
読むので、誰がいつ present しても描画途中は見えない。**P4 を Retro/sim と同じ
commit-on-present 方式にするのが正しい直し方**。タイムアウト等の heuristic では
mid-draw の隙間(GC・タスク切替・時刻文字列生成)が値を超えれば必ず漏れるので根絶
不可(idle-only 化でも時計更新時に残ったのはこのため)。

## Retro / Linux sim(fmruby-graphics-audio、同一コード)= ちらつかない

- **キャンバス二重バッファ**: `canvas_state_t` が `draw_buffer`(作業用)と
  `render_buffer`(確定用)の両方を持つ(graphics_handler.cpp)。描画コマンドは
  `draw_buffer` に落ちる。
- **present = commit**: PRESENT(PUSH_CANVAS dest=RENDER)は `g_canvas_mutex` を取り、
  `draw_buffer → render_buffer` を pushSprite でコピーして可視化。= copy-on-present。
- **合成器は確定側だけ読む**: `render_frame_internal` は各可視キャンバスの
  `render_buffer` を blend して 1 枚に push。**foreground の `draw_buffer` は読まない**。
- **専用タスクが固定レートで合成**: `graphics_task` が present とは無関係に ~60Hz で
  合成を回す。`g_canvas_mutex` で present の commit と合成が排他。
- 帰結: mid-draw は `draw_buffer` に閉じ、合成は `render_buffer` を読む →
  **構造的に mid-draw を拾えない**(タイミングに依存しない)。

## P4(fmruby-core/display_p4_task.cpp)= ちらつく

- **キャンバスは単バッファ**: `p4_canvas_t` は `LGFX_Sprite *sprite` のみ。アプリの
  描画も合成の読み出しも**同じ sprite**。確定バッファが無い。
- **`render_frame` は毎回全再合成**: `g_framebuffer->clear(0)` → 全可視キャンバスの
  sprite を blend。
- **present はグローバル**: `FMRB_LINK_GFX_PRESENT` は単一の `g_needs_render` を立てる
  だけ。合成は「キューが静穏になったら」発火する heuristic(受信タイムアウト)。
- 単一アプリなら「静穏=全キャンバス完成」が成り立つが、**複数アプリが 1 本の
  コマンドキューを共有**すると、静穏や別アプリの present の瞬間に**別アプリが
  mid-burst**でありうる → その sprite(半描画)を合成 → ちらつき。確定バッファも
  排他も無いので逃げ場が無い。
- 実際の発火: デスクトップは**毎秒メニューバーを再描画**(`system_desktop.app.rb`
  の `draw_menu_bar`: `fill_rect(0,0,幅,13)` で全クリア → 文字/時計描画 → present)。
  この clear→描画の隙間に全再合成が挟まると、メニューバーが下地色だけ = 文字ちらつき。
  同時に走るアプリのバーストを拾えばバーちらつき。= 「時刻表示に連動」。

## 経路(参考)

- アプリの present は `host_task.c` の `GFX_CMD_PRESENT` → link batch → transport で
  各バックエンドへ。P4 は in-process の display_p4_task、Retro は SPI 経由 WROVER、
  sim は socket 経由 linux ビルドの graphics-audio。**同じ GFX サブコマンド列**を
  各バックエンドの dispatcher が解釈する(合成実装は P4 と GA で別物)。

## 正しい直し方(P4 を Retro/sim に寄せる)

1. **キャンバスごとに確定バッファを追加(commit-on-present)**。`p4_canvas_t` に
   `render_sprite`(確定用)を足し、PRESENT で `sprite(作業) → render_sprite(確定)`
   をコピー。`render_frame` は **`render_sprite` だけ**を合成する。→ 描画途中の
   `sprite` は合成に一切影響しない。**これが本丸**。
   - **P4 は単一タスク**(描画コマンド処理も render_frame も display_p4_task)なので、
     GA と違い**排他 mutex は不要**(commit と合成が時間的に重ならない)。実装は GA
     より簡単。
   - **RAM**: キャンバス 1 枚分の追加(WxH×2)。**キャンバスは PSRAM(32MB)なので
     余裕**(内蔵 RAM は消費しない)。全画面 426x240 でも ~200KB/枚。
   - コピーコスト: present ごとに 1 回の sprite コピー(領域限定も可)。
2. **合成は「変化があった時だけ・≤30fps に coalesce」**。present 済みフラグが立った
   時のみ合成し、直近合成から 33ms 未満なら次までまとめる。→ ユーザ指摘の「無更新で
   描かない / 30fps 超で回さない」を満たす。GA は常時 60Hz だが、P4 は dirty 時のみで
   よい(確定バッファがあれば発火タイミングは安全なので、頻度は純粋に効率の話になる)。
3. カーソルのフレーム合成(実装済み)はそのまま活かせる(確定合成の後に焼き込み)。

## 現状の位置づけ

- 実装済みでキープすべき: **カーソルのフレーム合成**(カーソルのちらつき解消・確認済)。
- 部分対策: present 順序化 + idle-only render は「単一アプリ」には効くが、複数アプリの
  本質(確定状態が無い)は解けない。**上記 1 が入れば present 順序化/idle-only の
  小細工は不要になり、撤去できる**(render は確定合成に一本化)。
- 次段の実装は commit-on-present。実装可否・粒度(全画面 present のコピー範囲、
  確定バッファのライフサイクル)は着手時に詰める。
