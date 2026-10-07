# P4 でファイル転送の直後に core 0 が落ちる件

> 状態: 完了 | 更新: 2026-10-07 | 原因は P4 での CONFIG_SPI_FLASH_AUTO_SUSPEND の既定の方式 (中断の完了を固定の時間で待つ)。消去・書き込み中のキャッシュの取り込みがときどきバスのエラーになり、どちらのコアでも access fault になる。**CONFIG_SPI_FLASH_AUTO_CHECK_SUSPEND_STATUS=y を NARYAv4 の defaults に採用 (ユーザ承認、6fe8f050)**。計装なしで 64 KB の put 15 回・352 KB 3 回とも 0 回。保存のちらつきが戻っていないことをユーザが目視で確認 (2026-10-07)

## 目的

遠隔のファイル転送 (tab5_fs、アプリの配布、エディタの保存など、フラッシュへの大きめの書き込み) で実機が落ちない
ようにする。原因を突き止めて直す。

## 分かっていること

- hosted_mempool/report/m1.md 4 章 (2026-10-06、P4-Nano、調査用の計装を入れたビルド):
  `/fs/put` (/home へ 64 KB) と `/fs/get` をくり返すと、get の直後に core 0 で Guru Meditation。6 回の起動のうち
  4 回。種類は Instruction access fault (フラッシュ上の命令) と Store access fault (内蔵 RAM の .bss)。落ちた
  タスクは audio_p4 と fmrb_host、場所は `i2s_channel_write`・`audio_p4_hw_write`・`fmrb_wav_stream_mix`・
  `fmrb_transport_process` とばらばら。起動して落ち着いた後の最初の数回で起きやすい。
- reference/cpu_usage.md 5 章: CPU 統計の計測版のビルドで、352 KB の `/fs/put` で core 0 が Instruction access
  fault (2 回)。そのときは develop では同じ操作が通った。
- 両方とも計装を入れたビルド。計装なしのビルドで起きるかは確かめていない。
- NARYAv4 だけが `CONFIG_SPI_FLASH_AUTO_SUSPEND=y` (flash_write_flicker で採用、2026-10-02)。書き込み・消去の
  途中で中断してキャッシュからの読み出しを許す機能。Tab5 (p4 の defaults) には入っていない。f1 の検証の書き込みは
  2 KB で、64 KB の転送は試していない。
- 見立て (未検証): 消去の中断・再開の境目で、フラッシュからの命令・データの読み出しが壊れた値を返す、または
  中断が効かないまま読み出している。落ちる場所がばらばらなのはこの見立てと合う。

## 方針

1. 計装なしのビルド (develop) で再現を確かめ、起きる頻度を数える。
2. AUTO_SUSPEND を切った試験のビルドと比べる (リポジトリの sdkconfig / defaults は変えず、追加の defaults ファイルを
   別の build ディレクトリで重ねる。cpu_usage.md 5 章と同じやり方)。
3. 原因を絞る: ESP-IDF の該当部分 (spi_flash の suspend、GD25Q128 の扱い、P4 のキャッシュ) と、IDF の release
   ブランチの HEAD / master での修正の有無を先に確かめる。
4. 直し方は原因が分かってから決める。**保存のちらつきが戻る直し方 (AUTO_SUSPEND を切るなど) を選ぶ必要が出たら、
   ユーザに相談する**。

## 受け入れ条件

- 原因が分かり、直した後に 64 KB と 352 KB の put / get をくり返しても落ちない (回数は report に)。
- 保存のちらつきが戻らない (戻る直し方ならユーザが決める)。
- フラッシュの消耗に気をつける: 書き込みの試験は /home へ数 MB 以内。くり返しの多い試験は /tmp (RAM) で
  足りるならそちらで。

## 未確定事項

- 直し方: 決定 (下の「結果」)。
- Tab5 でも起きるか (device_check_backlog)。Tab5 は自動中断を使っていないので、この仕組みでは起きないはず。2026-10-07 の Tab5 の確認で 64 KB の書き込み 10 回は落ちなかった。

## 結果 (2026-10-07)

| 段階 | 内容 | 状態 |
|---|---|---|
| T1 | 再現・自動中断の有無の比較・原因の絞り込み・直し方の確認 (report/t1.md 1-8 章) | **完了** |
| 採用 | `CONFIG_SPI_FLASH_AUTO_CHECK_SUSPEND_STATUS=y` を `config/sdkconfig.defaults.naryav4` に追加 (ユーザが案 A を選択、6fe8f050)。確認は report/t1.md 9 章 | **完了** (実機で 0 回) |
| 目視 | 保存のちらつきが戻っていないこと (ユーザ) | **完了** (2026-10-07、ちらつきなし) |

- 原因: P4 で自動中断を使うと、IDF の既定の方式 (中断の後に固定の時間待つ) で、消去・書き込みの最中のキャッシュの取り込みがときどき
  バスのエラーになる。フラッシュの命令・rodata、PSRAM、内蔵 RAM のどれへのアクセスでも、どちらのコアでも落ちる。IDF v5.5 は P4 を
  自動中断の対象に挙げておらず、同じ症状の報告がある (espressif/esp-idf#18846)。tRS の不足は撤回した (report/t1.md 5 章)。
- 内蔵 RAM・速さに差は無い。Tab5 は自動中断を使っていないので対象外。
- 専用基板で flash の型番を変えるときは、自動中断の対応表 (flash_write_flicker/plan.md) に加えて、中断の後の WIP / SUS ビットの
  振る舞い (この設定の前提) もデータシートで確かめる。

## 残した選択肢: Tab5 に自動中断を入れる (2026-10-07 ユーザ決定で今はやらない)

- Tab5 のフラッシュは XMC (ID 0x464018、16 MB)。IDF は XMC の D シリーズ (0x46) を自動中断に対応するチップとして扱う
  (`spi_flash_chip_generic.c`) ので、入れても起動で止まらない。
- Tab5 はミュートを切り替えて少し待つと若干ちらつく (許容範囲、ユーザの目視)。自動中断でこれが消える見込み。
- 入れるなら P4-Nano と同じ形: `CONFIG_SPI_FLASH_AUTO_SUSPEND=y` と `CONFIG_SPI_FLASH_AUTO_CHECK_SUSPEND_STATUS=y` を
  `sdkconfig.defaults.p4` に入れる。その前に、XMC のデータシートで中断の後の WIP / SUS ビットの振る舞いを確かめ、
  report/t1.md の落ちやすくする試験 (書き込み中に別のタスクで rodata を読み続ける) を Tab5 で回す (書き込み 2-5 MB)。
