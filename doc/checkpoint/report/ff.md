# Ff: 基準の表をファームを作るときに作り、固定の表として渡す

> 状態: 検収済 | 更新: 2026-11-05 | 申告したアプリの VM の基準の常駐の費用を、ブラウザ版の別のビルド (32 bit) で 322,248 → 90,176 B (−72%)、ネイティブ版 (64 bit) で 356,264 → 110,768 B (−69%) にした。名前・メソッドの一覧・クラスの写し・mrblib の irep のハッシュを、ビルドの中で同じ gem の組み合わせの VM から C のソースに書き出してリンクし (案 A)、VM ごとには名前のアドレスと、基準のときのオブジェクトのアドレスの集合 (GC の root の hook で生かす)、名前でない値だけを持つ。C の関数は番地でなく名前で結ぶ (今までどおり VM をなめて表を引く)。表とファームが食い違っても名前が別のものに結び付かない作りにし、イメージには表の決まりの版 (`TBL0`) を入れて違えば断る。P4・S3 の表はホストの道具 (`rake checkpoint:table`) で作れる (ESP32 にしか無い port は posix の port と、名前だけの代役で置き換える)

計画は `doc/checkpoint/plan.md` と `flash_table.md`、指示は `doc/checkpoint/instruction_ff.md`。F1b の内訳は `report/f1b.md`。
mruby-checkpoint の側は `doc/format_v1.md` (§8.1 と §13 の `TBL0`) と `doc/plan.md` の Ff の行。

この段は前のサブが途中まで進めて止まったものを引き継いだ。gem の側 (固定の表の読み書き、`keep` と ivar の番号化、
`TBL0`、試験の `build:table` と `test_table.rb`) とネイティブ版・ブラウザ版の CMake の入口は前のサブのものを生かし、
足りない所を足して直した (8 章)。

## 1. 内訳と前後

ブラウザ版の測るための別のビルド (`FMRB_WASM_CHECKPOINT=1`、wasm32、`MRB_NO_BOXING` で値は 16 B) とネイティブ版
(x86-64) の 1 VM (転送のアプリ)。前は同じビルドで表を渡さないようにして測った (一時的に `mrb_checkpoint_set_table`
を呼ばない形で 1 回流し、戻した)。部分ごとの量は gem の `mrb_checkpoint_baseline_size` (割り当てに頼んだバイト数。
estalloc の頭を含まない) で、VM を開くたびにログに 1 行出す。

### 1.1 部分ごと (B)

| 部分 | 32 bit 前 | 32 bit 後 | 64 bit 前 | 64 bit 後 | ファームで決まるか (前の形) |
|---|---:|---:|---:|---:|---|
| 名前の塊 (`names`) | 86,210 | 25 | 98,755 | 25 | 決まる → 表へ |
| 名前の始まり・関数の印 | 14,761 | 10 | 16,757 | 10 | 決まる → 表へ |
| メソッドの写し (`mt`) | 34,836 | 12 | 37,920 | 12 | 決まる (名前の番号で持つ) → 表へ |
| ivar の写し (`iv`) | 31,032 | 96 | 35,976 | 96 | 値を名前の番号か値の場所にして → 表へ (値の場所だけ VM に残る、下の `slots`) |
| snapshot (`snaps`) | 21,344 | 64 | 27,200 | 64 | 対象・super を名前の番号にして → 表へ |
| irep (`ireps`) | 24,304 | 6,076 | 26,512 | 13,256 | ハッシュは決まる → 表へ。ポインタは VM ごと |
| 生かしておく集合 (`keep`) | 48,276 | 12,064 | 55,464 | 27,712 | VM ごと。値 16 B の Array → アドレスの配列 (GC の root の hook) |
| 名前ごとのアドレス (`ptrs`) | 14,308 | 14,308 | 32,488 | 32,488 | VM ごと (C の関数も。2 章) |
| 二分探索の番号 (`obj_idx`・`func_idx`・`irep_idx`) | 20,384 | 20,380 | 22,872 | 22,868 | VM ごと |
| 名前でない値 (`slots`、843 個 × 9 B) | | 7,587 | | 8,307 | VM ごと (定数の整数・シンボル、String・Array・Hash の中身のダイジェスト) |
| 表と違って見える snapshot の場所 (`snap_over`) | | 2,664 | | 3,396 | 1 つでも違えば snapshot の数ぶん (下の `@profile_icon`) |
| 構造体・グローバル変数 | 384 | 388 | 520 | 524 | |
| **部分の合計** | **295,839** | **63,654** | **330,464** | **108,738** | |
| **プールの差** | **322,248** | **90,176** | **356,264** | **110,768** | 合計 + gem の準備 (`Checkpoint` モジュール、データの型、受ける側のメソッド) + 割り当ての頭 |

(後の名前・始まり・写しの小さな値は、表に無い 1 つの名前 `V:Asterism@@profile_icon` とその snapshot。14 章)

### 1.2 前後

| どこで | 名前 | 前 | 後 | 作る間の一時的な量 (前 → 後) |
|---|---:|---:|---:|---:|
| ブラウザ版の別のビルド (wasm32) | 3,577 | 322,248 | 90,176 (−72%) | 234,912 → 172,968 |
| ネイティブ版 (x86-64) | 4,061 | 356,264 | 110,768 (−69%) | 275,720 → 276,896 |
| sim (x86-64、IDF の linux) | 4,055 | 355,952 | 110,696 (−69%) | 274,608 → 276,848 |
| mruby-checkpoint の既定の gembox、32 bit (`checkpoint names`、部分の合計) | 1,610 | 99,855 | 23,668 (−76%) | |

- 一時的な量はプールの最高位 − 作った後の使用量。作る間は今までどおり VM をなめて名前ごとにポインタ → 番号の
  ハッシュ表を作るので、表があってもあまり変わらない (32 bit で減ったのは、名前の塊を伸ばしながら作らなくなった分)。
  S3 のプール (約 500 KB) では、作る間に 32 bit で約 263 KB (残る 90 KB + 一時の 173 KB) が要る。F6 で実機の値を見る。
- 「数十 KB」の目標に対して、32 bit で部分の合計 64 KB、プールの差 90 KB。残りの大半は VM ごとに番地で決まる
  もの (名前ごとのアドレス 14 KB、生かしておく集合 12 KB、二分探索の番号 20 KB、irep のポインタ 6 KB) と値の場所
  (8 KB) で、表の形では減らせない。さらに減らすなら C の関数の番地を表に入れる形 (2 章の (a)) か、irep を名前の
  番号で引く形が要る。

## 2. 表の作り方: C の関数は名前で結ぶ

候補:

- (a) 生成した C のソースから関数を番地で参照する (`&mrb_ary_length` のように)。リンカが番地を埋めるので、
  VM ごとの `ptrs` のうち C の関数の分 (約 1,900 個) が要らなくなる。
- (b) 表は名前だけを持ち、番地は VM の起動の後に VM のクラスをなめて得る (クラスとメソッドの名前で結ぶ)。

**(b) にした**。理由:

- mruby・picoruby の C のメソッドの大半は `static` の関数で、別のソースから名前で参照できない。(a) にするには
  各 gem のソースに手を入れる (static を外す、または登録の表を足す) か、リンクの後でファームを書き換える必要が
  あり、どちらも取らない (指示書)。
- (b) なら、表とファームが食い違っても名前が別のものに結び付かない。VM は今までどおり自分の状態をなめ、出て
  きた名前を表で引いてアドレスを入れる。表に無い名前と表と違って見える snapshot は、表なしのときと同じ形で VM が
  自分で持つ (表は合う所で記憶を減らすだけ)。食い違いの検出が作りに組み込まれ、版の印 (5 章) は「名前の決まり」
  の版だけでよい。
- 費用: C の関数の分の `ptrs` と `func_idx` (32 bit で約 11 KB) が VM ごとに残る。

表の中身 (`mrb_checkpoint_table`、`include/mruby/checkpoint.h`): 名前の塊・始まり・関数の印・名前の順の索引、
snapshot (名前の番号・super の番号・`mt`・`iv` の範囲・値の場所)、メソッドの写し (メソッド名の位置・フラグ・
Proc か C の関数の名前の番号)、ivar の写し (名前の番号か値の場所)、メソッド名と ivar 名の文字列、mrblib の
irep のハッシュ、メソッド名の一覧 (名前の順)、全体のダイジェスト。ポインタ幅に依らない (64 bit で作った表を
32 bit のドライバが「完全に一致」で使えることを gem の試験で確かめた)。

`mrb_checkpoint_has_method` (F5 の止める前の問い合わせと、着いた側の `SND0`・`requires` の確認) は、表があれば
表のメソッド名の一覧を二分探索で引き、VM のクラスはたどらない (表に無い自分の snapshot だけは見る)。

## 3. 作る道具

| 構成 | どこで作るか | 作り方 |
|---|---|---|
| ネイティブ版 | ビルドの中 (`native/CMakeLists.txt`) | `tools/ckpt_table/ckpt_table.cmake` の `fmrb_ckpt_table`。gem の port と `fmrb_app_checkpoint.c` と `gen_main.c` を本体と同じ include・定義でコンパイルし、同じ libmruby とリンクして走らせる |
| sim | ビルドの中 (`main/CMakeLists.txt`、IDF の linux) | 同じ道具の `fmrb_ckpt_table_idf`。port は picoruby-esp32 の、glue は main の性質 (include・定義・オプション) で別々にコンパイルし、ビルドのコンテナの ruby で走らせる |
| ブラウザ版の別のビルド | ビルドの中 (`wasm/CMakeLists.txt`) | emcc でコンパイルし、node で走らせる (`NODERAWFS`) |
| P4・S3 | ホスト (`FMRB_HW_TARGET=NARYAv4 rake checkpoint:table` → `tmp/ckpt_table/<板>/`) | 下の 4 章 |

- 生成物はビルドの生成物 (各ビルドの `ckpt_table/` と `tmp/ckpt_table/`)。コミットしない。
- libmruby (gem の組み合わせ・mrblib)・port・glue が変わると作り直す。中身が同じなら書き直さない (後の
  コンパイルが走らない)。
- 道具 (`ckpt_table.rb`) は、作る側が持たないファームの関数 (描画・カーネル・HAL) を「0 を返すだけの代役」
  として足してリンクする。足りない関数は、一度未解決を許してリンクしたときのリンカの一覧 (emcc では生成した
  JS の `missing function:`) から得る。VM を開くのはクラスとメソッドを定義するだけなので、代役はほとんど
  呼ばれない。wasm では型の違う関数を呼ぶと trap になるので、代役は最初のリンクのモジュールの import の型
  (型と import のセクション) に合わせて作る。
- 作る VM が開くときに例外を起こしたら (gem の初期化の途中で止まり、後の gem の名前が欠ける) 断って止まる。
- 作る VM はファイルを見ない。ファイルの HAL だけは代役でなく「見つからない」を返す関数を `gen_main.c` に置いた。
  代役は 0 (= `FMRB_OK`) を返して出力引数を埋めないので、読み込みの途中でファイルを読む gem (Asterism のアイコン) が
  ごみの大きさで文字列を作り、`NoMemoryError` で gem の初期化が止まった (sim の `-O0` で出た。ネイティブ版では
  たまたま出ず、黙って違う値を読んでいた)。ファームの VM が読んだものは、表との違いとしてその VM が持つ。

## 4. ESP32 にしか無い gem の扱い

P4・S3 の表は、ホストで同じ gem の組み合わせの libmruby を作って集める (案 A)。

- libmruby: `lib/add/family_mruby_ckpt_table.rb` が端末の構成ファイル (`family_mruby_esp32.rb` /
  `family_mruby_esp32p4.rb`) をそのまま読み、ビルドの名前を変えて、コンパイラをホストのものに、64 bit に
  (`MRB_32BIT` を外す。表はポインタ幅に依らない)、mruby-checkpoint とセーフポイントの hook を足す。
  gem の並び・定義・環境変数 (`FMRB_ASTERISM`・`FMRB_IMU`・`FMRB_HW_TARGET`) は端末のファイルのまま。
- **ハードウェアの port は posix の port で置き換える** (`conf.ports :posix`、`PICORB_PLATFORM_POSIX`)。port は
  プラットフォームのデータ (環境変数・板の名前) を返す関数で、クラスもメソッドも定義しない (定義は gem の src に
  あり、libmruby に入る)。posix の port も無いもの (GPIO・I2C・RMT などのハードウェア) は代役 (0 を返すだけ)。
  最初は port を全部代役にしたが、`ENV` の初期化が代役の「キーと値を返す関数」から出力引数を受け取れず、
  ごみの長さで文字列を作ろうとして `NoMemoryError` になり、後の gem が全部欠けた (676 名前)。出力引数を持つ
  HAL があるので「名前だけの代役」では足りない。
- Family mruby の gem の port (`picoruby-fmrb-*/ports/esp32`) は、ホスト版 (ネイティブ版・ブラウザ版) と同じ
  ように Linux の枝でコンパイルする (ESP-IDF の枝はホストでコンパイルできない)。定義は端末の板のもの
  (`fmrb_hw_defines.cmake` をそのまま呼ぶ)、アプリの上限・プロファイルは端末の値。2 つの枝の違いは定数の値
  (`PLATFORM`・`BOARD`・`CHIP_MODEL` など。値は VM が自分で持つので表に影響しない) で、定義の有無が違う所は
  見つからなかった。
- 端末で違いが出ても、その VM が違いを自分で持ち、`table: used with differences (...)` と最初の違いをログに
  出す (表は合う所で効く)。F6 で実機のログを見る。
- 結果: NARYAv4 (P4) 4,153 名前・snapshot 866・irep 1,719、NARYAv3 (S3) 3,356 名前・687・1,266。どちらも
  32 bit の目的 (`-m32`、`MRB_32BIT`、`-Werror`) でコンパイルが通る。表の大きさ (flash の rodata) は P4 で
  約 264 KB、S3 で約 210 KB (`.data` は 80 B: 表の構造体のポインタ。ESP32 では const なので rodata に置かれるはず。
  F6 で確かめる)。

## 5. 版の印 (`TBL0`)

- イメージの任意のセクション `TBL0` (長さつき): `u32` 名前の決まりの版 (`MRB_CHECKPOINT_TABLE_VERSION`、今は 1)
  + `u64` 書いた側の基準のダイジェスト。書く側は `DBG0` の前に必ず書く。
- 読む側は、版が自分と違えば `the image names its permanent objects by the rules of table version N, this build
  by version M` で断る (gem の試験で、版を 2 に書き換えたイメージを表あり・表なしの両方のドライバが理由つきで
  断ることを確かめた)。
- ダイジェストは照らさない。ビルドが違えば違うのが普通 (sim ⇔ ネイティブ版、ブラウザ版 ⇔ ネイティブ版の移動は
  名前を 1 つずつ引いて成り立っている) で、2 章の (b) のとおり表とファームの食い違いは名前の引き当てを誤らせない。
  ダイジェストは診断のため (インスペクタの `TBL0` に出る)。
- 形式の版 (v1) は上げない。任意のセクションは古い読む側が長さで読み飛ばし (§13)、名前の付け方 (§8) は変えて
  いない。表を使う VM と使わない VM の名前の一覧は、基準のオブジェクトを生かす隠した Array の 1 つぶんだけ違うが、
  イメージが参照する名前は同じ (下)。

## 6. 試料のイメージは前の gem と同じか

- 表あり・表なしのドライバで、試料 (断るもの・仮想の時計のものを除く 20 個) を 64 bit・32 bit で書き、`TBL0` を
  除いて同じバイト列 (40 組。`tools/test_table.rb`)。
- 前の gem (`04a04d3`) との違いは `TBL0` の 20 B (タグ 4 + 長さ 4 + 版 4 + ダイジェスト 8) だけ。名前の付け方と
  ほかのセクションの中身は変えていない。
- 表あり・表なしのドライバの間でイメージを行き来させても通る (`rake test` が `run_tests.rb` を混ぜて流す)。

## 7. Family mruby に入れた所

- `main/app/fmrb_app_checkpoint.c`: `fmrb_app_checkpoint_prepare_vm` (gem を開く・データの型・受ける側の
  メソッド。作る道具も同じものを呼ぶので、作る VM の基準がファームの VM の基準と同じになる)。表は弱い参照で、
  あれば `mrb_checkpoint_set_table`。ログに表の状態と一時的な量、部分ごとの量を 1 行ずつ出す。
- IDF の sim では main がアーカイブなので、弱い参照だけでは表のメンバーが取り出されない。`-Wl,-u,fmrb_checkpoint_table`
  を足した (最初の `SIM=1` の通しで `table: not used: no table` と出て気づいた)。
- GC の root の hook: `lib/patch/picoruby-mruby/lib/mruby/src/gc.c` (mruby-checkpoint の `patches/0004`)。
  `MRB_VM_SAFEPOINT_HOOK` のビルドだけにポインタを置く (ESP32 は今は持たないので内蔵 RAM は +0)。
- 道具: `tools/ckpt_table/` (`ckpt_table.cmake`・`ckpt_table.rb`・`gen_main.c`・`CMakeLists.txt`)、
  `lib/add/family_mruby_ckpt_table.rb`、`rake checkpoint:table`。

## 8. 前のサブから引き継いで直したもの

| もの | 直したこと |
|---|---|
| インスペクタ (`tools/lib/checkpoint_image.rb` ほか) | `TBL0` を知らず、試料の読み直しが「知らないセクション」の警告で 190 件落ちていた。読む・表示・JSON の型を足し、試験の差し込み位置を直した |
| `test:asan`・`test:stress` | 表ありのドライバも AddressSanitizer で流すようにした (`build/asan[-stress]-tabled`) |
| sim | 表を作る入口が無かった。`fmrb_ckpt_table_idf` を足した (依存の輪を避けるため、性質はジェネレータ式で読み、リンクはしない) |
| ブラウザ版 | 代役の型が合わず、作る VM が `mrb_hal_task_init` の署名違いで trap していた。import の型から代役を作る形にし、未解決の一覧を生成した JS から読むようにした |
| ログ | 「表なし」の文言が、違いがあって使っているときも `not used` と出ていた。gem の状態をそのまま出す |
| P4・S3 | 道具が無かった。4 章 |
| 作る VM のプール | 32 MB は estalloc の上限 (24 bit の番地、16 MB) を超えていた。8 MB にした (P4 の表の `NoMemoryError` を追う途中で気づいた。その誤りの原因は別、4 章) |
| 作る VM のファイル | 代役のファイルの HAL が出力引数を埋めず、sim の表で `NoMemoryError`。「見つからない」を返す HAL を置き、作る VM が開くときに例外を起こしたら止まるようにした (3 章) |

前のサブの設計 (2 章の (b)、`keep` をアドレスの配列にして root の hook で生かす、ivar の値を名前の番号か値の
場所で持つ、`TBL0`) はそのまま使った。

## 9. 確認

すべて本体の checkout で `MRUBY_CHECKPOINT_DIR=<mruby-checkpoint の wt/ff>` を付けて流した。

| 何を | 結果 |
|---|---|
| mruby-checkpoint の `rake test` | 通る。試料 461 を表なし・表あり (64/32 bit)・表あり 64 bit と表なし 32 bit の混ぜで 3 回、表の試験 87 (イメージ 40 組が `TBL0` を除いて同じ、版違いの断り、表と違う VM)、狙った点、インスペクタ 3,259 |
| `rake test:asan`・`test:stress` | 通る (表なし・表ありのドライバそれぞれ 461 / 461) |
| `rake regress:checkpoint` (既定・`SIM=1`・`WEB=1` を一度に) | 通る (標準・全部 mruby のネイティブ版、sim の 4 構成 (retro・modern × 標準・mruby、sim ⇔ ネイティブ版)、ブラウザ版の 4 場面。15.4 分)。どの VM も `table: used with differences (1 names of its own, 0 missing, 1 snapshots seen otherwise)` |
| `rake regress` | 通る (ホスト 20 suites、描画の記録 205) |
| `rake regress:run` | 通る (retro・modern、幅の中) |
| `rake regress:screens ONLY=retro,modern,native,native-keys` | 通る (27 / 27) |
| `rake regress:size` | NARYAv3 (S3): DIRAM 125,923 (−112、mdns の版の固定によるもの)、IRAM +0、ほか +0。**内蔵 RAM は増えない**。TAB5 (P4) は IDF の構成の段 (`Missing required kconfig option after retry`、esp_hosted の manifest の Kconfig 変数が無いという警告の後) で止まり、測れなかった。部品をコンパイルする前の段で、この段の変更 (linux の枝と `MRB_VM_SAFEPOINT_HOOK` の中だけ) は通らない。直前の依存の変更 (`3401ba62`、mdns の版の固定) との関係は調べていない。11 章の 9 |
| P4・S3 の表 (`rake checkpoint:table`) | NARYAv4・TAB5 4,152 名前、NARYAv3 3,356 名前。32 bit でコンパイルが通る |

## 10. 普段の速さ

valgrind の命令数は測っていない。このホストに valgrind が無く (perf もハードウェアの数を取れない)、sudo を使わない
決まりのため。触ったのは基準を作る所と読み書き、GC の root をなめる最後の 1 か所 (hook のポインタが NULL かを
見る。gem のビルドでは VM ごとに `$__checkpoint_state__` を引いて基準を持つ VM だけ印を付ける) で、命令の
ループには触れていない。GC 1 回あたりの増分は、印を付ける対象 (基準のオブジェクト。表なしでは隠した Array の
要素として同じだけ印を付けていた) を除けばシンボルの引き 1 回。

## 11. サブの仮決め (ユーザが見直す)

1. C の関数は名前で結ぶ (2 章の (b))。番地を表に入れる形 (a) は gem のソースに手が要るので取らなかった。
2. ESP32 にしか無い port は posix の port で置き換え、それも無いハードウェアの port は 0 を返す代役 (4 章)。
   Family mruby の gem の port は Linux の枝でコンパイルする。
3. P4・S3 の表は 64 bit のホストで作る (`MRB_32BIT` を外す)。表がポインタ幅に依らないことは gem の試験で確かめた。
4. 版の印は名前の決まりの版だけを照らし、ダイジェストは照らさない (5 章)。形式の版は上げない。
5. 表と違う VM は断らずに違いを自分で持つ (ログに最初の違いを出す)。
6. 作る VM が開くときに例外を起こしたら、表を書かずにビルドを止める。
7. P4・S3 の表の置き場所は `tmp/ckpt_table/<板>/` (ビルドの外)。F6 で IDF のビルドに入れる。
8. VM を開くたびの部分ごとの量のログ (1 行) は残した (F6 で実機の量を読むため)。
9. `regress:size` の TAB5 の構成の誤りは、この段の外のものとして追っていない (9 章)。

## 12. `movable = true` を付ける候補 (この段では付けていない)

アプリの状態が小さく、ファイル・ソケット・Asterism のセッションを持ち続けず、`on_update` で毎回描くもの:

| アプリ | 理由 |
|---|---|
| `/app/demo/kamon.app.rb` | スプライト・音・ファイルを使わない。描き直しは `on_update` |
| `/app/game/blockgame.app.rb` | スプライトもファイルも無い。効果音は戻らないだけ |
| `/app/game/othello/othello.app.rb` | 盤の状態だけ。効果音は戻らないだけ |
| `/app/demo/piano.app.rb` | 状態が小さい。鳴っている音は戻らない |

外したもの: スプライトを使うもの (`flappy`・`shooter`。`on_restore` で作り直すコードが要る)、音楽の再生
(`nsf_player`・`smf_player`。ファイルと再生の状態)、ネットワーク (`weather`・`mahjong`)。

## 13. F6 に要るもの

- IDF の P4・S3 のビルドに gem を入れるとき、`rake checkpoint:table` の表を main のソースに足し、
  `-Wl,-u,fmrb_checkpoint_table` (main はアーカイブ) を付ける。表の構造体が rodata (flash) に置かれることを
  map で確かめる。
- 実機のログの `table: ...` を見る。違いがあれば最初の違いが出る (4 章の Linux の枝との差など)。
- 作る間の一時的な量 (32 bit で約 173 KB、残る量と合わせて約 263 KB) が S3 のプールに収まるか。
- 表の flash の量: P4 約 264 KB、S3 約 210 KB。

## 14. 撤回した仮説・踏んだ罠

- 「名前だけを持つ代役で ESP32 の port は足りる」: 出力引数を持つ HAL (`ENV_get_key_value`) で崩れた (4 章)。
- 「弱い参照で表があれば使う」: IDF の main はアーカイブなので、弱い参照ではメンバーが取り出されなかった (7 章)。
- 「emcc は未解決の関数を 1 つずつ警告する」: 2 回目以降は「warnings in JS library compilation」とまとめて
  しまい、一覧が空になった。生成した JS の `missing function:` から読む。
- 「wasm でも 0 を返す代役でよい」: 型の違う呼び出しは wasm-ld が unreachable の代役に替える (3 章)。
- 「P4 の表の `NoMemoryError` はプールが大きすぎるせい」: 32 MB が estalloc の上限 (16 MB) を超えていたのは本当で
  直したが、誤りの原因は ENV の代役だった (4 章)。
- アプリの VM を開くたびに Asterism のアイコン (`/usr/share/asterism/icon.png`) を読み、その中身を VM が持つ
  (`picoruby-fmrb-asterism` の読み込みで `Asterism.profile(...)` が `profile_hash(true)` を返し、アイコンを読む。
  コメントには「接続するまで読まない」とある)。表を作る VM にはファイルが無いので、これが表と違う唯一の所
  (`V:Asterism@@profile_icon`) になった。表の側では違いとして VM が持つだけで害は無い。アイコンを毎回読むのは
  この段の外の話なので直していない (Asterism の gem)。
