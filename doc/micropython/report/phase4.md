# Phase 4 実装レポート

## 完了条件の判定

| 条件 | 判定 | 検証内容 |
|---|---|---|
| S3 / P4 の両ターゲットで rake build:esp32 が成功 | OK | 下記 |
| サイズ実測表を記録し、フラッシュ残量が許容内であることを数字で示す | OK | phase4.md「サイズ実測」 |
| known_limitations.md 作成済み | OK | doc/micropython/known_limitations.md |
| 実機確認の結果を記録 | ほぼ OK | S3 で主要項目を確認。残り 3 件は下記 |

S3 実機で起動・描画・ページ切替・排他・終了・再起動・停止応答・
**GC のレジスタ退避**がすべて通り、**MicroPython 固有の問題はゼロ**だった。
実測値と結果は phase4.md「実機確認の結果」。残るのはビジーループ中の停止
(VM フック経由) と .rb/.lua/.bas の退行の 2 件で、どちらも Linux
シミュレーションで確認済み・実機固有の要素が薄いもの。

### ビルド

| ターゲット | 結果 |
|---|---|
| ESP32-S3 (NARYAv3 / N16R8, Xtensa) | 成功 |
| ESP32-P4 (TAB5 / Modern, RISC-V) | 成功 |
| Linux シミュレーション (回帰確認) | 成功。デモアプリの描画・GC とも正常 |

## Xtensa で 1 件だけ引っかかった: GC のレジスタ退避

S3 ビルドで唯一止まったのがこれ。

```
mp_embed/shared/runtime/gchelper_generic.c:194:2: error:
  "Architecture not supported for gc_helper_get_regs.
   Set MICROPY_GCREGS_SETJMP to use the fallback implementation."
```

GC は「レジスタにしか入っていないオブジェクト参照」を root として拾うために
callee-saved レジスタを読み出す必要がある。本家はアーキテクチャごとに
インラインアセンブラでこれを書いているが、**Xtensa 版が存在しない**
(x86/x86_64/ARM/RISC-V はある)。エラーメッセージ自体が代替手段を示している
とおり、`MICROPY_GCREGS_SETJMP` を立てると `setjmp()` で jmp_buf に
レジスタを吐き出させ、それを走査する実装に切り替わる。

mpconfigport.h で **全ターゲット共通で 1 に**した。理由は NLR を setjmp に
固定したのと同じで、linux (x86_64) と実機で GC の挙動を変えたくないため。
片方だけアセンブラ実装だと、「シミュレーションでは再現しない GC バグ」を
持ち込む余地が残る。

x86_64 でも壊れていないことは、テストアプリに入れた GC 確認で見た
(`pytest: gc ok=True free 241664 -> 245376`)。ローカル変数からしか
届かないオブジェクトを作って collect し、読み戻して内容が一致することを
確認する形にしてある。**Xtensa / RISC-V での同じ確認は実機待ち**
(取りこぼすと「後になって壊れる」出方をするので、実機の確認項目に入れた)。

生成物への手編集は不要で、mpconfigport.h の変更 + `rake micropython:gen` で
解決した (submodule も mp_embed も触っていない)。

## 実測値

### コンポーネント単位の寄与

全体バイナリの差分ではなく `idf.py size-components` を使った。MicroPython
だけの取り分がそのまま出るので、他の変更が混ざらない。

**ESP32-S3**

| アーカイブ | 合計 | Flash .text | Flash .rodata | 内蔵 RAM (.bss) |
|---|---|---|---|---|
| libmicropython.a | 127,912 B | 102,146 B | 25,321 B | 445 B |
| liblua.a (参考) | 119,100 B | 115,809 B | 3,291 B | 0 B |
| libbasic.a (参考) | 49,017 B | 41,186 B | 7,823 B | 8 B |

**ESP32-P4**

| アーカイブ | 合計 | Flash | 内蔵 RAM (.bss) |
|---|---|---|---|
| libmicropython.a | 149,992 B | 149,547 B | 445 B |
| liblua.a (参考) | 147,789 B | 147,789 B | 0 B |

**当初見込みのフラッシュ +200-300KB を下回った**。Lua とほぼ同じ規模で、
「Lua が入っているならこれも入る」と言える大きさ。

rodata が Lua より 22KB 多いのは、qstr 表と、ファームに焼き込んだ
Python 製アプリ基盤 (13.4KB) の分。

### バイナリとパーティション

| ターゲット | fmruby-core.bin | パーティション | 残量 |
|---|---|---|---|
| S3 | 2,451,968 B | 3 MB | 694,272 B (22%) |
| P4 | 4,048,048 B | 6 MB | 2,244,432 B (36%) |

### ヒープ配置 (作業項目 3)

アプリ用メモリプールは `components/fmrb_mem/fmrb_mempool.c` で
全て `EXT_RAM_BSS_ATTR` (= PSRAM) 宣言されている。GC ヒープ 256KB は
`POOL_ID_USER_APP*` (500KB) から取るので **PSRAM 側**。

内蔵 RAM に置かれるのは libmicropython.a の DIRAM .bss 445 B のみで、
IRAM の使用はゼロ。**`FMRB_MP_HEAP_SIZE` を内蔵 RAM 予算と相談して
決め直す必要はない**。

## 実機で確認してもらうこと (作業項目 4, 5)

Linux シミュレーションでは原理的に測れない・確かめられないものだけを残した。

| # | 確認項目 | なぜシミュレーションでは足りないか |
|---|---|---|
| 1 | python.app の起動・描画・ページ切替・終了 | 実機の描画経路 (NTSC / MIPI-DSI) を通っていない |
| 2 | 2 本目の起動が拒否され、ダイアログが出る | 排他は共通だが実機の通知経路を通していない |
| 3 | ビジーループ中の停止が効く | VM フックは共通だが、実機の割り込み負荷下では未確認 |
| 4 | `task stack low water` のログ値 | Linux は FreeRTOS を POSIX スレッドで模擬するので 120KB 超のスタックになり、16KB の実機予算を全く反映しない |
| 5 | `pytest: gc ok=True` が出ること | GC のレジスタ退避が Xtensa / RISC-V で正しいかは実機でしか分からない |

依頼手順は下記「実機確認の依頼」に書いた。

## 実機確認の依頼

### 書き込み

```
rake clean_all
rake build:esp32          # S3 (.env の FMRB_HW_TARGET=NARYAv3)
rake flash
```

Tab5 (P4) の場合は `FMRB_HW_TARGET=TAB5` を付ける。
Tab5 は DTR/RTS でのリセットに対応していないので、書き込み後に
本体のリセットボタンを押す。

### 確認 1: デモアプリ

1. メニュー -> Launcher。**一覧に "Python" が出ない場合はランチャー上で
   右クリックして再スキャン** (`/data/launcher_index` のキャッシュがブート時に
   そのまま使われるため、更新前のキャッシュが残っていると出ない)。
2. "Python" をダブルクリック。ウィンドウ枠 (タイトルバー・ハンバーガー・
   タイトル文字・白い閉じるボタン・角丸枠) と Shapes ページが出ること。
3. ユーザ領域をクリックして Shapes -> Lines -> Text と切り替わること。
4. Text ページに `sum(1..10) = 55` / `2 ** 16 = 65536` /
   `sorted = [1, 2, 3]` が出ること。
5. 閉じるボタンで終了し、もう一度起動できること。

### 確認 2: 排他

Python アプリを動かしたまま、シェルから
`run /home/test/pytest.app.py` を実行。
「Another Python app is already running.」のダイアログが出ること。
1 本目を閉じてから同じ操作をすると今度は起動できること。

### 確認 3: 停止とログ値

1. シェルから `run /home/test/pytest.app.py`。
2. 小さいウィンドウに `turn N` が 5 秒ごとに増えること。
3. **ログに `pytest: gc ok=True ...` が出ること** (False や
   異常終了なら GC のレジスタ退避が実機で効いていない)。
4. ユーザ領域をクリックするとビジーループに入る (`busy loop start`)。
   その最中に閉じるボタンを押して止まること。
5. 終了時のログ
   `[Python test] MicroPython runtime closed (task stack low water=NNNN bytes)`
   の **NNNN を報告してほしい**。アプリタスクのスタックは 16KB
   (`FMRB_USER_APP_TASK_STACK_SIZE`) なので、この値が小さすぎるようなら
   MicroPython のときだけスタックを増やす。

### 確認 4: 退行がないこと

`.rb` / `.lua` / `.bas` のアプリが従来どおり動くこと
(Shapes、Lua app demo、BASIC app demo あたり)。

### NG が出たら

ログを丸ごと回収してほしい。原因を特定してから直す
(推測での修正は繰り返さない)。

## 実装中の気づき

### 1. Xtensa には GC レジスタ退避のアセンブラ実装が無い [対応済み]

上記のとおり。**Linux ビルドだけでは絶対に出ない類の問題**で、
phase0 の申し送り (「ESP32 で初めて出るものがある」) がそのまま当たった。

### 2. サイズ比較は size-components のほうが素直だった

計画は「MicroPython 有り/無しの直前コミットとの比較」を指示していたが、
コンポーネントが入ったのは phase1 (8780693) で、その親コミットまで戻ると
phase2/phase3 のデスクトップ変更 (taskbar の色表、launcher の SCRIPT_EXTS、
デモアプリ 2 本) も一緒に差分へ混ざる。

`idf.py size-components` は libmicropython.a の寄与をそのまま出すので、
**知りたい数字が混ざりものなしで得られる**。比較対象として liblua.a を
並べれば「Lua と同程度」という判断もそのまま読める。

### 3. .env の HW ターゲット上書きは既に直っている

`FMRB_HW_TARGET=TAB5 rake build:esp32` がきちんと P4 を選んだ
(ログの `HW target: TAB5 (esp32p4)` で確認)。Rakefile の .env 読み込みが
`ENV.key?` を見るようになっており、コマンドラインが勝つ。

### 4. mpconfigport.h に extern 呼び出しを足すとホスト単体スモークが壊れる

phase2 で `MICROPY_VM_HOOK_LOOP` を入れたとき、vm.c から `fmrb_mp_vm_hook()`
が呼ばれるようになった。実体は fmrb_mp.c にあるが、ホスト単体スモークの
リンク対象には入らない (fmrb_mem 等に依存するため)。phase3 の
`MP_REGISTER_MODULE` も同じ理由で `fmrb_user_cmodule` を未定義にした。
どちらも `rake micropython:smoke` がリンクできなくなるだけで、
ファームウェアには影響しない。

**mpconfigport.h や生成物側から fmruby-core の関数・オブジェクトを参照する
ものを足したら、port/test/main.c にスタブが要る**。スタブは main.c に置く
(mpport.c はファームウェアビルドにも入るので、実体と定義が衝突する)。

そして**スモークは各フェーズの完了確認で毎回回す**。phase2 以降回していな
かったので、レビューで指摘されるまで壊れたままだった。

### 5. 実機でスタック上限が Linux の 1/10 になるが、それでも余る

シミュレーションでは `stack limit=121704`、S3 実機では `stack limit=12092`。
10 倍違うのは Linux ターゲットが FreeRTOS を POSIX スレッドで模擬していて
`FMRB_USER_APP_TASK_STACK_SIZE` (16KB) を無視するため。**phase2/3 の
スタック実測が実機の判断材料にならないと書いたとおりだった**。

実機のピーク使用量は 4,660 B で、上限 12,092 B に対して十分な余裕がある。
デモアプリの範囲では 16KB のままでよい。

### 6. アプリ基盤のコンパイルは実機で 63-118ms

Linux の 0-3ms に対して S3 は 63ms (デスクトップのみ) / 118ms
(シェルも動作中)。CPU の取り合いでほぼ倍になる。GC 消費は逆に減る
(8,496 B。32bit でオブジェクトが小さいため、Linux の 12,672 B より少ない)。

frozen bytecode (mpy-cross) を入れればこの時間は消えるが、mpy-cross を
ビルドに持ち込む手間に見合うかは微妙なところ。ランチャーのアプリ走査が
秒単位かかる中では体感に出ないので、当面は見送り、**判断の基準値は
この 63-118ms** としておく。

### 7. GC のレジスタ退避は Xtensa でも正しかった [実機確認済み]

phase4 で一番確かめたかった項目。`MICROPY_GCREGS_SETJMP` に切り替えた
GC のルート走査が実機で正しいかは、外すと**後になって壊れる**出方をするので
実機でしか分からない。

pytest.app.py に入れた確認 (ローカル変数からしか届かないオブジェクトを
作って collect し、読み戻して内容が一致するか) が
`pytest: gc ok=True free 246176 -> 248256` を返した。回収も効いている
(空きが 2,080 B 増えている)。

### 8. `_spin` 待機中の停止は実機でも 20ms

`on_update` が 5000ms を返すアプリで、turn から 3.9 秒後 (次の turn まで
1.1 秒あるタイミング) に閉じるボタンを押したところ、20ms 後に
`on_destroy` が走った。**待機の残りを待たされていない**ことが実機でも
確認できた。phase3 の設計 (受信でそのまま起きる + 100ms ごとの
停止フラグ確認) が意図どおり効いている。

### 9. 内蔵 RAM への影響がほぼ無いのは PSRAM 配置のおかげ

MicroPython 自体の内蔵 RAM は 445 B。GC ヒープが PSRAM のプールから
出ているので、内蔵 RAM 逼迫 (doc/reference/internal_ram_budget.md) の話には
絡まない。逆に言えば **PSRAM の無い構成では成り立たない**。
