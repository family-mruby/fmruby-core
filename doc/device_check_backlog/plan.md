# 実機の確認が溜まっているもの (Tab5 / Retro)

> 状態: 計画済 | 更新: 2026-10-05 | P4-Nano (NARYAv4) と sim で確かめた変更のうち、Tab5 と Retro (S3) の実機でまだ動かしていないものの一覧。実機をつないだ日にまとめて確かめる (ユーザ決定: 後日)

## 進め方

- 最初に起動ログ (serial_start) でクラッシュの印 (Guru / abort) が無いことを見る。
- 実機の作業の前に、ミュートにする (tab5_audio、devctl `/audio/mute?on=1`)。Retro は遠隔の画面が無いので、操作はユーザに依頼する。
- 区画の変更があるもの (Tab5 の P4 の 7M) は全体の書き込みが要り、/home が消える。先にユーザに確かめる。

## Retro (S3、NARYAv3) と WROVER

| 項目 | 確かめること | 経緯 |
|---|---|---|
| 上流 Spinel の取り込み | カーネル・エディタ (Spinel) の起動と打鍵、アプリの起動と終了、Guru 0 | doc/spinel_upstream_ext (P2b-2 は P4-Nano のみ) |
| ミュートと音量 | 済 (2026-10-07、ユーザ確認で問題なし)。WROVER のファームの更新 (SET_OUTPUT 0x0F)、ミュート・音量の段・起動の音、再起動で残る | reference/audio_output.md |
| WROVER のフラッシュの書き込みと NTSC | Config で音量を動かすと WROVER が自分の設定ファイルを書く。映像が乱れないか | archive/audio_mute (8.4) |
| 内蔵 RAM の R2 相当 | S3 のファイル書き込みの跳ね返し (`#if CONFIG_IDF_TARGET_ESP32S3`) を外す (-8,192 B)。書き込み数 MB 以内の最小の試験 | iram_reduction (R4) |
| アプリの終了・二重解放・mrb_close | Shell・ゲーム・BASIC などの起動と終了をくり返す。estalloc の点検を外した後で落ちないか | shell_input_lag (S2c)、app_exit |
| 同じ Spinel プログラムの 2 つ目を断る、gem の持ち主、.app.toml の single_instance / exclusive_group | エディタの 2 つ目、断られた後のキー入力 | spinel_multi_instance (C1-G4) |
| Shell の打鍵、App Store のスタック | 打鍵の感じ、App Store の install の連打 | shell_input_lag、App Store 16 KB |

## Tab5 (P4)

| 項目 | 確かめること | 経緯 |
|---|---|---|
| 保存のちらつき | 済 (2026-10-07、ユーザの目視): ミュートを切り替えて少し待つと若干ちらつくことがある (約 2 秒後のまとめた書き込み) が、許容できる範囲 (ユーザ判断)。Config の Save・エディタの保存は問題なし。自動中断 (AUTO_SUSPEND) は入れない | flash_write_flicker、p4_transfer_crash |
| 上流 Spinel の取り込み | 7M の区画: 済 (2026-10-07 に develop を全体で書き込み、起動 Guru 0) | spinel_upstream_ext |
| 全画面の高解像度 | 済 (2026-10-07、ユーザ確認で問題なし)。H1-H4 の動き (エディタの全画面、フォント) | fullscreen_hires |
| ミュートと音量 | ES8388 の音量の書き方 (P4-Nano の ES8311 と違う)。段の大きさ、コーデックのミュート | reference/audio_output.md |
| マウスの遅れ・Shell の打鍵の感じ | 済 (2026-10-07、ユーザ確認で問題なし)。ユーザの体感で確かめる。アプリの終了 (16 回)・Spinel の多重起動の断り・exclusive_group は済 (2026-10-07、同 3 章) | p4_cursor_lag ほか |
| Zenoh (Asterism Z2) | 済 (2026-10-07、report/tab5_20261007.md 1 章): 往復・開き直し 5 回・問い合わせ・生存の監視、内蔵 RAM は閉じて 3 分で戻る | ruby_asterism (Z2) |
| ESP-Hosted の mempool なし | 済 (2026-10-07、同 2 章): 負荷の後は 1 回だけ -7.7 KB で以後は増えない、速さ同等、BLE 起動、書き込みで落ちない | hosted_mempool |
