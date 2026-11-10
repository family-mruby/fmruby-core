# K2b・K3 報告: ファームへの組み込みと、変換の操作

> 状態: 完了 (sim の Modern の全画面だけ残件) | 更新: 2026-11-11 | 辞書を引く部品をファーム (ESP32・sim・ネイティブ版) に入れ、優先度の低いタスクが引く。かなモードの読みを preedit にため、スペース・変換キーで候補を画面の下の 1 行に出す。辞書が無い既定では何も作らず、画面の基準 33 件はすべて同じ。内蔵 RAM は増えていない (S3 DIRAM -40、P4 -352。基準は前のコミットなのでほかの変更の差も含む)。sim の Modern の全画面 (640x360) では 1 行が出ない (graphics-audio の Linux の全画面が持ち主の Canvas しか出さないため。直すには graphics-audio の変更が要る)

## 作ったもの

| ファイル | すること |
|---|---|
| `components/fmrb_kanji/CMakeLists.txt` | K2a の部品を IDF の component にした (ESP32・sim)。ネイティブ版は `hosted/hosted.cmake` で native のときだけ足す |
| `main/kernel/host/kanji_lookup.{h,c}` | 辞書のタスク。設定を受け取り、ファイルがあるときだけタスクを作る。タスクが辞書を開いて持ち、頼まれた読みを最長一致で引き、候補を UTF-8 に書き出して host task に知らせる。ブラウザ版では何もしない (スタブ) |
| `main/kernel/host/host_kanji.{h,c}` | host task の側: preedit・キーの扱い・画面の下の 1 行の Canvas と描画 |
| `main/kernel/host/host_internal.h` | host_task.c が host_kanji.c に貸す 3 つ (文字をアプリへ送る・自分の gfx の命令を直接送る・答えのある命令を送る) |
| `main/kernel/host/host_task.c` | かなの合成の隣に host_kanji を挟む。引いている間のキーをためて、答えの後に流す。`HOST_MSG_KANJI_RESULT`。ループで `host_kanji_tick` |
| `main/kernel/fmrb_kernel.c` | system_conf の `kanji_dict`・`kanji_convert` を読み、host task の後でタスクを始める |
| `main/drivers/display_local/display_local_task.cpp` | 全画面の高解像度のときも、z が `FMRB_GFX_Z_SYSTEM_OVERLAY` (30000) 以上の Canvas は合成する |
| `components/fmrb_gfx/fmrb_gfx.h` | `FMRB_GFX_Z_SYSTEM_OVERLAY` |
| `components/fmrb_common/include/fmrb_task_config.h` | `FMRB_KANJI_TASK_*` (スタック 6 KB 内蔵、優先度 3、ピン留めなし) |
| `config/system_conf_{n16r8,n8r8,p4,naryav4,linux,linux_p4}.toml` | 2 つの設定の説明 (注釈だけ。値は書いていない) |

### 設定

```
kanji_dict = "/sd/dict/fmrb_kanji.dic"   # 既定。書かなければこれ
kanji_convert = true                     # false で、ファイルがあっても変換しない
```

起動時にカーネルが `fmrb_hal_file_stat` で見て、**ファイルがあるときだけ**タスクを作る。無いときはログに
`no dictionary at ...: kanji conversion off` と出るだけで、タスクもバッファも作らない。開けない (壊れている・版違い) ときは
タスクが自分で閉じて消える。sim・ネイティブ版で試すときは `/home` の下に置いて指す (下)。

### 辞書のタスク (K2b)

- 名前 `fmrb_kanji`、優先度 3 (アプリの 2 より上、デバッガと同じ段)。重い仕事でなく、1 回の引きは塊 1 つ (4 KB) の読み。
  ゲームが回っていても変換が止まらないように、アプリより上に置いた。
- **スタックは内蔵 RAM (6 KB)**。SD のドライバはスタック上の構造体を DMA に渡すので、PSRAM のスタックは S3 で壊れる
  (doc/reference/internal_ram_budget.md の撤退の理由そのもの)。辞書の表 (15,509 バイト) と、塊のバッファ 4 KB・候補の
  書き出し先 (12 KB、最大 512 候補) は `fmrb_sys_malloc` (SYSTEM プール = 端末では PSRAM) に置いた。合わせて 32,919 バイト。
  static は全部 `FMRB_EXT_RAM_BSS_ATTR`。
- 受け渡し: host task が読み (UTF-8) を共有の場所に書き、タスク通知で起こす。タスクは引いて書き出し、
  `fmrb_host_kanji_result(seq)` で host の待ち行列に `HOST_MSG_KANJI_RESULT` を積む。頼みは同時に 1 つだけ
  (host が答えを受けるまで次を頼まない)。カタカナの読みはひらがなに直して引く (辞書の読みはひらがな、バイト数は同じ)。
- host task は引かない。答えを 3 秒待っても来なければ、以後の引きをやめ (バッファがまだ使われているかもしれないため)、
  読みはかなのまま確定できる状態に戻す。
- ブラウザ版: `kanji_lookup.c` はスタブになり、`fmrb_kanji.c` は組み立てに入れない。ブラウザの IME で変換する (K4) ので、
  端末の辞書は使わない。`host_kanji.c` は入るが、辞書が無いので何もしない。

### 変換の操作 (K3)

かなの合成 (`fmrb_kana_feed`) の後で、辞書が開いていてかなモードなら `host_kanji_key` が見る。状態は 読みなし・読み・
引いている・候補 の 4 つ。

| キー | 読みのとき | 候補のとき |
|---|---|---|
| かなになる打鍵 | 読みに足す (アプリへ送らない) | 選んでいる候補を確定し、残りの読みに足す |
| スペース・変換 (0x8A) | 最長一致で引く | 次の候補 (最後の次は最初) |
| Enter (テンキーも) | かなのまま確定 | 選んでいる候補を確定。最長一致の残りは次の読みになる |
| Esc | 読みを捨てる | 読みに戻る |
| BS | 読みの 1 字を消す (ローマ字の途中なら今までどおりそれを消す) | 読みに戻って 1 字消す |
| 数字 1-9 | (ほかのキーと同じ) | 今のページの n 番目を確定 |
| Shift などの修飾キーだけ | 何もしない | 何もしない |
| ほかのキー (矢印・Tab・記号・Ctrl+S など) | 見えているとおりに確定し、キーはそのままアプリへ | 同じ |

- 確定した文字は今までのかなと同じ 1 バイト 1 イベントで送る (`host_send_kana_byte`)。アプリは変えていない。
- 引いている間 (数十 µs〜数 ms) に来たキーはため (32 個まで)、答えの後に順に流す。「かんじ スペース Enter」を続けて打っても
  候補の方で確定する (sim で確認)。
- 押したキーを消費したとき、その離しも消費する (スキャンコードごとの 1 ビット)。
- 指示器と食い違わないように: かなモードを切ったとき (Ctrl+Space・半角/全角・指示器のクリック・PC の IME) と、
  フォーカスが別のアプリへ移ったときは、読みを打っていたアプリへ見えているとおりに確定し、1 行を消す
  (クリックとフォーカスは host のループの tick で見る)。
- 読みが 384 バイト (128 字) を超えたら、それまでをかなで確定してから続ける。

### 画面の下の 1 行

- 高さ 10 px、幅は今の画面の幅 (320 / 426 / 640)、下端。Canvas は z = 30000 (アプリはずっと下、デスクトップの前景が 254)、
  透明色なし。初めて読みができたときに host task が作り (`CREATE_CANVAS` を答えつきで送り、答えは tick で受ける)、
  読みが空になったら `SET_CANVAS_VISIBLE 0` で隠す。画面の大きさが変わったら (全画面の高解像度) 消して作り直す。
- 描画は host task が gfx の命令を組み、待ち行列を通さずに自分で batch で送る (アプリの描画と同じ変換を使う)。
- 字は misaki 8 の hybrid (ASCII は Font0 で 6 px、ほかは 8 px)。どの構成にもある。幅は同じ規則で数える。
- 左に読み (候補のときは一致した部分を白、残りを灰色、下線)、その右に番号つきの候補 (選んでいるものを反転)、右端に
  `n/全部`。ページは左から入るだけ (最大 9) 入れる。候補のとき読みの幅は画面の 1/3 まで (はみ出す分は先頭を落とす)。
- 引いても何も無いときは読みの後ろに `(候補なし)`。

## 確認

### 回帰の道具 (辞書なしの既定)

| | 結果 |
|---|---|
| `rake regress` | host 25/25、描画の記録 229/229 |
| `rake regress:screens` | 33/33 (modern 11・retro 9・web 6・native 3・native-keys 4)。かなの場面・エディタの 1 打鍵を含め、画素で同じ |
| `rake regress:size` | ok。NARYAv3 DIRAM 125,419 (-40)・IRAM +0、TAB5 DIRAM 129,346 (-352)。増えた内蔵 RAM の種類なし。基準 (4e7e7e61) はこのコミットより前なので、差にはほかの変更も入っている。static は全部 PSRAM に置いた |

### sim (Retro 320x240) と辞書

`flash/home/kanji_test/` に辞書を写し、生成された `flash/etc/system_conf.toml` に `kanji_dict` を足して起動した
(終わった後に両方消し、system_conf は config/ から写し直した)。開くのは 79 µs、1 回の引きは 17〜29 µs (塊 1 つ)。

| 確認したこと | 結果 | 画面 |
|---|---|---|
| エディタで かんじ → スペース | 1 行に 感じ 幹事 漢字 監事 完治 莞爾 (1/6) | retro_editor_candidates.png |
| スペース 2 回 → Enter | 漢字 が入る。Enter は改行にならない。1 行は消える | retro_editor_committed.png |
| 最長一致 いった | いっ の 13 候補、た は灰色 | retro_longest_match.png |
| 数字の 2 | 行っ が入り、た が読みに残る | |
| か を足して スペース → Esc → BS → Esc | 読みに戻る・た だけ残る・捨てる。エディタの文は変わらない。その後の Enter は改行 | |
| 読みの途中で Ctrl+Space | 見えている読みを確定してかなモードを切る (指示器は [A]) | |
| Shell で かんじ → 3 | 漢字 が入る | retro_shell_committed.png |
| 全画面エディタ (Retro は 320x240 のまま) で とうきょう | 1 行が見える (東京 TOKYO トウキョウ) | retro_fullscreen_editor.png |
| 続けて打つ: Enter Enter かんじ スペース Enter です スペース スペース Enter | 感じ・デス が入る (引いている間のキーが答えの後に流れた) | |
| カタカナモードで カンジ → スペース | ひらがなに直して引き、同じ候補 | |

### sim (Modern 426x240) と辞書

- こう (320 候補) → スペース 10 回: 2 ページ目の 1 番が反転、`10/320`。modern_paging.png
- 全画面エディタ (640x360) で かんじ → スペース → Enter: **1 行は出ない**が、変換と確定は効く (感じ が入る)。
  graphics-audio の Linux の全画面は持ち主の Canvas だけを画面に写す (`screen_mode_render_frame`)。core の 1 行の Canvas は
  640x10 で作られている (ログ)。直すには graphics-audio の `screen_mode_render_frame` で z >= 30000 の Canvas を重ねる変更が
  要るので、止まる条件として触っていない (残件)。全画面を抜けると 426 幅に作り直して出る。modern_after_hires.png
- Retro の子マイコン (WROVER) には全画面の高解像度が無く、Retro は graphics-audio を変えずに出る。

### ネイティブ版と辞書

`ruby tools/fmrb_native.rb up --user-dir <scratchpad>/k3_home` (ユーザの ~/.local/share/family-mruby-dev は使っていない)。
`/home/fmrb_kanji.dic` を指した。開くのは 155 µs、引きは 43〜46 µs。

- エディタ (426x240) で かんじ → スペース: 1 行が出る。native_editor_candidates.png
- **全画面エディタ (640x360、display_local) の上で 1 行が見える** (れんらくちょう → 最長一致 れんらく → 連絡、残り ちょう →
  107 候補 → 兆)。native_hires_editor.png・native_hires_rest.png・native_hires_done.png。Tab5 の表示も同じ display_local。
- `kanji_convert = false` (ファイルはそのまま): `kanji conversion disabled`、かんじ のあとスペースがそのまま入る
  (今のかな入力)。native_convert_off.png
- PC の IME があるときはかなの合成ごと止まるので (L12b)、この変換も動かない (コードの経路で確認。IME のある環境では試していない)。

画面は `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/d865add5-d638-40f7-89da-40dce8f29b87/scratchpad/k3/` (リポジトリの外)。

## 端末での速さの測り方 (実機は後で)

ログに必要な数が出るようにしてある。

1. `rake kanji:dict` の `tools/kanji/work/out/fmrb_kanji.dic` を microSD の `/dict/fmrb_kanji.dic` に置く (PC で写す。
   K5 で `asend --sd` も)。カードは 2 種類以上 (遅いものを含む)。
2. 焼いて (`flash`)、`serial_start` で起動ログを採る。見る行:
   - `kanji: /sd/dict/fmrb_kanji.dic open in N us: ...` — 開く時間 (頭と表 16 KB の読み)。
   - `M1|kanji|internal=..` — タスクを作った後の内蔵 RAM (直前の `host_ready` との差がスタックと TCB)。
3. エディタでかなモードにし、読みを打ってスペース。1 回ごとに
   `kanji: lookup B bytes: found N cands, matched M, R block reads, T us` が出る。T が 1 回の引き (塊の読み + 候補の書き出し)。
   最悪を見る読み: れんらくちょう・いった (最長一致で塊を読み直す場合)、こう (候補 320)、辞書の最初と最後の塊の読み。
4. 10 秒ごとの `fmrb_task:` の `fmrb_kanji` の Free で、スタックの残り (6 KB のうち) を見る。
5. 目安は 1 回の変換で SD 10 ms 以内 (K0)。Tab5 は tab5_input で同じ打鍵を送れる。NARYAv3 は人が打つ。

## 撤回した仮説

- 「1 行は既存の仕組み (アプリの gfx の口) で描ける」: `fmrb_gfx_submit` はアプリの文脈でしか使えず、host task が自分の
  待ち行列に積むと詰まりうる。host task が命令を自分で batch に変えて直接送る形にした。
- 「Canvas の作成は同期で待てばよい」: host task は答えを受ける側 (`fmrb_transport_process`) なので、自分で待つと止まる。
  答えつきで送り、答えの callback では id を控えるだけにして tick で描く。
- 「全画面の上でも、z を一番上にすれば出る」: Retro と display_local の通常の画面ではそうだが、全画面の高解像度は
  持ち主の Canvas だけを出す。display_local は直したが、graphics-audio (sim の Modern) は直していない。
- 「タスクのスタックも PSRAM に置ける」: SD を読むので置けない (上)。

## 残件

- **sim の Modern の全画面 (640x360) で 1 行が出ない**。graphics-audio の Linux の `screen_mode_render_frame` に、
  z >= `FMRB_GFX_Z_SYSTEM_OVERLAY` の Canvas を重ねる数行が要る。端末 (Tab5・NARYAv4 = display_local) とネイティブ版では出る。
  graphics-audio を変えてよいかは親・ユーザの判断。
- 辞書があるときの内蔵 RAM: タスクのスタック 6 KB と TCB が増える (辞書が無ければ 0)。実機で `M1|kanji` を測る。
- 実機での速さ (上の手順)。
- 候補を戻るキー (Shift+スペース・矢印) は無い。決めたキーにないので足していない。320 候補の読みは 1 周で戻る。
- 1 行の字は misaki 8 だけ。640x360 で efont 12 にするかは見た目の判断。
- エディタのステータス行の指示器は、フォーカスの無い間のモードの変化を受けない (sim で、Shell で on にした後エディタへ
  戻ると [A] のまま)。変換を入れる前からの動き。
- `fmrb_kanji_verify` (CRC) を呼ぶ道は無い (K5 で辞書を置く道具と一緒に)。
- 変換キー (0x8A) は sim の入力の道具 (`tools/fmrb_input.rb`) に名前が無く、注入では試していない (スペースと同じ分岐)。
