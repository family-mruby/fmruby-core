# family-mruby-sdk を 3.1.0 の境界に合わせて作り直す

> 状態: 計画済 | 更新: 2026-11-27 | SDK (family-mruby-sdk、0.1.0) の境界の定義は 11/16 のもので、本体 (3.1.0) より関数が 19 少ない (UART 15・描画 3・file_secret_path 1)。file_secret_path をアプリの境界から外したうえで、SDK の境界の定義・4 言語のつなぎ・見本を作り直し、SDK の版を上げる。3.1.1 の後に着手

## 1. 目的

- C・Rust・TinyGo・Zig のアプリから、3.1.0 で足した関数 (UART、描画の 3 つ) を使えるようにする。
- SDK の文書と手順書を、出ている本体 (3.1.x) に合わせる。

## 2. 今の状態 (2026-11-27)

| | SDK (`family-mruby-sdk/boundary/boundary.json`) | 本体 (`fmruby-core/sdk/boundary.json`) |
|---|---|---|
| 関数の数 | 138 | 157 |
| ABI の印 | `84e75e43` | `6c83d35f` |
| 最後の更新 | 2026-11-16 (`c265586`) | 2026-11-26 (`aabc1057`、UART) |

- 本体にあって SDK に無い 19 個: `uart_*` 15 個、`gfx_draw_tile_scaled_raw`・`gfx_picture_info`・`gfx_write_sprite_image`、
  `file_secret_path`。
- 消えた関数・形が変わった関数は無い (`app_set_text_cursor` の説明の文だけが違う)。
- ファームは wasm のアプリが呼ぶ関数を名前で照らし合わせる (`components/fmrb_wamr/fmrb_wasm.c` の `check_imports`)。ABI の
  印の一致は求めない。なので今の SDK で作ったアプリは 3.1.x でも読み込まれる見込み (実機では未確認)。

## 3. 方針 (決定 2026-11-27 ユーザ)

1. **`file_secret_path` はアプリに公開しない**。リモートシェル (Ruby、`flash/usr/share/transfer/rshell.rb` → 今は本体に焼き込んだ
   転送のアプリ) のために足した本体の内側の物。Ruby の側では使い続けるので、Ruby からは呼べて、ほかの言語 (Lua・MicroPython・
   wasm) と SDK には出ない形にする (段 S0 で、境界の生成の道具 `tools/boundary/gen.rb` の印 (`%a{fmrb.boundary}`・
   `%a{fmrb.internal}`) で表せるかを調べる)。外すと本体の ABI の印が変わる。
2. SDK の境界の定義は、本体の `sdk/boundary.json` を写す (手で直さない)。つなぎは SDK の道具 (`tools/gen_bindings.rb`) で作り、
   `tools/check_bindings.rb` で検査する。
3. 各言語の手書きの薄い層に UART を足すかは、S1 で生のつなぎを作ってから決める (足すなら 4 言語とも同じ形)。
4. **古い SDK で作ったアプリとの互換は気にしない** (ユーザ 2026-11-27: wasm のアプリを使っている人は今いない)。互換のための
   確かめや、関数の名前・形を古いまま残す工夫はしない。直した方がよい名前や形があれば、この機会に変えてよい (変えるときは
   本体と SDK を同時に)。

## 4. 段

| 段 | 内容 | 状態 |
|---|---|---|
| S0 | **`file_secret_path` を内側に**: sig の印を変えて、Ruby (mruby・Spinel) からは呼べ、Lua・MicroPython・wasm・SDK には出ない形にする。境界の生成物 (`sdk/boundary.json`・`sdk/c/fmrb_boundary.h`・MicroPython の qstr の表) を作り直し、リモートシェルの試験 (`rake rshell:test`)・`rake regress`・ESP32 のビルドを通す | 未着手 |
| S1 | **SDK の境界とつなぎ**: 本体の `sdk/boundary.json` を SDK へ写し、4 言語のつなぎを作り直す。`check_bindings.rb`・SDK の試験 (`test_version.rb` ほか) を通す | 未着手 |
| S2 | **言語ごとの層と見本**: UART を各言語の薄い層に足すか決める。tour・hello を 4 言語で作り直し、本体の `/app/tinygo`・`/app/rust`・`/app/zig`・`/app/c` の見本も新しい SDK で作り直す (sim・ブラウザ版・P4 で動くこと) | 未着手 |
| S3 | **文書と版**: SDK の README・手順書 (英語、日本語は README.ja.md)・CHANGELOG、SDK の版を上げる (0.1.0 → 0.2.0 の見込み)。サイトの WebAssembly のページ (languages/wasm) の SDK の版の記述 | 未着手 |
| S4 | **公開**: SDK のリポジトリに push してタグ、本体の見本の差し替えは本体の次のリリースに入れる。push・タグはユーザの指示で | 未着手 |

## 5. 受け入れ条件

- S0: Lua・MicroPython・wasm の生成物と SDK の C のヘッダに `file_secret_path` が無い。Ruby のリモートシェルの鍵の守り
  (試験と sim) は今までどおり。
- S1: SDK の `boundary.json` が本体のものと同じ (ABI の印も)。4 言語のつなぎに UART と描画の 3 つが入り、検査が通る。
- S2: 4 言語の tour と hello が sim・ブラウザ版・P4 で動く。
- S3: SDK の文書が新しい関数を載せ、版が上がっている。

## 6. 未確定事項

1. `file_secret_path` を Ruby だけに出す印の付け方 (S0 で調べる)。
2. UART を各言語の薄い層に足すか、生のつなぎだけにするか (S2)。
3. SDK の版の番号 (0.2.0 の見込み)。

## 7. ほかの計画とのつながり

- doc/wasm_apps/plan.md: wasm のアプリと SDK の元の計画。
- doc/app_uart/plan.md: UART (足した 15 個の元)。
- doc/remote_shell/plan.md: `file_secret_path` を足した元。
