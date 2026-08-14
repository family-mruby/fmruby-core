# 計画書: P4 表示ちらつき (高頻度描画アプリ) — present 順序化 + カーソル合成

Mic Spectrum のような高頻度描画アプリで、(1) ウィンドウ内(バー)と (2) マウス
カーソルがちらつく。前回の r1.md(quick-tap の壁紙ちらつき)と同じ根 —
**単バッファの g_dsi_fb を、レート制限で遅延した render が生スキャン中に更新する**
— が高頻度描画で顕在化したもの。作業ブランチ `feature/mic-spectrum-fft`(実装は
Claude、実機確認はユーザ)。

## 原因(確定)

`display_p4_task` のループ:
```c
while (1) {
    if (g_needs_render && now - last >= RENDER_MIN_INTERVAL_MS) { render_frame(); } // タイマー発火
    receive_cmd(&msg, timeout);   // 1ループ=1コマンド
    process_message(...);         // clear / fill_rect / present …
}
```
- `present` は `g_needs_render=true` を立てるだけ。render は上のタイマー判定で発火。
- **バグ列**: フレーム N の present → g_needs_render=true → 33ms 未満で見送り →
  フレーム N+1 の `clear` を適用 → 次ループで 33ms 経過 → **「N 用の遅延 render」が
  N+1 の clear 後(バー未描画)に発火** → 空フレーム = ちらつき。
  = **タイマー発火が present の順序を壊し、描画途中に割り込む**。
- カーソル: render_frame は PPA で g_dsi_fb を全面上書き(カーソル消える)後、
  別 DMA でカーソルを再パッチ(832-835)。**その隙間**を生スキャンが拾う。
  高頻度 render でこれが毎フレーム起き、ちらつく。

## 方針

**render を present と同じ順序(コマンドストリーム)で消化し、タイマーで割り込ませ
ない。数 msec のフレームレート揺らぎは許容する**(ユーザ方針)。

### A. present 順序化(バーのちらつき)

- `flush_pending_render()` を新設: `g_needs_render` なら render して flag を落とす。
- **`process_gfx_command` の先頭で呼ぶ**。present で立った保留 render は、次コマンド
  (=次フレームの最初の描画 op)を適用する**前**に in-order で消化される。present は
  描画 op の後にキューへ入るので、render は常に完成フレームを合成する。
- 保留 render はフレーム境界(present)でしか立たないので、flush はフレームあたり
  1 回(present 直後の最初の op)だけ発火。同一フレーム内の後続 op では発火しない。
- **タイマー発火 render(ループ 2783-2808)を撤去**。代わりに **受信タイムアウト
  (アイドル)時だけ** trailing フレームを描く(コマンドが来ない=描画途中でない
  ので安全)。保留中は受信タイムアウトを短く(5ms)して trailing の遅延を抑える。
- CLEAR ケースの既存 flush(998-1002)は先頭 flush に包含されるので削除。
- レート上限は「順序を壊す形の skip」をしない。app が自己ペーシング(mic 駆動
  ~30-60fps)なので present ごとに描く。CPU が問題ならフレームドロップは後で追加。

### B. カーソルのフレーム合成(カーソルのちらつき)

- render_frame の **capture 後・scale-out(PPA / software push)前**に、カーソル
  16x16 を g_framebuffer へ焼き込む(透過キー 0xF81F をスキップ)。scale-out が
  カーソル込みのフレームを **1 回の DMA で** g_dsi_fb へ書く → 隙間が消える。
- scale-out 直後に g_framebuffer の 16x16 を**復元**(焼く前の内容を保存しておく)。
  これで g_framebuffer は常にカーソルなし → capture(カーソル無しは現状維持)、
  fast-path の cursor_patch(move 時)、次フレームの合成、すべて従来どおり動く。
- post-PPA パッチ(832-835 の `g_cursor_drawn=false; cursor_overlay_update()`)を
  廃止し、焼き込み後に `g_cursor_drawn=true` と drawn 座標を更新。
- **fast-path(move/visible 変更時の cursor_patch)は温存**。アイドル時のカーソル
  移動で全再合成しないため。焼き込みと fast-path は g_framebuffer が常に復元される
  ので競合しない(拡大は双方 3x nearest で一致)。

## 変更ファイル

- `main/drivers/display_p4/display_p4_task.cpp` のみ。

## 検証

- **コンパイル**: esp32 (TAB5) ビルドで通ること(`file build/fmruby-core.elf` で確認は
  linux 用。P4 は `rake build:esp32`、TAB5 ターゲット)。※ display_p4 は P4 専用で
  linux ビルドには含まれない。
- **実機(ユーザ)**: Mic Spectrum でバー/カーソルのちらつきが消えること、通常の
  デスクトップ操作・カーソル移動・ウィンドウ操作に退行がないこと、体感で重く
  ないこと。r1.md の quick-tap 壁紙ちらつきが再発しないこと。

## リスク / 戻し方

- ブランチ作業なので develop に影響なし。効かない/退行時は revert。
- present ごとに render するので、極端に present 頻度が高い app は render 回数が
  増える(フレームドロップ未実装)。必要なら順序を壊さない形の drop を後追い。
- カーソル焼き込みの座標/クリップ誤りで文字化けの恐れ → cursor_patch と同じクリップ
  ロジックを流用して回避。
