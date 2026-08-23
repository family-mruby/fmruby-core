# 実装指示書 P4: 起動メニューと書き出し

対象: 実装担当セッション。作業リポジトリ: fmruby-core **と fmruby-graphics-audio**
(T1 だけ両方に手が入る)。先に読むもの: plan.md の P4、
instruction_p0_p1.md の「進め方」(そのまま適用)、report/p3.md (索引モードの
作りを流用する)。報告は report/p4.md。

## 確かめてある事実 (設計の前提)

- Tab5 の SD は `/mnt/sd` にマウント済み (sd_test.app.rb が書き、
  display_p4 が `/mnt/sd/movie/*.mjpg` を読む)。`Dir.mkdir` は picoruby-vfs
  にある。
- P4 の**ハードウェア JPEG エンコーダ**の包みが remote desktop 用にある:
  `main/drivers/remote_desktop/rd_encoder_jpeg.[ch]` (RGB565 426x240 →
  JPEG、幅は 432 に詰める、内部に出力バッファ 1 本)。合成後フレームの
  取り出しは `display_p4_capture_*` (display_p4_task.h)。
- GFX 命令の定義 `fmrb_link_protocol.h` は**両リポジトリに同じ内容の
  写しがある** (core: components/fmrb_common/include、graphics-audio:
  main/common)。片方だけ足すと番号がずれるので**必ず同じ差分を両方に**。
  display_p4 (Tab5) は core 側の display_p4_task.cpp、sim と WROVER は
  graphics-audio 側の graphics_handler.cpp が受ける。
- sim の graphics-audio は自前で合成したフレームを共有メモリへ出す
  (SDL は別プロセス)。書き出しは自前で BMP を書けばよい。
- core と display_p4 は同じプロセスなので `/mnt/sd` のパスをそのまま
  渡せる。sim では graphics-audio 側のストレージに書かれる (core からは
  見えない。検収は docker 経由で取り出す)。

## T1: GFX 命令 `EXPORT_FRAME path` (両リポジトリ)

- `fmrb_link_protocol.h` に `FMRB_LINK_GFX_EXPORT_FRAME` を 1 つ足す
  (空いている番号、両方の写しに同じ値)。ペイロードは
  `LOAD_SPRITE_IMAGE_BMP` と同じ形 (長さ + パス文字列)。
- core: `components/fmrb_gfx/fmrb_gfx_cmd.[ch]` に builder、
  `lib/add/picoruby-fmrb-app/ports/esp32/gfx.c` に
  `FmrbGfx#_export_frame(path)`、`fmrb-gfx.rb` に `export_frame(path)`
  (present を含まない。呼ぶ側が present してから呼ぶ)。Spinel 側
  (`fmrb_spx_gfx.c`) は PicoRabbit が mruby 専用なので**今回は足さない**
  (足さない旨を report に書く)。
- **意味: 「直前の present の合成結果」をそのパスへ書く**。命令列は
  順序が保たれるので、present の次に並べれば その絵が書かれる。
  display 側では**同期的に** (符号化と書き込みを終えてから次の命令へ)
  処理する。アプリは「最後のファイルが現れるまで `File.exist?` で待つ」
  形で完了を知る (core と display が同じ VFS なので Tab5 では可能。
  sim では見えないので、sim では固定の待ち時間でよい)。
- display_p4 (Tab5): 合成後フレーム (RGB565) を取り、`rd_encoder_jpeg`
  で符号化し、`fopen` / `fwrite` でパスへ書く。品質は 90。
  **エンコーダは内部バッファ 1 本を remote desktop の配信と共有する**ので、
  rd_task.c がどう排他しているかを読み、配信中に export が走っても
  壊れない形にする (同じロックを取る、または配信側が使っていない
  ときだけ使い、使用中なら配信を一時停止する)。出力の幅 432 は
  そのまま (閲覧側が気にしない)。
- graphics-audio (sim): 合成フレーム (RGB332) を **24bit BMP** にして
  パスへ書く (40 行程度の自前コード。画像ライブラリは要らない)。
  WROVER (Retro): `NOT_SUPPORTED` のログを 1 行出して無視。
- パスはディレクトリが存在する前提 (作るのはアプリ側、T3)。

## T2: 起動メニュー画面

起動直後に全画面のメニューを出す (今の「先頭の .md を即開く」をやめる)。

- **デッキ一覧**: `/home/slides/*.md` と `/mnt/sd/slides/*.md` (後者は
  ディレクトリが無ければ無視)。表示は `場所/ファイル名` (例
  `home/intro_ja.md`、`sd/talk.md`)。並びは場所ごとにファイル名順。
- 一覧の作りは**索引モードを流用** (size 1、1 行 1 件、選択行は反転、
  Up/Down/PgUp/PgDn で選択、カーソルを乗せてタップでも選択)。
- ボタンは **FmrbUI** で 3 つ: `Start` / `Export` / `Quit`。Enter は
  Start と同じ。FmrbUI は全画面でも user area = 画面全体で使える。
  `FmrbUI.new(self)` は `on_create` で 1 回、`handle` の戻り値を `case`。
- Start: 選んだデッキで従来どおり発表開始。**発表中の Esc はメニューへ
  戻る**に変更し、`q` だけがアプリ終了 (メニューでの Quit と同じ)。
  メニューに戻るときはスプライトを隠し、タイマーは次の Start で
  仕切り直す。
- Export: T3 へ。実行中はメニュー上に進捗 (`Exporting 3/12 ...`) を出し、
  終わったら結果 (`12 files -> /mnt/sd/picorabbit/intro_ja/`) を出す。
  `/mnt/sd` が無ければ `Export` ボタンを無効 (`set_enabled false`) に
  して理由を 1 行出す。
- メニューの見た目は趣味の範囲なので、**描けた時点で 1 枚見せて OK を
  もらう** (P3 の索引と同じ小関門)。

## T3: 書き出し

- 出力先は**デッキ名のディレクトリ**: `/mnt/sd/picorabbit/<デッキ名>/`
  (デッキ名 = ファイル名から `.md` を除いたもの)。`Dir.mkdir` で
  `/mnt/sd/picorabbit` と `<デッキ名>` を順に作る (既にあれば可)。
  ファイル名は `01.jpg` `02.jpg` ... (2 桁、100 枚以上なら 3 桁)。
  既存ファイルは上書き。
- 各スライドは**最終状態**で書く: wait を全部開く (`@step = @max_step`)、
  うさぎ・亀を隠す、残り時間表示を消す、ページ番号は残す。
  `render_slide` → (present は render_slide の中) → `export_frame(path)`
  の順で、スライドごとに 1 回。
- Tab5 では最後のファイルが現れるまで `File.exist?` で待ち (上限 10 秒)、
  現れなければ失敗と表示。sim では 1 枚あたり固定の待ち (200ms 程度) を
  入れる。
- 書き出し中はキーもタップも受け付けない (進捗表示だけ)。終わったら
  発表状態を元に戻してメニューへ。
- `fmrb` ブロックも最終状態で描かれる (P0 で precompile 済み)。

## T4: 日本語の確認用デッキ `intro_ja.md`

`flash/home/slides/intro_ja.md` を置いてある (発表らしい 8 枚: 目次、
ルビ、2 段の箇条書き、番号付き、引用、コード、wait、center、`{::goal/}`
で質疑をコース外に、`chime: true`、`text_size: 2`)。demo_ja.md (機能の
網羅) はそのまま残す。

- sim で全 8 枚を通し、欠けや折り返しの不自然さがあれば**デッキ側を
  直す** (renderer に手を入れるべき不具合なら report に書いて止める)。
- 表 (`|` 区切り) は**未対応**なので使っていない。表が欲しくなったら
  別段で。

## 検収

### sim (headless、426x240)

- メニュー: 起動直後のスクリーンショット (一覧に home/demo.md、
  home/demo_ja.md、home/intro_ja.md の 3 件)。Down + Enter で
  intro_ja.md が開く。Esc でメニューに戻り、もう一度 Start で
  demo.md が開く (タイマーが仕切り直されている = 残り時間が満タン)。
- Export (sim は BMP): `demo.md` (11 枚) を書き出し、graphics-audio 側の
  ストレージに `demo/01.bmp` .. `11.bmp` が**ちょうど 11 個**ある
  (`docker exec fmruby_graphics_audio ls ...` で数える)。
  `03.bmp` を PNG に変換し、sim で同じスライドを wait 全開・スプライト
  非表示で表示したスクリーンショットと pngdiff で **0 差分**
  (フッタの残り時間の帯は除く矩形)。
- 書き出し中に放置しても E/Exception 0。書き出し後に発表を始めて
  P3 と同じ操作が効く (回帰)。

### Tab5 (remote desktop + SD)

- メニューから `intro_ja.md` を Start し、全 8 枚を `fmrb_rd_snap.rb` で
  撮って report に並べる (**日本語の実機表示の確認**。これが T4 の目的)。
- Export を実行し、shell アプリで `ls /mnt/sd/picorabbit/intro_ja` を
  打って 8 個あることをスナップで示す。JPEG の中身 (PC で開く) は
  **ユーザ確認** (SD を抜いて見てもらう)。
- 書き出し中と後で remote desktop の配信が壊れていない (スナップが
  撮れる)。

## 受け入れ条件

- 上の検収が report/p4.md に画像と数字つきで揃う。
- コミット: (1) graphics-audio 側の命令受け (BMP 書き出し + WROVER の
  無視)、(2) core 側の命令 (protocol / builder / binding / display_p4)、
  (3) メニュー + Export + intro_ja.md。(1) と (2) は**同じ番号**を足した
  ことをメッセージに書く。英語、ユーザ確認のうえ。本書は (3) に含める。
- `.env` が作業前の値に戻っている。

## やらないこと

- 1280x720 での書き出し (DSI フレームバッファは縦長に回転しているので
  別扱い。要望があれば次段)。
- スライドの表、画像の書き出し以外の形式 (PDF 等)。
- 発表中のメニュー呼び出し (Esc で戻るだけ)。
