# 構想: 純 Ruby モジュールの Spinel 選択オフロード

状態: 構想 (実装なし)。実現性の一次検討まで。
発端: MIDI P6 (doc/midi/report/p6.md) で「実機の mruby では純 Ruby の
バイト処理が支配的コストになり、割り当てが GC 停止として音に出る」ことが
実測された。

## 1. 何をしたいか

アプリや gem の中の**依存が少ないほぼ純 Ruby な部分だけ**を Spinel で
AOT コンパイルし、mruby VM から呼べる形で firmware にリンクする。
アプリ全体・システム全体の Spinel 化 (現在 SystemDesktop で試行中、
FFI と依存の保守負荷から常用は無効) とは別の道である。

### なぜ「選択的」だと成立しやすいか

Spinel 化の作業量は FFI 面積で決まり、その面積は**そのコードが呼ぶ外界の
総和**である。desktop は GFX・HID・ファイル・メッセージのほぼ全部を呼ぶので
面積が最大になる。一方「純 Ruby な計算モジュール」は定義からして外界を
呼ばず、境界は**自分の公開 API だけ**になる。面積が「依存先の数」から
「自分の API の数」に変わる — この逆転が本構想の核である。

### 期待できる効果 (実測に基づく)

- **速度**: mruby の解釈コストが消える。MIDI P6 の実例では、22 KB の SMF を
  舐める `channel_usage` が実機 7.9 秒 (割り当て除去後の見積り 450 ms)。
  ネイティブ化でさらに桁が落ちる見込み。
- **GC 分離 (こちらが本質)**: オフロードされた側は Spinel インスタンスの
  プールで生き、**mruby の GC を原理的に起こせない**。P6 で手作業でやった
  「割り当てゼロ化」が、仕組みとして保証される。リアルタイム性が要る
  モジュール (MIDI、将来の音声処理など) に効く。

## 2. 既にある足場 (2026-08-04 に確認済み)

構想の大半は新規開発ではない。kernel/desktop の Spinel 化 (phase1-6) が
必要な機構をほぼ作り終えている。

| 機構 | 場所 | 状態 |
|---|---|---|
| ライブラリモード | `spinel --no-main --entry <name>_entry` | **ある**。main を持たず `int <name>_entry(void)` を公開する C を吐く (main/prebuild_scripts/compile_ruby_to_spinel.cmake) |
| ビルド統合 | `rake spinel:gen` + `prepare_ruby_spinel_source` | **ある**。ホストで .c を生成し、docker ビルドは生成物を拾うだけ |
| 多重インスタンス | `fmrb_spinel_instance_begin/end` (components/fmrb_spinel_rt) | **ある**。SP_MULTI_CTX。タスクごとに current が切り替わり、プール (estalloc) を指定して作る。`ps` の est 統計にも既に載る |
| mruby と同居 | kernel=Spinel + アプリ=mruby の混在ビルド | **実証済み** (S3 実機で kernel-only Spinel が安定動作) |
| FFI (Spinel→C) | `ffi_func` 宣言 (main/prebuild_scripts/spinel/fmrb_ffi.rb) | **ある**。:int / :str(+len) / :binstr (バイナリ安全 String) / :ptr |
| バイト処理の語彙 | String#getbyte / setbyte / bytesize | **Spinel が対応済み** (コンパイラの型推論に組み込み) |

つまり「Spinel の生成物を firmware にリンクし、C から entry を呼び、
専用プールで動かす」までは**今日でもできる**。足りないのは「mruby の
メソッド呼び出しとして見せる」部分だけである。

## 3. 提案する形 (v1: コンパイラ無改造で成立する範囲)

### 3.1 呼び出し規約

現在の entry は `void -> int` の 1 本だけなので、**引数と結果は C 側の
受け渡し領域を経由**する (コマンド方式):

```
mruby アプリ
  -> gem の C バインディング (ports/esp32)
       引数を受け渡し領域 (静的構造体) に置く
       <module>_entry() を呼ぶ            ← 同期、呼び出し元タスク上
  <- 結果を受け渡し領域から読んで mrb_value に変換

Spinel 側 (.rb):
  entry のトップで FFI (sp_offload_get_cmd / get_arg) を読み、
  case で処理に分岐、結果を FFI (put_result) で書き戻す
```

- 引数は Integer と String (ポインタ+長さ) に限る。FFI の :str / :binstr が
  そのまま使える。
- **同期呼び出し・呼び出し元タスク上**なので、スケジューラとの統合は不要。
  instance_begin がタスクに current を紐づける仕組みもそのまま使える。

### 3.2 境界の規律 (v1 で守る制約)

1. **戻り呼び出し禁止**: Spinel 側から mruby 側のコードを呼ばない。
   コールバックが要る処理は「イベントを事前確保バッファに詰めて返し、
   mruby 側が送出する」形に分割する。
2. **大きなデータは 1 回だけ渡す**: 曲データのような String は load 時に
   1 回 binstr で渡して Spinel プールに複製し、以後はハンドル (Integer) と
   スカラーだけが境界を跨ぐ。毎呼び出しのコピーをしない。
3. **モジュールは自己完結**: Spinel は全プログラム型推論なので、オフロード
   単位が 1 つの完結したプログラムになる。これは制約ではなく選別基準
   そのもの (「依存が少ない純 Ruby」の判定をコンパイラがやってくれる)。

### 3.3 メモリ

- インスタンスのプールは**アプリの mempool と同じく PSRAM** でよい。
  数十 KB 級で足りるはず (プレーヤの生存データは曲 + 解析結果程度)。
- インスタンスの持ち方は 2 案: (a) 使うアプリごとに 1 個 (プール消費 x N、
  排他不要)、(b) system 側に 1 個 (mutex で直列化)。v1 は (a) が素直。

## 4. 第一被験者: SmfPlayer (MIDI P6 の成果物)

条件を全部満たしている:

- String と Integer しか触らない (バイト走査 + 算術)
- 演奏経路は割り当てゼロ (P6)、escaping proc なし
- **ビフォーの実測がある**: `channel_usage` 実機 7.9 秒。効果測定が
  「同じ曲・同じアプリ・同じログ行」でできる
- `tool/midi/test/` に 122 項目のホストテストがあり、**同じ Ruby ソースを
  mruby と Spinel の両方で走らせて出力を突き合わせる**検証にそのまま使える

分割は「復号 = Spinel、送出 = mruby」:

- `load` / `channel_usage` / 「期限が来たイベントをバッファに詰める」を
  Spinel 側へ
- `device.send_note_on` などの送出 (外界との接点) は mruby 側に残す

## 5. 課題と未知 (正直な一覧)

| 課題 | 見立て |
|---|---|
| 呼び出し 1 回のオーバーヘッド | 未実測。entry 呼び出し + FFI 読み書きだけなので小さいはずだが、PoC で最初に測る。1 tick に 1 回の呼び出し粒度なら問題にならない見込み |
| flash 消費 | spinel_rt + 生成 C の分だけ増える。kernel-only 構成の実測はあるが「追加モジュール 1 個あたり」の増分は未計測。S3 は flash 残 23% なので要監視 |
| picoruby 方言との差 | SmfPlayer は picoruby の語彙 (getbyte 等) で書かれており主要部は Spinel も対応済みだが、**全メソッドの突き合わせは未実施**。コンパイラが落とすので、未対応は隠れずに現れる (fail-loud) |
| 意味論の差 | 同じソースが 2 つのランタイムで走る。差はコンパイル時エラーか両ランタイム突き合わせテストで検出する方針。既知の制約は escaping proc のローカル捕捉不可など (doc/spinel_aot/embedded_constraints.md) |
| entry の引数サポート | v1 は受け渡し領域で回避。常用するなら、複数の公開関数と型付き引数をコンパイラが直接吐く形 (公開 API 宣言) が欲しくなる。これは Spinel 側の改修 |
| Linux sim | spinel_rt は POSIX でも動く (desktop の T5-4 が Linux で先行)。sim でも同じ経路が通る見込みだが確認は要る |

## 6. 段取り案

1. **PoC**: `channel_usage` 相当だけを持つ最小モジュールを 1 個作り、
   MIDI APU デモから呼ぶ。測るのは (a) 走査時間、(b) 呼び出し 1 回の
   オーバーヘッド、(c) flash 増分。実機 1 回で全部採れる
   (P6 の計装がそのまま使える)。
2. 効果が数字で立ったら、SmfPlayer の復号系を全部移して「復号 = Spinel /
   送出 = mruby」の分割を完成させる。
3. 一般化 (gem の作法として整備、公開 API 宣言のコンパイラ対応) は
   2 の結果を見てから。

## 7. 文脈

- アプリ層全体の Spinel 化 (T5-4 desktop) とは補完関係。desktop が FFI
  面積の上限を探る話だとすれば、こちらは面積最小の点から始める話。
- mruby 本体への貢献 (vm.c の tick 修正が upstream 済み) からの連続で、
  「マイコンの Ruby で、C に降ろす代わりに Ruby のまま速くする」という
  発表の題材になり得る (RubyKaigi 候補)。
