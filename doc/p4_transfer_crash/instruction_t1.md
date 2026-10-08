# p4_transfer_crash T1 指示書: 再現と原因の絞り込み

> 状態: 指示 | 更新: 2026-10-06 | 計装なしの develop で再現を確かめ、AUTO_SUSPEND の有無で比べ、原因を突き止める。直すのは原因が分かってから

前提: 同じディレクトリの plan.md、hosted_mempool/report/m1.md 4-5 章 (再現の手順と落ちた記録)、
reference/cpu_usage.md 5 章、flash_write_flicker/plan.md と report/f1.md (AUTO_SUSPEND を入れた経緯)。

## 1. やること

1. **計装なしで再現**: develop (08090634 以降、mempool なし) を `FMRB_HW_TARGET=NARYAv4` で作り、P4-Nano に焼く。
   m1 と同じ手順 (64 KB の put / get / 照合をくり返す) で、落ちるかと頻度を数える。起動して落ち着いた後の最初の
   数回で起きやすいので、起動し直しを挟んで何周か回す。352 KB の put も試す。
2. **落ちたときの記録**: シリアルのログから Guru のレジスタ (MEPC、MTVAL、MCAUSE、RA) と backtrace を取り、
   addr2line でどこかを確かめる。MTVAL の番地がフラッシュのどの区画 (命令 / rodata) か内蔵 RAM かを分類する。
3. **AUTO_SUSPEND なしと比べる**: リポジトリの sdkconfig / defaults は変えずに、`CONFIG_SPI_FLASH_AUTO_SUSPEND=n`
   を書いた追加の defaults ファイル (スクラッチパッド) を `SDKCONFIG_DEFAULTS` の後ろに重ね、別の build ディレクトリ・
   別の sdkconfig で作る (cpu_usage.md 5 章の idf.py の例)。同じ手順・同じ回数で落ちるかを比べる。
4. **原因を絞る**: ESP-IDF v5.5.4 の spi_flash の suspend の実装 (`esp_flash_api.c`、`spi_flash_os_func_app.c`、
   `memspi_host_driver.c` ほか)、GD25Q128 の扱い (tSUS などの待ち時間の設定 `CONFIG_SPI_FLASH_SUSPEND_TSUS_VAL_US` 等)、
   P4 のキャッシュと suspend の組み合わせを読む。直す前に、IDF の release/v5.5 の HEAD と master で該当部分に
   修正が入っていないかを先に確かめる (Espressif の GitHub の issue・コミットも)。
5. **書き込みの経路**: /home のファイルシステム (littlefs か FAT か) が書き込みと消去をどう出しているか、
   書き込みの最中に動いている core 0 のタスク (audio_p4、fmrb_host) が何を読みにいっているかも確かめる。
6. 原因が分かったら、直し方の案を並べる。**直し方の実装は、保存のちらつきに影響しないもので、範囲 (3 章) の中に
   収まるものだけ行ってよい**。ちらつきが戻りうるもの、sdkconfig の変更が要るものは、案として返す。

## 2. 受け入れ条件 (T1)

- 計装なしで再現するかどうかが、回数とともに分かっている。
- AUTO_SUSPEND の有無で落ちる頻度が比べられている (同じ手順・同じ回数)。
- 落ちた記録 (MEPC / MTVAL / backtrace の分類) が report にある。
- 原因の見立てに、コードか IDF の修正の記録の根拠がある。直したなら、直した後に 64 KB・352 KB の put / get を
  くり返して落ちないこと。

## 3. 触ってよい範囲

- fmruby-core: `main/` と `components/` の中で、原因に関わる部分 (直すと決めた場合)。`doc/p4_transfer_crash/report/t1.md`。
  試験のビルドのための追加の defaults ファイルはスクラッチパッドに置く (リポジトリに入れない)。
- 触らないもの: `.env` (TAB5 のまま)、リポジトリの sdkconfig / sdkconfig.defaults*、managed_components / ESP-IDF の中、
  パーティションの表、fmruby-graphics-audio。

## 4. 止まる条件

- 直し方が sdkconfig の変更や AUTO_SUSPEND を切ることになるとき (案を書いて返す)。
- ESP-IDF の中を直す必要があるとき (修正の箇所と根拠を書いて返す)。
- 全体の書き込み (/home が消える) が要るとき。
- 実機のボタン操作が要るとき (落ちたまま戻らない、DL モードで止まったなど)。

## 5. report (`report/t1.md`) に書くこと

- 再現の表 (ビルド、起動、何回目で、種類、MEPC / MTVAL、場所)。
- AUTO_SUSPEND あり / なしの比較。
- 原因の見立てと根拠、撤回した仮説、直し方の案 (それぞれの影響: ちらつき、速さ、内蔵 RAM)。
- フラッシュへの書き込みの総量 (消耗の目安)。

## 6. 作業の決まり

- ブランチ `feature/p4-transfer-crash` を fmruby-core の develop から切る (英文のコミット、`<領域>: <要約>`、
  Co-Authored-By)。マージと push はしない。
- P4-Nano (192.0.2.15) はミュート中 (確かめる)。動いているアプリは終了させてよい。シリアルは親の capture が
  動いている (`serial_start` を呼ばない。`serial_log` で読む)。焼くのは MCP の `flash` (`app_only`)。試験のビルドを
  別の build ディレクトリで作ったときは、焼き方を工夫してよい (esptool を docker で直接、など。ポートは capture と
  取り合うので、MCP の flash と同じく capture を止めて再開する手順を守る。分からなければ止まって返す)。
- 書き込みの試験は /home に数 MB 以内。終わったら試験のファイルを消す。
- 終わったら `build/` を Linux の標準構成 (x86-64) に戻し、試験の build ディレクトリは消す。P4-Nano には develop を焼き戻す。
