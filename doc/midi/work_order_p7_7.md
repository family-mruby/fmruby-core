# MIDI 対応 作業指示書 (P7.7: Ruby アプリの MML)

対象: 実装セッション (Opus)
ブランチ: `feature/midi`
**前提: P7.6 は完了済み** (report/p7_6.md — 必読。実機で定常 avg
183〜931us を確認済み)。MML の発音は P7.6 のキュー
(`fmrb_midi_sched`, SmfPlayer の補充パターンが実装見本) に乗せる。
参照: Midori の `tmp/midori/mrbgems/picoruby-midi-mml/`。

## 0. この段の位置づけ

Ruby アプリから MML で演奏できるようにする。BASIC の MML (C++、
BASIC 専用) とは**独立のまま**にする (確定方針)。

Midori の gem 構成 (調査済み):

| ファイル | 行数 | 扱い |
|---|---|---|
| `mrblib/midi_mml.rb` (`MIDI::MML::Sequence` = MML パーサ) | 345 | **無改変で輸入** (依存は picoruby-midi のみ。こちらに既にある) |
| `mrblib/midi_mml_player.rb` (`MIDI::MML::Player`) | 347 | **輸入しない**。`MIDI.start!` の bpm_loop (24 分割クロックのポーリング) 前提で、P7.6 が捨てた「ポンプでの時刻決め」に依存するため |

## 1. やること

### 1.1 パーサの輸入 (無改変)

- `lib/add/picoruby-midi-mml/` として gem ごと輸入し、
  `mrblib/midi_mml.rb` は **1 バイトも変えない** (picoruby-midi と同じ
  規律。`FAMILY_MRUBY_PORT.md` を添えて、何を輸入し何を輸入しなかったか
  と理由を書く)。md5 一致をレビューで確認できる形にする。
- gembox / Rakefile への追加は picoruby-midi の前例に倣う。

### 1.2 プレーヤはこちらの流儀で書く (`FmrbMidi::MmlPlayer`)

`picoruby-fmrb-midi` 側に、SmfPlayer と同じ顔のプレーヤを新設する:

```ruby
player = FmrbMidi::MmlPlayer.new(device)   # device は APU でも serial でも
player.load_string("t120 o4 l4 cdefgab>c")  # MIDI::MML::Sequence を内部で使う
player.start / stop / playing? / tempo_scale=
```

- **発音は P7.6 のキューに積む** (Sequence のクロック位置 → μs 変換は
  `60e6 / (bpm * 24)`。Sequence は 4 分音符 = 24 クロック)。
  これで MML も C タイマー精度で鳴る。
- 声の解決は SmfPlayer と同じく **積む時点で** P7.5 のグループ機構を通す。
- ループ再生 (Midori の Player が持っていた機能) は対応する。
  ループ境界での note_off 保証も (Midori 実装の同名処理が参考になる)。
- **割り当てを増やさない**: パース (load_string) 時の割り当ては構わないが、
  **再生中のイベント供給は 0 個/イベント**を維持する。Sequence の
  イベント表現が配列/Hash なら、load 後に packed Integer へ変換して
  保持する (P6 の流儀)。

### 1.3 使える形にする

- デモ: 既存の midi_apu デモにメニューを 1 つ足すか、小さな
  `mml_demo.app.rb` を作るか、**判断して報告に書く**。
  「APU でも Unit MIDI でも同じ MML が鳴る」ことが見えること。

## 2. 検証

1. ホストテスト: Sequence のパース結果 (音名・オクターブ・長さ・
   テンポ・タイ・ループ) を Midori の example
   (`play_scale.rb` / `two_part.rb`) の MML 文字列で固定する。
2. 再生中 0 個/イベント (bench)。
3. sim: monitor のタイムスタンプで音符間隔が MML の指定どおりか実測
   (`l4` @ t120 = 500ms 間隔。P5-sim と同じ検証手法)。
4. APU / serial 両経路で同じ音高・同じ間隔 (これも P5-sim の枠組み)。
5. 既存テストと md5 一致の維持。

## 3. やらないこと

- BASIC の MML との統合・共通化 (確定方針: 独立)。
- Midori の `MIDI.start!` / bpm_loop / Clock の輸入。
- MML 方言の拡張 (Midori の Sequence が解釈する範囲のみ)。

## 4. 守ること

- 報告は `doc/midi/report/p7_7.md`。README は書き換えない。
- lib/ 編集後の clean は build/ ごと (p6.md 1.1)。
- コメントは英語、コミットログは英文、ASCII 以外の記号は使わない。
- linux と esp32 の両方でビルドが通ること。

## 5. 完了の条件

1. `FmrbMidi::MmlPlayer` で MML が APU / serial の両方から鳴る
   (sim で実測、実機で聴くのはユーザ)。
2. 音符間隔が指定どおり (monitor 実測、P7.6 の精度で)。
3. 輸入ファイルは無改変 (md5)、輸入しなかったものと理由が
   PORT.md にある。
4. 再生中の割り当て 0 個/イベント。
5. 既存テスト全通過 + MML のホストテスト追加。
