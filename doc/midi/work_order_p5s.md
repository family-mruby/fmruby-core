# MIDI 対応 作業指示書 (P5-sim: 外部 MIDI 出力を sim で作り切る)

対象: 実装セッション (Opus)
ブランチ: `feature/midi`
前提: [README.md](README.md)、[report/p2.md](report/p2.md)、
[report/p4.md](report/p4.md) (**先に読むこと**)

## 0. この段の位置づけ

ユーザは **Unit MIDI (SAM2695) を持っているが、実機を触れないタイミングがある**。
そこで **UART MIDI 出力を sim で作り切り、実機ではケーブルを挿して
確かめるだけ**にする。README の方向F (P3) と方向E-1 (P5) を、この目的で
1 つにまとめた段である。

**実機に残す作業を「配線が正しいか」だけにするのがゴール**。
バイト列の中身・タイミング・API はすべて sim で確定させる。

## 1. 使える足場 (調査済み)

### UART HAL は POSIX 実装がある

`components/fmrb_hal/fmrb_hal_uart.h` の API は
**ESP32 と POSIX の両方に実装がある**:

| | ESP32 | POSIX (sim) |
|---|---|---|
| 実装 | `platform/esp32/fmrb_hal_uart_esp32.c` | `platform/posix/fmrb_hal_uart_posix.c` |
| 開き方 | `uart_num` + `tx_pin` / `rx_pin` | **`device_path`** (`/dev/pts/N` など) |
| ボーレート | 実際に効く | termios で設定 (相手が pty なら意味は持たない) |

つまり **transport のコードを 1 本書けば、sim では pty や仮想シリアルに、
実機では GPIO47/48 に、同じ API で出せる**。sim 用に別実装を書く必要はない。

### Unit MIDI の仕様 (決定済み、README E-1)

- SAM2695、GM 互換、16 ch、**31250 baud**、Grove HY2.0-4P。
- **黄 = ユニットの UART_RX** なので、**こちらの TX が GPIO47**。
- Midori の `picoruby-sam2695` は `picoruby-uart_midi` の薄いラッパ (43 行)。
  **`SAM2695.new(tx_pin, rx_pin = -1)` と送信専用が既にサポートされている**。

## 2. やること

### 2.1 シリアル MIDI transport

`send_packet(cable, cin, b1, b2, b3)` を **MIDI のバイト列**にして UART へ出す。

- USB-MIDI パケット (CIN + 3 バイト) から**実際に送るバイト数が決まる**
  (Note On/Off は 3、Program Change / Channel Pressure は 2、
  System Common は種類による)。**CIN ごとの長さ表が要る**。
  Midori の `picoruby-uart_midi` (`src/` と `mrblib/`) に既にあるはずなので、
  **まず読んでから書く**こと。
- **ランニングステータスは使わない**ことを推奨 (毎回ステータスバイトを送る)。
  受け側の実装差で事故りやすく、31250 baud に対して曲の情報量は十分小さい。
  採否と理由を報告に書くこと。
- **所有権は system 側**。README E-1 のとおり、UART を Ruby から直接触る
  経路 (hw_proxy の UART チャネル) は作らない。**C 層に閉じ、Ruby には
  `MIDI::Device` の transport としてだけ見せる**。
  P2 の `ApuTransport` と同じ見え方にすること。

### 2.2 sim での受け皿 (ここが今回の肝)

**送ったバイト列を host 側で観測できるようにする**。方式は判断してよいが、
**pty (`openpty`) が最も素直**と思われる: POSIX HAL が `device_path` で
開ける形になっているので、sim 側で pty を作り、その片側を core に渡し、
もう片側を host のツールで読む。

満たしてほしい条件:

- **バイト列をそのまま記録・検証できる**こと (16 進で突き合わせられる)。
- **タイミングが分かる**こと。各バイトの到着時刻が採れると、
  テンポや note_off の間隔を実測できる (P4 のホストテストは Ruby 内で
  完結していたが、こちらは経路を通った実測になる)。
- 可能なら **host の音源で実際に鳴らせる**とよい (`aconnect` で ALSA の
  仮想 MIDI ポートに繋ぐ、`fluidsynth` / `timidity` に流す等)。
  **必須ではない** (ユーザの環境に依存するため)。できるなら
  「こうすれば鳴らせる」を報告に書く程度でよい。

### 2.3 Unit MIDI (SAM2695) 層

`picoruby-sam2695` 相当を薄く載せる。**Midori の API をなるべくそのまま**
(P2 と同じ方針):

```ruby
sam = FmrbMidi.sam2695_device      # 内部で serial transport を組み立てる
sam.program_change(0)             # GM 音色
sam.note_on(60, 100)
```

- GM リセットなど SAM2695 固有の初期化があるなら入れる
  (Midori の実装を読んで判断)。
- **ピン番号やデバイスパスはボード定義から受け取る**形にしておくこと
  (README E-1 の「P4 のピンは後で決める」に対応するため)。

### 2.4 ピンの排他 (実機用。sim では効かないが書いておく)

GROVE 端子2 を UART に使うと I2C2 と両立しない。
`components/fmrb_hal/fmrb_hal_pin_manager.h` の
`FMRB_PIN_USER_UART` で確保し、**I2C と取り合ったら明示的に失敗させる**。

sim では pin manager が動かない (ESP32 のみ) ので**動作確認はできない**。
**コードを入れておくところまで**でよい。実機で確かめるのは P5 実機編。

## 3. 検証 (sim で完結させる)

1. **バイト列の正しさ**: `scale.mid` を SMF プレーヤ (P4) から serial
   transport へ流し、**pty で受けたバイト列を復号して期待と比較**する。
   `90 3C 64` (Note On ch1 C4 vel100) のような形で突き合わせられること。
2. **タイミング**: 受信時刻から音符間隔を実測し、**500ms 指定どおりか**
   確認する。P4 のホストテストと同じ曲・同じ期待値が使える。
3. **`tempo_scale` が効くこと**: 経路を通った実測で比が合うこと。
4. **APU と外部で同じ曲が同じに鳴ること**: 同じ `scale.mid` を
   `ApuTransport` と serial transport の両方へ流し、
   **音高 (MIDI ノート番号) と発音間隔が一致する**ことを確認する。
   P4 でプレーヤが出力先を知らない設計にした狙いが、ここで効くはず。
5. ホストで走る単体テスト (`tool/midi/test/`) を優先する。
   CIN ごとのバイト長やランニングステータスの判断は、**UART を通さずに
   テストできる**はず。

## 4. 実機に残す作業 (P5 実機編。今回はやらない)

報告書の最後に、**実機で何をどう確かめるかの手順**を書いておくこと。
実機が使えるタイミングで、それを見ながら進められる形にする。

- S3 の **UART2 が空いているか** (P0-3 が未了のまま)。
- GROVE 端子2 の**黄/白がどちらの GPIO に来ているか** (I2C の命名からの推定)。
- Unit MIDI を挿して**実際に鳴るか**。
- pin manager の排他が効くか。
- P2/P4 の APU 経路が実機でも動くか (sim のみで確認済み)。
- 受信側の遅延計装 (P2 で入れた `audio_note_lat`) の数字を採る。

## 5. 守ること

- 報告は `doc/midi/report/p5s.md`。
- **`doc/midi/README.md` は書き換えない**。前提が覆ったら報告書に書く。
- `lib/add/picoruby-midi/` は**無改変を保つ** (md5 一致を維持)。
  追加は `picoruby-fmrb-midi` 側へ。
- **sdl2-display を変更した場合は `docker compose build sdl2-display` が要る**
  (ルート `CLAUDE.md`)。
- コメントは英語、コミットログは英文、ASCII 以外の記号は使わない。
- `lib/add/` を編集したらビルド前に `rake clean`。
- **作業ツリーは Linux ビルド**。この段は sim で完結する。
- 音の最終確認はユーザ。

## 6. 完了の条件

1. SMF プレーヤから serial transport 経由で MIDI バイト列が出る。
2. 出たバイト列を sim 側で受けて**内容とタイミングを検証**した。
3. 同じ曲が APU 経路と外部経路で**同じ音高・同じ間隔**になる。
4. `FmrbMidi.sam2695_device` 相当が Midori と同じ見え方で使える。
5. 実機での確認手順が報告書に書かれている (4 章)。
