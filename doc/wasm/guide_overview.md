# 解説 1: Family mruby on WebAssembly — 全体像

> 状態: 完了 (P4 時点の実装を記述) | 更新: 2026-08-29 | 外部説明用の解説群 その 1

この文書群は、Family mruby core をブラウザ (WebAssembly) に移植した成果を、
外部に説明できる粒度で解説する。4 本構成:

1. **本書** — 全体像と成立原理
2. guide_freertos_port.md — FreeRTOS 移植の詳細 (核心部)
3. guide_integration.md — core 統合・入出力・ページの詳細
4. guide_commits.md — 関連コミットの日本語解説

一次資料は doc/wasm/ の plan.md / implementation_plan.md / report/p1〜p4a.md。
本書群は「読み物」として再構成したもので、数値・経緯の正はそちら。

## 何を達成したか

ESP32 向けファームウェアである Family mruby core を、**ソースをほぼそのまま**
WebAssembly にコンパイルし、ブラウザ内で動かした。動いているのは描画デモでは
なく OS 一式である: FreeRTOS カーネルの上で 15 本前後のタスクが走り、Spinel
AOT コンパイルされたカーネル VM、mruby のデスクトップ、Spinel のエディタ、
APU (音源) までが 1 つの wasm モジュールに入っている。実機 (Tab5) と同じ
壁紙のデスクトップが出て、マウスとキーボードが効き、起動ジングルが鳴る。

技術的な新規性は 1 点に集約できる:

> **FreeRTOS の公式 WebAssembly 移植は存在しない**。最も近い POSIX 移植は
> プリエンプションをシグナル (SIGALRM) に全面依存しており、シグナル配送の
> 無い wasm では 1 行も動かない。本移植はカーネル本体を無改変のまま、
> port 層を協調スケジューリングとして新規に書いて成立させた。

## 層の構造

```
ブラウザのメインスレッド (JS)     … Canvas 描画・入力捕捉・AudioWorklet
  ↑ SharedArrayBuffer (フレーム/入力/音の各リング)
wasm モジュール (Web Worker 群)
  wasm/backend/                  … 表示・入力・音の wasm 差し替え口
  main/ + components/           … core 本体 (Linux ビルドとほぼ同一集合)
  libmruby + Spinel 生成 C       … 言語処理系 (emcc でクロスビルド)
  wasm/vendor/freertos/          … FreeRTOS カーネル (ESP-IDF 版、無改変)
  wasm/port/                     … 本移植の核心: Emscripten 協調 port
```

- 1 タスク = 1 pthread = 1 Web Worker。ただし**走るのは常に 1 本**
  (単核 CPU の再現)。
- main() 自体も Worker の中 (-sPROXY_TO_PTHREAD)。ブラウザの本来の
  メインスレッドは wasm を実行せず、画面と入力の橋渡しだけを行う。
  だから core がビジーでもページは固まらない。

## 成立させた 3 つの原理

### 1. 単一実行の協調スケジューリング

走っていないタスクの Worker は、自分専用の条件変数 (実体は
SharedArrayBuffer 上の futex = Atomics.wait) で本当に眠っている。
文脈切替は「次のタスクの条件変数を signal して、自分のを wait する」だけ。
割り込みが存在しないため、クリティカルセクションは入れ子カウンタ 1 個に
退化する (締め出すべき非同期実行が無い)。

### 2. 実時間からの追いつき tick

タイマ割り込みが無いので、1ms tick は「壁時計と比べて遅れている分を
まとめて供給する」方式にした。供給点は 2 箇所だけ:
yield の入口と、全タスクが眠っているときのアイドルフック (1ms 寝てから
追いつく)。起動時刻を基準に数えるので恒久的なずれは発生しない。
実測: 10 秒走らせて実時間とのずれ 0.2〜1.2ms、vTaskDelay(100ms) の誤差 1ms 未満。

### 3. 自給タイムスライス (VM が自分で自分を剥がす)

協調 port では「CPU を握った Ruby プログラムを外から剥がす」ことが
原理的にできない。そこで mruby VM 自身が、バイトコード 65536 命令ごとに
壁時計を確認し、50ms のスライスが尽きたら自分に切替要求を立てる方式を
作った (MRB_TASK_TICK_SELF_SUPPLY。ESP32/Linux では従来方式のまま)。
実測でタイムスライスは実機と同一の 50ms、sleep 精度も同一。

この 3 つから、この port の**生存条件**が導かれる:

> **全タスクが、どこかで必ず FreeRTOS の待ちに入ること。**
> 協調 port では待ちに入ることがそのまま譲り合いであり、待たずに
> スピンするタスクが 1 本あるだけで全員止まる。
> (Spinel プログラムはイベント駆動でループごとに queue 待ちに入る構造
> だったため、追加対応なしでこの条件を満たした。)

## 制約 (割り切ったもの)

- プリエンプション無し。C コードが無限ループすれば全体が止まる
  (Ruby コードは自給タイムスライスで守られる)。
- スタック計測 (uxTaskGetStackHighWaterMark) は無意味。タスクは
  Emscripten が確保した pthread スタックで動き、カーネルのスタック
  ブロックは per-task 記録の置き場でしかない。溢れ検出は Emscripten 側
  (-sSTACK_OVERFLOW_CHECK)。
- タスクスタックは下限 64KB (実機の 2KB 級指定はそのまま使えない)。
- MIDI の 1ms 精度・動画 (HW JPEG)・マイク・ネットワーク (BSD ソケット) は
  初期スコープ外。デバッグ系 (debugd TCP) は起動しない。

## 主要な数字 (report 実測)

| 項目 | 値 |
|---|---|
| PoC (P1) | 23 検査項目 x 5 回連続 全 PASS |
| tick のずれ | 0.2〜1.2ms / 10 秒 |
| タイムスライス | 両方式とも 50ms (実機と同一) |
| 自給方式の VM 速度 | 既定比 +4% (N=65536) |
| CPU 合成の正しさ | 実機 5 画面で PPA と 0 px 一致 (P3) |
| ブート (node/WSL2) | 最初のログ ~1s、デスクトップ生成 ~1.5s |
| 成果物サイズ | core_web.wasm 5.3MB + データ 1.4MB |
| 起動ジングル | DFT で C3→F3→G3→C4 を数値確認 (261.6Hz 系) |

## 外部説明のための一言まとめ

「Ruby で動く自作 OS (Family mruby) を、FreeRTOS カーネルごと WebAssembly に
移植した。公式移植の無い FreeRTOS を、シグナルの無い世界で協調スケジューリング
として成立させ、Ruby のタイムスライスは VM の自己申告方式に置き換えた。
描画合成は事前に実機とピクセル一致を検証した CPU 実装を共有しており、
ブラウザに出るデスクトップは実機と同じ画である。」
