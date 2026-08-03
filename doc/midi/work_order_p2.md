# MIDI 対応 作業指示書 (P2)

対象: 実装セッション (Opus)
ブランチ: `feature/midi`
前提文書: [README.md](README.md) (方針)、[report/p0.md](report/p0.md) /
[report/p1.md](report/p1.md) (P0/P1 の結果。**先に読むこと**)

## 0. ゴール

**内蔵音源 (APU) を MIDI で鳴らす**。すなわち:

```ruby
device = MIDI::Device.new(FmrbMidi::ApuTransport.new)   # 名前は仮
device.program_change(0, channel: 0)
device.note_on(60, 100)
sleep 0.5
device.note_off(60)
```

が Family mruby のアプリから動く状態にする。sim で完結する
(実機・外部音源は P5)。

## 1. 決定済みの前提 (ユーザ判断)

| 項目 | 決定 |
|---|---|
| APU transport の実装言語 | **Ruby で書いて様子を見る** |
| `picoruby-midi` の取り込み | **ユーザ側から見た API はそのままにする** |
| Midori の API | **なるべくそのまま採用する** |
| 計測 | **準備しておく** (下記 5) |

P0-1 の実測 (送出 1 回 115us、必要量とは 1 桁以上の開き) が Ruby 実装を
支持している。**速度で困ったら C に降ろす**という順序で進めてよい。

## 2. 調査で分かっている足場

**`MIDI::Device` は transport を duck typing で受ける**。
`mrblib/midi_device.rb` の要求は 5 つだけ:

```
send_packet(cable, cin, b1, b2, b3)
read_available
bytes_available
connected?
device_info
```

つまり **Ruby のクラスがこの 5 つを持てば `MIDI::Device` にそのまま挿さる**。
APU transport を Ruby で書くという判断と、Midori の API をそのまま使うという
判断は、ここで両立する。

### C に依存している部分 (注意)

`MIDI::Device` のうち以下は **C 実装 (`src/mruby/midi.c`) を呼ぶ**ので、
そのままでは動かない:

| メソッド | 依存 | 扱い |
|---|---|---|
| `trigger(note, vel, duration:)` | `MIDI._trigger` (C の scheduler) | 下記のとおり判断する |
| `trigger_batch(events)` | `MIDI._send_batch` | 同上 |
| `MIDI::Clock` | `_init_timer` ほか (C タイマ) | **今回は対象外**でよい |
| `MIDI::Input` | `_start_task` ほか (受信タスク) | **今回は対象外** (送信専用) |

`note_on` / `note_off` / `control_change` / `program_change` などの
**基本的な送信は純 Ruby で `@transport.send_packet` を呼ぶだけ**なので、
C 無しで動く。

**判断してほしいこと**: `trigger` (自動 note-off) をどうするか。
選択肢は (a) 今回は未対応にする、(b) Ruby で同等品を書く
(タイマまたは既存の周期処理に載せる)、(c) C 部分も移植する。
**(a) か (b) を推奨**。`trigger` は「鳴らして放っておくと消える」ための
利便機能で、SMF プレーヤ (P4) は自前で note_off を送るため必須ではない。
決めた理由を報告に書くこと。

## 3. やること

### 3.1 gem の取り込み

`picoruby-midi` の **Ruby 部分**を `lib/add/` に置き、
`lib/add/family_mruby.gembox` に登録する
(既存 gem のやり方に倣う。Rakefile のコピー行も要る)。

- 取り込む: `mrblib/midi.rb` / `midi_device.rb` / `midi_constants.rb`
  (`midi_clock.rb` / `midi_input.rb` は今回対象外だが、**一緒に置いておくか
  外すかは判断**。置くなら C が無い状態で `require` しても壊れないこと)。
- **ユーザから見た API は変えない**。`MIDI::Device#note_on(note, velocity,
  channel:)` の形はそのまま。Family mruby 固有の事情は transport 側に隠す。
- `MIDI.usb_host_device` / `MIDI.sam2695_device` のような Midori の
  ファクトリメソッドは、**Family mruby には無い transport を指すので
  そのままでは動かない**。APU 用のファクトリを足すか、この層は使わずに
  `MIDI::Device.new(transport)` を直接使うかを判断する。
- gem 名は `picoruby-midi` のまま取り込むのが素直
  (upstream に返す道を残すため)。**改変は最小限にし、改変した箇所は
  報告書に一覧で残す** (P2 の成果を Midori/upstream に返すときの材料になる)。

### 3.2 APU transport (Ruby)

`send_packet(cable, cin, b1, b2, b3)` を受けて `FmrbAudio` の
`note_on` / `note_off` に変換するクラスを Ruby で書く。

**写像は P1 の変換器が既に持っている**。`tool/midi/smf2fmsq.rb` の
`PulseVoice` / `TriangleVoice` / `NoiseVoice` と `DRUM_MAP` が、
MIDI ノート -> APU の対応 (音高、音量、ドラム、声の奪い方) の実装済みの答なので、
**同じ規則を実時間側に持ってくる**。

ただし **引数の形が違う** (P0-2 報告「設計への影響 4」):

- pulse: `freq` は **Hz** (`FmrbAudio#note_on(ch, freq, vol, duty, sweep)`)。
  FMSQ 側はタイマ値だったので、変換をやめて Hz を直接渡す。
- triangle: `freq` は Hz。音量制御は無い。
- **noise は特殊**: `freq` の下位 4bit が period、bit7 が短周期モード。

**チャンネル割り当ては SUB の 4 声に対して行う** (P0-2 で MAIN/SUB が
独立と確定した)。BGM (FMSQ instance=0 = MAIN) と同時に鳴らせる。
既定は README 方向B のとおり (ch1,2 -> 矩形波、ch3 -> 三角、ch10 -> ノイズ)。
**割り当て表は差し替え可能にしておく** (P1 の `--map` に相当するものを
実行時に持つ)。

注意: `note_on`/`note_off` は SUB を **FMSQ instance=1 と共有する**ので、
両方使うと奪い合う。**APU transport は SUB を占有する前提**でよい
(その旨を報告と、可能ならコードのコメントに書く)。

### 3.3 所有権 (複数 VM への対応)

Midori は「Ruby タスク 1 本がハードを持つ」前提だが、Family mruby は
複数 VM が同時に走る。**APU transport が `FmrbAudio` 経由になっている限り、
既存の音の所有権の枠組みに乗る**ので、まずはそれで進めてよい。

- 2 つのアプリが同時に MIDI を鳴らしたらどうなるかは、**現時点では
  「後から鳴らしたほうが勝つ」で構わない**。`FmrbAudio` の note_on が
  元々そうなっているため。
- ただし**その挙動を報告書に明記する**こと。排他が必要と分かった時点で
  kernel 側に持ち上げる (README 方向A の「所有権は system 側」)。
- **今回 hw_proxy に新チャネルを作る必要は無い**。UART を触るのは P5 から。

### 3.4 動作確認

- sim で動くデモアプリを 1 つ (`flash/app/demo/` か、P1 と同じく
  `tool/midi/test/` に置いて実行時に複写する形)。
  音階を鳴らす / 和音を鳴らす / BGM (FMSQ) と同時に鳴らす、の 3 つが見えるとよい。
- **BGM との同時再生を必ず確認する**。P0-2 の結論 (MAIN/SUB は加算合成) が
  実際の経路でも成り立つことの確認になる。
- 音は `tools/fmrb_audio_probe.rb` で波形として確認できる
  (ヘッドレスでも読める)。官能評価はユーザ。

## 4. P1 の成果との関係

`tool/midi/smf2fmsq.rb` (事前変換) と APU transport (実時間) は、
**同じ写像の 2 つの実装**になる。P2 の時点では**重複を許してよい**
(片方は host の Ruby、片方はデバイス上の Ruby で、動く場所が違う)。

ただし **写像の規則がずれると「変換したら鳴るのに実時間だと違う音」**という
一番厄介な不一致になる。同じテスト曲を両経路で鳴らして、
**同じ音高・同じ長さになることを確認する**こと (P1 の
`tool/midi/test/scale.mid` が使える)。ずれたらどちらが正しいかを報告する。

## 5. 計測の準備 (ユーザ指示: 準備しておく)

P0-1 で **end-to-end の遅延が測れていない**ことが分かっている
(core と graphics-audio が別プロセスで、タイムスタンプを突き合わせられない)。
P2 の実装と同時に、**測れる状態にしておく**。

- **受信側の計装**: graphics-audio の音声側に、`note_on` を受け取ってから
  APU に反映するまでの件数と遅延を周期ログに出す。
  形は core 側の入力遅延統計 (`spx: hid_lat: n=1000 sum_ms=.. max_ms=..
  ge1=.. ge5=..`) に倣うと、既存のログ読みの習慣がそのまま使える。
- **測るのは P2 の完了条件ではない**。数字を採るのは実機がある時
  (P5 と同じセッション) でよい。**この段階では「採れる状態にする」まで**。
- 送出側 (アプリ VM から見たコスト) は P0 の `audio_bench.app.rb` が
  そのまま使えるので、MIDI 経路版が要るなら同じ形で足す。

## 6. 守ること

- **`doc/midi/README.md` は勝手に書き換えない**。前提が覆ったら
  `doc/midi/report/p2.md` に「README のここが違う」と書く。
- 実装中の気づき・実測値・申し送りは `doc/midi/report/p2.md` に書く。
- **取り込んだ gem への改変は一覧で残す** (upstream に返すため)。
- コード中のコメントは英語。コミットログは英文。ASCII 以外の記号は使わない。
- `lib/add/` を編集したらビルド前に `rake clean`
  (`fmruby-core/CLAUDE.md`)。gem の追加は Rakefile のコピー行 + gembox の
  両方が要る。
- **作業ツリーは現在 Linux ビルドが入っている** (`build/` = linux)。
  P2 は sim で完結するので、そのまま使える。esp32 に切り替える必要は無い
  (切り替えると戻すのに再ビルドが要る)。
- **音の最終確認はユーザが行う**。数値・波形での確認まではこちらで詰める。

## 7. 完了の条件

1. アプリから `MIDI::Device` 経由で APU が鳴る (音階・和音)。
2. **BGM (FMSQ) と同時に鳴る**ことを波形で確認した。
3. P1 の変換器と同じ音高・長さになることを確認した (`scale.mid`)。
4. 受信側の遅延計装が入っていて、**実機が繋がれば数字が採れる状態**である。
5. 報告書 `doc/midi/report/p2.md` に、判断 (特に 2 章の `trigger` の扱いと
   3.3 の所有権) と gem への改変一覧が書かれている。
