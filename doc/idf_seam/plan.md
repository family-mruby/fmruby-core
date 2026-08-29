# ESP-IDF 依存の継ぎ目整理 (idf_seam)

> 状態: 計画済 | 更新: 2026-08-29 | 共有コードの esp_* 直接依存を fmrb_* の platform 継ぎ目へ吸収し、wasm/stub をカーネル用最小に縮める

2026-08-29 起草。wasm 対応 (doc/wasm/) の P4a が可視化した設計負債を、
独立テーマとして早期に解消する。ユーザ方針: ラッパーの底は core 側の
platform 分岐 (ifdef / ファイル分割) で吸収するのが本来の設計意図であり、
esp_* を装う stub ヘッダは仮設にすぎない。

## 背景 (経緯の要点)

- fmrb の規約 (ログは fmrb_log、malloc は fmrb_mem、戻り値は fmrb_err) は
  守られてきたが、**ラッパーの実装の底**は esp_* に立っていた
  (fmrb_hal_time.c → esp_timer、fmrb_sysinfo.c → esp_mac、
  fmrb_task.c / fmrb_msg.c → esp_heap_caps / esp_log など)。
- Linux sim は「ESP-IDF の linux ターゲット」なので esp_* が常に実在し、
  この寄りかかりは検出されなかった。**posix 側の platform ファイル
  (fmrb_hal_i2c_posix.c 等) までもが esp_log.h を include している**。
- wasm (IDF ゼロの初ターゲット) はこれを wasm/stub/include/ の偽 esp_*
  ヘッダ 10 枚 + esp_stub.c で仮埋めして成立させた (doc/wasm/report/p4a.md)。
  仮埋めの小ささ (10 枚) は規約の成果だが、置き場が設計意図と異なる。

## 目的

1. 共有コード (components/ と、wasm・linux がコンパイルする main/ の範囲)
   から esp_* の直接 include を無くし、fmrb_* の抽象の下の
   platform 実装 (esp32 / posix / wasm) に閉じ込める。
2. その結果として **wasm/stub/include/ を vendored カーネル自身が読む
   4 枚 (sdkconfig.h / esp_assert.h / esp_compiler.h / esp_heap_caps.h の
   カーネル糊) だけに縮める**。stub の枚数が抽象化の完成度の計器になる。

## 方針

- **S1: 全数調査と台帳化。** wasm のソース集合 (report/p4a.md の 316 + 追加)
  に含まれるファイルの esp_* include を全列挙し、ヘッダごとに
  「どの fmrb 抽象へ吸収するか」を確定する。現時点の概観 (stub が代弁):
  esp_log / esp_err / esp_system / esp_timer / esp_random / esp_mac /
  esp_attr / esp_cache (+cache_private) / esp_heap_caps。
- **S2: ラッパーの底の platform 分割。** fmrb_log / fmrb_hal_time /
  fmrb_sysinfo / fmrb_task / fmrb_msg などの実装を、fmrb_hal の既存流儀
  (platform/ ファイル分割を優先、局所は ifdef 可) で esp32 / posix / wasm に
  分ける。posix 実装からも esp_* を剥がす (linux は IDF 上で害は無いが、
  台帳の一貫性のため posix = 素の POSIX とする)。
- **S3: 網の穴の吸収。** 規約外だった直接使用を fmrb 抽象へ:
  - esp_attr → **fmrb_attr.h を新設** (EXT_RAM_BSS_ATTR / IRAM_ATTR 等を
    ESP32 以外では空展開。既存の使用箇所を一括置換)。
  - esp_random → fmrb_hal に乱数 1 関数を追加。
  - esp_system / esp_heap_caps の直接使用 (fmrb_spx_app.c 等) →
    既存の fmrb_sys* / fmrb_mem へ寄せる。
  - esp_err_t が共有コードに漏れている箇所 → fmrb_err.h へ (規約どおり)。
  - esp_cache (display 系) → platform ガードか display_backend 側へ。
- **S4: stub の縮小と規約の明文化。** wasm/stub/include を 4 枚に減らし、
  fmruby-core/CLAUDE.md に 1 行足す: 「esp_* を直接 include してよいのは
  fmrb_hal ほか各抽象の esp32 platform 実装と、実機専用 driver のみ。
  共有コードは fmrb_* 経由」。
- 対象外 (触らない): 実機専用 driver (ble / wifi / flash / partition /
  conn_check 等、wasm・linux がコンパイルしないもの)、platform/esp32/ 配下、
  vendored カーネルとその stub 4 枚。

## スコープ

- 変更はヘッダの付け替えと実装ファイルの分割が主で、**挙動変更ゼロ**を
  原則とする (リファクタと機能変更を混ぜない)。
- wasm/stub の削除は S2/S3 で参照が消えたものから段階的に。

## 受け入れ条件

1. wasm/stub/include/ がカーネル用 4 枚のみになる (esp_stub.c は
   対応する fmrb 抽象の wasm platform 実装へ移動・改名)。
2. 全構成のビルドが通る: S3 (NARYAv3) / TAB5 / linux / wasm (core, core_web)。
3. 既存ターゲットの挙動不変: Linux sim ブート + エディタ 1 打鍵、
   Tab5 ブート回帰 (既定ビルド)。idf.py size-components の差が誤差範囲。
4. wasm: rake wasm:poc 全 PASS、node ブート + デスクトップ描画の回帰。
5. CLAUDE.md への規約追記。

## 未確定事項

- fmrb_attr.h の置き場 (components/fmrb_common/include が有力) と、
  IRAM_ATTR 系を linux/wasm で空にしたときの警告有無。
- posix 実装から esp_log を剥がす際のログ出力先 (printf 直か、
  fmrb_log の posix 実装を1 枚立てるか)。
- esp_idf_version.h / esp_check.h を共有コードが読んでいる箇所の扱い
  (S1 の調査で確定)。
- 着手順: wasm P5 (配信) と独立に進められるが、同一ファイルを触る
  衝突が出た場合はどちらを先行させるか。
