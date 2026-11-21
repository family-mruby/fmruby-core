# C3b 報告: 絵のビューワー・ファイルの画面の最初のフォルダ・PNG の透明

> 状態: 完了 (2 点を親に返す) | 更新: 2026-11-21 | 絵のビューワー (`flash/app/tool/image_viewer.app.rb`) を作り PNG・BMP の既定にした。ファイルマネージャーの右クリック (と Tab) のメニューに「スプライトエディタで開く」(キー S) を足した。デスクトップのファイルの画面に最初のフォルダ (`request_file_select(mode, dir:)`) を足し、スプライトエディタの開く・保存・名前を付けて保存は `/home/stamps/` から開く。端末の PNG の読み込み (png332.c) で α < 128 と tRNS の透明を `0x01` にした。描画側の読み込み (`SpriteImage#load_bmp`、graphics-audio と display_local_task.cpp) は直していない (6 章 Q1)。ブラウザ版は `/etc/associations.toml` を束ねていないのでダブルクリックの関連付けが効かない (前から。6 章 Q2)。指示書は instruction_c3b.md

## 1. 作ったもの・直したもの

| ファイル | 中身 |
|---|---|
| `flash/app/tool/image_viewer.app.rb` (新) | 絵のビューワー。BMP332.load で読み、整数倍で出す。上の帯に 開く・スプライトエディタで開く・< > |
| `flash/app/tool/image_viewer.app.toml`・`.icon`・`.png` (新) | 名前「画像ビューワー / Image Viewer」、316x222 (x=2・y=14、Retro に収まる)、大きさを変えられる (最小 240x120)、ランチャーに出す |
| `flash/usr/share/doc/image_viewer.md`・`_ja.md` (新) | 端末の案内 |
| `config/associations.toml` | `png`・`bmp` = ビューワー |
| `main/ruby/kernel/system_desktop/file_manager.rb`・`i18n.rb` | 絵 (拡張子 png・bmp) の行のメニューに「スプライトエディタで開く」、キー S |
| `lib/gems/picoruby-fmrb-app/mrblib/fmrb-app.rb`・`main/ruby/spinel/fmrb_app_base_spinel.rb`・`sig/fmrb_app.rbs` | `request_file_select(mode = "open", dir: "")` |
| `main/ruby/kernel/fmrb_kernel.rb`・`system_desktop.app.rb`・`system_desktop_keys.app.rb`・`system_desktop/file_selector.rb` | `"dir"` を渡し、ファイルの画面をそこから開く |
| `flash/app/tool/sprite_editor.app.rb`・`flash/usr/share/doc/sprite_editor*.md` | 開く・保存 (名前の無い絵)・名前を付けて保存を `/home/stamps/` から |
| `lib/gems/picoruby-fmrb-bmp332/ports/esp32/png332.c`・`include/fmrb_png332.h` | α・tRNS の透明を `0x01` (`FMRB_PNG332_TRANSPARENT`) に |
| `tools/rgb332_png.rb`・`test/png332/run.rb` | Ruby の読み手も同じ扱いに。試験に α・tRNS の作った絵 5 枚 |
| `tools/gen_picorabbit_sprites.rb` | 無くなった `SpriteEditorApp::PALETTE` を指すコメントを直した |

## 2. 3 つの結果

### 2.1 ビューワーと右クリック

- 確かめた事実 (sim の Retro 320x240・Modern 426x240、ブラウザ版):
  - ファイルマネージャーで PNG (`world.png`・`alpha_disc.png`・Asterism の `icon.png`) と 8 ビットの BMP をダブルクリックするとビューワーが開き、絵・名前・`WxH`・倍率 (`x4` など) が出る。
  - 透明 (`0x01`、PNG の α の透明) は白と灰色の市松で出る。
  - 左右のキーで同じフォルダの前・次 (名前の順、端で回る)。開けない絵 (4 ビットの BMP `tile_a.bmp` など) は飛ばし、状態の行に「開けません」。
  - 200x150 の絵は 426x240 で x1、Window を小さくすると `1/2` に縮めて全体が出る (Window の大きさに追従)。
  - 「スプライトエディタで開く」(帯と E) で、同じ絵がスプライトエディタで開く (24x24 は全体、64x32 はタイルのシート)。
  - ファイルマネージャーのメニュー (Tab で出したもの。右クリックと同じ一覧) に、絵の行だけ「Open in Sprite Editor  S」が出て、押すとスプライトエディタで開く。ほかの行には出ない。
  - Retro の sim でも開き、Window が画面に収まる (最初 x=13 で右がはみ出したので x=2 にした)。
- 推定・見ていないもの: マウスの右ボタンで出したメニュー (sim_input にボタン 3 の書き方が無い。同じ `fmgr_ctx_items` を通るので同じ一覧のはず)。日本語の表示 (帯とメニューの文字) は目で見ていない。

### 2.2 ファイルの画面の最初のフォルダ

- 確かめた事実:
  - スプライトエディタ: 新規 → 1 点 → Ctrl+S で、ファイルの画面が「Save: /home/stamps」で開き、名前を打って Enter で `/home/stamps/newone.png` に保存 (Modern の sim)。名前を付けて保存も同じ (Retro の sim)。ブラウザ版でも新規 → Ctrl+S → `/home/stamps` で開き、`webtest.png` ができた。
  - 名前の無い絵で閉じる (Ctrl+Q) → 確認の「保存」→ ファイルの画面 (`/home/stamps`) → 名前 → 保存されてアプリが閉じる。
  - テキストのエディタの File > Open と Save as は今までどおり「/」で開く (Modern の sim)。
  - ビューワーの開くは、開いている絵のフォルダ (無ければ `/home`) から開く (sim・ブラウザ版)。
- 決めたこと: 無いフォルダを渡されたら、一覧が取れる一番近い親へ戻る (最後は `/`)。ファイルの画面はフォルダを作らない。作るのは頼んだアプリ (スプライトエディタは無ければ `/home/stamps` を作ってから頼む)。

### 2.3 PNG の透明

- 確かめた事実: 試験 (`rake png332:test`、`rake regress` の中) で、RGBA (α 0・127・128・255)、灰色 + α、パレット + tRNS (パレットより短い tRNS)、灰色 + tRNS、RGB + tRNS の 5 枚が、C と Ruby の両方で期待どおり (α < 128 と tRNS の色が `0x01`)。sim で α つきの絵 (24x24 の赤い円、外は α 0、間に α 100 と α 200 の輪) をビューワーとスプライトエディタで開くと、外と α 100 の輪が透明 (市松)、α 200 の輪は赤。それを保存した PNG をホストで読むと透明の 300 点が `0x01` ((0,0,85) のパレットの色)。
- しきい値: α < 128 を透明 (見立てのとおり)。tRNS も同じ (パレットの α < 128)。灰色・RGB の tRNS はその値ちょうどの画素。
- 紺 (0,0,85) の近くの不透明の色が `0x01` に丸まる件は今までどおり (変えていない)。スプライトエディタの透明の書き方そのもの。
- 同じコードか: アプリの側の読み込み (`BMP332.load`/`parse` = png332.c) は sim・ESP32・ネイティブ版・ブラウザ版で同じファイル (lib/gems から各ビルドへ写される)。**描画側の読み込み (`SpriteImage#load_bmp` → `load_sprite_image`) は別のコード**: sim と Retro は graphics-audio の `graphics_handler.cpp` (LovyanGFX の drawPng を、0 で埋まったスプライトの絵へ)、P4・ネイティブ版・ブラウザ版は fmruby-core の `display_local_task.cpp` (drawPng を 0 で埋まった下書きへ)。どちらも α 0 の点を飛ばし、半透明を下の色と混ぜる。sim での実物 (緑の上に、透明色 0x01 のスプライトの絵として `draw_tile`):
  - α の PNG を `load_bmp`: 外が**黒い四角**、α 100 の輪は黒と混ざった暗い赤。
  - 先に `0x01` で塗ってから `load_bmp`: 外は抜ける。α 100 の輪は紺と混ざった色で残る (しきい値の扱いと違う)。
  - ビューワー・エディタで保存した PNG (`0x01` がパレットの色) を `load_bmp`: 外も α 100 の輪も抜ける。
  - アプリの側で読んだ画素をスプライトの絵に描く: 同上 (抜ける)。
  - よって「PC で描いた α の PNG をそのままスプライトとして描くと抜ける」には描画側の直しが要り、その片方は graphics-audio。指示に従い直さずに返す (6 章 Q1)。スプライトエディタかビューワーを通して保存し直した絵は今でも抜ける。

## 3. 受け入れ条件

| # | 結果 | 根拠 |
|---|---|---|
| 1 | 通った | 2.1。Modern の sim で PNG (world.png) と BMP (Retro の sim で world8.bmp) のダブルクリック → ビューワー → 「スプライトエディタで開く」→ 同じ絵 |
| 2 | 通った | 2.2 |
| 3 | アプリの側は通った、描画側は返す | ビューワー・スプライトエディタで透明が透明色 (市松) で出る。保存し直した絵はスプライトとして抜ける。α の PNG を直に `load_bmp` すると黒 (2.3、6 章 Q1) |
| 4 | 一覧あり | 4 章。見た目が悪くなる所は無い |
| 5 | 一部 | ブラウザ版: ビューワーを開く (パスで起動・開くの画面)、α の円が市松で出る、「スプライトエディタで開く」、新規 → 保存が `/home/stamps`。**ファイルマネージャーのダブルクリックでビューワーが開かない** (ブラウザ版の束に `/etc/associations.toml` が無い。前からのこと、6 章 Q2)。Retro の sim でビューワーが開く |
| 6 | 通った (regress:run は親へ) | `rake regress` 33 suites・310 runs 全部通った (直しの前・後)。`rake regress:screens` 28 passed / 0 failed (retro 6/6・modern 6/6・web 5/5・native 6/6・native-keys 5/5、8.3 分、最後に 1 回)。png332 の試験に透明の分を足した。`rake regress:run` はデスクトップが +2 KB で FAIL (5 章、6 章 Q3) |
| 7 | 通った | `git status` は前からの 3 つだけ。sim・ブラウザ版は止めた。試験で sim の `flash/home` に置いた `c3b/`・`stamps/` は消した。ブラウザ版の `/home` に置いたファイルは消したが、空のフォルダ `/home/c3bt` が IndexedDB に残った (道具でフォルダを消せない) |

## 4. 今ある絵で読んだ結果が変わるもの

`git ls-files "flash/*.png"` の 109 枚のうち、α か tRNS を持つのは RGBA の 3 枚だけ (tRNS を持つ絵は無い)。変わる画素 (32x32 = 1,024 点中):

| 絵 | 変わる点 | 今まで | 使われ方 |
|---|---|---|---|
| `flash/usr/share/asterism/icon.png` | 372 (α 0 が 316、1〜127 が 56) | 0x00 (黒) 316・0xB6 48・0x24 4・0x49 4 | Asterism の端末のアイコン。ファイルのまま網へ送るだけで、端末では読まない |
| `flash/usr/share/asterism/icon_browser.png` | 264 (α 0 が 256、1〜127 が 8) | 0x00 256・0x4D 8 | 同上 (ブラウザ版の束で icon.png になる) |
| `flash/app/test/asterism_demo.png` | 308 (α 0 が 284、1〜127 が 24) | 0x00 284・0xB0 24 | asterism_demo のアイコン。同上 |

- どれも端末の上で png332.c を通して描くアプリは無い (grep で、読むのは Asterism がファイルを送るときだけ)。変わるのは、この 3 枚をビューワーかスプライトエディタで開いたとき: 角と縁の半透明が黒でなく透明 (市松) になる。見た目は良くなる方で、悪くなる所は無い。
- BMP (2 枚、BASIC の 4 ビットのもの) は変わらない (BMP の読み方は直していない)。
- 描画側の読み込みは直していないので、どのアプリの画面も変わらない (`rake regress` の描画の記録も 310 件同じ)。

## 5. 数字

- **ビューワーの VM プール (sim、64 ビット)**: コンパイル直後 used 448,840 B (コンパイル中の peak 594,016 B)。24x24 の絵を開いた後 543,696 B (Retro)、200x150 の絵 (30,000 点) で 595,936 B (Modern)。上限 65,536 点は、絵の文字列 64 KB と読み込みの一時領域 (IDAT・展開・画素) が 1.5 MB のプールに収まる大きさとして、スプライトエディタと同じにした。
- **描画側の資源**: スプライトの絵 1 枚 (市松の帯、表示幅 x 8 px、最大でも数 KB)。`create_image` は使わない。
- **描画の量**: 市松は 8 行ごとに 1 回の `draw_tile`、色は 1 行の同じ色の連なりごとに `fill_rect` 1 つ。点の絵・タイル (64x32 の world.png で x4) も、色が毎点変わる絵 (200x150 のぼかし) も、sim で 1.5〜3 秒後に撮った画面ではもう出ていた (描く時間そのものは測っていない。命令数は絵による。実機の Retro では遅い見込み)。
- **デスクトップの VM プール**: `rake regress:run` は system_desktop の 10 項目が基準より **約 +2.0 KB** (retro idle 814,448 → 816,472 B、+1,896〜+2,048 B) で FAIL。同じ木から私の直し (main/ruby・lib/gems/picoruby-fmrb-app・sig・config の差分) だけを外して流すと FAIL は無く (desktop 795K)、増分はこの直しのもの。デスクトップは mruby のバイトコードで動く (ps の vm 0) ので、足したメソッド (`fsel_existing_dir`・`fmgr_open_in_sprite_editor`・`fmgr_ctx_label(_w)`・`fmgr_picture?` ほか) のバイトコードと定数と見ている (推定)。試したこと: i18n の表に 2 語足したのを外しても変わらない (+2,000 前後のまま)、ビューワーをランチャーに出さなくても変わらない。基準 (`test/regress/sim_baseline.json`) は「人が確かめた変化だけ」の決まりなので更新していない。親が確かめて `rake regress:run:update` をするか、削る方を選ぶかを決めてほしい (6 章 Q3)。kernel・アプリの値は基準どおり
- **内蔵 RAM**: 増やすものは無い。C の変更は png332.c だけで、静的領域なし・スタックは局所変数 4 つ分 (tRNS の位置と長さ、RGB の鍵)。デスクトップの直しは引数 1 つと、メニューの項目 1 つ (常に持つ領域なし)。`rake regress:size` は指示どおり流していない (親が流す)。

## 6. 親 (ユーザ) に決めてほしいこと

- **Q1. 描画側の PNG の読み込み (`SpriteImage#load_bmp`) の透明**。今は α 0 が黒 (0x00)、半透明は黒と混ざる。直す場所は 2 つ: graphics-audio の `graphics_handler.cpp` (sim・Retro) と fmruby-core の `display_local_task.cpp` (P4・ネイティブ版・ブラウザ版)。
  - (推奨) A: 両方の描画側で、PNG を png332.c と同じ決まり (α < 128・tRNS → 0x01) で読む。graphics-audio は fmrb_wire と同じく隣の fmruby-core の png332.c を使う形にすれば決まりが 1 か所にまとまる。graphics-audio の直しになるので、別の段で。
  - B: fmruby-core の `SpriteImage#load_bmp` だけで、読む前に絵をその透明色で塗る。α 0 は抜けるが、半透明の縁は透明色 (紺) と混ざった色で残る (2.3 の 2 つ目)。描画の命令が 1 つ増える。
  - C: 直さない。PC で描いた絵は、ビューワーかスプライトエディタで開いて保存し直してから使う (案内に書く)。
- **Q2. ブラウザ版の関連付け**。`rake wasm:web` が `config/associations.toml` を束の `/etc` に入れていない (前から。`md` → picorabbit なども効かない)。ファイルマネージャーのダブルクリックは、表に無い種類と同じ「エディタで開く」になる。
  - (推奨) A: `rakelib/wasm.rake` で `config/associations.toml` を `etc/associations.toml` に写す (sim・ESP32 と同じ。2 行)。指示書の範囲の外なので直していない。
  - B: そのまま。
- **Q3. デスクトップの VM プール +2 KB** (5 章)。
  - (推奨) A: 機能の分として受け入れ、親が確かめて `rake regress:run:update` (別コミット)。
  - B: 削る。例えば無いフォルダの親へ戻る処理を「無ければ `/`」だけにする、メニューの項目の幅の計算を Linux の項目と 1 つにまとめる、など。どこまで減るかは測っていない。

## 7. 自分で決めた細部

| 項目 | 決めたこと |
|---|---|
| 名前・置き場 | `flash/app/tool/image_viewer.app.rb`、「画像ビューワー」/「Image Viewer」、クラス `ImageViewerApp` |
| 大きい絵 | 縮小 (整数分の 1、k 点ごとに 1 点を拾う)。スクロールは無い。全体が見えることを取った |
| 拡大 | Window に収まる最大の整数倍、16 倍まで |
| 透明の見せ方 | 4 px の白と灰色 (0xFF・0xB6、スプライトエディタと同じ色) の市松。絵の点の大きさとは揃えない |
| 前・次 | 左右のキーと帯の < >。名前の順、端で回る。開けない絵は飛ばす |
| 上限 | 65,536 点 (スプライトエディタと同じ)。超えると状態の行に「大きすぎて開けません: 名前 WxH」 |
| 開くの最初のフォルダ | ビューワーは開いている絵のフォルダ、無ければ `/home`。スプライトエディタは開くも `/home/stamps/` (スタンプの置き場として決定 20 に揃えた) |
| 名前の無い絵の保存 | 名前を付けて保存と同じにした (前は黙って `sprite<N>.png`)。決定 20 のとおり |
| 無いフォルダ | 一番近い親へ戻る。ファイルの画面は作らない |
| メニューの項目 | 絵の行だけ。キー S (今までの E・C・V・L と重ならない)。ほかの種類のファイルには広げていない |
| ビューワーからエディタへ | カーネルへ `spawn` + `open_path` (ファイルマネージャーと同じ道)。ビューワーは閉じない |
| 半透明のしきい値 | α < 128 を透明 |

## 8. 確認していないこと・気づいたこと

- 実機 (Retro・Tab5・P4-Nano) とネイティブ版では動かしていない。
- マウスの右ボタンで出したメニュー、日本語の表示は目で見ていない (2.1)。
- ブラウザ版で、ファイルマネージャーのダブルクリックが何も出さずに閉じた。Q2 のとおりエディタで開こうとしたものと見ているが、エディタが出なかった理由は追っていない。
- ビューワーの帯は Window の幅 240 px 未満で文字と < > が重なるので、最小幅を 240 にした。
