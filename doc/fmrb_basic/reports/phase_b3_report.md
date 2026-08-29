# Phase B3 報告書: ゲーム機能 (互換レベル L3)

実施: 2026-07-28 / ブランチ `fmrb-basic` (fmruby-core)
対象タスク: `phase_b3.md` T3-1 - T3-7

スプライト、自動移動、パッド入力、サウンドを実装し、ベーマガ級ゲームが
成立する状態にした。ゴールデンテストはホスト 86 ケース + linux sim 11 ケース
が green。自作 L3 サンプル 5 本が linux sim で動作する。

## 0. 成果物一覧

| 追加/変更 | 内容 |
|---|---|
| `core/basic_sprite.cpp` | スプライト定義・DEF MOVE エンジン・CRASH・パッドレジスタ |
| `core/basic_mml.cpp` | MML -> FMSQ 変換、PLAY / BEEP |
| `core/basic_screen.cpp` | 1/60 tick (`service_frames` / `frame_tick`)、実行速度スロットル |
| `core/basic_exec.cpp` | DEF / SPRITE / MOVE / CUT / ERA / CAN / POSITION / PLAY / BEEP / CGEN / CGSET |
| `core/basic_expr.cpp` | XPOS / YPOS / MOVE(n) / VCT / CRASH / STICK / STRIG |
| `core/basic_charset.cpp` | ひらがな -> カタカナ畳み込み、長音符の代替 |
| `extension/fmrb_basic_gfx.c` | グリフシート化 (draw_tile)、差分描画、スプライト描画、CGEN 切替 |
| `assets/basic_tile_a.c/.h` + `tools/gen_basic_tiles.py` | キャラクタテーブル A のプレースホルダタイル |
| `fmrb_basic.cpp` | キーボード/パッド -> STICK/STRIG、FMSQ チャンク転送、BEEP |
| `components/fmrb_audio/audio_commands.h` | graphics-audio 側と同期 + `LOAD_BINARY_CHUNK` 追加 (`main/drivers/audio_p4/` から移動) |
| `main/drivers/audio_p4/audio_p4_handler.c` | チャンク組み立て (P4) |
| `test/bench/` | スクロールベンチ 3 本 |
| `test/golden/30x, 31x, 320` | スプライト・MOVE・PLAY・CGEN のゴールデン |
| `reports/phase_b3_progress.md` | サンプルのブリングアップ記録 |
| **fmruby-graphics-audio (別リポジトリ、未コミット)** | `audio_commands.h` / `audio_handler_shm.c` / `audio_handler_esp32.c` にチャンク組み立てを追加 |

## 1. T3-1: tick 統合と描画方式の判断

### 1.1 tick

- インタプリタが自分でフレームを刻む: 32 ステートメントごとの tick 点で
  ホストの時計を読み、到来したぶんだけ `frame_tick()` を回す。バックログは
  4 フレームで打ち切る (長い停止のあとに一気に走らせない)。
- `frame_tick()` の順序は指示書どおり: 入力取り込み (ホストの `on_tick` が
  直前に実行) -> DEF MOVE の座標・アニメ更新 -> present。CRASH は座標から
  その場で判定するので記録は不要だった。
- **実行速度**: 1 フレームあたりのステートメント数に上限を設ける方式にし、
  既定を **60 ステートメント/フレーム (= 3,600/秒)** とした。Family BASIC は
  1.79MHz の 6502 が自前のトークンを解釈する速度なので、上限を設けないと
  同じプログラムが数十倍速く走って遊べない。値は校正ノブで、B5 で実機映像と
  比較する。`basic_config::statements_per_frame = 0` で無効化できる。
- PAUSE はミリ秒ではなく**フレーム数**を数えるようにした。移動と同じ時計に
  乗り、時計を持たないホスト (テストランナー) ではフレームを直接進めるので、
  ゴールデンテストが移動を観測できる。
- linux sim と実機の差はアダプタ (`fmrb_basic.cpp`) が吸収する:
  コアは `ticks_ms` / `sleep_ms` しか触らない。

### 1.2 描画コストの計測と判断 (B2 report sec 2.3 の持ち越し)

ベンチ (`components/basic/test/bench/`) は 2 種類:
`bench_scroll` は 120 行を印字して 96 回スクロール (行内容が似ているケース)、
`bench_scroll_worst` は毎行違うパターンを敷いてスクロール (全セルが変わる
最悪ケース)。linux sim (docker、SDL2 プロセス) で計測:

| 方式 | 通常のスクロール | 最悪ケース |
|---|---|---|
| B2 のまま (セル = 背景矩形 + グリフのラン、約 10 コマンド/セル) | **4.5 s / 行** | **4.7 s / 行** |
| + 差分描画 (変わらないセルは描かない) + CLS を 1 矩形に | 0.26 s / 行 | 4.7 s / 行 |
| + **グリフシート化 (draw_tile 1 コマンド/セル)** | **2.1 ms / 行** | **19 ms / 行** |

判断と根拠:

- **(a) canvas viewport の ring 方式は採用できない**。
  `doc/gfx/gfx_canvas_viewport_scroll.md` のとおり `SET_CANVAS_VIEWPORT` は
  **P4 専用で Retro (S3+WROVER) 側は未実装**。BASIC の主ターゲットは Retro
  なので、ここでは選択肢にならない。
- **(b) グリフシート化を採用**。`create_sprite_image` +
  `set_sprite_image_target` + `draw_tile` はいずれも graphics-audio 側
  (WROVER / linux) に実装済み。属性 (0-3) ごとに 128x128 のシートを遅延生成
  し、初回だけグリフを描き込む (約 12 コマンド)。以後そのグリフは
  1 コマンドで置ける。
- 併せて入れた差分描画は、通常の文字出力 (行が似ている画面) で効く。
  CLS は 1 矩形 + キャッシュリセットになった。
- 結果、全画面書き換えでも 19 ms (1 フレーム強) に収まり、ゲームの BG 更新が
  律速にならなくなった。

補足: リンクのコマンドレートは計測中 1,000-1,500 コマンド/秒だったのに対し、
draw_tile 中心にすると実効 35,000 コマンド/秒相当まで伸びた。小さな矩形を
大量に送る形が特に不利だったことになる。

## 2. T3-2 / T3-3: スプライトと自動移動

- **状態はコアが持つ**。座標・移動残量・アニメ位相・当たり判定はすべて
  `core/basic_sprite.cpp` にあり、レンダラは通知を受けて描くだけ。おかげで
  移動仕様をゴールデンテストで固定できる (`300`-`304`)。
- 実装した仕様 (core_spec sec 8/9、v3_spec の対照表):

  | 要素 | 挙動 |
  |---|---|
  | DEF SPRITE n,(A,B,C,D,E)=文字式 | 配色・8x8/16x16・優先度・XY 反転。タイルは文字式のコード列 |
  | SPRITE n,x,y / SPRITE n / ON / OFF | 表示・消去・面の有効化 |
  | DEF MOVE(n)=SPRITE(A,B,C,D,E,F) | 種類 0-15、方向 0-8、速さ C、全移動量 D、優先度、配色 |
  | 速度 | **2C フレームごとに 2 ドット** (C=1 で 60 ドット/秒)。C=0 は 256 フレーム周期 |
  | 全移動量 | **2D ドット**を使い切ると停止し MOVE(n) が 0 になる |
  | MOVE / CUT / ERA / CAN | 開始 / 停止 (表示維持) / 停止+消去 / 定義と位置ごと破棄 |
  | POSITION n,X,Y | 初期座標。未定義スロットの既定は (120,120) |
  | XPOS / YPOS / VCT / MOVE(n) | 現在座標 / 移動方向 (停止時 0) / 完了 0・移動中 -1 |
  | CRASH(n) | 16x16 矩形の重なり。最小番号を返す。なし -1、未定義 -2 |

- **DEF SPRITE と DEF MOVE のスロットは別系統**にした (レンダラへは 0-7 と
  8-15)。spec が別々に番号を振っており、16x16 のアニメキャラは実機でも
  複数ハードウェアスプライトを占めるので、共有すると作品側にない制約を
  作ってしまう。疑義リスト #19 に記録。
- `MOVE` は文と関数の両方: 式の中で `MOVE(n)` と書かれたら進捗問い合わせ、
  文頭の `MOVE n` は開始。1 トークン先読みで判別する。
- レンダラはスロットごとに sprite image + instance を 1 つ持ち、**絵柄が
  変わったときだけ画像を描き直す**。サンプルが方向転換のたびに DEF MOVE を
  し直す書き方をするため、これがないと毎フレーム 40 コマンド以上かかる。
- タイル絵柄は `tools/gen_basic_tiles.py` のプレースホルダ (16x16 が 1 つの
  枠付きボックスに見えるよう、4 コードで枠を分担する手続き生成)。

## 3. T3-4: STICK / STRIG

- HID イベントは INKEY$ と同じ tick でアプリのメッセージキューから取り込み、
  最新状態をコアのレジスタへ書く。ビット割当は core_spec sec 11。
- **キーボード代替**: 矢印 = 十字、Z = A、X = B、Enter = START、
  Space = SELECT。spec 案の Shift は sdl2 経由で単独イベントが取れないため
  Space に置き換えた (疑義 #20)。
- ゲームパッドは SDL の一般的な配置 (0=A, 1=B, 6=SELECT, 7=START、軸 0/1 を
  十字に、デッドゾーン 8000) を仮定。
- headless 検証: `fmrb_input.py` でキーを注入し、STICK/STRIG の値を画面
  ダンプで読み出して確認した (`key right` -> 1、`key up` -> 8、`key z` -> 8)。

## 4. T3-5: PLAY / BEEP

- **MML -> FMSQ 変換をコアに実装**。3 声を並行に進め、到来した声だけ処理して
  最短の待ちぶんだけ WAIT を出す 1 パス方式 (イベント配列を持たない)。
  変換は決定的なので、**ホストゴールデンでバイト列を固定**した
  (`310`-`313`。ランナーは `FMSQ|<len>|<hex>` を出す)。
- 実装した MML: 音階 C-B と `#`、音長 0-9 (32 分〜全音符、付点あり)、
  休符 R、オクターブ O0-O5、テンポ T1-T8、音量 V0-V15、エンベロープ M0/M1、
  デューティ Y0-Y3、チャンネルセパレータ `:` (最大 3 声)。
- 暫定値 (spec が実装依存としている箇所):
  - オクターブ対応: O0 の C を C1、O5 の C を C6 とした (既定 O3 が中央ハ)。
  - テンポ: T4 で 4 分音符 0.5 秒 (30 フレーム)。T1-T8 は線形。
  - **三角波は APU クロックを 32 分周する**ので、同じ音程でも矩形波の
    半分のタイマ値になる。ここは実装時に一度取り違えて音が 1 オクターブ
    ずれた (ゴールデンのバイト列比較で気付いた)。
- **転送はチャンク分割ロード**: アプリ -> host のペイロードは 176 バイトで、
  PLAY 1 回分の FMSQ は数百〜数千バイト。指示書の設計どおり
  `FMRB_AUDIO_CMD_LOAD_BINARY_CHUNK` (0x0C) を追加し、順番に流してから
  `PLAY_SLOT` で開始する (同一キューの FIFO なので ACK 不要)。受信側は
  組み立て中のスロットを 1 本だけ持ち、順序が崩れたら破棄する。
- `audio_commands.h` は **fmruby-core 側が古かった** (play_slot の instance
  フィールドや STOP/PAUSE/RESUME の構造体が欠けていた) ので、
  graphics-audio 側の内容へ同期してからチャンクコマンドを足した。併せて
  ヘッダを `components/fmrb_audio/` へ移動した (コンポーネントから参照する
  ため。main への依存は方向が逆になる)。
- **BEEP** は NOTE_ON/NOTE_OFF 経路 (プロトコル変更なし、FMSQ スロットを
  消費しない)。数フレーム後に tick から NOTE_OFF する。
- linux sim で経路全体を確認した:
  `Audio command: cmd_type=0x0c` (チャンク) -> `load_chunk: slot 15 complete`
  -> `FMSQ loaded from memory: version=1, frames=120` -> `FMSQ play_slot`
  -> `playback ended`。**音そのものの確認はユーザにお願いしたい**
  (headless では不可)。

### 4.1 fmruby-graphics-audio 側の変更 (未コミット)

| ファイル | 内容 |
|---|---|
| `main/common/audio_commands.h` | `FMRB_AUDIO_CMD_LOAD_BINARY_CHUNK` + 構造体 + 上限定数 |
| `main/audio/audio_handler_shm.c` | チャンク組み立て (linux) |
| `main/audio/audio_handler_esp32.c` | チャンク組み立て (WROVER) |

同リポジトリで `rake build:linux` が通ることを確認済み。**ブランチ指示を
待って独立コミットする**。

## 5. T3-6: PALET / CGSET / CGEN

- CGEN 0-3 で BG 面・スプライト面が使うキャラクタテーブル (A/B) を切り替える。
  テキスト面はグリフキャッシュを捨てて描き直し、スプライトは再通知する。
- CGSET m,n はパレットバンクの選択。**バンクの色コードは spec に無い**ので
  プレースホルダ (バンク 1 が起動時配色) を置いた。疑義 #21。
- PALET S がスプライト面に効くようになった (B2 では解析のみ)。バックドロップは
  両面共通。

## 6. T3-7: ブリングアップ

ユーザ供給の作品リストは未着のため、`test/samples/sample_1x` の自作 L3
サンプル 5 本を代替コーパスとして使った。詳細は
`reports/phase_b3_progress.md`。

### 6.1 互換状況表

| 作品 | 状態 | 理由・備考 |
|---|---|---|
| `sample_10_dodge` | 動く | DEF MOVE 再定義 + POSITION の操作定型、CRASH、ERA、STICK、スコア表示 |
| `sample_11_shoot` | 動く | MOVE 3 本、STRIG、CRASH ペア、CAN、起動時 PLAY |
| `sample_12_maze` | 動く | SCR$ 判定、COLOR、カナ、ランダム壁 |
| `sample_13_music` | 制限付き | 画面・転送・再生は動く。**音の確認がユーザ待ち**。2 回目の PLAY は 1 回目を置き換える (下記 #23) |
| `sample_14_hit` | 動く | RND、PAUSE、INKEY$、カナ、CHR$ 一致判定 |

「動く」= linux sim で BASIC エラーなく起動・描画し、入力に反応するところまで。
手触りと音はユーザ確認。

### 6.2 ブリングアップで直したもの

- サンプルのカナがすべて `?` になっていた。文字コード表 (表 B) にひらがなも
  長音符も無く、変換で置換文字になっていたため。**ひらがなはカタカナへ畳み、
  長音符は `-` に置換**するようにした (疑義 #22)。

## 7. 検証結果

| 検証 | 結果 |
|---|---|
| `rake basic:test` (ホスト) | **86 passed, 0 failed** (B2 の 75 + スプライト/MOVE 5 + PLAY 4 + CGEN 1 + 差し替え) |
| `python3 tools/basic_screen_check.py` (linux sim) | **11 passed, 0 failed** |
| `rake build:linux` / `rake build:esp32` (NARYAv3) | 両方成功 |
| fmruby-graphics-audio `rake build:linux` | 成功 |
| サンプル 5 本 | linux sim で動作 (sec 6.1) |
| 音 | **未確認 (ユーザ依頼)** |

### 7.1 メモリ

linux sim でサンプル実行後の値:

```
fmrb_basic: BASIC usage: pool used=29600 free=475688 of 505288 bytes,
            stack headroom=122936 bytes
```

B2 の 27,504 バイトから +2,096 バイト。内訳はスプライト/MOVE の状態
(コアのオブジェクト内)、PLAY の変換バッファ 2,048 バイト (初回 PLAY 時に
確保)、レンダラのスプライト管理表。タイルセットは const 2,048 バイトで
rodata。C スタックの深さは B1 から変わっていない (frame_tick も再帰しない)。
**ESP32-S3 実機のスタック計測は引き続き B5 送り**。

## 8. 仕様の疑義リスト (spec は変更していない)

B0 sec 8.1 の 3 件、B1 sec 9 の 8 件、B2 sec 8 の 5 件は未決のまま。B3 で新規:

| # | 内容 | 採用した挙動 | 影響 |
|---|---|---|---|
| 19 | DEF SPRITE のスプライト番号と DEF MOVE の動作番号が同じ 8 スロットを共有するのか | 別系統 (各 8 個) として実装 | 両方を同時に使う作品でスロット数が実機と食い違う可能性 |
| 20 | パッド代替キーの SELECT。spec 案は Shift だが sdl2 経由では単独イベントが取れない | Space に割り当て | headless 検証の手順のみ |
| 21 | CGSET のパレットバンクの色コード (spec は「BG 用 2 種・スプライト用 3 種」とだけ書き内容を列挙していない) | プレースホルダ配色 (バンク 1 = 起動時配色) | 見た目のみ。実機映像で差し替え |
| 22 | ひらがな・長音符が文字コード表に無い | ひらがな -> カタカナへ畳み、長音符 -> `-` | 実機は表示できないはずなので、互換より可読性を採った |
| 23 | 再生中に次の PLAY が来たときの挙動 (キューする / 置き換える / 待つ) を spec が定義していない | **置き換える** (新しい FMSQ をロードして再生開始) | BGM を繋いでいく作品 |
| 24 | MOVE の速さ C=0 (spec「256 フレーム毎」) と 2C フレーム周期の関係 | C=0 は 512 フレーム周期 (2C の規則をそのまま延長) として実装 | ごく低速の移動 |
| 25 | 移動が画面端に達したときの挙動 (spec に記述なし) | 座標をそのまま更新し、描画側でクリップ | 端で跳ね返る/消える作品 |

## 9. 実装外で見つけた基盤側の課題 (B3 の対象外)

ブリングアップ中に繰り返し当たったので記録する。**BASIC 固有ではなく
アプリ基盤側**の挙動:

1. **アプリを kill するとタスクのメモリプールが解放されない**。同名アプリの
   次回起動が `Failed to create BASIC state` で失敗する (コンテナ再起動で
   復旧)。INKEY$(0) 待ちのアプリを止めるときに必ず踏む。
2. **kill されたアプリの canvas が graphics 側に残る**。次のアプリの画面が
   古い画面に覆われる。
3. debugd の spawn で起動したアプリは**デスクトップのフォーカス対象にならない**
   ため、キー入力を効かせるには一度画面をクリックする必要がある。

ブリングアップ手順ではサンプルごとにスタックを起動し直すことで回避した
(`phase_b3_progress.md`)。

## 10. Phase B4 への引き継ぎ

- **POKE/PEEK**: 現在 POKE は式を評価して捨て、PEEK は 0 を返す。B4 の
  仮想メモリマップはここに入る。頻出アドレスはユーザ作品の棚卸し待ち。
- **BGGET / BGPUT / SCREEN / VIEW / FILTER / BACKUP** は未実装
  (`ext_statement` -> IL)。`test/golden/151_unsupported_statement` が
  この境界を固定しているので、実装したらケースを差し替える。
- **ON ERROR GOTO / RESUME / ERROR**: エラーコードと発生行はコアが保持済み
  (ERL / ERR は実装済み)。ハンドラへの分岐だけが未実装。
- **直接モードコンソール**の要否判断も B4。デクランチ (LIST の逆変換) は
  B1 で用意済み、`fmrb_basic_list()` から使える。
- **音の校正**: テンポ T1-T8 とオクターブ対応は暫定値。ユーザが実機映像・
  録音と比較できる段階で `core/basic_mml.cpp` の 2 つの表を直す。
- **描画**: グリフシート方式で全画面 19 ms。さらに詰めるなら GfxBlock VM
  (DEFINE_PROG/EXEC_PROG、graphics-audio 側に実装あり) で 1 メッセージに
  複数セルを載せる余地がある。
