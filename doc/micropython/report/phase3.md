# Phase 3 実装レポート

## 完了条件の判定

| 条件 | 判定 | 検証内容 |
|---|---|---|
| 検証 1-7 がすべて通る | OK | 下記 |
| .rb / .lua / .bas に退行がない | OK | 下記 |
| 生成物の作り直し手順を README に追記 | OK | README「生成物を作り直すタイミング」 |
| prelude のコンパイル時間と GC 消費、後続に回した API を記録 | OK | 下記 |

### 検証 1-7 (headless, Linux シミュレーション)

| 項目 | 結果 |
|---|---|
| 1. ビルドと起動 | 未定義参照ゼロ、警告ゼロ。デスクトップ通常起動 |
| 2. ランチャーから起動 | 右クリック再スキャンで "Python" が一覧に出る (アイコン文字 "P")。ダブルクリックで起動 |
| 3. 描画の確認 | ウィンドウ枠 (タイトルバー・ハンバーガー・タイトル文字・閉じるボタン・角丸枠) と 3 ページの図形/線/文字がすべて描画。Ruby アプリと同じ見た目 |
| 4. ページ切替 | ユーザ領域クリックで Shapes -> Lines -> Text と切り替わる (on_event が効いている) |
| 5. 閉じるボタンで終了・再起動 | `Python script executed successfully` -> 正常終了 -> 再度起動できる (排他も解放されている) |
| 6. lua.app との共存 | 両ウィンドウが同時に描画される (Lua "Running: 20s" と Python の Shapes ページ) |
| 7. _spin 待機中の停止 | on_update が 5000ms を返すアプリで、turn 4 の 1.2 秒後に閉じるボタン -> 同じミリ秒で `on_destroy` が走る。5 秒待たされない |

検証 7 のログ:

```
19:25:19.109 pytest: turn 4
19:25:20.325 Close button on micropython app: request stop PID 5
19:25:20.325 pytest: on_destroy after 4 turns
```

追加で、ビジーループ中の停止 (VM フック経由) も確認した:

```
19:25:41.222 pytest: busy loop start
19:25:42.146 Close button on micropython app: request stop PID 5
19:25:42.146 [Python test] Python script stopped on request
19:25:42.146 [Python test] C cleanup: deleting canvas 5
```

### 退行確認

| アプリ | 結果 |
|---|---|
| `/app/demo/lua.app.lua` | 描画・閉じるボタンとも正常 (Python と同時実行でも) |
| `/app/demo/shapes.app.rb` | 図形デモ正常 |
| `/app/demo/basic.app.bas` | 55 行ロード、BASIC 画面キャンバス生成 |

## 実測値

### prelude (Python 層) のコスト

| 項目 | 値 |
|---|---|
| ソース | 13,409 B (fmrb_gfx.py 3,394 + fmrb_app.py 9,970) |
| 焼き込み後 (fmrb_prelude.h) | 17,051 B |
| コンパイル + 実行時間 | 0-3 ms (Linux シミュレーション) |
| GC ヒープ消費 | 12,672 B (起動直後 192 B -> prelude 後 12,864 B) |

256KB のヒープに対して 12.7KB なので**現時点では frozen bytecode (mpy-cross) を
入れる理由が無い**。0-3ms もアプリ起動の中では埋もれる。ESP32 は CPU が遅い分
時間が伸びるので、phase4 で再計測して判断する。

### タスクスタック

| アプリ | 終了時の最小空き | ピーク使用量 (起動時 123,752 B からの差) |
|---|---|---|
| デモアプリ (python.app.py) | 119,504 B | 約 4,248 B |
| テストアプリ (pytest.app.py) | 119,912 B | 約 3,840 B |

phase2 の素のスクリプト (約 1,376 B) より深い。クラス定義とメソッド呼び出しの
入れ子が増えた分で、prelude の compile が一番深いところを作っている。
**Linux の値なので ESP32 の可否判断には使えない** (phase4 で再計測)。

### コード量

| ファイル | 行数 | 役割 |
|---|---|---|
| modules/fmrb_module.c | 631 | MicroPython 側 (qstr 生成対象) |
| modules/fmrb_bridge.c | 380 | ファーム側 |
| modules/fmrb_mp_bridge.h | 116 | 両者の境界 |
| prelude/fmrb_app.py | 285 | FmrbApp |
| prelude/fmrb_gfx.py | 106 | FmrbGfx |

## 設計上の判断

### C モジュールを 2 ファイルに割った理由

qstr の抽出 (`rake micropython:gen`) は、モジュールのソースを**ホストの gcc で
プリプロセスする**。ESP-IDF のヘッダも FreeRTOS も無い環境なので、
`fmrb_app.h` を include した瞬間に生成が失敗する。

`#ifdef` で逃げる手もあるが、それだと生成時と実ビルドで別の翻訳単位になり、
qstr 表が実際にコンパイルされるコードとずれる余地が残る。そこで:

- `fmrb_module.c` — MicroPython 側。include するのは `py/*.h` と
  `fmrb_hid_msg.h` (stdint のみ) と `fmrb_mp_bridge.h` (fixed-width 整数と
  ポインタだけ) に限る。**これだけが SRC_USERMOD に載る**。
- `fmrb_bridge.c` — ファーム側。ヘッダは自由に使えるが、生成器からは見えない。

`fmrb_module.c` は `fmrb_msg.h` を include できないため、
`FMRB_MAX_MSG_PAYLOAD_SIZE` とメッセージ種別の値を自前で持っている。
これは黙ってずれると「メッセージが切れる」「別のハンドラに配送される」という
出方をするので、`fmrb_bridge.c` 側に `_Static_assert` を置いて
**ずれたらビルドが落ちる**ようにした。

### 描画コマンド送出の重複

`send_gfx_command` (host_task のキューへ gfx_cmd_t を積む処理) は、これで
**4 つ目のコピー**になった:

| 場所 | 用途 | セマフォ |
|---|---|---|
| picoruby-fmrb-app ports/esp32/gfx.c | mruby アプリ | あり |
| main/app/fmrb_spx_gfx.c | Spinel アプリ | あり |
| components/lua/extension/fmrb_lua_gfx.c | Lua アプリ | **なし** (3 回リトライ) |
| components/micropython/modules/fmrb_bridge.c | Python アプリ (今回) | あり |

`fmrb_spx_gfx.c` は中身が理想的な mrb-free C API そのものだが、
main/ にあり、かつ Spinel デスクトップを選んだときしかコンパイルされない
(`sp_net_bin_len` に依存するため無条件化もできない)。コンポーネントから
main/ の関数を呼ぶこと自体は `PRIV_REQUIRES main` で可能で picoruby-esp32 に
前例もあるが、条件付きコンパイルのソースには依存できない。

**将来の整理候補**: 共通のコマンド組み立てを components/fmrb_gfx あたりに
`fmrb_gfx_cmd_*` として括り出し、4 箇所を差し替える。今回やらなかったのは
デスクトップの描画経路に触るため phase3 の範囲を超えるから。Lua だけ
セマフォを使っていない点も同時に直せる。

### prelude をどう読み込ませたか

ファイルシステム import が無いので、prelude は**モジュールではない**。
`rake micropython:prelude` が prelude/*.py を決まった順序 (fmrb_gfx.py ->
fmrb_app.py) で連結し、C の文字列リテラルにしたヘッダを吐く。
`fmrb_mp_start` がユーザスクリプトの前にそれを実行するので、両者は同じ
グローバル名前空間を共有し、ユーザスクリプトからは `FmrbApp` が
トップレベルに見える (Ruby アプリと同じ見え方)。

生成ヘッダは 1 行 1 リテラルにしてある。Python を 1 行直したら差分も 1 行になり、
生成物のレビューが成立する。

## 実装中の気づき

### 1. 強制停止 (VM abort) では on_destroy が走らない [仕様として記録]

ビジーループ中に停止を要求すると、VM フックが `mp_sched_vm_abort()` を呼び、
`exec_source` の nlr バッファまで一気に巻き戻る。**Python の `destroy()` も
`on_destroy()` も通らない**。

キャンバスとメッセージキューは C 側の cleanup が回収するので資源は漏れないが
(ログの `C cleanup: deleting canvas 5` で確認)、**アプリは on_destroy で後始末を
する前提のコードを書けない**。Lua も同じ性質なので新しい制約ではない。
_spin 待機中の停止 (こちらが通常経路) では `destroy()` まで正しく走る。

### 2. ランチャーはキャッシュを信じるので新しい .py は右クリック再スキャンが要る

`/data/launcher_index` はブート時に無条件で信用される。SCRIPT_EXTS に "py" を
足しただけでは、既存のキャッシュを持つ環境では Python アプリが一覧に出ない。
ランチャー上で右クリックすると再スキャンしてキャッシュを書き直す
(`Launcher: rescan rebuilt instances (35 -> 37)`)。

### 3. GfxBlock を使わないぶん枠の描画コマンドが増えている

Ruby 版の `draw_window_frame` は GfxBlock で枠一式を 1 コマンドに畳んでいるが、
Python 版はプリミティブごとに 1 コマンド送る (枠 1 回で 8 + 角丸クリア 8 の
16 コマンド)。枠を描き直す頻度は低いので実用上の問題は出ていないが、
resizable なアプリを作ると毎 redraw で効いてくる。GfxBlock 対応は後続。

### 4. 角丸の外側ピクセルは自前で塗り直している

Ruby 版は composite region を使って角の 4 マスだけ透過比較させる最適化を
入れているが、Python 版は入れていない (フルエリアの透過 push のまま)。
見た目は同じで、GA 側の比較負荷が高いだけ。ただし**角の外側 3px を透過色
(0x01) で塗り直す処理は必要**で、これを省くと `clear()` の後に角が丸く
見えなくなる。`_clear_corners` として draw_window_frame の末尾に入れてある。

### 5. デスクトップのウィンドウ座標の目安 (検証を書く人向け)

ランチャーは `@launcher_x = 8`, `@launcher_y = 21`, 300x190。
スクロールバーの列は x 297..307 で、**x >= 308 をクリックするとランチャーの
外側なので閉じてしまう**。ドロップダウンは `DROPDOWN_Y = 13`,
`DROPDOWN_ITEM_H = 12` なので n 番目の中心は y = 19 + 12n。

## 後続フェーズに回した API (計画どおり、実装しない)

| 分類 | 内容 |
|---|---|
| スプライト | SpriteImage / SpriteInstance / タイルマップ / TileRing |
| 画像 | create_image / load_image / draw_image / BMP / マスク |
| 文字 | set_font (日本語含む) / set_text_size / hybrid 描画 / text_width の多バイト対応 |
| 描画最適化 | GfxBlock / composite region / viewport (ハードウェアスクロール) |
| ライフサイクル | on_suspend / on_resume の呼び出し (フラグは持っている) / on_resize / reload |
| 連携 | pub-sub (subscribe / publish) / file_select / request_run |
| その他 | 音声 / arc / blend_rect / get_pixel / 追加キャンバス / スクロールバー / p5 互換層 |

これらは「無いものは AttributeError で落ちる」方針にしてある
(存在だけ作って無言で何もしない、はしない)。
