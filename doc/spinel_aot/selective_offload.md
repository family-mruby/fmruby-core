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

## 4. 開発体験の設計 (本構想の要)

3 章の呼び出し規約が「動くか」の話だとすれば、この章は「使われるか」の
話である。**gem の作者が CMake にも C にも触れずに済む**ことを設計の
中心に置く。ここが崩れると、結局 C 拡張を書くのと手間が変わらず、
構想の意味がなくなる。

### 4.1 置き場所の規約: gem の `spinel/` ディレクトリ

```
lib/add/picoruby-fmrb-midi/
  mrblib/fmrb-smf.rb        # 通常の mruby Ruby (いままで通り)
  spinel/smf_core.rb        # Spinel でネイティブ化するモジュール
  mrbgem.rake               # 変更不要
```

- Rakefile が `lib/add/*/spinel/*.rb` を**規約で発見**し、`rake spinel:gen`
  が全部まとめてコンパイルする。生成 C は 1 つの gen ディレクトリに落ち、
  CMake は**そこを 1 回だけ glob する汎用規則**で拾う。
  **モジュールを足すたびの CMake 編集は無い**。
- コンパイラの調達は既に自己完結している (`rake spinel:setup` が pin された
  fork を vendor/ に取得・ビルドする。SPINEL_PIN + IMPORT_INFO の整合検査
  つき)。gem 作者は Spinel の存在を意識しなくてよい。

### 4.2 公開 API の宣言: `sp_export` (ffi_func の対称形)

Spinel には既に `ffi_func` (Spinel から C を呼ぶ側の宣言 DSL) がある。
その対称として、**C/mruby 側へ公開する関数の宣言**をソース内に書く:

```ruby
# spinel/smf_core.rb
sp_export :scan_channels, [:binstr], :binstr   # 曲データ -> 使用ch表
sp_export :load_song,     [:binstr], :int      # 曲データ -> ハンドル
sp_export :next_events,   [:int, :int], :binstr # ハンドル, 今時刻 -> イベント列

module SmfCore
  def self.scan_channels(data) ... end
  ...
end
```

この宣言 1 か所から、生成器が**両側のつなぎを自動生成**する:

- Spinel 側: entry のコマンド分配 (v1) または公開関数そのもの (v2)
- mruby 側: 引数の詰め替えと entry 呼び出しをやる C バインディング
  (ports/esp32 相当のファイルを機械生成)

gem 作者が書くのは **Ruby モジュールと sp_export 行だけ**。手書きの C も
受け渡し領域の知識も要らない。この生成器が、本構想で**新規に作る唯一の
道具**である (小さな DSL を読んで定型 C を吐くだけなので、規模は
既存の prebuild スクリプト程度)。

### 4.3 単一ソース二重バックエンド (開発体験の核心)

`spinel/*.rb` は **picoruby としてもそのまま有効な Ruby** で書く
(バイト処理系のモジュールは実質的に両者の共通部分言語に収まる。
SmfPlayer の P6 後のコードがその実例)。ビルドは同じソースを 2 通りに使う:

| 構成 | 使われる実装 |
|---|---|
| Spinel 有効 (S3/P4 実機) | ネイティブ (生成 C) |
| Spinel 無効・Linux sim・他ターゲット | 同じ .rb を mruby bytecode として (いままで通り) |

これで得られるもの:

- **sim と開発ループが Spinel 無しで回る**。オフロードは「速くなる」だけの
  差分で、機能はどの構成でも同じ。1 つの構成しか通らない経路を作らない。
- **意味論の差が構造的にテストできる**。同じソース・同じテストを両
  バックエンドで走らせて出力を突き合わせる (MIDI の 122 項目が前例)。
- 呼び出し側のコードは**分岐なし**。`SmfCore.scan_channels(data)` と書くだけで、
  どちらの実装かはリンクが決める。

### 4.4 バックエンドの切り替え (ビルド時、1 スイッチ、排他)

切り替えは**ビルド時**に、既存の `FMRB_KERNEL_ENGINE=spinel` と同じ流儀の
環境変数 1 個 (仮称 `FMRB_SPINEL_OFFLOAD`) で行う。分岐点は **Rakefile の
gem コピー段階**:

| | ON | OFF |
|---|---|---|
| spinel/*.rb | spinel:gen がネイティブ C + バインディング C を生成 | mrblib のソースとして一緒にコピー (bytecode) |
| モジュール定義 | 生成 C バインディングの 1 つだけ | bytecode 版の 1 つだけ |

**どちらのビルドでも定義は常に 1 個** (排他)。呼び出し側は同名モジュール・
同名メソッドなので、アプリも gem の mrblib も一切変わらない。

**ランタイム切り替えは採らない**。理由: (1) 両方積むと flash の二重払い
(S3 は残量に余裕がない)、(2) picoruby に `defined?` が無く、
`const_defined?` の分岐が呼び出し側へ漏れて「分岐なし」の原則が崩れる、
(3) 「今どちらが動いているか」が実行時の状態になり、ビルドログ 1 行で
確定できなくなる。

既定値の案は実機ターゲット ON / Linux sim OFF (ON にできるかは PoC の
flash 実測待ち)。ホストの突き合わせテストは Spinel をホストバイナリに
コンパイルして走らせるので、sim OFF でも成立する。

**切り替え時の clean は仕組みで吸収する**: コピー段階がスイッチ値を
記録し、前回と不一致なら該当部分を自動で作り直す。「切り替えたのに前の
ままビルドされた」は既存の同型の罠 (lib/ 編集後の clean 忘れ、
FMRB_GC_PROFILE 切替) で実際に踏んでいる穴なので、手順書での注意ではなく
機構で塞ぐ。

### 4.5 インスタンス管理は見せない

生成バインディングが初回呼び出しで `fmrb_spinel_instance_begin` を遅延実行
(小さな PSRAM プールを確保) し、アプリのコンテキストに登録する。アプリ終了時は
既存の後始末 (cleanup_terminated_app) が解放する。gem 作者にもアプリ作者にも
インスタンスの存在は見えない。

### 4.6 開発ループ

```
spinel/smf_core.rb を編集
  -> rake build:linux / build:esp32   (依存関係で spinel:gen が自動再実行)
     - サブセット逸脱は Spinel のコンパイルエラーとしてここで出る
       (「純 Ruby に収まっているか」の判定器を兼ねる)
  -> 通常どおり実行
```

CMake を触る瞬間が存在しないこと、エラーが rake の段階で
人間に読める形で出ることの 2 点が受け入れ条件である。

## 5. 第一被験者: SmfPlayer (MIDI P6 の成果物)

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

## 6. 課題と未知 (正直な一覧)

| 課題 | 見立て |
|---|---|
| 呼び出し 1 回のオーバーヘッド | 未実測。entry 呼び出し + FFI 読み書きだけなので小さいはずだが、PoC で最初に測る。1 tick に 1 回の呼び出し粒度なら問題にならない見込み |
| flash 消費 | spinel_rt + 生成 C の分だけ増える。kernel-only 構成の実測はあるが「追加モジュール 1 個あたり」の増分は未計測。S3 は flash 残 23% なので要監視 |
| picoruby 方言との差 | SmfPlayer は picoruby の語彙 (getbyte 等) で書かれており主要部は Spinel も対応済みだが、**全メソッドの突き合わせは未実施**。コンパイラが落とすので、未対応は隠れずに現れる (fail-loud) |
| 意味論の差 | 同じソースが 2 つのランタイムで走る。差はコンパイル時エラーか両ランタイム突き合わせテストで検出する方針。既知の制約は escaping proc のローカル捕捉不可など (doc/spinel_aot/embedded_constraints.md) |
| sp_export 生成器 | **本構想で新規に作る唯一の道具** (4.2)。宣言を読んで両側の定型 C を吐く。v1 は entry+受け渡し領域方式の裏方に徹し、コンパイラ改修 (型付き公開関数の直接生成) は v2 に送る |
| 共通部分言語の境界 | 単一ソース二重バックエンド (4.3) は picoruby と Spinel の両方で通る書き方を要求する。バイト処理には十分だが、境界の明文化 (使ってよい語彙の一覧) がいずれ要る |
| Linux sim | spinel_rt は POSIX でも動く (desktop の T5-4 が Linux で先行)。sim でも同じ経路が通る見込みだが確認は要る |

## 7. 段取り案

1. **PoC (配線は手書きでよい)**: `channel_usage` 相当だけを持つ最小
   モジュールを 1 個作り、MIDI APU デモから呼ぶ。この段階では sp_export
   生成器を作らず、バインディングを手書きして**数字だけ先に取る**。
   測るのは (a) 走査時間、(b) 呼び出し 1 回のオーバーヘッド、(c) flash 増分。
   実機 1 回で全部採れる (P6 の計装がそのまま使える)。
2. 効果が数字で立ったら **sp_export 生成器と規約 (4 章) を作る**。
   手書きした PoC のバインディングが、生成器が吐くべき出力の見本になる。
3. SmfPlayer の復号系を全部移して「復号 = Spinel / 送出 = mruby」の分割を
   完成させ、単一ソース二重バックエンドの突き合わせテストを CI に載せる。

## 8. 文脈

- アプリ層全体の Spinel 化 (T5-4 desktop) とは補完関係。desktop が FFI
  面積の上限を探る話だとすれば、こちらは面積最小の点から始める話。
- mruby 本体への貢献 (vm.c の tick 修正が upstream 済み) からの連続で、
  「マイコンの Ruby で、C に降ろす代わりに Ruby のまま速くする」という
  発表の題材になり得る (RubyKaigi 候補)。
