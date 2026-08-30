# P2 実装指示: NARYAv4 表示 bring-up (HDMI 800x600)

> 状態: 計画済 | 更新: 2026-08-30 | 別セッション実装用のフェーズ指示。fmruby デスクトップを P4-Nano + LT8912B の HDMI に出すまで

## 最初に読むもの

- doc/naryav4/report/p0.md — **「結論」「追試と切り分けの結果」「環境メモ」
  は必読**。採用モード・使ってよいクロック・LT8912B の癖 (DDS ロックに
  約 20 秒かかる、APLL 不可) の一次情報。
- doc/naryav4/report/p1.md — pin_assign の現状 (I2S 等は先置きで未消費、
  RESTRICTED_USB_DN/DP の名前ずれ、set-target ログの誤読注意)。
- doc/naryav4/plan.md の P2 節。
- doc/reference/ppa_lgfx_notes.md (Tab5 の PPA + LovyanGFX パイプライン)。

## ゴールと受け入れ条件

`FMRB_HW_TARGET=NARYAv4` のビルドを P4-Nano に焼くと、**fmruby の
デスクトップが HDMI モニタに 800x600@60 で安定表示される**。

受け入れ条件:

1. デスクトップが表示され、起動から約 20 秒後 (LT8912B の DDS ロック後)
   以降は乱れなく安定している (最終判定はユーザの目視)。
2. 内部 426x240 を **1.5x で 639x360** に拡大し、黒縁 (左右 80/81px、
   上下 120px) で中央配置。回転なし。
3. ブートで crash マーカー (`Guru|abort`) 0。GFX STATS が周期的に出る。
4. TAB5 ビルドが退行しない (build 成功 + Linux sim の標準スモーク:
   デスクトップ表示 + エディタ起動 1 打鍵)。
5. WiFi / タッチ / 内蔵キーボード / audio は**このフェーズの対象外**。
   NARYAv4 実機でこれらの初期化が失敗ログを出すのは想定内 (P3 で対応)。
   ただし表示検証を妨げる場合の最小ガードは可 (後述)。

## 確定済みの設計パラメータ (P0 で実証済み。変えない)

| 項目 | 値 |
|---|---|
| 解像度 / タイミング | 800x600@60、VESA 標準 (htotal 1056 / vtotal 628、hfp 48 / hs 128 / hbp 88、vfp 1 / vs 4 / vbp 23、両極性 +) |
| DPI クロック | **40MHz、クロック源は PLL_F240M (既定) の整数分周**。APLL は使わない (LT8912B の DDS がロックできない。根拠 report/p0.md) |
| DSI | 2 lane、1000Mbps/lane、`disable_lp = true` |
| 色形式 | DSI フレームバッファは **RGB888** (LT8912B は RGB888 入力のみ)。800x600x3 = 1.44MB (PSRAM) |
| LT8912B I2C | GPIO7=SDA / GPIO8=SCL (P1 の `FMRB_PIN_I2C1_*`)、アドレス 0x48/0x49/0x4A の 3 面。基板に 2.2k プルアップ実装済み |
| 初期化列 | esp-bsp `esp_lcd_lt8912b` 0.2.0 の**純正テーブルのまま** (DDS の種いじりは全て悪化した。撤回記録は report/p0.md) |
| 電源シーケンス | 不要 (Tab5 の PI4IO は存在しない)。リセット GPIO もなし |

参考実装 (動作確認済みの現物): family-mruby/tmp/naryav4_hw/display/
(esp-bsp サンプル + ローカル改変。**telemetry 等の実験コードが混ざって
いる**ので、コピーするなら P0 report の環境メモと突き合わせて純正部分
だけ持つこと)。

## 作業ステップ

### step 0: P1 バイナリのブート観測 (半日以内、report/p1 の残項目)

NARYAv4 ビルド (中身はまだ Tab5 ドライバ) を P4-Nano に焼き、シリアルで
どこまでブートするか・表示初期化 (PI4IO プローブ) がどう失敗するかを
観測して記録する。P2 で直すべき箇所の実地確認になる。

- ポートは /dev/ttyACM1 直指定。`rake check-port` 禁止 (Tab5 を
  リセットする)。`.serial_port` の中身を確認してから `rake flash`、
  または idf.py -p 直叩き。
- P4-Nano のシリアルは開くときに DTR でリセットが掛かることがある
  (毎回ではない)。ブート冒頭が要るときはリセット覚悟で開く。

### step 1: PPA の 1 パス検証 (未確定事項の解消。最初に潰す)

**PPA SRM が「RGB565 入力 → RGB888 出力 + 1.5 倍拡大」を 1 パスで
できるか**を確認する。plan.md 未確定事項に残る最後の技術リスク。

- tmp/naryav4_hw/ 配下にスタンドアロンの小テストを作るのが早い
  (426x240 RGB565 のテストパターン → PPA SRM → 800x600 RGB888 FB →
  既存サンプルの LT8912B 表示に載せると目視まで一気に検証できる)。
- PPA SRM のスケールは 1/16 刻みなので 1.5 は表現可能。確認すべきは
  **色形式変換の同時実行可否**と出力先オフセット (черная縁のための
  部分矩形書き込み。Tab5 も 1278→1280 の中央寄せをしているので前例は
  display_p4_task.cpp にある)。
- 不可だった場合の代替 (どれかを選び report に記録):
  (a) PPA 2 パス (拡大 → 形式変換)、(b) 拡大は PPA・形式変換は
  DMA2D/CPU、(c) blend 段から RGB888 で持つ。性能目標は Tab5 同等
  (GFX STATS で比較)。

### step 2: display_p4_task.cpp のボード分岐

- Tab5 ローカルの定数とコード (`TAB5_I2C_*`、`PI4IO*`、
  `tab5_power_on()`、`tab5_probe_panel()`) を `#if defined(FMRB_HW_TAB5)`
  で囲い、NARYAv4 側に `naryav4_display_init()` を新設:
  I2C マスタバスを `FMRB_PIN_I2C1_SDA/SCL` (7/8) で作り、LT8912B の
  応答確認 (0x48 の ACK かチップ ID 読み) をして、バスハンドルを
  パネル初期化へ渡す。
- I2C バスの所有は Tab5 と同じ「display が作り、後続 (P3 の audio) が
  ハンドルを借りる」構造を保つ (doc/reference/tab5_i2c_bus_notes.md の
  設計ルールに従う)。hw_proxy_i2c.c の unit 1 直列化強制が GPIO31/32
  前提になっていないか確認し、NARYAv4 では 7/8 のバスに同じ扱いを
  適用する。

### step 3: パネル層 (LGFX_Naryav4)

lgfx_tab5.hpp と同じ流儀で LGFX_Naryav4 を作る (Bus_DSI + パネル +
タッチなし、Light なし)。パネルの実装は**着手時に 2 案を比較して
選ぶ** (判断基準つき):

- 案 A: m5gfx v0.2.28 同梱の `Panel_LT8912B` (managed_components 内、
  platforms/esp32p4/)。**採用条件**: 初期化列が esp-bsp 純正テーブルと
  同等 (特に DDS 設定)、RGB888 FB と 800x600 タイミングを渡せる、
  disable_lp 相当があること。満たすなら Tab5 との対称性が最も高い。
- 案 B: 自前の Panel_DSI 派生を書き、初期化列は esp-bsp
  `esp_lcd_lt8912b` 0.2.0 の純正テーブルを移植する (P0 で動いた現物)。
  案 A の中身が別系統の初期化 blob だったらこちらにする。
- どちらでも、**display_p4 が FB ポインタを取得して PPA が直接書く**
  構造 (Tab5 と同じ) が成立することを確認してから進む。lgfx の
  Panel_DSI が RGB888 FB を扱えない場合は、FB とパネルを esp_lcd 側で
  持ち、lgfx はスプライト描画にだけ使う構成も可 (Tab5 でも合成は PPA、
  lgfx は素材描き)。選択と理由を report に書く。

### step 4: 解像度・スケールの差し替え

- DSI FB 寸法 (Tab5 の `DSI_FB_W 720 / DSI_FB_H 1280`) を NARYAv4 では
  800x600 に。**回転なし**なので Tab5 の「3x + 90 度回転」の PPA 設定を
  「1.5x + 回転なし + 中央オフセット」に分岐する。
- 426x240 x1.5 = 639x360。縁は左右 80/81、上下 120。縁は起動時に
  一度黒で塗る。
- `FmrbConst::HW_FAMILY` / MCP sim の期待解像度 (426x240) はそのまま。

### step 5: 表示検証と後始末

1. NARYAv4 ビルド → flash → HDMI 目視 (ユーザ依頼)。**起動直後 20 秒は
   乱れてよい** (DDS 引き込み)。それ以降の安定を見る。
2. 乱れの数値判定が要るときは、LT8912B の DDS 読み出し (0x49 面の
   0x0c-0x0f、ワードが静止すればロック) を一時ログとして使う。手法と
   基準値は report/p0.md。恒久コードには残さない。
3. GFX STATS を Tab5 実測と比較して report に記録。
4. TAB5 ビルド退行 (build) + sim スモーク (rake build:linux は
   clean_all を挟む。sim 検証はルート CLAUDE.md の sim_* ツール。
   エディタ 1 打鍵まで)。
5. boot.c の radio / touch / tab5_keyboard が NARYAv4 で失敗ログを
   吐くのは想定内。**表示検証を妨げる (ハング・リブート) 場合のみ**、
   boot.c での起動を `FMRB_HW_TAB5` 系ガードで飛ばす最小変更を許可する
   (本対応は P3。やったら report に明記)。

## 罠と約束 (P0/P1 からの引き継ぎ)

- **APLL を使わない**。DPI 40MHz は PLL_F240M/6 で正確に出る。
  APLL 160MHz 指定はエラー無しでクロックが死に DSI 初期化でハングする
  地雷 (report/p0.md)。
- DDS の種・0x51 レジスタをいじらない (全滅の記録あり)。
- 720p 系・htotal の長いモードに逃げない (LT8912B のライン測定 16bit
  飽和)。
- lib/ を触ったら `rake clean`、ターゲット/構成切替は `rake clean_all`。
- Tab5 実機がシリアル (ttyACM0) に attach されている場合がある。
  ポートは常に直指定。flash 前に .serial_port を確認。
- コミットは求められたときだけ。コメントは英語。esp_* 直 include は
  実機専用 driver の範囲なので display_p4 内は可 (CLAUDE.md の規約)。

## 書き残し (フェーズ完了時)

- doc/naryav4/report/p2.md (経過・PPA 検証結果・パネル実装の選択理由・
  GFX STATS 実測・踏んだ罠)。
- plan.md の状態行更新、未確定事項の PPA 項を消し込み。
- `rake docs:index`。
