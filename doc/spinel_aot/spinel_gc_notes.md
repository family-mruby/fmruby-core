# Spinel GC の知見 (fmruby での観測と調整)

Spinel ランタイム (components/fmrb_spinel_rt/spinel_rt) の GC が
「いつ・どのくらい止まるか」と、fmruby 側で制御できるものの整理。
mruby VM の GC は別物で、そちらは [../reference/gc_monitoring.md](../reference/gc_monitoring.md)。
メモリ消費の分析 (48B ヘッダ、live 1.9x 等) は
reports/memory_model_findings.md が正で、本書は**タイミング**の話。

## 1. 仕組み (sp_alloc.c / sp_gc.c、2026-08-06 時点の fork)

- **発火は割り当て駆動・同期・stop-the-world**。インクリメンタル機構は
  無い。しきい値を超えた**その割り当ての中で** `sp_gc_collect_retune()`
  がフル収集を実行する。つまり止まる場所は選べない (カーネルなら入力
  イベント処理や MIDI 転送の途中でも止まる)。
- **ヒープは 2 本で別勘定**: オブジェクトヒープ (`sp_gc_bytes` /
  `sp_gc_threshold`) と文字列ヒープ (`sp_str_heap_bytes` /
  `sp_str_threshold`)。文字列の圧力はオブジェクト側に**合算されない**
  (意図的)。どちらが超えても収集は両ヒープを掃く。
- **しきい値の自動調整** (両ヒープ同じ式):
  - ランタイム既定の初期値/下限は 256KB だが、**fmruby のインスタンス
    初期化はこれを pool/32 に上書きする** (kernel: 500KB プール →
    **15.6KB**。`main/kernel/fmrb_kernel.c:612-620`、desktop Spinel 実験も
    同値)。理由はコメントに明記: **Spinel の枯渇は `sp_oom_die` =
    ファームウェア全体の abort** なので、バーストがプール天井に届く前に
    早めに刈る。
  - 収集後: **生存量 x 4** (下限 = 上記の初期値)
  - 回収が 25% 未満だった場合: **しきい値を倍に** (バックオフ)
  - 収集後に `sp_gc_enforce_mem_limit()` がプール上限への頭打ちを効かせる
  - **帰結**: 文字列ヒープは生存が数 KB と小さいため、しきい値が下限
    (15.6KB) 近くに張り付く。**文字列 churn がそのまま収集頻度に直結する**
    (下記 2 の MIDI 時の数字)。
- **世代別もどき・write barrier 無し**: young/old リストがあり、sweep は
  `SP_GC_FULL_INTERVAL` = 8 回に 1 回だけフル。ただし **mark は毎回
  生存集合の全量** (`sp_gc_mark_all` が全ルート + 全走査)。
  「収集 1 回のコスト ≒ 生存量のフルマーク」で、これは分割できない。
- フル sweep の回では `malloc_trim(0)` (Linux で実メモリ返却。ESP32 では
  実質 no-op)。

## 2. コストの当て方

- 停止時間 ~ 生存オブジェクト数 x ポインタ追跡コスト。**ESP32 では
  ヒープが PSRAM にあるため、この「追跡」が sim との最大の乖離点**
  (mruby で実測 330 倍の前例。Spinel は個別確保 + intrusive list なので
  参照局所性はさらに悪い可能性がある)。
- kernel の実測 (2026-08-06、P4): live 96〜166KB を小さく往復。
  しきい値は live x4 = 400〜660KB 相当だが 512KB プールに頭打ち →
  **おおよそ churn 250〜350KB ごとに 1 回の STW フルマーク**。
  アイドルでは滅多に発火しないが、MIDI 演奏中はカーネルが毎秒数百
  メッセージを pure-Ruby msgpack で処理するので間隔が縮む。
- **P4 実機での停止時間はまだ実測していない**。対策の要否はまず計測から
  (下記 4)。

### 2.1 カーネル熱経路の churn カタログ (2026-08-06 机上解析、ILP32 換算)

割り当て単価: `[]` リテラル/Array ビルダ = PolyArray **~296B**、
`{sym: v}` = SymPolyHash **~456B**、poly キー Hash = **~668B**、
文字列 = 26+nB、整数の to_s/補間 = **58B**。ブロックは for に
インライン化され proc 割り当て無し。Symbol/Integer は即値でゼロ。

| シナリオ | オブジェクトヒープ | 文字列ヒープ | 収集頻度の目安 |
|---|---|---|---|
| アイドル | ~0 | ~0.8KB/s | ~20 秒に 1 回 |
| マウス移動 30Hz | ~15KB/s (ドラッグ中 22KB/s) | ~8KB/s | 1〜5 秒に 1 回 |
| MIDI 300msg/s | **~840KB/s** | **~200KB/s** | **毎秒 7〜12 回** |

主要な発生源 (レート x 単価の降順):

1. **音声メッセージの `MessagePack.unpack`** (audio_handler.rb:10 →
   msgpack_pure.rb) — note_on 1 件で ~2.9KB。`read_bytes` がフィールド
   ごとに中間 PolyArray (296B) を作るのが主犯。ワイヤ形式は C 側で固定
   なので **getbyte 直読みに置き換えれば unpack ごと消せる** (さらに
   踏み込むなら C shim で HOST へ直転送し Ruby を起こさない)
2. **`_poll_message` のメッセージ Hash** (fmrb_kernel_base_spinel.rb:84)
   — 1 メッセージ 456B + payload コピー。ディスパッチは同期なので
   **Hash 1 個を使い回せる**
3. **熱経路の `Log.debug` 補間** (fmrb_kernel.rb:90、audio_handler.rb:29)
   — C 側で捨てられても **Ruby 側の補間は毎回実行される** (整数 to_s
   58B x2 + バッファ ~97B)。マウスで 6.4KB/s、MIDI で 100KB/s 相当。
   ivar ガードか削除
4. **ウィンドウ一覧の再構築** — mouse-down ごと + ドラッグ中 ~3Hz で
   1 回 ~2.6KB (4 窓)。ドラッグ中は座標だけ**キャッシュ内を直接更新**
   すれば再構築不要
5. HID の作業バッファ `"\x00"*6` が毎イベント新規 (input_router.rb:131
   ほか) — ivar に持って setbyte (送信側は即 memcpy するので安全。
   pending_move 用に 2 本目が要る)
6. アイドルポーリングが timeout 判定の**前に**空文字列を作る
   (fmrb_kernel_base_spinel.rb:80) — 30Hz x 26B。peek FFI を足せば消えるが
   低優先

対策 1+2+3 で MIDI 時 churn の **~95%**、2+3+5 でマウス時の **~85%** が
消える見積り。HID のバイト解析 (getbyte)、tick_process、ウィンドウ検索は
既に無割り当てで手当て不要。カーネル⇔アプリの周期ハートビートは存在しない
(LED は C のみ)。

**採用判断 (2026-08-06)**: P4 実機の体感 (MIDI 数十秒以上安定、操作に
気になる引っかかり無し) から、**「小さな生存集合を細かく頻繁に刈る」現行
設計は意図どおり機能している**と判断。#1 (msgpack 除去) はコードの明瞭さを
犠牲にするため見送り、**#2 (受信 Hash の使い回し) と #3 (熱経路 Log.debug
の `KERNEL_TRACE` ガード) のみ実施**。#1 と C 直転送は、将来 MIDI 時の
GC が問題として観測された時の切り札として残す。

## 3. ビルド定義の罠 (ESP32)

- `SP_GC_MARK_STACK_MAX` / `SP_GC_STACK_MAX` は **8192** に設定してある
  (components/fmrb_spinel_rt/CMakeLists.txt)。上流デフォルトの 64K
  エントリはマークスタックだけで LP64 512KB / ILP32 256KB を食うため。
  **ランタイムと生成 C の両方が同じ値でコンパイルされる必要がある**
  (CMake が単一変数から両方に配る。手で片方だけ変えると壊れる)。
- デバッグ環境変数 (再ビルド不要、sim で有効):
  - `SPINEL_GC_STRESS=1` — しきい値を 2KB に落とし、割り当てのたびに
    ほぼ毎回収集。解放済みオブジェクト参照系のバグを最短で出す
  - `SPINEL_GC_VERIFY=1` — マーク中に非ヒープ/破損オブジェクトへ
    到達したら即 abort + どのルート/走査で踏んだかを表示

## 4. fmruby 側の制御点 (現状)

- **カーネルのメインループにアイドル時 GC は無い**。発火タイミングの
  制御は現状ゼロで、唯一の明示フックは `fmrb_spx_app_gc()`
  (main/app/fmrb_spx_app.c。Spinel 版ランチャーのアイコンロード churn を
  ループ内で明示回収するために作られた)。
- 対策の設計方針 (未実装、優先順):
  1. **計測が先**: `sp_gc_collect_retune` の呼び出し回数と最大停止時間を
     周期ダンプ (fmrb_app の ExcHW 欄の隣) に出す。無割り当てで可能。
     P4 の実数が無いと対策の規模を決められない。
  2. **アイドルポイント収集**: カーネルの「キューを掃き切った直後」に、
     しきい値の 7 割超で先回り `sp_gc_collect()`。Spinel は分割できない
     ので、mruby の idle_gc のように「仕事を割る」のではなく
     **「止まる瞬間を選ぶ」**のが Spinel 流。
  3. **churn 削減**: 熱経路 (メッセージ受信 FFI / input_router /
     音声転送 / msgpack_pure) の割り当てを減らす。頻度に直接効く。
     mruby 側の定石 (gc_monitoring.md 7 章) は Spinel でもそのまま有効。

## 5. 参考

- 収集本体: components/fmrb_spinel_rt/spinel_rt/sp_gc.c (`sp_gc_collect`)
- しきい値と再調整: 同 sp_alloc.c 冒頭 / sp_alloc.h の
  `sp_gc_alloc` / `sp_str_alloc`
- メモリ消費の分析: reports/memory_model_findings.md
- 上流仕様の裏取り: [spinel_upstream_notes.md](spinel_upstream_notes.md)
  (GC は公式 2 行のみ、我々の調査の方が詳しい)
- mruby 側 GC (desktop/アプリ): [../reference/gc_monitoring.md](../reference/gc_monitoring.md)
