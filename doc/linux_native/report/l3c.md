# L3c 報告: パッケージ版の決まり (debugd と配布用の設定)

> 状態: 完了 (受け入れ 4 点) | 更新: 2026-10-26 | 指示書は instruction_l3c.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

## 項目名

system_conf の 2 つの項目を足した。読むのは `main/drivers/debug/fmrb_debugd.c` だけで、
Linux の TCP の受け口を持つビルド (sim と native、`CONFIG_IDF_TARGET_LINUX`) に限る。
端末 (BLE) とブラウザ版 (debugd なし) のコードは変わらない。

| 項目 | 意味 | 既定 (sim・開発用 native) | 既定 (パッケージ版) |
|---|---|---|---|
| `debugd` | debugd を起動するか | `true` | `false` |
| `debugd_bind` | 待ち受ける IPv4 の番地 | `"0.0.0.0"` (全部の口) | `"127.0.0.1"` |

- パッケージ版 (`NATIVE_PACKAGE=ON`、`FMRB_NATIVE_PACKAGE`) は `debugd_bind` に
  127.0.0.1 以外を書いても使わない (警告をログに出して 127.0.0.1 で待つ)。
  ユーザの決定「有効にしたときも 127.0.0.1 だけ」をコードで守る。外からは ssh の転送で使う。
- 番地として読めない値は debugd を起動しない (理由をログに出す)。
- 止めたときは `debugd: debugd off (system_conf debugd = false)` を出す。
  待ち受けのログは `dbg_tcp: listening on <番地>:<port>`。
- 待ち受けの番地は `fmrb_debug_transport_tcp_set_bind()` (fmrb_debug_transport.h) で
  TCP の受け口に渡す。受け口が自分で設定を読むことはしない。

起動の表示: `main/boot/boot.c` の起動のログの 1 行を、パッケージ版だけ
`Running as a native Linux program (installed package)` にした (開発用は
`... - Development mode` のまま)。指示書の触ってよい範囲 (`main/drivers/debug/`) の
外だが、受け入れ 2 の「Development mode が出ない」はこの 1 行のことで、
`FMRB_NATIVE_PACKAGE` の分岐だけなので他のビルドの出力は変わらない。

## 配布用の system_conf

`config/system_conf_native_release.toml` (正は config/)。`rake native:root` が配布物の木の
`etc/system_conf.toml` と `etc/system_conf.factory.toml` をこれで置き換える
(開発用の `native:etc` が置く `native/build/root/etc` は開発用のまま)。

開発用 (`config/system_conf_native.toml`) との差:

| 項目 | 開発用 | 配布用 |
|---|---|---|
| `debug_mode` | `true` | `false` (周期の GFX STATS などのログを出さない) |
| `zenoh_router` | `"tcp/127.0.0.1:7447"` | `""` (利用者が設定する) |
| `debugd` | (書かない = 既定の true) | `false` |
| 先頭と zenoh_router の説明のコメント | 開発用の説明 | 配布用の説明 |

- 2 つが離れないように、`native:root` が「上の 3 項目とコメントを除いた設定の行が
  一致すること」を確かめ、違えば止まる (`native_check_release_conf!`、
  `NATIVE_RELEASE_CONF_KEYS`)。max_apps を 1 つだけ変えた写しで止まることを確かめた。
- `/etc` の更新の規則 (L3a) はそのまま。パッケージ版の種が配布用になったので、
  古い利用者の `/etc` には `debugd = false` が足される (下の deb_check の 6 で
  `system_conf.toml: added debugd` を確認)。既に入れた利用者の `debug_mode = true` などは
  変えない (指示書どおり直さない)。

## zenoh_router が空のとき

Asterism のアプリは `FmrbAsterism.settings` で、config.toml・system_conf の
`zenoh_router`・資格情報の順に探し、どれも無ければ組み込みの `tcp/zenohd:7447`
(sim の compose の名前) に落ちる。開発用の native で `zenoh_router = ""` にして
`/app/test/zenoh_nodes.app.rb` を起動すると、11 ms で
`linux failed: cannot open a session to tcp/zenohd:7447 (-128)` が画面とログに出て、
アプリもデスクトップも固まらない (キーも通る)。パッケージ版では `zenohd` という名前が
引けないので必ずこの形になる。分かるエラーにはなっているが、落ちる先の名前が sim の
ものなのは L5 以降で見直す候補 (残件)。

## 受け入れ条件

| 条件 | 結果 |
|---|---|
| 1. パッケージ版で debugd が止まっている・有効時は 127.0.0.1 だけ | `rake native:deb ARCH=arm64 && ruby test/native/deb/deb_check.rb --arch arm64` で 13/13。arm64 (qemu) のコンテナの `/proc/net/tcp` (base のイメージには ss が無い) で、既定では待ち受けが 0 個、ログに `debugd off`。利用者の system_conf を `debugd = true` にして起動し直すと、待ち受けは `127.0.0.1:5565` の 1 つだけ。コンテナの中から 127.0.0.1:5565 には繋がり、コンテナ自身の番地 (172.17.0.2:5565) には中からも外 (WSL2 の host) からも繋がらない (中からは connection refused、host からは timeout) |
| 2. Development mode が出ず debug_mode は false | 同じ確認の `release settings`: 起動のログは `Running as a native Linux program (installed package)`、利用者の system_conf は `debug_mode = false`・`zenoh_router = ""`・`debugd = false` |
| 3. 開発用の native は今どおり | `ruby tools/fmrb_native.rb up` で `dbg_tcp: listening on 0.0.0.0:5565`、`fmrb_native.rb app ps` (MCP の `native_app` の中身) が kernel・desktop・Services を返す。`ss -ltn` でも `0.0.0.0:5565` |
| 4. 回帰 | `rake regress`: host 19/19、描画の記録 205/205。`rake regress:screens`: **26 passed / 0 failed** (retro 8/8、modern 10/10、web 6/6、native 2/2、9.4 分)。基準は更新していない。build/ は TAB5 (esp32p4) に戻った |

deb_check.rb には 2 つの確認 (`release settings`、`debugd off by default`) と最後の手順 7
(`debugd on: loopback only`) を足した。debugd を使って確かめていた所は元から無かった。

## 踏んだ罠

- 1 回目の regress:screens で sim (retro・modern) のビルドが
  `-Werror=address` (文字列の literal との比較) で落ちた。native の gcc は警告も
  出さなかった。既定の番地を `static const char[]` にして、その番地と比べる形に直した。
  パッケージはこの修正の後で作り直し、deb_check を流し直した。

## 残件

- Asterism の既定の行き先 (`tcp/zenohd:7447`) はパッケージ版では引けない名前。
- debugd を外から使う手順 (ssh の `-L 5565:127.0.0.1:5565`) の案内は L3b・L5 の実機の文書で書く。
