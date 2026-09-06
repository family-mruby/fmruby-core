# 描画境界 (FmrbGfx ↔ 描画側) の課題と方針

> 状態: 随時更新 | 更新: 2026-09-04 | 課題 4 件、いずれも未着手。仕様変更が落ち着いてから順に

Ruby の `FmrbGfx` は命令の列 (`fmrb_link_protocol.h`) を描画側へ送り、描画側が
LovyanGFX で実装する。描画側は 4 通り (WROVER = graphics-audio、Tab5 =
display_p4、wasm = display_p4 の core、sim = graphics-audio) あり、renderer は
1 つで済んでいる。この境界の**置き方は変えない**。ここに書くのは、境界を
越えるときに足りていないもの (失敗の理由、実装の一致、資源の寿命、持ち物の
申告) と、それを**速度と通信量を増やさずに**足す候補である。

思いつきの提案を繰り返さないために、先に制約を書く。

## 制約

| # | 制約 | 意味 |
|---|---|---|
| C1 | **速度を犠牲にしない** | 成功経路のメッセージ数・往復数を増やさない。同期 (`send_sync`) を増やすと打鍵〜present の遅延に乗る |
| C2 | **Retro の描画側は別コア、通信は UART 921600bps だけ** | 実効 90KB/s 前後。資源 (画像・sprite・canvas) は WROVER 側にあり、core は id しか持たない。同期点を増やすと遅く、減らすと状態が食い違う |
| C3 | **Retro の実出力 (NTSC) は自動で取れない** | ただし描画実装 (graphics-audio) は sim が同じコードで動かしており、SHM で絵は取れる。取れないのはテレビに出た絵だけ |
| C4 | **画像は使い回す** | 同じ絵を複数の枚・複数の描画で使う。「一度出して終わり」の設計にはできない。動画は 1 本で寿命が明確なので対象外 |
| C5 | **実装が 2 系統** | graphics-audio (WROVER / sim) と display_p4 (Tab5 / wasm)。同じ命令に同じ絵を出す約束を仕組みでは持っていない |
| C6 | **テストは仕様変更が落ち着いてから** | 命令が動いている間は golden の維持費が高い。ただし命令を足すときに見本を 1 枚足す習慣は先に始められる |

## 課題 1: 失敗しても応答が来ない (graphics-audio の NACK 未実装)

**現象**: `send_sync` で答えを待つ命令 (create_image / video_open / sync_file /
status) が graphics-audio 側で失敗すると何も返らず、core はタイムアウト
(create_image 10 秒、video 5 秒) まで止まる。未知の命令 (Retro に video を
送る) も同じ。

**事実** (2026-09-04 に確認):

- protocol には `RESPONSE_MSG_ACK (0xF0)` と `RESPONSE_MSG_NACK (0xF1)` がある。
- **core は NACK を受けられる**: `components/fmrb_transport/fmrb_transport.c:677`
  が NACK を status=1 として `send_sync` を即エラーで返す。
- **display_p4 は返している**: `CREATE_IMAGE_FROM_FILE` の失敗 (開けない /
  復号失敗 / 確保失敗) は結果入りの応答を `send_ack` で返す。Tab5 と wasm で
  無い画像が即 nil になるのはこのため。
- **graphics-audio は返していない**: `main/tasks/message_handler_task.c:151` が
  `if (result == 0) send_ack` で、`result < 0` は無応答。comm 層に `send_nack`
  が無い。

**候補** (C1 を満たす: 成功経路は変わらず、失敗時に小さな応答が 1 つ増えるだけ):

- graphics-audio の comm 層に `send_nack` (0xF1、`send_ack` の写し) を足し、
  dispatcher に `else if (result < 0) send_nack(type, seq, 理由 1 バイト)`。
  未知の命令も同じ経路で NACK。core も protocol も触らない。
- 答えを待たない命令 (draw 系、batch queue) には NACK を足さない (受け手が
  無い)。代わりに描画側が「最後に拒んだ命令の番号と理由」を数個覚え、
  Ruby が `present` の後などに読める診断の口を GFX STATS と同じ扱いで置く。
  制御には使わない。

**当面の回避**: Ruby 側で `sync_file` の戻り値 (false = 写せなかった) を見て
create を送らない (PicoRabbit P7/P9)。これは NACK が入っても残してよい
(送らなくてよいものを送らないのは正しい)。

**状態**: 未着手。graphics-audio 側の 1 分岐 + comm 1 関数。

## 課題 2: 2 系統の実装のずれを検出する仕組みが無い

**現象**: display_p4 の `DRAW_IMAGE` が `scale_x/scale_y` を無視して等倍で
貼っていた (graphics-audio は `drawPng(..., scale_x, scale_y)` で効く)。
426x240 では倍率 1.0 なので気づかれず、852x480 のブラウザで発覚
(fmruby-core `ff34804d`、2026-09-04)。命令の列という設計は「同じ命令に
同じ絵」を仕組みで約束しない (C5)。

**候補** (C3 を踏まえる: Retro の実装 = graphics-audio は sim で絵が取れる):

- 「全機種で自動」は狙わない。**命令ごとの見本デッキ (Ruby アプリ) 1 本**を
  sim (graphics-audio) と wasm (display_p4) で撮り、`fmrb_pngdiff` で比べる。
  2 系統とも手元でヘッドレスに取れるので、実機は要らない。
- JPEG のようにハード復号 (Tab5) とソフト復号 (wasm、TJpgDec) で画素が揺れる
  ものは許容差を持つか、見本は PNG で作る。
- 命令を足すときは見本に 1 枚足す (C6 の「先に始められる習慣」)。

**状態**: 未着手。見本デッキの置き場と比較の手順は未定。C6 により仕様変更が
落ち着いてから。

## 課題 3: 資源 (画像) の寿命が暗黙で、使い回しの表が Ruby 側にある

**現象**:

- 画像は描画側の資源で数に限りがあるが、Ruby からは数も残りも見えない。
- アプリ終了時は系が回収するが、`on_destroy` で `delete_image` を呼ぶと
  graphics context が先に無くなっていて例外になる (PicoRabbit P9 で発見)。
- 使い回すために Ruby 側 (アプリごと) に path → id の表を持つことになり、
  寿命の責任がアプリに散る (PicoRabbit の `@images`)。
- `create_image` の応答は幅・高さを返すために必ず待つ。`delete_image` も
  待つ。

**候補** (C2 / C4 を踏まえる: 使い回しの表は資源のある側に置く):

- **描画側が同一性で使い回す**: `create_image(path)` は、同じ中身が既に
  載っていればその id を返す。同一性の判定は `sync_file` が既に行っている
  内容比較を流用。Ruby の表は要らなくなり、デッキの開き直しも別アプリの
  同じ絵も転送・復号なしで済む。
- **参照数で捨てる**: create で +1、delete で -1。枠が足りないときに参照数
  0 のものを古い順に捨てる。捨てられたかを Ruby が気にする必要は無い
  (次の create が返す id が変わるだけ)。
- **`delete_image` を待たない命令に落とす**: batch queue に乗せれば draw との
  順序は保たれる。往復が 1 つ減り、context が無くても例外にならない。
- **残り枠は create の応答に相乗り**: 幅・高さの隣に 1 バイト。問い合わせの
  命令を足さずに見える。
- UART での同期点は create の応答だけ (幅・高さのために今も待っている)。
  増えない。

**動画は対象外**: 資源が 1 本で寿命が明確 (open / stop)。今のままでよい。

**状態**: 未着手。graphics-audio と display_p4 の両方の image store に手が
入る。同一性の鍵 (path か内容の hash か) が未定。

## 課題 4: 持ち物の表 (`FONT_AVAILABLE`) が Ruby 側の写し

**現象**: `lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb:74` の `FONT_AVAILABLE`
は描画側の firmware が持つフォントの写しで、描画側を変えると食い違いうる。
`set_font` が「実際に選ばれた [family, size]」を返す仕組みはあるので、
renderer は辻褄を合わせられるが、表そのものは手で保つ必要がある。

**候補** (C1 を踏まえる: 実行時の往復はゼロにする):

- **ブート時に 1 回だけ吸い上げる**。描画側は既に INIT_DISPLAY で同期して
  いる。そこで「持ち物の記録」(フォント一覧、画像の枠数、画面の大きさ、
  ハード JPEG の有無) を 1 メッセージ返し、カーネルがファイル
  (`/var/run/display_caps.toml` のような) か `FmrbConst` に置く。
- アプリは手元を読むだけ。`FONT_AVAILABLE` はこの記録から作る形になり、
  描画側の firmware が変わっても Ruby を直さなくてよい。
- 毎回問い合わせる形にはしない (C1)。

**状態**: 未着手。記録の形式 (toml か packed struct か) と置き場は未定。

## 関連

- 境界の設計そのもの: `components/fmrb_common/include/fmrb_link_protocol.h`、
  doc/archive/gfx_unification (送出経路の統一)。
- 課題 1〜3 が実際に出た記録: doc/archive/video/report/p1_p4.md (未知の命令で
  ACK が返らない)、doc/picorabbit/report/p8.md / p9.md (無い画像の 10 秒待ち、
  on_destroy の delete_image、DRAW_IMAGE の scale)。
