# P4 でファイル転送の直後に core 0 が落ちる件

> 状態: 進行中 | 更新: 2026-10-06 | P4-Nano で 64 KB の /fs/put・/fs/get の直後に core 0 のタスクが Instruction / Store access fault で落ちることがある。CONFIG_SPI_FLASH_AUTO_SUSPEND との組み合わせを疑う。まず計装なしのビルドで再現を確かめる

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

- 直し方 (原因次第)。
- Tab5 でも起きるか (device_check_backlog)。
