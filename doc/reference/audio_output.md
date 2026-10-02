# 音の出力: ミュートと音量 (使い方と仕組み)

> 状態: 完了 | 更新: 2026-10-02 | 本体の音を一括で消すミュートと 0-10 の音量。音源は動かしたまま、出力の最後の段で無音・倍率をかける。全機種・sim・ブラウザ版で同じ。経緯は archive/audio_mute/

## 使い方

| 口 | 操作 |
|---|---|
| メニューバー | 時計のすぐ左のスピーカの印をクリックでミュートの入・切。鳴るときは波、ミュート中は白地に ×。遠隔での変更も 1 秒以内に表示に出る |
| メニュー | 「消音 / Mute」「消音を解除 / Unmute」 |
| Config | 「音量 / Volume」0-10。矢印で動かすとその場で聞こえる。保存は Save のときだけ。Save せずに閉じると元の値に戻る |
| アプリ | `FmrbApp.set_audio_mute(bool)` / `FmrbApp.audio_muted?` / `FmrbApp.set_audio_volume(n)` / `FmrbApp.audio_volume` (mruby と Spinel) |
| devctl (開発用の HTTP) | `GET /audio/mute`、`POST /audio/mute?on=0\|1`、`GET /audio/volume`、`POST /audio/volume?level=0..10`。答えは `{"ok","muted","volume"}` |
| debugd (sim) | `audio` 命令 (`on`、`volume` は省略可) |
| MCP (親リポジトリ) | `tab5_audio` / `sim_audio` (action: get / mute / unmute / volume)。実機を触る作業の前にミュートにする |

アプリはミュートを知らずに普段どおり鳴らす。ミュートを外すと、鳴っている曲は途中から聞こえる。
外部 MIDI 出力 (シリアル) は本体の音ではないので止めない。

## 設定 (system_conf)

| キー | 既定 | 意味 |
|---|---|---|
| `audio_mute` | false | ミュート |
| `audio_volume` | 7 | 音量の段 (0-10、0 は無音) |
| `audio_level_min` | -50 | 段 0 の名目の大きさ (dB)。設定ファイルだけ |
| `audio_level_max` | 0 | 段 10 の大きさ (dB)。設定ファイルだけ |

- 段 n (1-10) は `min + (max - min) * n / 10` dB。既定では 1 段 5 dB、7 = -15 dB、10 = 0 dB。
- 0 dB はハードが持ち上げずに出せる最大 (ソフトの倍率は等倍)。上限を 0 dB より上にできる出力もある
  (ES8311 は +32 dB まで) が、音が割れる。-50 dB の根拠は、それより下では APU の音がノイズと区別できない
  こと (sim で測定)。
- 設定は起動のごく早い段階で読まれ、起動の音 (P4 の「ピコ」、デスクトップのジングル、WROVER の起動の音)
  より前に効く。
- **保存のまとめ方**: フラッシュへの書き込みは画面をちらつかせうる (doc/flash_write_flicker/) ので、Config は Save のときだけ、メニューバー・メニュー・devctl・debugd
  からの変更は最後の変更から約 2 秒後に 1 回だけ書く。メニューからの再起動の前には書く。**最後の変更から
  約 2 秒のうちに電源を切ると、その変更は残らない**。

## 仕組み

すべての音は最後に 1 か所を通り、そこで無音・倍率をかける。命令の種類で鳴らす・鳴らさないを分けない
(抜けと状態の食い違いが出るため。ユーザの設計判断)。

| 機種 | 最後の段 | 音量のかけ方 | ミュート |
|---|---|---|---|
| NARYAv4 (ES8311) | audio_p4 のフレームループ | コーデックの音量 (DAC レジスタ 0x32、0.5 dB 刻み、0xBF = 0 dB) | サンプルを 0 + コーデックのミュート |
| Tab5 (ES8388) | audio_p4 のフレームループ | コーデックの音量 (LDACVOL / RDACVOL、0.5 dB 刻み、0x00 = 0 dB)。表示ドライバの I2C 経由 | サンプルを 0 + DACControl3 の bit 2 |
| ブラウザ版 | audio_p4 → AudioWorklet | ソフトの倍率 (Q16) | サンプルを 0 |
| Retro (WROVER)・sim | graphics-audio の `audio_out_write` | ソフトの倍率 | サンプルを 0 |

- core から graphics-audio へは `FMRB_AUDIO_CMD_SET_OUTPUT` (0x0F、両リポジトリの `audio_commands.h`。
  `{cmd_type, muted, volume, level_db_x10}`) を変更のたびと、INIT_DISPLAY の直後に送る。古い WROVER の
  ファームはこの命令を知らず、1 行の警告を出してミュート・音量なしで鳴り続ける。
- graphics-audio は最後の設定を自分のフラッシュ (`flash/etc/audio_output_{linux,esp32}.txt`) に持ち、
  core とつながる前の起動の音にも効かせる。
- 経路の一覧 (アプリの FmrbAudio、C の note 経路、MIDI の内蔵 APU、BASIC、MicroPython、起動の音) と、
  どれも最後の段を通ることの確認は archive/audio_mute/report/m1.md の 2 章。

## 未確認・残り

- Retro の実機での確認 (WROVER のファームの更新が要る)。
- WROVER は SET_OUTPUT を受けて値が変わるたびに自分のフラッシュに書く (Config のプレビューの 1 段ごとにも)。
  WROVER の NTSC 映像がフラッシュの書き込みで乱れるかは未確認。乱れるなら graphics-audio 側もまとめる。
