# Phase B3 指示書: ゲーム機能 (互換レベル L3)

前提: Phase B2 完了 (テキスト画面 + テキストダンプ検証が使える)。
B3 はベーマガ級ゲームを成立させる機能群 — スプライト、自動移動、
パッド入力、サウンド — を実装するフェーズ。仕様の正は core_spec
sec 8 (スプライト) / sec 9 (MOVE) / sec 10 (サウンド) / sec 12
テーブル A (キャラクタタイル) / v3_spec (命令対照表・追加書式・
コントローラ)。ここからはターゲット作品を 1 本ずつ動かして潰す
進め方に切り替える (作品コードはユーザがローカル供給。リポジトリには
入れない)。

## タスク

### T3-1: tick 統合 (基盤。最初にやる)

- インタプリタに 1/60 秒相当の tick 処理を組み込む (compat_plan
  sec 4.1-6)。アプリタスクのメインループから駆動し、tick で行う処理は
  順に: パッド/キー状態の取込み -> DEF MOVE の座標更新・アニメ切替 ->
  CRASH 判定の記録 -> 画面 present。
- B1 T1-2 で入れた「N ステートメントごとのホスト tick コール」を
  この 1/60 tick に接続する。BASIC 実行が速すぎる場合の待ち合わせ
  (実行速度は実機並みで十分 = 過度に速くしない) の方針もここで決めて
  report に記録する。
- linux sim と S3 実機での tick 供給源の違い (SDL ループ / FreeRTOS
  タイマ) を fmrb_basic.cpp 側で吸収する。
- **描画コストの判断 (B2 report sec 2.3 の持ち越し) をここで行う**:
  スクロールを毎 tick 行うベンチ .bas で現方式 (セル直描き 672 セル) を
  計測し、律速になるなら (a) canvas viewport の ring 方式 か
  (b) グリフシート化 + DRAW_TILE のどちらかを選んで実装する。
  計測値と判断根拠を report に残すこと。律速でなければ現方式のまま
  進んでよい (その場合も計測値は残す)。

### T3-2: SPRITE / DEF SPRITE

- DEF SPRITE n,(属性)=文字列 (書式・属性は core_spec sec 8): タイルは
  テーブル A のコードで指定される。8x8 / 8x16、反転、パレット指定。
- SPRITE n[,x,y] / SPRITE ON / SPRITE OFF。
- 実装は fmrb_gfx のスプライト C API (fmrb-sprite.rb が使っている経路)
  を extension 側から直接呼ぶ。BASIC 側のスプライト番号と fmrb_gfx の
  スプライト id の対応表を extension が管理する。
- テーブル A のタイル絵柄: プレースホルダ (色分けした番号入りタイル等、
  自作) で開始してよい。アセット形式は B2 で定義済みのものを使う。

### T3-3: DEF MOVE 系 (自動移動)

- DEF MOVE(n)=... のパラメタ解析 (書式・パラメタ意味は core_spec sec 9 /
  v3_spec の対照表)。MOVE n / CUT n / ERA n / CAN n (v3) /
  POSITION n,x,y。
- tick 内で座標更新・アニメパターン切替・画面端挙動を行い、
  XPOS(n) / YPOS(n) / VCT(n) / CRASH(n) が tick 記録から答える。
  CRASH はスプライト矩形の重なり判定 (判定条件は spec に従う)。
- 自動移動はスプライト実体 (T3-2) の上に載せる。移動定義数・同時
  移動数の上限は spec の値に合わせる。

### T3-4: STICK / STRIG

- kernel からの HID イベント (ゲームパッド + キーボード) を tick で
  取り込み、最新状態レジスタを保持。STICK(n) / STRIG(n) は core_spec
  sec 3.3 (compat_plan) のビット割当で返す。
- パッド非接続時のキーボード代替: 矢印 = STICK、Z/X/Enter/Shift =
  STRIG (A/B/START/SELECT)。headless ハーネスの fmrb_input.py だけで
  ゲームが検証できることを確認する。

### T3-5: PLAY / BEEP

- 裁可済みの a-2 方式 (B0 report sec 9.1-2): MML パーサ (core_spec sec 10
  の文法: 音階・音長・O/T/V、3 チャンネル同時) -> FMSQ 変換を実装し、
  下記のチャンク分割ロードで転送して `PLAY_SLOT` で再生する。
  PLAY 文字列の演奏継続・終了検知は tick と連動。
- BEEP は既存の NOTE_ON/NOTE_OFF で実装 (プロトコル変更なし、
  FMSQ スロットを消費しない)。

#### チャンク分割ロードコマンドの設計 (レビュー担当による事前調査済み)

新コマンド (両リポジトリの `audio_commands.h` に同一定義で追加):

```c
FMRB_AUDIO_CMD_LOAD_BINARY_CHUNK = 0x0C

typedef struct {
    uint8_t  cmd_type;    // 0x0C
    uint32_t music_id;    // slot id (LOAD_BINARY と同じ空間)
    uint16_t total_size;  // FMSQ 全体のバイト数 (全チャンクで同値)
    uint16_t offset;      // このチャンクの先頭オフセット
    uint8_t  chunk_len;   // 続くデータのバイト数 (最大 160)
    // chunk_len バイトのデータが続く
} __attribute__((packed)) fmrb_audio_load_chunk_cmd_t;  // ヘッダ 10 B
```

受信側の組み立てセマンティクス (3 バックエンド共通):

- 組み立てバッファは 1 本 (同時に組み立てるのは 1 スロットのみ)。
  `offset == 0` で total_size 分を確保して開始。進行中と異なる music_id
  や不連続 offset が来たら破棄してエラーログ (`offset == 0` なら
  新規開始として受け直す)。
- `offset + chunk_len == total_size` の受信で完成。既存の
  LOAD_BINARY の格納処理 (music_tracks への保存) と同じ経路へ渡し、
  組み立てバッファは解放する。
- total_size の上限は 16KB とする (保護。PLAY 1 回分の FMSQ は数 KB 想定)。
- ACK は不要: 同一メッセージキューを FIFO で処理するため、チャンク列の
  直後に送った PLAY_SLOT が追い越されることはない。

変更ファイル (すべて既存 LOAD_BINARY の case の隣に追加):

| リポジトリ | ファイル | 内容 |
|---|---|---|
| fmruby-graphics-audio | `main/common/audio_commands.h` | enum + 構造体 |
| fmruby-graphics-audio | `main/audio/audio_handler_shm.c` | case + 組み立て (linux) |
| fmruby-graphics-audio | `main/audio/audio_handler_esp32.c` | case + 組み立て (WROVER) |
| fmruby-core | `main/drivers/audio_p4/audio_commands.h` | enum + 構造体 (P4/共通ヘッダ) |
| fmruby-core | `main/drivers/audio_p4/audio_p4_handler.c` | case + 組み立て (P4) |

組み立てロジックは 3 箇所に重複実装せず、可能ならバックエンド内の
静的ヘルパとして同型のコードを保つ (リポジトリをまたぐ共通化は
しない。ヘッダの流用関係が既にこの形)。

**注意 (事前調査で判明)**: 両リポジトリの `audio_commands.h` は現在
**同一ではない**。graphics-audio 側には `fmrb_audio_play_slot_cmd_t` の
`instance` フィールド (+ legacy サイズ互換)、STOP/PAUSE/RESUME/STATUS の
構造体、コメント拡充が入っており、fmruby-core 側が古い。B3 でチャンク
コマンドを足す際に **fmruby-core 側ヘッダを graphics-audio 側の内容へ
同期**させること (BASIC からの PLAY_SLOT は instance を明示指定できた方が
よい: BGM=MAIN、効果音は NOTE_ON 経路なので通常 MAIN のみで足りる)。

fmruby-graphics-audio 側の変更はそのリポジトリで独立コミットとする
(そのリポジトリの CLAUDE.md に従う。ビルド確認は同リポジトリの
`rake build:linux`。ブランチはユーザ指示に従う)。
- 音の検証は headless では不能。linux sim の音声はユーザ確認に回し、
  MML -> FMSQ 変換自体はホストテスト (変換結果のバイト列比較) で
  ゴールデン化する。

### T3-6: PALET / CGSET / CGEN

- PALET (スプライト・BG のパレット指定。52 色表 -> RGB332 の対応は
  B2 T2-2 の表を拡張)、CGSET (キャラクタセット切替)、CGEN (書式は
  core_spec sec 7)。
- CGSET/CGEN で切り替わるタイルバンクの構成をアセット側に用意する。

### T3-7: ターゲット作品ブリングアップ

- ユーザ供給の作品リスト (B0 T0-5 の棚卸し済み) から 1 本ずつ動かし、
  非互換を潰す。作品ごとの症状と修正は `reports/phase_b3_progress.md` に
  記録する (作品コード自体は書かない。行番号 + 症状の記述にとどめる)。
- 併せて L3 機能を使う自作サンプルを test/samples/ に 2 本以上追加
  (スプライト移動 + STICK 入力の最小ゲーム等)。

## 受け入れ基準

1. 既存ゴールデン (ホスト + linux sim) green 維持
2. 自作 L3 サンプルが headless ハーネス (入力注入 + テキストダンプ +
   必要ならスクリーンショット) で動作確認できる
3. MML -> FMSQ 変換のホストゴールデンが green (a 案の場合)
4. ターゲット作品のうちユーザ合意した本数が linux sim で動く
5. `reports/phase_b3_report.md` 完成

## 報告

`reports/phase_b3_report.md`。作品ごとの互換状況表 (動く / 制限付き /
非対応 + 理由) を含める。B5 の実機確認でこの表を再検証する。
