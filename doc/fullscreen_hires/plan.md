# 全画面の高解像度モード (P4 系とブラウザ版)

> 状態: 進行中 | 更新: 2026-10-16 | **H0-H4 完了・ユーザの目視で合格 (NARYAv4 とブラウザ版)**。H5 で Modern の Linux sim も対象にした (report/h5_sim.md)。全画面の高解像度の自動切り替え、入力の追従、エディタのフォント 8/12/16、遠隔デスクトップの追従、表示側の守り。残りは Tab5 の実機確認のみ。切り替えの瞬間の一瞬の乱れは保留 (report/h1.md 6 章)

## 目的

Modern (P4) を「作る機械」にするため、エディタを全画面で広く使えるようにする。
今の全画面は内部 426x240 (3 倍で 1278x720) で、エディタの 12px の日本語
フォントでは 35 桁前後しか入らない。**全画面のときだけ**内部解像度を上げる。

## 方針 (ユーザ決定、2026-09-27)

- **対象は P4 系 (Tab5 / NARYAv4) とブラウザ版 (wasm)**。Retro (S3 +
  WROVER) は対象外。Linux の sim は graphics-audio 側で描くので、H0-H4 では
  対象外だった。**H5 (ユーザ決定、2026-10-16) で Modern の sim も対象にした**:
  graphics-audio の Linux ビルドだけに SET_SCREEN_MODE を入れる (WROVER の
  バイナリは変えない)。Retro の sim は従来どおり。
- **高解像度にするかはアプリが .app.toml で選ぶ**。組み込みアプリは spawner の
  表で同じ属性を持つ。
- **エディタは P4 系で既定オン**。
- **エディタのフォントは選べる** (既存のフォントから。選んだ値は /home に残す)。
- 内蔵 RAM を増やさない (ユーザの方針。高解像度の canvas は PSRAM)。

## 仕組みの考え方

- 今の表示は、426x240 の内部フレームバッファにデスクトップと各 canvas を
  PPA Blend で重ね、最後に PPA SRM で 3 倍して DSI のフレームバッファへ送る。
- エディタ本気モードの計画 (editor_serious_mode/plan.md 4 章) が
  「アプリ単位の解像度は採らない」としたのは、PPA Blend が拡大縮小できず、
  高解像度の canvas を 426x240 のデスクトップに**混ぜられない**ため。
- 全画面のときは他のアプリが止まり、デスクトップも見えないので**混ぜる必要が
  ない**。高解像度モードの間は重ね合わせを飛ばし、全画面アプリの canvas 1 枚を
  **2 倍で SRM して** DSI のフレームバッファへ送る。
- 640x360 x 2 = 1280x720 で、NARYAv4 (1280x720) と Tab5 (720x1280 を回転) に
  ちょうど合う。canvas は 640x360 RGB565 = 460,800 バイト (PSRAM)。

## 確定した設計 (H0 の推奨をユーザが採用、2026-09-28。詳細は report/h0.md 8 章)

1. **入り口と出口はカーネルが決める**。アプリは属性で「高解像度にしてよい」と
   宣言するだけで、実行中に要求する口は作らない。全画面の積み (enter /
   pop / park / unpark) が変わるたびに `sync_screen_mode` 1 か所で判定する
   (一番上の park 中でない app が属性を持ち、構成が対応していれば高解像度)。
   F11 で入るときの大きさは pid 別に返す。
2. **表示へは新しい GFX コマンド `SET_SCREEN_MODE{owner_canvas_id, w, h, flags}`**。
   表示側は、持ち主の canvas が新しい大きさで present するまで前の絵を保つ
   (500ms 程度で打ち切り)。高解像度中に持ち主の canvas が消えるか隠れたら、
   表示側が自分で通常へ戻す。戻るときは DSI の余白を黒で塗り直す。
   カーネルは、入る向きは他を隠すより先に、戻る向きは後に送る。
3. **確保**: 640x360 の fb を別に 1 枚、最初の使用時に取る。倍率は定数を
   やめ、fb の大きさから導く (3 か 2)。canvas は大きい要求で取り直し、縮めない。
   PSRAM は約 2MB 増、内蔵 RAM は増えない。
4. **キー名**: .app.toml `fullscreen_hires = true`、表・attr・ctx は
   `.fullscreen_hires`。組み込みの editor と editor_fs に付ける (P4 で既定オン)。
   構成の判定は C の 1 関数 (`FMRB_HW_MODERN || FMRB_PLATFORM_WASM` かつ実行時の
   基本の大きさが 426x240。`HW_FAMILY` は sim でも modern なので単独では使わない。
   H5 で「Linux かつ `FMRB_HW_FAMILY_MODERN` かつ graphics-audio が INIT_DISPLAY の
   ACK で SET_SCREEN_MODE を知ると伝えた」を足した)。
   カーネル Ruby へは `_fullscreen_hires_size` (nil か [640,360])。mruby 版と
   Spinel 版の両方。
5. **入力**: 「いまの画面の大きさ」を host_task に 1 つ置き、touch / usb /
   rd_input が読む。**マウスの速さは倍率で補正し、画面上の速さを今と同じに
   保つ**。カーソルが画面上 48px から 32px に小さくなるのは許容 (必要なら後で)。
6. **遠隔の画面取得**: 取り込みは最大の大きさで取り、1 枚ごとに幅と高さを
   記録。静止画 (JPEG、tab5_screenshot) は H1、動画 (H.264) とページは H4。
7. **エディタのフォント**: 今ある **ja12 (既定) / ja16 / misaki 8** の 3 つから
   選ぶ (新しいフォントは足さない。Tab5 の flash が逼迫)。窓と全画面で共通。
   保存先は **/home/editor.toml**。LINE_H / CELL_W / EDIT_FONT_SIZE を変数にする。

## 段階

| 段階 | 内容 | 状態 |
|---|---|---|
| H0 | 調査と設計の確定 | **完了** (report/h0.md) |
| H1 | 表示: 640x360 の fb の差し替え、倍率の導出、canvas の取り直し、SET_SCREEN_MODE、カーソル、静止画の取り込み・JPEG・EXPORT、wasm の C 側。試験用にアプリから SET_SCREEN_MODE を直接出す口 | **完了** (report/h1.md)。切り替えの瞬間の一瞬の乱れは保留 (6 章、2 枚方式の差分は report/h1_dualbuffer.patch) |
| H2 | カーネル・アプリ・入力: 属性 (mruby / Spinel)、構成の判定、sync_screen_mode、fullscreen_size(pid)、入力の大きさとマウスの速さの補正 | **完了** (report/h2.md、ユーザの操作確認で合格) |
| H3 | エディタ: P4 で既定オン、フォントの選択と /home/editor.toml への保存、LINE_H / CELL_W の変数化 | **完了** (report/h3.md、フォントとドラッグの直しはユーザの目視で合格) |
| H4 | 残り: 表示側の守り (h3.md 9.5 の固まった件)、遠隔デスクトップ (remote.js) と H.264、ブラウザのページ、MCP の道具、試験用の口を外す、文書 | **実装完了・目視待ち** (report/h4.md)。画面が止まる穴を 1 つ直した。h3.md 9.5 の板ごと固まった件は再現せず原因不明 |

| H5 | Modern の Linux sim: graphics-audio (Linux) に SET_SCREEN_MODE、SHM の layout 2 (640x360 の枠と枠ごとの大きさ)、sdl2-display の追従、能力を INIT_DISPLAY の ACK で伝える、canvas の確保量を超える要求の切り詰め、BMP の上限 (Linux)、道具 | **完了** (report/h5_sim.md、sim の確認まで。GUI の見た目はユーザの目視) |

段階ごとに instruction_hN.md を書き、report/hN.md に結果を残す (H5 は指示書なし、report/h5_sim.md)。

H2 の判断 (ユーザ、2026-09-28): Ctrl+Tab で park するときの順番は変えない。試験用の口 (`_dev_screen_mode`) は H4 で外す。`.app.toml` は `fullscreen_hires = true` を正式とし、整数 1 も受ける。

## 受け入れ条件

- P4 系 (Tab5 と NARYAv4 の実機) で、エディタが全画面で 640x360 相当の
  広さで表示され、打鍵・スクロール・メニュー・F11 での窓との往復・Ctrl+Tab の
  切り替えで崩れない (見た目はユーザの目視で確認)。
- 全画面を抜けたとき、デスクトップと他のアプリが従来どおり表示される。
- .app.toml でオフにしたアプリは、従来どおりの全画面。
- ブラウザ版で同じ動作。
- エディタのフォントを選べ、再起動後も残る。
- 内蔵 RAM (静的な D/IRAM と待機時の空き) が増えない。Tab5 と NARYAv4 と
  S3 のビルドが通る (S3 は機能なしで従来どおり)。
- Modern の sim でも、属性を持つアプリの全画面が 640x360 になり、抜けると
  426x240 に戻る (H5 で変更、ユーザ決定 2026-10-16。元は「sim は従来どおり
  動く (高解像度にはならない)」)。Retro の sim は従来どおり 320x240。
  WROVER のビルドのサイズは変わらない。

## 未確定事項

- 高解像度モードの入り口 (カーネルが全画面に入れるときに決めるか、アプリが
  要求するか) と、表示タスクへの伝え方 (GFX のコマンドか、既存の INIT_DISPLAY
  の延長か)。H0 で決める。
- 640x360 以外の大きさ (たとえば 1280x720 等倍) を将来選べるようにするか。
  今回は 640x360 だけ。
- マウスカーソルと入力座標: 426x240 前提の箇所の洗い出し (H0)。
- エディタで選べるフォントの候補 (6x8、misaki 8x8、12px、efont 16px など、
  display_local と wasm に実際にあるもの)。
- ちらつき対策で入れた DSI フレームバッファの 4KB 揃え (reference/dpi_frame_buffer_alignment.md)
  との関係: 高解像度でも走査の帯域は同じ (DSI 側は 1280x720 のまま) なので
  影響しない見込み。H1 で確かめる。

H4 の判断 (ユーザ、2026-09-28): report/h3.md 9.5 の「板ごと固まった」件は、見つかった穴 (画面が止まる件) を直したうえで原因不明のまま閉じる (次に起きたときの手順は report/h4.md 10 章)。表示のドライバから host_task の `fmrb_host_set_screen_size` を呼ぶ依存と、H.264 の見出しの 14 バイト化は、この形でよい。
