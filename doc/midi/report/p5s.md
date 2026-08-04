# P5-sim 報告書: 外部 MIDI 出力を sim で作り切る

対象: `doc/midi/work_order_p5s.md`。
前提: [README.md](../README.md)、[p2.md](p2.md)、[p4.md](p4.md)。

UART MIDI 出力を sim で最後まで作り、**バイト列・タイミング・API を確定**した。
実機に残るのは配線の確認だけである (手順は 6 章)。

## 1. 作ったもの

| 位置 | 中身 |
|---|---|
| `lib/add/picoruby-fmrb-midi/include/fmrb_midi_serial.h` + `ports/esp32/fmrb_midi_serial.c` | UART を system 側で 1 本だけ開いて保持し、バイトを書く C 層 + 小さな Ruby バインディング |
| `lib/add/picoruby-fmrb-midi/mrblib/fmrb-serial-midi.rb` | `SerialTransport` / `Sam2695` / `FmrbMidi.sam2695_device`。**CIN ごとのバイト長など wire の話は全部ここ** |
| `lib/add/picoruby-fmrb-midi/mrblib/fmrb-midi.rb` | 発音予約を `NoteScheduler` として括り出し、両 transport で共用 |
| `components/fmrb_hal/platform/posix/fmrb_hal_uart_posix.c` | tty でないデバイスパス (FIFO) を受け付けるように修正 |
| `tools/fmrb_midi_monitor.rb` | sim の受け皿。FIFO を作り、届いたバイトを時刻つきで復号し、GM 音源にも流せる |
| `tool/midi/test/serial_midi_test.rb` | ホストで走る単体テスト (26 項目) |
| `flash/app/demo/midi_apu.app.rb` | 「7 Out」で出力先を APU / 外部 MIDI に切り替え |
| `flash/app/tool/smf_player.app.rb` | ファイル一覧つきの SMF プレーヤ (NSF Player と同じ作り)。再生中にテンポと出力先を変えられる |

アプリから見た形は Midori と揃えてある:

```ruby
sam = FmrbMidi.sam2695_device     # 内部で serial transport を組み立てて GM リセット
sam.program_change(0)
sam.note_on(60, 100)
```

## 2. 判断したこと

### 2.1 ランニングステータスは使わない (指示書 2.1)

**毎回ステータスバイトを送る**。理由は 2 つ:

- 受け側の実装差で事故りやすい。特に電源投入直後や途中から繋いだ受信機は、
  最初のステータスを取り逃すと以降のデータバイトを誤解釈し続ける。
- **帯域に余裕がある**。31250 baud は 1 バイト 320us なので 20ms に約 62 バイト
  流せる。同じ 20ms に和音 10 音を出しても 30 バイトで、**約半分**しか使わない。
  1 音 1 バイトを削る価値より、ストリームが常に自己記述的である価値が大きい。

ホストテストで「同じチャンネルの連続する note on が毎回 `90` を伴う」ことを
固定してある。

### 2.2 バイト長の表は Ruby に置いた

CIN (USB-MIDI の Code Index Number) から実際に送るバイト数を決める表は、
**C ではなく Ruby (`WIRE_BYTES`)** に置いた。picoruby-midi はどの transport でも
USB-MIDI 形式のパケットを作るので、serial 側はそれをほどく必要がある。

Ruby に置いた理由は**ホストでテストできるから**である。実際 26 項目のうち
9 項目が「この CIN は何バイトか」を直接見ており、UART もビルドも要らない。
C 側は「開く・閉じる・状態・バイトを書く」の 4 つだけになった。

Midori の `picoruby-uart_midi` の C 実装 (`wire_bytes_for_cin`) を読んで
同じ表にしてある。**値は一致**、置き場所だけが違う。

### 2.3 所有権は system 側 (指示書 2.1)

UART は **C 層が 1 本だけ開いて保持する**。2 回目以降の `_open` は既存の
ハンドルを共有し、Ruby には `MIDI::Device` の transport としてしか見えない。
hw_proxy の UART チャネルは作っていない。

書き込みはミューテックスで直列化する。**1 つのメッセージが別のアプリの
メッセージと混ざると、受信側はステータスバイトの後に違うデータバイトを
読むことになる**ため、ここは排他が要る (APU 経路と違い、壊れ方が静かでない)。

### 2.4 sim の受け皿は FIFO (指示書 2.2)

`fmruby-core/midi_out.fifo` に出す。理由は指示書の調査どおりで、
`./fmruby-core:/project` がホストのバインドマウントなので**コンテナからも
ホストからも同じものが見える**唯一の素直な場所だからである。

**ブロックの心配は無かった**。POSIX HAL は `O_RDWR | O_NONBLOCK` で開くので、
読み手がいなくても FIFO の open は成功し、書き込みも詰まらない
(パイプバッファに溜まり、モニタを後から起動すると溜まった分が読める。
実際にそうなった)。

ただし**1 つ修正が要った**: POSIX HAL は開いた直後に無条件で `tcgetattr` を
呼び、FIFO では ENOTTY で失敗して open ごと失敗していた。**tty でなければ
termios の設定を飛ばす**ようにした (FIFO にボーレートも 8N1 も意味がない)。
これが無いと sim では 1 バイトも出せない。

## 3. 検証 (sim で完結)

### 3.1 ホストの単体テスト (26 項目、すべて成功)

`ruby tool/midi/test/serial_midi_test.rb`。C を差し替えて、**書かれたバイトを
16 進で突き合わせる**。

| 見ているもの | 期待 | 結果 |
|---|---|---|
| Note On | `90 3C 64` | 一致 |
| Note Off | `80 3C 00` | 一致 |
| チャンネル 4 | `93 3C 64` | 一致 |
| Program Change | `C0 05` (**2 バイト**) | 一致 |
| Channel Pressure | `D0 40` (2 バイト) | 一致 |
| Control Change | `B0 07 64` | 一致 |
| MIDI Clock | `F8` (1 バイト) | 一致 |
| SysEx | `F0 7E 7F 09 01 F7` | 一致 |
| データを持たない CIN | 送らずに -1 | 一致 |
| 連続する note on | 毎回ステータス付き | 一致 |

加えて SAM2695 層 (起動時に GM リセット、`FmrbMidi.sam2695_device` が
`MIDI::Device` を返す、ポートが開かなければ nil)、`trigger` の予約と発火、
そして**同じ曲を APU と serial の両方に流して突き合わせる**節がある。

### 3.2 同じ曲が両経路で同じになること (完了条件 3)

ホストテストで、`scale.mid` を SMF プレーヤから両 transport に流した結果:

| | APU 経路 | serial 経路 |
|---|---|---|
| 音数 | 8 | 8 |
| 中身 | 音高テーブルの周波数 | ノート番号 60 62 64 65 67 69 71 72 |
| 発音時刻 | 0, 500, 1000, ... 3500 ms | **完全に同一** |

**ミリ秒単位で一致**する。P4 でプレーヤが出力先を知らない設計にした狙いが
そのまま確認できた。

### 3.3 実際に経路を通した実測 (sim)

デモアプリの「7 Out」で出力先を serial に切り替え、FIFO から受けた実バイト列:

```
90 3C 64   note on  ch1 C4 vel=100
80 3C 00   note off ch1 C4
90 3E 64   note on  ch1 D4 vel=100
...
```

| 測ったもの | 結果 |
|---|---|
| ノート番号 | `[60, 62, 64, 65, 67, 69, 71, 72]` (ファイルどおり) |
| note on / note off | 8 / 8 (取りこぼしなし) |
| 音符間隔 (x1.0) | **500, 500, 500, 500, 500, 500 ms** (指定どおり。最初の 1 つだけ 400-460ms で、これはアプリが再生を始めるまでの分) |
| 音符間隔 (`tempo_scale = 1.5`) | **332, 333, 334, 333, 333, 334 ms** (500/1.5 = 333.3) |
| SAM2695 の初期化 | `F0 7E 7F 09 01 F7` + 全 16ch の All Notes Off / All Sound Off = 33 メッセージ |
| 打楽器 (`3 Drums`) | `99 24 6E` (ch10 C2 = キック)、`99 2A 6E` (F#2 = ハイハット)、`99 26 6E` (D2 = スネア) の 8 発。**GM の打楽器マップどおり** |
| `trigger` の自動 note off | 発音の 90ms 後に `89 24 00` (指定した duration どおり) |

**P4 の sim 実測 (波形から) には系統誤差があった**が、こちらは経路を通った
バイトの到着時刻なので**そのまま正しい**。500ms 指定が 500ms で出ている。

出力先を serial にしている間、**APU 側は無音**であることも波形で確認した
(ピーク 0)。切り替えが本当に切り替わっている。

### 3.4 GM 音源で実際に鳴らす (指示書 2.2.1、成立した)

**成立した**。指示書の調査では未インストールとされていたが、実際には
`fluidsynth 2.2.5` と `FluidR3_GM.sf2` が**既に入っていた**ので apt は不要だった。

```
core (コンテナ) --FIFO--> fmrb_midi_monitor (ホスト) --stdin--> fluidsynth -a pulseaudio
```

再現手順:

```
# 1. 受け皿 + GM 音源を起動 (FIFO はこのツールが作る)
ruby tools/fmrb_midi_monitor.rb --fluidsynth \
     --soundfont /usr/share/sounds/sf2/FluidR3_GM.sf2

# 2. sim を起動して「MIDI APU」を開き、「7 Out」で出力先を serial にする
tools/dev_run_check.sh --keep      # 音も聞くなら --gui か通常の docker compose up

# 3. 「5 SMF」や「2 Chord」を押す
```

`fluidsynth` は起動時に ALSA シーケンサが無い旨のエラーを出すが**無害**である
(WSL2 の `/dev/snd` にはタイマしかない)。MIDI 入力を ALSA から受けないだけで、
こちらはコンソール (stdin) から流すので影響しない。音声出力は PulseAudio で出る。

**ここで 1 つ踏んだ**: 最初 fluidsynth を `-i` 付きで起動していた。`-i` は
**`--no-shell`**、つまり**この橋渡しが書き込む先のコマンド読み取りを止める**
オプションである (interactive の i だと取り違えていた)。シェルが無いので
`noteon` は黙って捨てられ、**エラーは一切出ないまま無音になる**。

紛らわしいのは、この状態でも起動ログが正常に見えることである
(`Using PulseAudio driver` まで出る)。切り分けには `channels` のような
**応答のあるコマンドを送って返事が来るか**を見るのが早い。返事が無ければ
シェルが動いていない。`-i` を外すと `chan 0, Yamaha Grand Piano` と返ってきて、
`noteon` も復唱される。

**ユーザの試聴で実際に鳴ることを確認済み** (ピアノ音でドレミ、その後
デモ曲も再生)。

### 3.5 実際の曲を流したときの帯域 (実機で効く数字)

sim の FIFO には速度制限が無いので詰まらないが、**実機の MIDI DIN は
31250 baud = 3125 バイト/秒**しか流せない。和音の厚い曲で足りるかを
ホスト側で測った (`tool/midi/test/` と同じ経路を使った使い捨てスクリプト)。

| 曲 | 同時最大 | 平均 | 最悪の 100ms | 判定 |
|---|---:|---:|---:|---|
| The Entertainer (Joplin) | 7 音 | 62 B/s | 36 B | 余裕 |
| Bethena (Joplin) | 7 音 | 63 B/s | 45 B | 余裕 |
| 羊は安らかに草を食み (BWV 208) | 4 音 | 40 B/s | 24 B | 余裕 |
| ポロネーズ (BWV Anh.117a) | 5 音 | 29 B/s | 30 B | 余裕 |

MIDI DIN は 100ms で 312 バイト運べるので、**最悪でも 14% しか使わない**。
ランニングステータスを使わない判断 (2.1) の裏付けにもなっている。
実機で Unit MIDI を挿したとき、これらの曲はそのまま鳴らせる見込みである。

### 3.6 デモ曲

`flash/data/midi/song.mid` に The Entertainer (Mutopia の Public Domain
表記のファイル) を置いた。デモは `song.mid` があればそれを、無ければ
`scale.mid` を再生する。出典とライセンスは
`flash/data/midi/README.md` に記録した。**PD の曲でも打ち込んだ人の権利が
別に生じうる**ので、サイト単位ではなくファイル自身のメタデータで確認している。

この曲は「7 Out」の切り替えで狙いがそのまま出る: GM 音源では 7 音の和音が
そのまま鳴り、APU では 3 声しかないので 653 回の奪い合いが起きて伴奏が
潰れる。**同じバイト列を出していて、差は音源側の都合だけ**である。

**Docker Compose からも同じことができる**。`docker-compose.midi.yml` と
`docker/midi-gm/Dockerfile` を足したので、ホストに何も入れずに済む:

```
docker compose -f docker-compose.yml -f docker-compose.wsl.yml \
               -f docker-compose.midi.yml up -d
```

`midi-gm` サービスが FIFO を作って読み、fluidsynth (PulseAudio) で鳴らす。
音声は sdl2-display と同じ WSLg の PulseAudio ソケットを通る。2 点だけ
実際に踏んだ注意がある:

- **root では鳴らない**。WSLg の PulseAudio ソケットはホストユーザのもので、
  root からの接続は Access denied になる。他のサービスと同じく
  `user: "${UID:-1000}:${GID:-1000}"` を指定してある。
- **FIFO のパーミッション**。作った側と読む側でユーザが違いうるので、
  モニタは `mkfifo -m 666` で作る。

この経路でも曲が届くことを確認した (音符間隔 500ms、8 音すべて到着)。

なお `--render out.wav` も用意したが、**fluidsynth の file ドライバは
MIDI ファイルの一括レンダリング専用**で、こちらのような実時間の入力では
使えなかった (`No midi file specified!`)。数値で確かめたい場合は
モニタの `--log` (メッセージと到着時刻の記録) を使う。

## 4. ついでに直したこと

### 4.1 `MIDI::Device#trigger` が全 transport に飛んでいた

P2 で入れた `MIDI._trigger` の代替実装が、**登録済みの transport すべてに**
発音していた。APU しか無いうちは表に出なかったが、serial を足した瞬間に
「APU に鳴らしたはずの音が外部にも出る」ことになる。

`MIDI::Device` が渡してくる mask (= その device の `transport_id`) と
**一致する transport にだけ送る**ようにした。C 実装の「種類ごとにブロード
キャストする」意味と同じで、APU (id 0) と serial (id 2) は互いに混ざらない。

### 4.2 発音予約の重複

`trigger` / `tick` / `next_due_in` を `FmrbMidi::NoteScheduler` として
括り出し、`ApuTransport` と `SerialTransport` の両方が include する形にした。
最初 serial 側に `trigger` が無く、デモのドラムボタンが `NoMethodError` で
落ちたのが発端である。

## 5. 申し送り

- **受信 (MIDI IN) は未実装**。`read_available` は空を返す。Unit MIDI の
  UART_TX は 5V の可能性があり、S3 の入力に直結できるか未確認なので、
  送信専用のまま P5 実機編に送る。
- **ピンの排他は入れたが sim では動かない**。`FMRB_PIN_USER_UART` で TX/RX を
  確保し、I2C と取り合ったら open を失敗させるコードは入っている
  (`claim_pins`)。pin manager は ESP32 のみなので**実機で確かめる**。
- **P4 のピン (Tab5) は未定のまま**。C の既定値は Retro (NARYAv3) 用で、
  Tab5 では `FMRB_MIDI_SERIAL_TX_PIN` などを足す必要がある。Ruby からは
  `FmrbMidi.sam2695_device(uart: 1, tx: 53)` のように上書きできる。
- **sim の FIFO は開発用**である。`fmruby-core/midi_out.fifo` はモニタが
  作るので、リポジトリには置いていない。
- デモの「7 Out」は SMF プレーヤだけでなく音階・和音・ドラムのボタンにも
  効く。**同じ材料を APU と GM 音源で聴き比べられる**ので、APU の 4 声制限に
  よる破綻か、プレーヤ側の問題かを切り分けられる (指示書 2.2.1 の狙い)。

## 6. 実機に残る作業 (P5 実機編の手順)

sim でバイト列・タイミング・API は確定した。実機で確かめるのは**配線と、
sim では動かない部分**だけである。上から順に実施する。

### 6.1 準備

```
cd fmruby-core
rake clean_all           # linux -> esp32 の切り替えに必要
rake build:esp32
rake check-port          # 初回のみ
FLASH_BAUD=115200 rake flash
```

### 6.2 UART2 が空いているか (P0-3 が未了)

ブートログで `midi_serial: MIDI serial out ready (... uart=2 tx=47 ...)` が
出れば開けている。出ない場合は `open failed` の行に理由が出る。

```
python3 ../tools/fmrb_serial_capture.py -t 20 boot.log
grep midi_serial boot.log
```

### 6.3 GROVE 端子2 の結線 (黄/白がどちらの GPIO か)

現在の既定は **TX = GPIO47 (I2C2_SDA と同じピン)**。I2C の命名からの推定なので、
逆の可能性がある。**確かめ方は「鳴るかどうか」で足りる**:

1. Unit MIDI を GROVE 端子2 に挿す (電源は端子から取れる。5V 側にすること)。
2. MIDI APU アプリを起動し、「7 Out」を押してから「1 Scale」を押す。
3. 鳴らなければ TX/RX が逆なので、`fmrb_midi_serial.c` の
   `FMRB_MIDI_SERIAL_TX_PIN` を `FMRB_PIN_I2C2_SCL` (GPIO48) に変えて再確認する。
   **配線をやり直す必要はない** (GPIO マトリクス経由なので入れ替えは software 側)。

### 6.4 ピンの排他が効くか

MIDI を開いた状態で I2C2 を使うアプリ (GPIO Viewer など) を起動し、
**I2C 側が明示的に失敗する**ことを確認する。逆順 (I2C が先) では
`midi_serial: TX pin 47 is already in use` が出るのが期待動作。

### 6.5 sim で確定した内容が実機でも同じか

- `tools/fmrb_midi_monitor.rb` の代わりに、**Unit MIDI の音**が答になる。
  音階・和音・ドラム・SMF 再生 (`5 SMF`) の 4 つを鳴らす。
- `tempo_scale` (「6 Fast」) が効くこと。
- **APU 経路 (P2/P4) が実機でも動くこと**。これは sim のみで確認済みなので、
  「7 Out」を APU 側に戻して同じ操作を繰り返す。

### 6.6 受信側の遅延計装の数字を採る (P2 で入れたもの)

APU 経路で曲を鳴らしながらログを採り、`audio_note_lat` の行を見る。

```
python3 ../tools/fmrb_serial_capture.py -t 40 play.log
grep audio_note_lat play.log
```

sim では平均 36us / 最大 93us (演奏程度の頻度) だった。**実機は UART 921600
を挟むので桁が変わるはず**で、ここが P0-1 で測れなかった end-to-end の
実測値になる。
