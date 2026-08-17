# Phase 5: 土台の穴埋め (時刻・ファイル・メッセージ・ライフサイクル)

## 目的

phase3 で第一段階に絞った結果、Python 側に「無いと何も組み立てられない」
部品がいくつか欠けたまま残っている。ゲームを書く前に、そこを埋める。

このフェーズの中身は **C の新規実装が少なく、prelude (Python 層) が主**である。
配送側の C は既に用意ができていて、受け手が居ないだけ、という箇所が多い。

## 先にやること: P5-0 性能の実測

**設計より先に測る**。Python の 1 フレームに何が入るか分からないまま
ブロック崩し (phase9) を設計すると、作り直しになる。

測る対象は 3 つ。Linux シミュレーションと ESP32-S3 実機の両方で取る
(シミュレーションは FreeRTOS を POSIX スレッドで模擬するので、実機の値と
ずれる。両方に意味がある)。

| 測るもの | 方法 | 何の判断に使うか |
|---|---|---|
| 素の実行速度 | 空ループ 10 万回、整数演算、属性参照、メソッド呼び出し、リスト操作の各回数/秒 | 毎フレームに置ける処理量 |
| 描画コマンド 1 本の往復 | `fill_rect` を n 回並べて 1 回 present、n を変えて計測 | 「毎フレーム全部描き直す」か「差分だけ描く」かの分岐 |
| GC の停止時間 | `gc.collect()` の所要時間を、ヒープの埋まり具合を変えて計測 | フレーム落ちの上限。音の途切れにも効く |

計測用のアプリは `flash/app/python/pybench.app.py` として置き、結果は
report/phase5.md に表で残す。**この数字が phase9 のフレーム予算そのもの**に
なるので、目標フレーム時間 (33ms = 30fps を仮の目標とする) に対して
どれだけ余裕があるかを明記すること。

## やること

### 1. `_fmrb` に足す関数 (C)

| 関数 | 内容 | 理由 |
|---|---|---|
| `ticks_ms()` | 起動からの経過ミリ秒 (`fmrb_mp_bridge_now_ms` が既にあるので公開するだけ) | `time` モジュールが使えないので、Python には**時計が一つも無い**。タイマも効果音の消音予約もこれ無しには書けない |
| `read_file(path)` -> `bytes` / `None` | ファイルを丸ごと読んで bytes で返す | `open()` が無い。データを読む唯一の手段であり、**`exec` と組み合わせるとアプリを複数ファイルに分けられる** (phase7 で使う) |
| `file_size(path)` -> `int` / `None` | 大きさだけ問い合わせる | 読む前に大きすぎないか確かめられるようにする |

`read_file` には上限を設ける (初期値 64KB、`FMRB_MP_READ_FILE_MAX`)。
上限超えは例外にし、黙って切り詰めない。読み込み先の bytes は GC ヒープから
取るので、大きなファイルはヒープを圧迫する点を known_limitations に書く。

### 2. msgpack 組み立ての拡張 (C)

現在の `pack_value` は **文字列 255 バイトまで・整数 32 ビット・要素 15 個までの
辞書**しか作れず、配列とバイト列に至っては対応が無い。アプリ間通信
(phase7) と音 (phase8) でそのまま詰まるので、ここで直す。

**配列は phase7 で必ず要る**。世界の状態には入れ子の配列が含まれる。

- 配列 (list / tuple) を fixarray / array16 で書けるようにする
- バイト列 (bytes / bytearray) を bin8 / bin16 で書けるようにする
- 辞書は 16 要素以上 (map16) を書けるようにする
- 収まらないときは例外 (今は 0 を返して "not encodable" になる)。
  どの値で溢れたかがログに出るようにする

読み側 (`unpack_value`) にも対応する型が抜けていないか併せて確認する。

### 3. ライフサイクルの残り (prelude)

C 側は既に呼びに来ているのに Python に受け手が無い、というものが 2 つある。
**黙って捨てられている**ので、まずここを塞ぐ。

| 受け手 | 現状 | やること |
|---|---|---|
| `_handle_resize(w, h)` | C の `dispatch_control` が呼びに行くが prelude に無い | 追加し、`window_width` / `user_area_*` を計算し直して `on_resize(w, h)` を呼ぶ |
| `on_control(msg)` | 同上。未知の cmd はここに流れてくる | 追加し、pub/sub の配信・`run_result`・file_select の応答を振り分ける |
| `on_suspend` / `on_resume` | `_handle_system_control` はフラグを立てるだけ | フラグの切り替えに合わせて呼ぶ |

`on_resize` は全画面切り替えでも呼ばれる。枠を描き直さないと画面が壊れる
(Ruby 側で踏んだ問題と同じ) ので、既定の実装で `draw_window_frame` を
呼ぶかどうかを実装時に決め、決めた理由を report に書く。

### 4. タイマ (prelude)

Ruby 版と同じ形にする。

```python
tid = self.set_timer(500, self.blink)   # 500ms ごとに blink を呼ぶ
self.clear_time(tid)
```

- Ruby 版は `Machine.board_millis` を使う。Python は `_fmrb.ticks_ms()`。
- **`main_loop` だけでなく `_fmrb.spin` の中でも動かす**。イベントを待って
  いる間にタイマが止まると、効果音の消音が遅れて音が鳴り続ける。
  C の `spin` から `_run_timers` を呼ぶ形にし、Ruby 版が spin の中で
  タイマを回しているのと同じ挙動に揃える。
- 何も期限が来ていないときの費用をゼロに近づける (Ruby 版が「何も無いときは
  配列も作らない」と書いている理由と同じ)。

### 5. カーネルへの依頼一式 (prelude)

いずれも `send_message` を包むだけ。Ruby 版の引数と cmd 名をそのまま写す。

- `subscribe(topic)` / `unsubscribe(topic)` / `publish(topic, data=None)`
- `request_run(path, prev_pid=None)`
- `request_fullscreen(on)` / `toggle_fullscreen()`
- `request_file_select(mode="open")`
- `request_reload()`

配信されてくる側 (`on_control` に届く publish、`run_result`、
file_select の結果) の形も Ruby 版に合わせる。

### 6. ファイルからの import

アプリを複数のファイルに分けて書けるようにする。phase7 の「脳だけ別ファイル」
がこれで普通に書ける。

**部品は既にビルドされている**。`builtinimport.c` / `lexer.c` / `reader.c` は
生成物に入っていて、移植設定が 2 行で止めているだけである
(`MICROPY_ENABLE_EXTERNAL_IMPORT` と読み手の設定)。

書くもの:

| もの | 中身 |
|---|---|
| `mp_import_stat(path)` | `fmrb_hal_file_stat` を呼び、無い / ファイル / ディレクトリ を返す |
| `mp_reader_new_file(reader, name)` | ファイルを丸ごと読んで `mp_reader_new_mem` に渡す |

**読み手を自前にする**のが要点。既製の POSIX 読み手は 20 バイトずつ読むので
フラッシュ上では読み出し回数が跳ねる。丸読みなら 1 回で済み、ファイル操作を
firmware のファイル層に通せる。

探す場所 (`sys.path`) は**アプリ自身のディレクトリと共有の置き場だけ**に
限る。カーネルがアプリの経路を /app と /home に限っているのと揃える。
既定で入っている空文字列と `.frozen` は外す。

ホストのスモークテストは firmware を繋がないので、上の 2 つの代役を
`port/test/main.c` に置く (`mpport.c` に置くと firmware 側と衝突する)。

設定を変えるので `rake micropython:gen` のやり直しが要る。

## やらないこと

- ファイルの書き込み。読むだけにする (アプリのデータ保存は phase9 の
  ハイスコアで必要になったら、そのとき最小限を足す)。
- 共通層 (prelude) を import できる module にすること。共通層は今までどおり
  アプリの名前空間で実行する。**import した側からは共通層のクラスが
  見えない**という差が出るので、これは制限として書き残す。
- 事前コンパイル。共通層は解析したまま使う (README の判断記録を参照)。
- `time` / `json` など extmod のモジュール追加。
- GfxBlock (描画のまとめ送り)。phase6 の計測しだいで判断する。

## 確定した事項 (実装で決めた結果)

- `read_file` の上限は **64KB** (`FMRB_MP_READ_FILE_MAX`)。import する
  module にも同じ上限が効く。超えたら例外で、切り詰めない。
- `on_resize` の既定は**何もしない**。枠と中身の描き直しはアプリが決める
  (Ruby 版と同じ。窓の大きさが変わるアプリは毎描画で枠を描く決まり)。
- import には移植側の関数が **3 つ**要った。`mp_import_stat` と
  `mp_reader_new_file` に加えて `mp_lexer_new_from_file`。理由は
  report/phase5.md。
- **import した側から共通層のクラスは見えない**。必要なものは引数で渡す。

## 完了条件

0. `import` でアプリを 2 ファイルに分けて書ける。探す場所の制限が効いている。
1. `pybench.app.py` の計測値がシミュレーションと S3 実機の両方で取れ、
   report/phase5.md に表として残っている。
2. Python 版の pub_demo / sub_demo が、Ruby 版と同じように topic を
   やりとりできる (シミュレーションで画面確認)。
3. タイマで一定間隔の描画更新ができ、`spin` で待っている間も止まらない。
4. ウィンドウの全画面切り替えで `on_resize` が呼ばれ、枠と内容が壊れない。
5. 既存の Python アプリ (python.app.py / pytest.app.py) が退行していない。
