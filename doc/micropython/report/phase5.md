# Phase 5 実装レポート (土台の穴埋め)

## 完了条件の判定

| 条件 | 判定 | 確認方法 |
|---|---|---|
| 0. import でアプリを 2 ファイルに分けられる | OK | PySub の描画部分が別ファイル (pysub_panel.py) にあり、画面に出ている |
| 1. 性能の実測 | 半分 | シミュレーションで実測。**実機はまだ** (書き込みが要る) |
| 2. Python 版 pub/sub が Ruby アプリと topic をやりとりできる | OK | 両方向を画面で確認 (下記) |
| 3. タイマが待ち時間中も止まらない | OK | 500ms ごとの点滅が、メッセージ待ちの間も続く |
| 4. 全画面切り替えで on_resize が呼ばれ、枠と内容が壊れない | OK | 切り替え前後の画面を確認 |
| 5. 既存の Python アプリが退行していない | OK | python.app.py / pytest.app.py ともに起動する。ホストのテスト一式 (`rake test`) も通過 |

## 入れたもの

### C 側

- `_fmrb.ticks_ms()` / `_fmrb.read_file(path)` / `_fmrb.file_size(path)`。
  ファイル操作は firmware のファイル層 (fmrb_hal_file) 経由。read_file の
  上限は 64KB (`FMRB_MP_READ_FILE_MAX`)。超えたら例外で、切り詰めない。
- msgpack の組み立てを拡張: 配列 (list / tuple)、バイト列、16 要素以上の
  辞書、256 バイト以上の文字列。読み側にもバイト列と浮動小数を足した。
- `spin` からタイマを回す (`_run_timers` を毎周呼ぶ)。
- resize の配送に `fullscreen` を足し、辞書引きを「無ければ None」にした
  (未知のメッセージで例外を投げると、spin を貫いてアプリが落ちるため)。
- `quit_request` を `_handle_system_control` に回す (Ruby と同じ扱い)。
- import の 2 つの穴: `mp_import_stat` と `mp_reader_new_file`。加えて
  `mp_lexer_new_from_file` も**移植側で書く必要があった** (下記)。

### Python 側 (共通層)

- `ticks_ms()`、`on_suspend` / `on_resume` / `on_resize` / `on_quit_request`、
  `_handle_resize`、`_recalc_user_area`。
- タイマ (`set_timer` / `clear_time` / `_run_timers`)。一回限りで、繰り返す
  なら callback が次を仕掛ける (Ruby 版と同じ)。
- カーネルへの依頼: `subscribe` / `unsubscribe` / `publish` / `request_run` /
  `request_fullscreen` / `toggle_fullscreen` / `request_file_select` /
  `request_reload`。

### 検証用アプリ

- `flash/app/python/pysub.app.py` + `pysub_panel.py` + toml。
  phase5 で足したものを一通り使う。R で Ruby の発信アプリ、S で Ruby の
  受信アプリ、E で Ruby のロボット世界を起動し、P で発信、F で全画面。
- `flash/app/python/pybench.app.py` + toml。P5-0 の計測。

## 分かったこと

### 1. import は移植側の関数 3 つで入る (2 つではなかった)

`mp_import_stat` と `mp_reader_new_file` を書けば足りる、という見立てだった
が、**`mp_lexer_new_from_file` も要る**。py/lexer.c のこの関数は
`#if MICROPY_READER_POSIX || MICROPY_READER_VFS` の中にあり、既製の読み手を
どちらも使わない構成では**コンパイルされない**。中身は 2 行 (読み手を作って
字句解析器に包む) なので移植側に置いた。リンク段階まで分からない類の穴で、
ホストのスモークテストが最初に教えてくれた。

### 2. 読み手は自前にして正解だった

既製の POSIX 読み手は 20 バイトずつ読む。丸読みにすれば 1 回で済み、
firmware のファイル層に通せるので経路の扱いも揃う。

### 3. 探す場所は起動時に絞れる

`sys.path` の初期値は `["", ".frozen"]` で、空文字列は「作業ディレクトリ
から」の意味になる。アプリ開始時に**アプリ自身のディレクトリと
`/usr/lib/python` だけ**に置き換えた。経路の検査をどこにも書かずに、
「アプリの外は import できない」が成り立つ。

### 4. import した側から共通層は見えない (仕様として残す)

`pysub_panel.py` の中では `FmrbApp` も `FmrbGfx` も `Log` も未定義になる。
共通層はアプリの名前空間で実行されるもので、module ではないため。
描画に要るものはアプリ自身を引数で渡す形にした。**この形のほうが、
分けた側が単体で読めるので、制限というより作法として書ける**。

### 5. 自分の publish は返ってこない

カーネルは発信者を配送先から外す (`next if sub_pid == pid`)。1 つの Python
アプリだけでは配列の往復を確かめられないので、Ruby 側と組んで確かめた。
Python アプリは同時 1 本なので、**Python どうしの通信は試せない**。

### 6. アプリの停止 (suspend) は 2 通りある

`fmrb_app_suspend` は**タスクを止めて canvas を隠すだけ**で、メッセージは
送らない。`{"cmd":"suspend"}` が飛ぶのは、止めずに走らせ続けるアプリ
(デスクトップなど) だけである。したがって Python の `on_suspend` /
`on_resume` は Ruby との同等性のために持つもので、普通のアプリが
全画面アプリに隠されるときに呼ばれるわけではない。確認中に
「PySub に suspend が来ない」と一度誤診したので、ここに書いておく。

### 7. 引数 3 つのコールバックで firmware ごと落ちた (作り込んだ不具合)

`call_if_present` の作業用配列が 4 要素しかなく、束縛メソッドと self を
先頭に置く仕様なので**引数 2 つまでしか入らなかった**。resize を 3 引数に
した瞬間に配列の外へ書き、core が Segmentation fault で落ちた。配列を
`2 + FMRB_MP_CALL_MAX_ARGS` にし、超える呼び出しは例外にした。

ビルドも通り、ホストのスモークテストも通り、**シミュレーションで全画面に
切り替えて初めて落ちた**。新しく通した経路を実際に通すまで安心してよい
理由がない、という例として残す。

## 実測 (P5-0)

シミュレーション (WSL2 上の docker、x86-64)。1 回の呼び出しあたり。

| 項目 | 値 |
|---|---|
| 空ループ | 45 ns |
| 整数演算 | 80 ns |
| 属性の読み書き | 90 ns |
| メソッド呼び出し | 145 ns |
| 配列への追加 | 160 ns |
| 辞書引き | 65 ns |
| **fill_rect 1 本** | **45,000 ns (45 us)** |
| **present 1 回** | **33,000 ns (33 us)** |
| GC 1 回 (生存 2000 個) | 1 ms 未満 |

**描画命令 1 本が、Python の命令 1 つの 300〜1000 倍**である。しかもこれは
Python の費用ではなく、命令をカーネル経由で送る経路の費用なので、実機でも
この比は大きく変わらないと見てよい。

phase6 / phase9 への申し送り: **フレームの予算は Python の行数ではなく
描画命令の本数で決まる**。30 フレーム/秒 (33ms) なら、シミュレーションでも
描画命令は数百本が上限になる。動くものはスプライトに任せ (位置替えは
1 本)、背景は 1 回だけ描く、という phase6 の初期案でよい。

**実機の計測は未了**。`rake build:esp32` と書き込みが要るので、
ユーザに依頼する。同じ `PyBench` を起動して `pybench:` の行を採れば
同じ表が採れる。

## 確かめた画面 (シミュレーション)

- Ruby -> Python: Ruby の PubDemo で Publish を押すと、PySub の
  `received` と `from` / `n` が増える。
- Python -> Ruby: PySub で P を押すと、Ruby の SubDemo に `n=2 python` が出る。
  **中に配列を入れて送っており、Ruby 側で復号できている**ことが、
  そのまま配列を書けている証拠になる。
- Ruby -> Python (入れ子の配列): Ruby の RoboExplorer が流す state の
  `view` (配列の配列) を受け取り、`5:[1, 0, 'floor']` と表示できた。
  **phase7 が要求する形はこれで通ることが分かった**。
- import: 画面の描画一式が別ファイル (pysub_panel.py) 側にある。
- read_file: 自分の .app.toml を読んで 226 バイトと表示。
- タイマ: 500ms ごとの点滅が、メッセージ待ちの間も続く。
- 全画面: F で 320x240 に広がり枠が消え、戻すと枠と題字が戻る。
- 排他: PyBench を 2 本目に起動しようとすると
  「Another Python app is already running」で拒否される (phase2 のまま健在)。

## 次への申し送り

- **実機での計測がまだ**。上の表の実機版を採る。
- ランチャーの一覧はブート時のキャッシュから作られる。**新しいアプリを
  置いたら、右クリックで再走査してからシミュレーションを起動し直す**
  (再走査だけでは、その場の一覧に出ないことがある)。検証で 3 回引っかかった。
- `/usr/lib/python` はまだ存在しない。共有の Python ライブラリを置くときに
  作る。無くても import は動く (探して見つからないだけ)。
- ファイルへの書き込みは無いままなので、得点の保存などは phase9 で判断する。
