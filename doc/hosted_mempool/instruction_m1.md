# hosted_mempool M1 指示書: mempool を切って測る

> 状態: 指示 | 更新: 2026-10-06 | P4-Nano で CONFIG_ESP_HOSTED_USE_MEMPOOL を y と n で比べ、内蔵 RAM の戻り・速さ・細切れを数字で並べる

前提: 同じディレクトリの plan.md と、fmruby-core/doc/ruby_asterism/report/z3.md の 12 章 (原因の調査。調査用の
計測の diff は親のスクラッチパッドにある: /tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/z3_ram_diag.patch)。

## 1. やること

1. **変更**: `config/sdkconfig.defaults.p4` と `config/sdkconfig.defaults.naryav4` に
   `CONFIG_ESP_HOSTED_USE_MEMPOOL=n` を入れる (理由を英語のコメント 1-2 行で)。これはユーザの承認済みの変更。
   ほかの sdkconfig の項目は変えない。defaults を変えたら `rake clean_all` してから作る (sdkconfig を
   作り直すため)。生成された sdkconfig にその値が入っていることを確かめる。
2. **比べ方**: 同じコミットの上で、mempool あり (今の develop の defaults) と なし の 2 つのビルドを作って
   比べる (別の日の記録と比べない)。機体は P4-Nano (NARYAv4、192.168.10.15)。`FMRB_HW_TARGET=NARYAv4` を
   環境変数で渡す (`.env` は TAB5 のまま触らない)。
3. **測るもの** (どちらのビルドでも同じ手順で):
   - 起動直後: `M1|` 行、周期ダンプの `IRAM free:`、内蔵 RAM の一番大きく取れる空き。BLE の起動のログ。
   - 負荷: (a) `tab5_fs` で 64 KB のファイルの put と get を数回、(b) `tab5_screenshot` を連続 (MJPEG)、
     (c) `/ws_video` に 30 秒つなぐ (やり方は doc/reference/cpu_usage.md 5 章)。
   - 速さ: (a) の転送の時間、(b) (c) の fps。
   - 負荷のあと 3 分おいてから: `IRAM free`、一番大きく取れる空き、その後にアプリを 1 本起動して落ちないこと。
   - 計測のために調査用の計装 (上の patch など) を一時的に入れてよい。コミットには残さない。両方のビルドに
     同じ計装を入れて比べる。
4. **zenoh の確認**: Asterism Z3 は `feature/asterism-z3` にあり develop にまだ入っていない。今回は develop の
   上で作業し、zenoh の試しは要らない (通信の負荷は上の a-c で足りる)。

## 2. 受け入れ条件

plan.md の 1-4。数字は表にして並べる (mempool あり / なし)。採用の判断はユーザがするので、サブは判断材料を
そろえるところまで。

## 3. 触ってよい範囲

- fmruby-core: `config/sdkconfig.defaults.p4`、`config/sdkconfig.defaults.naryav4` の上の 1 項目だけ。
  `doc/hosted_mempool/report/m1.md`。調査用の計装 (一時的、コミットしない)。
- 触らないもの: `.env`、そのほかの sdkconfig の項目、managed_components の中、パーティションの表、
  fmruby-graphics-audio、S3 の構成。

## 4. 止まる条件

- mempool を切ると起動しない・WiFi や BLE が動かないなど、壊れたとき (原因が分かる範囲で書いて戻す)。
- ほかの sdkconfig の項目も変えないと成り立たないとき。
- 実機のボタン操作が要るとき。

## 5. report (`report/m1.md`) に書くこと

- 表: 起動直後 / 負荷の後 3 分 の内蔵 RAM と一番大きく取れる空き、速さ (転送時間、fps)。mempool あり / なし。
- 見立てと違った点、踏んだ罠、Tab5 で確かめること。

## 6. 作業の決まり

- ブランチ `feature/hosted-mempool` を fmruby-core の develop から切ってコミットする (英文、`<領域>: <要約>`、
  Co-Authored-By)。マージと push はしない。
- P4-Nano はミュート中 (`GET http://192.168.10.15/audio/mute` で確かめる)。動いているアプリは終了させてよい。
  シリアルは親の capture が動いている (`serial_start` を呼ばない。`serial_log` で読む)。焼くのは MCP の
  `flash` (sdkconfig が変わるので `app_only` でよいが、ブートローダなどが変わる場合は全体の書き込み。
  /home は消えてよいか分からないので、全体の書き込みが要るときは止まって知らせる)。
- 終わったら `build/` を Linux の標準構成 (x86-64) に戻す。P4-Nano には mempool なしのビルドを残してよい。
