# Phase B4 報告: 周辺・体験 (互換レベル L4)

対象指示書: `phase_b4.md`。ホストゴールデンは `rake basic:test`、
実機経路は Linux シミュレーション (headless、mruby kernel / mruby desktop、
`keyboard_layout = "jp"`) で確認。

## 1. T4-1: 直接モードコンソールの要否 (ユーザ判断: 不要)

**結論: 直接モードコンソールは作らない (2026-07-28 ユーザ判断)。**
判断の言葉は「エディタからの RUN で十分だと思う」。

提示した材料:

- B3.5 で「編集 -> F5 -> 実行 -> Ctrl-Q -> 編集」のループが、クリックなしで
  回るようになった (B3.5 report sec 7、本フェーズ T4-7 で Ctrl-Q が入り
  fullscreen からも戻れるようになった)。
- 直接モードのコストは「画面エディタ + LIST/AUTO/RENUM/DELETE/FIND +
  CONT の対話利用 + エディタとインタプリタの状態共有」で、B1-B3 の
  どのタスクより大きい。得られるのは主に「実機と同じ操作感」。
- ファイルの読み書きは T4-4 の LOAD / SAVE で BASIC 側からも可能になった。

したがって T4-6 は実施していない。行エディット系コマンド
(LIST / AUTO / DELETE / RENUM / FIND) と GAME は、プログラム中に現れたら
**IL エラー + ログ 1 行 `DIRECT|command is direct mode only`** で落とす
(golden `430_direct_mode_only`)。エラー表に「直接モード専用」コードは
無いので IL を借り、理由はログに出す方式にした。

## 2. 変更ファイル

| ファイル | 内容 |
|---|---|
| `core/basic_core.hpp` / `.cpp` | ON ERROR 状態、ERR/ERL 保持、TRON、仮想メモリ、第2 BG 面、入力待ちマーカーの状態と run ループ側の分岐 |
| `core/basic_exec.cpp` | ON ERROR GOTO / RESUME / ERROR、POKE、SCREEN / FILTER / BGGET / BGPUT / VIEW / BACKUP / TRON / TROFF、SAVE と無視される LOAD / LOAD?、直接モード専用コマンド、入力待ちマーカー |
| `core/basic_expr.cpp` | PEEK (仮想メモリ)、ERL / ERR、INKEY$(0) の入力待ちマーカー |
| `fmrb_basic.cpp` | SAVE のホスト実装 (`/home/<名前>.bas`) |
| `test/runner/main.cpp` | SAVE のホスト実装 (`BASIC_PROGRAM_DIR`)、INWAIT マーカーは golden に出さない |
| `test/run_golden.sh` | ケースごとに SAVE の書き込み先を用意 |
| `main/prebuild_scripts/kernel/fmrb_kernel.rb` | Ctrl-Q (system_interrupt) で HID target アプリを停止 |
| `main/app/fmrb_app_spawner.c` | `.toml` が無いファイルの名前をファイル名から補う。組み込みアプリの spawn も kernel へ通知 (フォーカス) |
| `tools/fmrb_input.py` | `keyboard_layout` に追従した文字→キー変換 (`--layout` で上書き) |
| `tools/basic_screen_check.py` | `.keys` ケースを sim で実行 (キー注入) |
| `doc/fmrb_basic/virtual_memory_map.md` | POKE / PEEK 対応表 (新規) |
| `test/golden/4xx` | B4 のゴールデン 16 本 |

## 3. T4-2: 仮想 POKE / PEEK

対応表は **`doc/fmrb_basic/virtual_memory_map.md`** に独立文書として起こした
(受け入れ基準 2)。要点:

- `$0000-$07FF` と `$6000-$7FFF` は素の RAM。触られたときだけ確保する
  (使わない作品のコストは 0)。`$7000-$703F` は spec が POKE 禁止としている
  ので書き込みだけ無視。
- `$D000` からの BG 画面はテキストのシャドウバッファへマップ。POKE は
  描画経路 (`screen_set_cell`) を通るので画面にも出る。golden
  `411_poke_screen` が画面ダンプで確認している。
- 未対応アドレスは PEEK=0 / POKE 無視。警告は**同一 256 バイトページに 1 回**
  (`POKEW|` / `PEEKW|` を fmrb_log 経由、BASIC 画面には出さない)。

対象作品への影響: 現在のコーパス (自作サンプル 6 本) は POKE を使わない。
ターゲット作品が入ったら、非対応アドレスへの POKE はログに出るので、
`POKEW|` を拾えば互換状況を機械的に洗い出せる。

## 4. T4-3: ON ERROR / RESUME / ERROR / ERL / ERR

- `raise()` に分岐を足した: ON ERROR GOTO が有効でハンドラ内でなければ、
  **報告せず** `error_pending_handler_` を立てて false を返す。run ループが
  「文が巻き戻ったあと」にハンドラ行へ飛ぶ。式評価の途中から直接ジャンプ
  しないのは、呼び出し側が中途半端な値で処理を続けてしまうため。
- ハンドラに入った時点で run 状態のエラーは消す (ハンドラ内の END が
  正常終了になる)。ERR / ERL は別に保持するので RESUME 後も読める。
- `RESUME` = エラー行から再実行、`RESUME NEXT` = 次の行、`RESUME 行番号` =
  指定行 (v3_spec)。ハンドラ外の RESUME は RE エラー。
- `ERROR n` は n 番のエラーを発生させる。範囲外は IL。
- ハンドラ行が存在しない場合は、元のエラーではなく UL を報告する
  (ハンドラの書き間違いが見えるほうが有益)。

golden `400`-`405`。

## 5. T4-4: LOAD / SAVE / LOAD?

- ファイル名規則: **1-16 文字、パス区切り不可**。ホスト側 (`fmrb_basic.cpp`)
  が `/home/<名前>.bas` へマップする (`.bas` は付いていなければ補う)。
  ホストランナーは `BASIC_PROGRAM_DIR/<名前>.bas`。
- `SAVE` は LIST 形式のテキストを書く。実機経路で確認済み:
  `SAVE "DEVTEST"` -> `/home/DEVTEST.bas` (114 バイト)。
- **LOAD / LOAD? は何もしない (2026-07-28 ユーザ判断)**。理由: これらは
  本来「インタプリタのプロンプトで打つ」直接モードのコマンドであり、
  プログラム中に書くものではない。直接モードを作らない (T4-1) 以上、
  プログラム中の LOAD に自然な意味は無い。
  - 実装: 名前の operand は解析する (書式誤りは SN エラーのまま) が、
    実行はせず `DIRECT|LOAD ignored (direct mode command)` をログに出す。
  - **SAVE は実装したまま**。プログラムを書き出す動作は直接モードが無くても
    そのまま役に立つため。
  - 当初は「差し替えて 1 行目から続行」(チェーンロード) にしていたが、
    上記判断により撤回した。ホストインタフェースの `program_read` も
    使われなくなったので削除した (読み込み経路は残していない)。

golden `440` (SAVE + 無視される LOAD?) と `441_load_ignored`。

## 6. T4-5: 画面系

| 命令 | 実装 |
|---|---|
| `SCREEN 表示面[,アクティブ面]` | 第 2 BG 面を実装 (28x24 の文字+属性をもう 1 面、最初の切り替えで確保)。アクティブ面が PRINT の書き込み先、表示面切り替えで全面再描画 |
| `BGGET` / `BGPUT` | BG 面のスナップショット退避・復元。スナップショット無しの BGPUT は NB エラー。退避先は専用バッファ (実機はユーザー RAM だが、そこは POKE 用に見せているので衝突させない) |
| `BACKUP` | 何もしない (電池バックアップ相当。`/home` が永続なので不要) |
| `VIEW` | 何もしない (BG GRAPHIC 面は BGTOOL 前提で対象外なので、その面に絵が入ることがない)。CLS の代わりに VIEW を使う作品がそのまま走ることを優先 |
| `FILTER 色` | 値の検証 (0-7) と保持のみ。描画側に着色段が無いので画面は変わらない (疑義 #30) |
| `TRON` / `TROFF` | 行が変わるたび `*行番号` を表示。ジャンプで同じ行に戻ったときも出す |

golden `420`-`423`、`405`。

## 7. T4-7: 予約キー Ctrl-Q

Ctrl-Q の横取り自体は既にあった (`host_task.c` がルーティング前に拾い、
kernel へ `system_interrupt` を送る。アプリには配送しない)。従来の処理は
「fullscreen を抜ける」だけで、アプリは走り続けていた。

- kernel の `system_interrupt` を拡張し、**HID target のアプリへ
  APP_CONTROL `{"cmd":"stop"}`** を送るようにした。対象は `run` と同じ基準
  (`/app` / `/home` 配下から起動したアプリ)。組み込みアプリ (エディタ等) は
  除外し、従来どおり fullscreen 解除にとどめる (エディタは未保存確認を
  自前で持っているので、無条件 stop で潰してはいけない)。
- 停止後のフォーカスは B3.5 の返却機構に任せる。

検証 (headless): エディタで新規プログラムを打ち -> F5 で fullscreen 実行 ->
`key ctrl+q` -> `Ctrl-Q: stopping pid=5` -> アプリ終了 ->
`HID target back to pid=4 (app 5 terminated)` -> **クリックなしで F5** ->
再実行。これを 2 周確認 (受け入れ基準 5)。

### 副産物: `.toml` の無いファイルが起動できなかった

`SAVE` やエディタで `/home` に作ったプログラムを RUN すると、spawner が
`.toml` の `app_screen_name` が無いと言って拒否していた。ユーザが今書いた
ファイルを走らせられないのは RUN の穴なので、**`.toml` が無ければファイル名を
アプリ名として使う**ようにした (他の項目は既に既定値がある)。

### 副産物: 組み込みアプリの spawn がフォーカスを取っていなかった

B3.5 で足した「C から spawn したら kernel へ通知」は
ファイル起動アプリ (`spawn_user_app`) だけで、組み込みアプリ
(`default/editor` 等) は通知していなかった。debugd からエディタを起動すると
キーがデスクトップに行ってしまう (デスクトップのショートカットが誤反応して
別アプリが開く) ので、組み込み側にも通知を足した。システムアプリ
(デスクトップ) は除外。

## 7.5 追加要望: fullscreen の .bas は全面黒で起動する (ユーザ指示)

fullscreen の .bas アプリはテキスト面 224x192 のキャンバスを画面中央に
出していたため、周囲にデスクトップの壁紙 (家や雲) が残って額縁のように
見えていた。**fullscreen のときはキャンバスをウィンドウ全面にし、テキスト面を
その中央に置く**ことで、周囲を黒く塗る。

- `basic_console_ctx_t` に `pad_x` / `pad_y` (キャンバス内でのテキスト面の
  オフセット) と `canvas_w` / `canvas_h` を追加。fullscreen なら
  キャンバス = ウィンドウ寸法・present 位置 (0,0)・pad = 中央寄せ量、
  ウィンドウ表示なら従来どおり キャンバス = 224x192・pad = 0。
- 面座標 -> キャンバス座標の変換は 3 箇所だけ (セル描画、スプライトの
  インスタンス位置、CIRCLE) なので、そこに pad を足した。CLS はテキスト面の
  矩形だけを塗り、外周の黒は初期化時に一度塗って以後触らない。
- コスト: graphics 側のキャンバスが 224x192 (43KB) から ウィンドウ寸法
  (320x216 なら 69KB) へ増える。描画コマンド数は変わらない。
- デスクトップ側 (壁紙を黒にする) では実装していない。指示が「BASIC アプリの
  ときだけ」であり、かつ fullscreen 時のデスクトップはタスクごと suspend
  されるので、Ruby 側の on_suspend は走らない (試して分かった)。

検証: fullscreen の `sample_10_dodge` が全面黒で表示され、ウィンドウ表示の
`bounce` は従来どおり (224x192 を (10,15) に表示、周囲は壁紙)。
ゴールデン 102 / sim 15 も維持。

**副産物**: `bounce.app.bas` が B1 で撤去した `LET` / `WAIT` を使ったままで
起動即エラーになっていたので、Family BASIC 記法へ書き換えた (B1 でデモを
書き換えたときの取りこぼし)。

## 8. T4-8: キーボード配列の追従

### 調査結果: scancode -> 文字の変換はどこにあるか

| 層 | 何をしているか | 配列設定への追従 |
|---|---|---|
| `main/drivers/usb/fmrb_keymap.c` | US / JP の変換テーブル本体 | **唯一のテーブル** |
| `main/kernel/fmrb_kernel.c` | 起動時に system_conf の `keyboard_layout` を `fmrb_keymap_set_layout()` に流す | 追従の起点 |
| `main/kernel/host/host_task.c` | HID イベントを配送する際に `fmrb_keymap_scancode_to_char(scancode, mod, 現在の配列)` で `character` を埋める | **追従済 (唯一の変換点)** |
| `usb_task.c` / `usb_task_linux.c` / `tab5_keyboard.c` | 物理入力 -> scancode + 修飾。文字変換はしない | 影響なし |
| BASIC コア | 受け取った `character` を Family BASIC コードへ (charset)、カナはローマ字 2 打 | 追従済 (入口が上記 1 箇所) |
| fm-editor / desktop / shell | `ev[:character]` を使う。ショートカットは scancode | 追従済 |
| `tools/fmrb_input.py` | 文字 -> scancode + shift (**US 固定だった**) | **今回修正** |

つまり端末側は既に 1 箇所集約済みで、食い違っていたのは注入ツールだけ
だった。`fmrb_input.py` は **firmware の `fmrb_keymap.c` を読んでテーブルを
逆引き**するようにした (テーブルを二重に持たない)。配列は
`config/system_conf_linux.toml` の `keyboard_layout` から自動判別し、
`--layout us|jp` で上書きできる。

検証: jp 設定のまま、エディタへ `10 PRINT "OK"` / `20 X=1:Y=2` /
`30 IF X=1 THEN PRINT "YES";` を注入 -> F5 で `/home/kbtest.bas` に保存させ、
**ファイル内容が打った通り** (`"` `=` `:` が化けない) であることを確認。
以前は `"` が `*`、`=` が `^`、`:` が `+` になっていた。us 設定でも
ホストゴールデンは配列に依存しないので green のまま。

カナ入力は現行の TAB トグル + ローマ字 2 打を維持。JIS カナ配列
(かな刻印どおりの直接入力) を足すかは、テーブルが 1 箇所に集約されている
ことが分かったので**追加コストは小さい**が、実機のキーボード事情
(刻印) の判断材料が必要なのでユーザ判断待ち (疑義 #31)。

## 9. T4-9: `.keys` ゴールデンの決定化

2 段構えにした:

1. **入力待ちマーカー**: 本当に待つ箇所 (`INKEY$(0)` のブロッキング待ちと
   `INPUT` / `LINPUT`) で、待ち開始・終了のエッジに `INWAIT|1` / `INWAIT|0`
   を fmrb_log 経由で 1 回出す。ホストランナーは golden を汚さないよう
   この行を捨てる。
2. **ポーリング型ケースの窓**: `208` / `212` は `INKEY$` (引数なし) を
   ポーリングし、キューが空になった時点で先へ進む型なので「待ち」が無く、
   マーカーでは同期できない。ケース先頭に `PAUSE 120` (2 秒) を置いて
   注入の窓を作った。PAUSE はホストランナーではフレームを即進めるので
   golden の出力は変わらない。

`basic_screen_check.py` は `.keys` を読んで `fmrb_input.py` でキーを打つ
(TAB = カナトグル、印字可能 ASCII)。`208` には `_SCRDUMP` を足して sim でも
比較できるようにした。結果: **sim 15 ケース green (うち `.keys` 2 本)**。

## 10. 検証結果

| 項目 | 結果 |
|---|---|
| `rake basic:test` | **102 passed, 0 failed** (B3 の 86 + B4 の 16) |
| `tools/basic_screen_check.py` (linux sim) | **15 passed, 0 failed** (2xx 11 + 4xx 2 + `.keys` 2) |
| 実機経路の SAVE / LOAD? / POKE / PEEK | ログとファイルで確認 (sec 5) |
| Ctrl-Q -> フォーカス復帰 -> F5 | 2 周確認 (sec 7) |
| jp 配列での記号入り注入 | 保存ファイルの内容一致 (sec 8) |
| 音声・実機 | 未確認 (headless の範囲外) |

## 11. 受け入れ基準の対応

1. 既存ゴールデン green 維持 + B4 機能のゴールデン追加 -> **達成** (102、うち
   ON ERROR 6 / POKE・PEEK 3 / SAVE・LOAD 2 / 画面系 4 / 直接モード 1)
2. 仮想メモリマップ対応表の文書化 -> **達成** (`doc/fmrb_basic/virtual_memory_map.md`)
3. POKE 依存作品の互換状況 -> **現コーパスに POKE 依存作品が無い**ため更新なし。
   `POKEW|` ログで機械的に洗い出せる仕組みは用意した
4. 直接モードコンソール -> **不実施** (T4-1 でユーザが不要と判断)
5. fullscreen .bas を `ctrl+q` で終了し、クリックなしで F5 再実行 -> **達成**
6. jp で記号入り注入が化けない / us で golden green -> **達成**
7. `.keys` ケースが決定的に green -> **達成** (sim 2 本、マーカー + PAUSE 窓)
8. 本レポート -> 本ファイル

## 12. 疑義・申し送り

| # | 内容 | 扱い |
|---|---|---|
| 29 | ~~LOAD の意味~~ -> **解決**: LOAD / LOAD? は直接モードのコマンドなので、プログラム中では何もしない (2026-07-28 ユーザ判断)。SAVE は実装のまま |
| 30 | FILTER は値を保持するだけ (BG 面の淡い着色は描画側に段が無い) | 実装するなら graphics 側にフィルタ段が必要 |
| 31 | JIS カナ配列 (直接入力) を足すか。テーブルは 1 箇所に集約済みなのでコストは小さい | ユーザ判断 |
| 32 | ERR は「エラー未発生」も 0 を返す。エラー表の No 0 は NF なので区別できない | 実機挙動が不明。ERR はハンドラ内で使う前提なので実害は小さい |
| 33 | BG 画面 `$D000` をネームテーブル行 0 列 0 とみなし、可視 28x24 を左上に取った (実機は 32x30 のどこを映すかがテレビ依存) | 資料待ち |
| 34 | LIST / SAVE の出力は `&H6000` を `24576` に、`LOAD?` を `LOAD PRINT` に展開する (数値は 16bit 値として格納、`?` は PRINT の別名という B1 の設計の帰結) | 実害は小さいが、SAVE したものを人が読む前提なら要検討 |
