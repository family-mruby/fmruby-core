# 3.1.0 の前の直し その 1 (小さい物 5 つ)

先に読む: `doc/release_3_1/plan.md` (3 章の表)、`doc/app_uart/plan.md` と `report/u1.md`、`doc/reference/flash_layout.md`。
ブランチは今の `app-uart` (UART の U1・U2 が入っている)。直しごとに別のコミットにする。

## 1. ls でフォルダに `/` が付かない (ESP32)

`components/fmrb_hal/platform/esp32/fmrb_hal_file_esp32.c` の `fmrb_hal_file_readdir` が `info->is_dir` だけを埋め、
`info->mode` を 0 のままにしている。`main/app/fmrb_boundary_file.c` の `fmrb_bnd_file_list_raw` は `FMRB_S_ISDIR(info.mode)`
を見るので、ESP32 では全部ファイル扱いになる (sim・ネイティブ版は正しい)。readdir の中で既に `stat` しているので、
その結果から `mode` を埋める (stat が失敗したときは `d_type` から)。同じ構造体を使うほかの所 (`is_dir` を見る所、
POSIX の実装) と食い違わないこと。直す前に、ESP32 の他の HAL の関数 (stat など) が mode をどう埋めているかを読む。

## 2. メニューバーの印「RS」を Modern とブラウザ版だけに

リモートシェルの対象は Tab5・P4-Nano・ブラウザ版 (doc/remote_shell)。印を描くコードがデスクトップに入り、Retro でも
デスクトップのプールが 4.7 KB 増えた。Retro (S3) のデスクトップから外す。デスクトップは Spinel で、機種で分ける今ある
やり方 (ビルドの定義・`# combine:`・機種ごとの部品の差し替えなど) を読んで、それに倣う。`regress:run` の Retro の
デスクトップの数が元 (リモートシェルの前) に戻ることを、`rake regress:run ONLY=retro` で確かめる (この 1 つは流してよい)。

## 3. シリアルの MIDI 出力を、開いたアプリが終わったら放す

`lib/gems/picoruby-fmrb-midi/ports/esp32/fmrb_midi_serial.c` は、一度開くと UART2 とピンを再起動まで持ち続ける
(ピンの台帳で持ち主なし)。開いたアプリが終わったら本体が閉じる形にする (ユーザ決定 2026-11-26)。持ち主の記録は、
UART の境界の関数が使う片付けの経路 (`hw_proxy_release_resources` ほか。sim でも走る) に揃える。MIDI のアプリの
動き (鳴らしている間・出力先の切り替え) は変えない。Tab5 で UART のアプリと取り合ったときの、両方向の理由の文は今のまま。

## 4. UART: 開き直した後の前のオブジェクト

同じアプリが同じ番号を開き直すと、前の `UART` オブジェクトへの呼び出しが新しい方に効く (report/u1.md)。前のオブジェクトは
閉じた物として扱い、呼ぶと IOError (閉じた物と同じ理由の文)。開くたびに番号ごとの世代を持たせ、包みが自分の世代を
覚えて渡す形などで。ホストの試験を足す。

## 5. app_sync が消さない古い本体のファイル

`rakelib/device.rake` の app_sync (ファームを書き、storage は WiFi で差分を送る) は、新しい像に無くなったファイルを端末から
消さない。最後に、像に無い本体の領域のファイルを一覧に出し、`PRUNE=1` のときだけ消す (ユーザ決定 2026-11-26)。
- 対象: `/usr`・`/help`・`/etc`・`/lib`・`/boot`・`/bin`・`/cache`、`/app` の下のうち像から来るフォルダ。
- 対象外 (一覧にも出さない): `/home`・`/app/usr`・`/app/test`・`/mnt`・`/tmp`・`/var`。
- 一覧は数と大きさの合計と、先頭の数行。消したときは消した数。
- MCP の flash ツールの app_sync の道も同じ動き (`PRUNE` を渡せるなら、その引数の説明も)。`tools/mcp/` の該当の所。
- 端末を使う確認は親がする。ホストの試験 (像の一覧と端末の一覧から消す候補を選ぶ所) を足す。

## 触ってよい範囲・確認・止まる条件

- fmruby-core の上の物と、その試験・生成物・文書。sdkconfig・`vendor/`・`lib/upstream/` の上流の写し・ほかのリポジトリは
  触らない。push しない。
- 確認: `rake regress` (直しごと)、2 の `rake regress:run ONLY=retro`。sim は `rake clean` → `rake build:linux` で、
  1 はネイティブ版・sim の ls が今までどおり、3 は MIDI のアプリでシリアル出力を選んでから閉じ、UART のアプリが UART2 を
  開けること (sim の MIDI の FIFO を使う。CLAUDE.md の「音の確認」)。
- **ESP32 のビルド・実機・シリアル・MCP の tab5_*・flash は触らない** (親がする)。`rake clean_all` を流さない。
- 止まる: 利用者から見た動きが上の決定のほかに変わる選択が要るとき、範囲の外を変える必要が出たとき、内蔵 RAM の静的な
  増分が出るとき。

## report (`doc/release_3_1/report/r1.md`)

直しごとに、原因・直し・確かめた事・残り。撮った画面は作業用の場所 (リポジトリの外)。実在の値を書かない。
終わったら sim を止め、置いた設定・試験用のファイルを消す。
