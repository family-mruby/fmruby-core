# 実装指示書 P4: FMRB API の RBS 充実

対象: 実装担当セッション。前提: P3 完了 (補完/ホバー/診断が sim で動作、
PIN = fmrb-dev d40a9e6)。plan.md と report/p1.md〜p3.md を先に読むこと。

report は doc/editor_ti/report/p4.md へ。タスクごとにコミット。

## P4 のゴール

子供が触る FMRB API の補完・ホバー・診断を一通り効かせる。同時に
**sig/ を API ドキュメントの正**とする運用を立ち上げる。

対象クラス (lib/add/picoruby-fmrb-app ほかの実装が正):
FmrbApp / FmrbGfx / GfxBlock / SpriteImage / SpriteInstance /
TileMap / TileRing / TileSheet / FmrbAudio / FmrbI18n /
FmrbMidi / MIDI モジュール / NsfHeader / P5 (p5 互換層)

## 方針 (決定済み)

- **実在するメソッドだけを書く** (P1 からの原則)。実装 (mrblib の Ruby と
  C バインディング) を読んで書き、推測で書かない。書けない/書かないと
  判断したものは report に一覧を残す。
- **必須引数のみの形で書く** (エンジンがオプション引数・キーワード引数を
  持たないため)。キーワード引数が本質の API は「最頻の呼び方」を必須
  引数化した形で書き、正確に書けなかったことを report に記録する。
- **doc コメントは日本語の一文** (全角 20 文字目安)。補完とホバーで
  ステータス行にそのまま出る、子供向けの説明文。何をするかだけを書き、
  引数の説明はシグネチャに任せる。
- sig/ は**ドメインごとにファイル分割** (fmrb_app.rbs / fmrb_gfx.rbs /
  fmrb_sprite.rbs / fmrb_tile.rbs / fmrb_audio.rbs / fmrb_midi.rbs /
  fmrb_misc.rbs 等)。既存 fmrb.rbs は分割して廃止してよい。
  **上流由来の基本型 15 ファイルは引き続き変更禁止**。
- RBS の attr_reader 構文は tidbgen が読まないので、読み取り属性も
  `def gfx: () -> FmrbGfx` の形で書く。

## T1: gfx. 表記の解禁 (attr_reader)

@gfx が効くようになったので保留していた件。両方のスペルを出す:

1. lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb と
   main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb の attr_reader に
   `:gfx` を追加 (P1 報告で名前衝突なしは確認済み。追加はこの 1 シンボル)。
2. sig の FmrbApp に `def gfx: () -> FmrbGfx` と `@gfx: FmrbGfx` の両方を
   置く。
3. sim で `gfx.dr` + Tab と `@gfx.dr` + Tab の両方が draw_* を出すこと。

## T2: FmrbGfx と FmrbApp の網羅

- FmrbGfx: 描画・画像・キャンバス・スプライト転送系の公開メソッド全部
  (`_` 始まりの内部メソッドは書かない)。
- FmrbApp: ライフサイクル (on_update / on_event 系のフック)、
  ウィンドウ操作、メッセージ送受、タイマー、ファイルダイアログ、
  wallclock、wifi/ble/kana などクラスメソッド群。
  フックメソッド (on_update 等) も宣言する — 子供が `def on_` まで打って
  補完から選べるのが狙い (継承チェーン経由で出る)。

## T3: Sprite / GfxBlock / TileMap 系

SpriteImage / SpriteInstance / GfxBlock / TileMap / TileRing / TileSheet。
ゲーム作りの主線なので doc は特に丁寧に (それでも一文)。

## T4: 音と MIDI とその他

FmrbAudio / FmrbMidi / MIDI モジュール / NsfHeader / FmrbI18n。
P5 (p5 互換層) は量を見て判断してよい: 大きすぎるなら主要メソッド
(setup/draw から呼ぶ描画系) だけ書き、残りは report に持ち越しとして
記録する。

## T5: sig/README.md (運用の正の宣言)

短い README を sig/ に置く:

- sig/ は FMRB API の型定義であり、**エディタの補完/ホバー/診断と
  PC 側ツールの共通の源** であること
- Ruby API を追加・変更したら sig/ を更新すること (レビュー観点)
- 書き方の約束 (実在のみ / 必須引数のみ / doc は日本語一文 /
  上流由来 15 ファイルは変更禁止 / ドメインごとにファイル分割)

## T6: 検証とサイズ実測

1. `rake ti:test` green (sig の追加はテストを壊さないはずだが、
   GPIO/Enumerable の既存宣言を動かさないこと)。
2. sim (標準構成) で代表補完を一巡してスクリーンショット:
   - `gfx.` (T1) / SpriteInstance のインスタンスで `sp.mo` -> move
   - `class MyApp < FmrbApp` 内で `def on_` -> フック候補
   - FmrbAudio か MIDI 系で 1 つ
   - どれか 1 つでホバーと診断 (誤引数) も確認
3. **サイズをドメインごとに記録**: 生成 db (.c) のサイズと、追加ごとの
   増分。最後に S3 (NARYAv3) をビルドして flash 実測 (P3 時点比)。
   tidbgen の文字列プールは name/signature/document 各 65535 バイト上限で
   超えると生成が止まる。**各プールの使用量と残りを report に記録**する
   (tidbgen に使用量を出力させる小改造をしてよい。その場合は fork の
   fmrb-dev に載せて PIN を更新する)。
4. 補完候補は 1 要求 64 件上限 (エンジン)。`gfx.` 素の状態で溢れる場合は
   その事実だけ記録 (prefix を 1 文字打てば絞れるので v1 は許容)。

## 受け入れ条件

1. T6-2 の代表補完が全て通る (スクリーンショット付き)。
2. rake ti:test green。S3 ビルド通過 + サイズ記録 (flash 残と
   文字列プール残)。
3. 書かなかった API の一覧が report にある (「全部書いた」ではなく
  「何を書かなかったか」が分かる状態)。
4. sig/README.md がある。

## やらないこと (P4 の範囲外)

- 定数 (FmrbGfx::BLACK 等) の補完 — 上流 db の機能追加が要る。相談事項
  として温存 (report に必要性の所感だけ書く)。
- キーワード引数・オプション引数の正確な表現 (同上)。
- doc コメントの英語版 (言語切替)。
- File / JSON 等の picoruby 標準 gem の網羅 — 子供の使用頻度を見て
  次段で判断。最小 (File.open/read/write 程度) は fmrb_misc.rbs に
  入れてよい。
- 実機確認・arena PSRAM 化 (P5)。
