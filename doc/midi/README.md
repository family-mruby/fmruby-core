# Family mruby MIDI 対応 検討メモ

Midori (PicoRuby ベースの MIDI ファームウェア) と互換性を持たせる形で、
Family mruby に MIDI 機能を入れるための方向性をまとめる。実装計画ではなく、
どの道を通るか・何を先に潰すかを決めるための文書。

最終更新: 2026-08-03 (方針決定 + 現行コードとの突き合わせ)

実装は `feature/midi` ブランチで進める。段ごとの指示は
[work_order_p0_p1.md](work_order_p0_p1.md) (完了) /
[work_order_p2.md](work_order_p2.md) (完了) /
[work_order_p4.md](work_order_p4.md) (着手中。P3 より先に P4 をやる)、
実装中に分かったことは [report/](report/) に置く。

## 0. 決定済みの前提

ユーザ判断により、以下は確定として検討する。

| 項目 | 決定 |
|---|---|
| 対象ボード | **S3 (NARYA) と P4 (TAB5) の双方で使える機能にする** |
| Family mruby の立ち位置 | **鳴らさせる側 (MIDI を送る制御機・シーケンサ)** |
| 内蔵音源 (APU) | **SMF で鳴らせるようにしたい** |
| 外部音源 | **M5Stack Unit MIDI (SAM2695) を使う** |
| MML | **共通化しない** (BASIC は Family BASIC 互換の別実装のまま。2026-08-03 変更) |
| USB-MIDI デバイス化 | **やらない** |

「双方のボードで使える」を優先すると、USB-MIDI デバイス化は自動的に外れる。
S3 の USB-OTG は host と device が排他で、Family mruby は HID host として
使っているため、device 側には回せない (ポート数の問題ではない)。
逆に USB-MIDI **host** と UART MIDI (Unit MIDI 等) は両ボードで成立しうる。

## 1. 現状の突き合わせ

### 1.1 Midori 側の構造

Midori は層がきれいに分かれており、そのまま借りられる。

- `picoruby-midi` が中核。`include/midi_transport.h` に
  `send_packet / read_bytes / bytes_available / is_connected / transport_id`
  だけの抽象 (以下 transport 抽象) を持つ。
- その下に transport の実装が並ぶ: `picoruby-usb_midi_host` /
  `picoruby-usb_midi_device` / `picoruby-uart_midi` / `picoruby-sam2695`。
- 上に発音予約 (scheduler。`trigger` の自動 note-off) と MIDI クロック、
  さらに `picoruby-midi-mml` (MML 解析 + 演奏)。
- mruby と mrubyc の両バインディングを持つ。Family mruby は mruby VM なので合う。
- 各 gem は独立リポジトリで、upstream PicoRuby への標準 gem 提出が最終段階まで
  進んでいる。Family mruby が 2 つ目の利用者になることは、その抽象が本当に
  移植可能かの検証になる。

### 1.2 Family mruby 側の構造

前提がかなり違うので、そのままは載らない。

- 音は NES APU エミュレータ。Ruby からは `FmrbAudio`
  (`lib/add/picoruby-fmrb-app/mrblib/fmrb-audio.rb`) が kernel メッセージ経由で
  `play` / `note_on(ch, freq, vol, duty, sweep)` / `note_off` / FMSQ スロットを叩く。
  実際の発音は子チップ (WROVER) または P4 ローカルの apu_emu。
- アプリは複数 VM で走り、ハードウェアは `hw_proxy` (gpio / i2c / rmt / file) 経由で
  system 側が所有する。**UART の proxy は存在しない**。
- USB は `main/drivers/usb/usb_task.c` が `usb_host_lib` と `hid_host` を握っており、
  HID 専用。Modern (P4) でも USB host は有効。
- gem の追加は submodule ではなく `lib/add/` へ置いて `family_mruby.gembox` に
  登録する方式。

### 1.3 ここから出てくる設計の芯

Family mruby を「送る側」にすると、送り先が 3 種類になる:

1. USB-MIDI host に挿した外部機器
2. UART MIDI の先にある外部音源 (M5Stack Unit MIDI = SAM2695) と、
   その先の DIN-5 MIDI 機器
3. **内蔵の APU**

3 を transport 抽象の 1 実装 (以下 APU transport) として実装すれば、
同じ Ruby コード・同じ MML・同じ SMF プレーヤが、外部機器にも内蔵音源にも
そのまま流せる。「内蔵音源を SMF で鳴らす」という要求も、SMF プレーヤの
出力先を APU transport にするだけで満たせる。

これが本検討の中心になる。副次的に、USB でも UART でも BLE でもない種類の
transport が 1 つ増えるので、Midori 側の抽象の妥当性検証にもなる。

## 2. 方向性

### 方向A: MIDI 送出の中核層を入れる (最優先)

`picoruby-midi` を `lib/add/` に取り込み、Family mruby の system 側に
MIDI サービスを置く。

- transport 抽象・発音予約・MIDI クロックは C 層をそのまま使う。
- **所有権は system 側**。Midori は Ruby タスク 1 本がハードを直接持つ前提だが、
  Family mruby は複数 VM なので、そのままアプリ VM に gem を入れると
  排他と所有権が壊れる。`FmrbAudio` と同じ形の `FmrbMidi` メッセージ、または
  hw_proxy の新チャネル越しにアプリから叩く形にする。
- Ruby 層 (`MIDI::Device` など) の見え方は Midori と揃えたい。アプリの移植性と、
  Midori 向けに書かれた例がそのまま動くことの価値が大きい。

### 方向B: APU transport (内蔵音源を MIDI の送り先にする)

`send_packet(cable, cin, b1, b2, b3)` を受けて、既存の APU コマンドに変換する。

- Note On/Off → `note_on(ch, freq, vol, duty, sweep)` / `note_off(ch)`。
  ノート番号から周波数、ベロシティから音量への変換が要る。
- **4 声しかない** (矩形波 2 + 三角 1 + ノイズ 1。`FMRB_APU_CH_PULSE1` /
  `PULSE2` / `TRIANGLE` / `NOISE`、fmruby-graphics-audio の
  `main/common/audio_commands.h`)。MIDI の 16 チャンネル・和音をどう割り当て、
  どう音を奪うかは実装より先に決める設計判断。
  既定案: MIDI ch1,2 -> 矩形波、ch3 -> 三角、ch10 (打楽器) -> ノイズ、
  それ以外は割り当て表で指定。
- **FMSQ 再生と note_on の同居**を先に確かめる。`play_slot` は instance を
  持ち (0=MAIN は NSF 再生と、1=SUB は note_on/off の効果音と、それぞれ
  インスタンスを共有する)、4 声はこのインスタンス単位の話になる。
  **BGM を鳴らしながら MIDI で発音したとき、声を奪い合うのか別系統なのか**で
  「BGM + 実時間演奏」が成立するかが決まる。P0 で確認する。
- Program Change / Control Change の大半は無視するが、duty と sweep を
  CC に割り当てると「ファミコン音源らしい表現」が MIDI から操作できる。
- **成立条件は遅延とスループットの両方**。Ruby -> kernel メッセージ ->
  UART link (921600) -> WROVER -> APU の往復が何 ms かに加えて、
  **和音の連打が詰まらないか**を測る。MIDI は 1 音 3 バイトが束で来るので、
  効くのは 1 回の往復時間より単位時間あたりのイベント数のほうである。
  同じ経路 (host メッセージキュー、長さ 128) には GFX も乗っており、
  実測で 160 cmds/s 級までは流れている。両方 P0 で測る。

### 方向C: SMF (.mid) 再生

やり方が 2 つあり、両方いる。

| 案 | 中身 | 向き先 | 長所 | 短所 |
|---|---|---|---|---|
| C-a 事前変換 | SMF -> FMSQ に変換して再生 | 内蔵音源のみ | 発音タイミングが正確 (フレーム駆動)、CPU 負荷が低い、BASIC の PLAY と同じ方式 | 外部機器に流せない、実時間の操作ができない |
| C-b 実時間再生 | SMF を解析して MIDI イベントを transport へ | 内蔵・外部の両方 | 経路が 1 本、テンポ変更や停止が効く | 遅延と負荷が乗る |

**推奨は C-b を基本、C-a を併存**。長い BGM や BASIC 互換の経路は C-a、
アプリから曲を操作する用途は C-b。変換ツール (SMF -> FMSQ) は
`fmrb-audio-tools` に Ruby で置く。Midori 側に `midi_to_mml.rb` /
`mml_to_midi.rb` があるので素材は揃っている。

C-a はハードウェアに一切依存しないので、**最初に着手できる**。

### 方向D: MML は共通化しない (2026-08-03 決定)

**当初は「MML 仕様を共通化したい」を前提に置いていたが、取り下げる。**
BASIC の MML は Family BASIC 互換という別の制約を背負っており、
Midori (`picoruby-midi-mml`) とは独立した実装のままにする。

P0-5 の差分調査 (`report/p0.md`) が、この判断を裏づけている。
**同じ字面で意味が違う箇所が 2 つあり、どちらも黙って音がずれる**:

| 項目 | BASIC の PLAY | picoruby-midi-mml |
|---|---|---|
| 音長 | 音名の後の 1 桁は**長さコード** (`C4` = 4 分音符の 6/8) | 数値は**分母** (`c4` = 4 分音符) |
| オクターブ | **`O3` の C が中央ハ** | **`o4` の c が中央ハ** |
| 変化記号 | 音名の**前** (`#C`)、フラット無し | 音名の**後** (`c+` `c-`) |

さらに BASIC 側は `:` で 3 声を書き分ける・音色 (`Y`/`M`) を持つ、
picoruby 側はループやタイや複付点を持つ、と守備範囲も違う。
無理に 1 つに寄せると、どちらの利用者にとっても既存の曲が壊れる。

**結論**: 2 つの MML 実装を併存させる。共通化の対象にするとしたら、
MML の文法ではなく**その先** (音高・音長の計算、FMSQ 書き出し、
MIDI 送出) であり、それは方向A/B の transport 抽象が既に担っている。
当初の進め方にあった P6 (MML 共通化) は**不要**。

### 方向E: transport 実装の追加

| transport | S3 (NARYAv3) | P4 (TAB5) | 備考 |
|---|---|---|---|
| APU (内蔵) | 可 | 可 | 方向B |
| **UART MIDI 出力 (Unit MIDI)** | **GROVE 端子2 (GPIO47/48) で確定** | 空き GPIO (割り当ては後で決める) | 下記 E-1。最初の外部出力はこれ |
| USB-MIDI host | 可 (要改造) | 可 (要改造) | 下記 E-2 |
| BLE-MIDI | 可 (BLE ドライバあり) | 要調査 | 将来。Midori 側も未実装 |

#### E-1: UART MIDI 出力 / Unit MIDI (両ボード共通の第一の外部出口)

ユーザ判断により、**S3 は GROVE 端子2 を使う**。両ボードで同じ形の外部出力に
なるので、S3/P4 双方対応という条件に最も素直に合う。

**ピン (S3: 確定)**

- **GROVE 端子2 = GPIO47/48**。`fmrb_pin_assign.h` では I2C2 として定義されているが、
  **プルアップが載っていない**ため I2C としての利用は外部部品前提であり、
  実質は汎用の 2 線端子。**UART として使用可能**であることをユーザが確認済み。
- I2C1 (GPIO14/21) は RTC と I2C キーボードが使う内部バスなので触らない。
- TX / RX の割り当ては Unit MIDI の結線で決まる (下記「接続先」を参照)。
  ESP32-S3 は GPIO マトリクス経由なのでソフトウェア側で入れ替えられ、
  逆だった場合も配線のやり直しにはならない。

**ピン (P4: 今回は決めない)**

Tab5 側のピン割り当ては**現時点では決めない**。空き GPIO があることは分かっているので、
transport 側は「ボード定義から TX/RX ピンと UART ユニット番号を受け取る」形にしておき、
実際の番号は後から `fmrb_pin_assign.h` に足す。実装が P4 で止まらないようにするのが要点。

**UART ユニットの空き**

- S3: UART0 = コンソール、UART1 = graphics-audio との通信 (`fmrb_hal_link_uart_esp32.c`
  が `UART_NUM_1`)。**UART2 が空いている**見込み (実機で確認)。
- P4: 使用状況は P4 のピン割り当てを決めるときに合わせて確認する。
- 汎用 UART の HAL (`components/fmrb_hal/platform/esp32/fmrb_hal_uart_esp32.c`) が
  すでにあるので、transport はこの上に載せられる。

**ピンの排他**

GROVE 端子2 のピンを UART に使うと、同じピンを I2C2 として使う道と両立しない
(プルアップ無しのため I2C 利用は元々外部部品前提だが、排他は必要)。
`components/fmrb_hal/fmrb_hal_pin_manager.h` (include/ の下ではない) の
`fmrb_pin_usage_t` に **`FMRB_PIN_USER_UART` が既に定義済み**なので、
MIDI transport の初期化時にここで確保し、I2C と取り合ったら明示的に失敗させる。
「GROVE 端子2 は I2C か MIDI のどちらか一方」という排他をユーザに見える形にするのが要点。

**接続先: M5Stack Unit MIDI (決定)**

外部音源は **Unit MIDI** を使う。GROVE ケーブル 1 本で挿さるので、
**自作回路は要らない**。

| 項目 | 値 |
|---|---|
| 音源 | SAM2695 (GM 互換、16 ch、64 音ポリ / エフェクト有効時 38 音) |
| 接続 | Grove HY2.0-4P、UART |
| 結線 | 黒 GND / 赤 5V / **黄 = ユニットの UART_RX** / 白 = ユニットの UART_TX |
| ボーレート | **31250** (Midori の `picoruby-sam2695` README が「SAM2695 は実際には常に 31250」と明記。M5 のページの 31520 は誤記) |
| 端子 | DIN-5 x2 (IN/OUT)、3.5mm ステレオジャック x3 (音声出力を含む) |
| 動作モード | Bypass (IN と OUT を直結) / Separate (内蔵音源で処理) |

**TX/RX の向きはこれで決まる**。Grove の I2C 用途では 黄 = SDA (GPIO47)、
白 = SCL (GPIO48) なので、Unit MIDI を挿すと 黄 = ユニットの RX = **こちらの
TX が GPIO47**、白 = ユニットの TX = **こちらの RX が GPIO48** になる。
ただし **Narya v3 の GROVE 端子2 の実際の結線 (どのピンが黄/白か) は基板側の
確認が要る**。逆なら GPIO マトリクスで入れ替えるだけで済む。

**電源: 解決済み**

**GROVE 端子2 は 5V / 3.3V を選択して供給できる** (ユーザ確認済み)。
Unit MIDI は 5V 側で駆動する。別途給電は要らないので、ケーブル 1 本で完結する。
選択がソフトウェア制御なら制御ピンを pin manager で扱う必要があるが、
`fmrb_pin_assign.h` にそれらしいピンは無いため、**基板側の設定 (ジャンパ等)**
と理解している。ソフト制御だった場合はピン定義の追加が要る。

**残る確認点**

- **黄/白がどちらの GPIO に来ているか**。I2C の命名 (SDA=47, SCL=48) からの
  推定なので、基板で確認する。逆でも GPIO マトリクスで入れ替えるだけ。
- **ロジックレベル**。5V 給電時にユニットの UART_TX が 5V で出るなら、
  ESP32-S3 の入力には直結できない。**送信専用 (MIDI OUT) で使う限り RX は
  繋がなくてよい**ので、まずは TX のみで通し、受信が要るときに確認する。
- Bypass モードがあるので、Unit MIDI は **DIN-5 の出口としても使える**。
  外部の MIDI 機器を鳴らしたいときも、抵抗 2 本の自作回路を作らずに
  このユニット経由で出せる可能性が高い。

**自作 DIN 出力 (当面不要)**

Unit MIDI を使う前提なら不要だが、必要になったときのために: MIDI OUT は
TX ピンから抵抗 2 本を介して DIN-5 (または TRS type A) に出すだけで、
絶縁も外部電源も要らない。MIDI IN は光結合が必要で、**当面は対象外**
(transport 抽象は入力も持っているので後から足せる)。

**gem の再利用 (調査済み)**

Midori の `picoruby-sam2695` は **`picoruby-uart_midi` の薄いラッパ (Ruby 43 行)**
で、SAM2695 固有の既定値 (31250 baud) と、将来の GM/GS リセット等を置く場所を
持つだけだった。したがって取り込む本体は **`picoruby-uart_midi`** で、
sam2695 はその上の小さな層として一緒に持ってくればよい。

`SAM2695.new(tx_pin, rx_pin = -1)` と **rx_pin 省略で送信専用**が既にサポート
されている。GROVE 端子2 に TX だけ繋いで使う形がそのまま書ける。

**Ruby からの見え方**

Ruby から UART を触る経路 (hw_proxy の UART チャネル) は現状無い。
**transport を C 層に閉じ、Ruby には `MIDI::Device` としてだけ見せる**のを推奨。
汎用の UART proxy を先に作ると、ピン所有権と排他の問題が MIDI と無関係に広がる。

#### E-2: USB-MIDI host

`picoruby-usb_midi_host` は `USB_MIDI_HOST_start_driver()` の中で自分で
`usb_host_install()` してしまうが、Family mruby では `usb_task.c` がすでに
ホストを立てている。**「すでに立っているホストにクライアントとして相乗りする」
入口を分ける**必要がある。`usb_host_lib` はクライアントを複数登録できるので、
HID と MIDI は同居できる。この分割は Midori 側 (および upstream 標準化) にも有益。

UART より手数が多いので、外部出力の第一手は E-1 とし、こちらは後続とする。

### 方向F: Linux シミュレーションの MIDI 経路 (開発の足場)

sdl2 側プロセスに ALSA の仮想 MIDI ポートを開き、socket 経由の transport を
1 つ足す。これがあると、方向A・B・C・D の設計の大半が実機なしで詰められる。
自律検証 (headless 起動 + 画面確認 + 入力注入) の枠組みに MIDI が乗る。

投資対効果が高いので、ハードウェア側 (方向E) より先に置く。

### 方向G: gem を共有資産として育てる

Family mruby から Midori 側 / upstream に返せるもの:

1. `usb_host_install` 相乗りモード (方向E で必要)
2. APU transport — USB でも UART でも BLE でもない種類の transport 実装例
3. `picoruby-ble_midi` (Midori 側で将来追加扱い。Family mruby core には
   BLE ドライバがあるので、こちら発で出せる)

運用上の注意: Family mruby は gem を `lib/add/` にコピーする方式、Midori は
submodule 方式。共有するなら `repos.yaml` と Rakefile での扱いを決める。
当面は `lib/add/` にコピーし、変更点が溜まったら上流へ返す形が現実的。

## 3. 先に潰すべき制約

- **APU への発音遅延**。方向B の成否を決める。実機と sim の両方で測る。
- **APU は 4 声**。和音とチャンネル割り当ての方針を決めないと、SMF 再生の
  聞こえ方が破綻する。
- **S3 の UART2 が空いているかの確認**。ピン (GROVE 端子2 = GPIO47/48) は確定済み。
- **GROVE 端子2 の排他**。同じピンを I2C として使う道と両立しないので、
  pin manager で確保して衝突を明示的に失敗させる。
- **P4 のピン割り当てが未定**。transport をボード定義から受け取る形にしておき、
  P4 の番号が決まっていないことで実装が止まらないようにする。
- **GROVE 端子2 の結線とロジックレベル**。電源は 5V を選択できることを確認済み。
  黄/白がどちらの GPIO に来ているかと、受信を使う場合のレベル整合は基板で
  確認する (E-1 参照)。送信専用で始めれば後者は回避できる。
- **picoruby のバージョン差**。Family mruby は独自パッチが多く、Midori は
  `picoruby-esp32` をほぼそのまま使う。`picoruby-machine` 依存部の API 差が
  移植時に効く。
- **複数 VM とハードウェア所有権**。gem をアプリ VM に直接入れない。
- **音の確認は headless では取れない**。FMSQ から WAV を起こして周波数解析する
  手はあるが、最終確認はユーザの試聴になる。
- **S3 の flash 残量**。2026-08-03 の WiFi 対応で app パーティションを
  3M -> 4M に広げたが、networking gem (mbedTLS 込み) を積んだ結果、残りは
  24% (約 1MB) しかない。`picoruby-midi` + transport 数種 + MML gem は
  ここに効くので、**gem を入れた直後にイメージサイズを確認する**
  (P0 の項目に含める)。足りなければパーティションを更に広げる余地はある
  (16MB のうち使用は約 9MB)。ただしパーティション変更はフル書き込みが要る。

## 4. 進め方の案

| 段 | 内容 | ハード依存 | 目的 |
|---|---|---|---|
| ~~P0~~ | 計測と調査 (**Linux 分は完了**、実機分は残) | 実機/sim | APU 発音遅延**と和音連打のスループット**、FMSQ 再生中の note_on の挙動 (instance/声の奪い合い)、S3 の空き UART ユニット確認、gem 追加後の flash サイズ、MML 方言差分表 |
| ~~P1~~ | SMF -> FMSQ 変換 (方向C-a) **完了** | 無し | 先に成果が出る。以降のテスト素材にもなる |
| ~~P2~~ | MIDI 中核層 + APU transport (方向A/B) **完了** | 無し (sim) | 「内蔵音源を MIDI で鳴らす」が成立した |
| P4 | 実時間 SMF プレーヤ (方向C-b) **着手中** | 無し | アプリから曲を操作できる |
| P3 | sim の MIDI 経路 (方向F) | 無し | 外部機器へ流すための足場。P5 の直前でよい |
| P5 | **UART MIDI 出力 / Unit MIDI (方向E-1)** | 実機 (GROVE ケーブルのみ、給電も端子から) | 外部音源を鳴らす第一手。S3 の GROVE 端子2 に Unit MIDI を挿す。まず送信のみ |
| P6 | USB-MIDI host 相乗り (方向E-2) | 実機 | 挿した機器を鳴らす。手数が多いので後 |
| P7 | アプリ (シーケンサ、パッド、MML プレーヤ) | - | Midori のアプリを移植 |

P0/P1 の結果は `report/p0.md` / `report/p1.md`。**P2 は Ruby 実装で足りる**
見込みが立った (送出 1 回 115us、必要量とは 1 桁以上の開き)。また
**BGM (MAIN) と実時間演奏 (SUB) は声を奪い合わない**ことが確認できたので、
方向B は「SUB の 4 声に割り当てる」と読み替えてよい。
P5 は Unit MIDI を GROVE 端子2 に挿すだけで試せる (電源も端子から取れる)。
基板改版も自作回路も待つ必要はない。

## 5. 未決事項

- APU transport の MIDI チャンネル割り当て表の既定値。
- SMF プレーヤを C に置くか Ruby に置くか (P0 の遅延計測後に決める)。
- gem を submodule で共有するか `lib/add/` コピーのままにするか。
- **P4 (TAB5) の UART MIDI のピン割り当てと UART ユニット番号**。今回は決めない。
  S3 側が通ってから、実機の空き状況を見て決める。
- BLE-MIDI をやるかどうか (当面は対象外でよい)。
