# mruby-checkpoint の持ち込み (動いているアプリの状態を運ぶ)

> 状態: 進行中 | 更新: 2026-11-06 | F0・F1 検収済、Ff (基準の表をビルドで作ってリンクし、VM ごとには名前のアドレスだけ) 実装済・検収待ち、F6 (P4 の実機に gem を入れる) P4 実装済・実機の場面も通った・検収待ち、F1b 実装済・検収待ち、F5 (Asterism で移す。受け口は転送のアプリ、段取りはカーネル、止める前に相手に聞く) 実装済・検収待ち、F2 (同じビルドのネイティブ版 2 つの間で、書いて止めたままにし、イメージを分けて運び、着いた側が起動できたと返してから閉じる。断られた・返事が無いときは続ける) 検収済。F3 (窓のあるアプリ。窓は基底クラスが作り直し、描いた絵はアプリが `on_restore` で描き直す。移すために止めている間は窓に「移動中」の印) 検収済。F4 (sim ⇔ ネイティブ版、ブラウザ版 ⇔ ネイティブ版。画面の大きさが違えば `on_resize` の後に `on_restore`。呼ぶメソッドの名前をイメージに入れ、着いた側は足りない名前を教えるだけで、`.app.toml` の `requires` に書いた名前だけで断る) 検収済。mruby の VM で動くアプリを止めて状態をイメージに書き、別の VM・別のプロセス・別の端末で続ける。原理検証は別リポジトリ mruby-checkpoint で済み、ここはそれを Family mruby に入れる段。アプリを書く側の決まりは `movable_apps.md`

## 目的

mruby の VM で動いているアプリを、アプリの協力なしに途中で止め、状態 (ヒープ・
フレーム・Task) をイメージに書き出し、別の VM で続きから動かす。最初のデモは、
1 本の mruby アプリを 2 つの Family mruby (同じビルドのネイティブ版) の間で運ぶこと。

原理・形式・試験は別リポジトリ **mruby-checkpoint** (family-mruby/mruby-checkpoint)
にある。この文書には Family mruby の側で決まったことだけを書き、向こうの内容は写さない。

- 原理検証の計画と、Family mruby に持ち込む段 F0-F6・ユーザの決定 D1-D16:
  mruby-checkpoint の `doc/plan.md` (「Family mruby に持ち込む段」と
  「決まったこと (2026-11-03、Family mruby への持ち込み)」)
- 隔たりの調査 (VM・ビルド・アプリの作り・gem・時分割・転送): 同 `doc/survey_family_mruby.md`
- イメージの形式: 同 `doc/format_v1.md`
- 運べるアプリを書く人のための前提・制約・作法: この目録の `movable_apps.md`

## 方針

- gem は mruby-checkpoint を固定点で取り込む (D2)。`lib/add/MRUBY_CHECKPOINT_PIN` の
  commit を `rake checkpoint:setup` が `vendor/mruby-checkpoint` に取り、`rake setup` が
  gem (`mrbgem.rake`・`include/`・`src/`) を picoruby の木へ写す。作業中のチェックアウトは
  `MRUBY_CHECKPOINT_DIR=<dir>` で使える (取りに行かず、pin と違えば rake がそう言う)。
- mruby への patch は `lib/patch/picoruby-mruby/lib/mruby/` の丸ごとの写しに混ぜる (D3)。
  印は `fmrb: checkpoint`。中身は mruby-checkpoint の `patches/0001` (セーフポイントの
  hook、判定の遅い側を関数に、hook の後のフレームの取り直し) と `0002` (切り替えの要求を
  数える)。`0003` は Family mruby に同じ直しが既にある。
- patch は全ビルドに入る (普段の速さが変わらないことを命令数で確かめる)。hook の関数
  ポインタだけは `MRB_VM_SAFEPOINT_HOOK` のときに置く。この定義は gem を入れるビルドだけが
  持つ。F6 からポインタは `mrb_state` のフィールド (VM ごと、実機では PSRAM のプール) で、
  定義は ABI の定義 (report/f6.md の 3)。ESP32 の静的な領域 (内蔵 RAM) は増えない。
- gem を入れるのは、まずネイティブ版と sim だけ (D4)。端末は F6 で内蔵 RAM と flash を
  測ってから。ブラウザ版の公開のビルドには F5 (Asterism の受け口) ができてから入れる (2026-11-04 決定)。
  それまでは F4 で作った測るための別のビルド `FMRB_WASM_CHECKPOINT=1` がある。

## スコープ (段)

段の中身は mruby-checkpoint の `doc/plan.md` の表が正。ここでは Family mruby の側の状態だけを持つ。

| 段 | 内容 | 状態 |
|---|---|---|
| F0 | patch と gem を Family mruby に入れる (ネイティブ版と sim) | 検収済 (2026-11-03、report/f0.md) |
| F1 | ネイティブ版の 1 プロセスの中で、窓のないアプリを止めて同じ所で続ける | 検収済 (2026-11-03、report/f1.md) |
| F1b | F1 の残り: 基準の表を小さくする、`regress:checkpoint` を `regress:full` に、止める入口を増やす (開発の道具・Shell・アプリ自身の API) | 実装済・検収待ち (report/f1b.md) |
| F2 | ネイティブ版の 2 プロセスの間で受け渡す | 検収済 (2026-11-04、report/f2.md) |
| F3 | 窓のあるアプリ | 検収済 (2026-11-04、report/f3.md)。2026-11-06 直し: テーマの違う端末へ移すと枠だけ書いた側のテーマのままだったのを、枠の `GfxBlock` を着いた側で作り直すようにした (実装済・検収待ち、report/f3_frame_theme.md) |
| F4 | ビルドの違う端末の間 (F4a: sim ⇔ ネイティブ版、F4b: ブラウザ版 ⇔ ネイティブ版)、呼ぶメソッドの名前 (`SND0`) と `requires` | 検収済 (2026-11-04、report/f4.md) |
| F5 | Asterism で送り、受け取った側で起動する。止める前に、呼ぶメソッドの名前の一覧を相手に送って確認する (下の F4 の決まったこと)。ブラウザ版の公開のビルドに gem を入れる | 検収済 (2026-11-04、report/f5.md。ブラウザ版は持ち越し → 2026-11-05 時計を直した後に両向きとも移せた、`regress:checkpoint WEB=1`。report/f5.md 12 章。公開のビルドに gem を入れるかは未決) |
| Ff | 基準の表を flash に置く (ファームを作るときに作り、VM ごとには名前のアドレスと基準のオブジェクトの集合だけ。案 A: ホストで同じ gem の組み合わせから作る) | 検収済 (2026-11-05、report/ff.md。1 VM の基準の常駐の費用は 32 bit で 322,248 → 90,176 B、64 bit で 356,264 → 110,768 B) |
| F6 | 実機 (まず P4-Nano (NARYAv4)、その後 S3。2026-11-05 ユーザ: Tab5 の代わりに P4-Nano) | P4 実装済・検収待ち (report/f6.md。P4 の構成に gem・表 (flash 264 KB)・`mrb_state` の hook。内蔵 RAM −256 B、命令数は F0 と 1% 以内。実機の場面は 2026-11-06 に P4-Nano で全部通った (`regress:checkpoint BOARD=naryav4`)。外から起こされた転送のアプリをカーネルが引き取る直しを足した) |

## 受け入れ条件 (F0)

- sim (`rake build:linux`) とネイティブ版 (`rake native:core`) が gem 入りで作れ、
  `rake test` (ホスト) が通る。
- ESP32 の設定は gem を持たず、内蔵 RAM の増分 0 (同じコミットの前後で `idf.py size-components`)。
- 普段の速さ: patch の前後で、mruby のベンチマークの命令数 (valgrind) の差が 1% 以内。
  Family mruby の定義 (`MRB_USE_DEBUG_HOOK`、自前の時分割あり / なし) で測る。
- gem は 1 プロセスに VM がいくつあっても動く (静的な表を持たない)。mruby-checkpoint の
  試験 (`rake test`・`test:asan`・`test:stress`) が通る。
- `rake regress` と sim の画面 (`regress:screens`)・VM プール (`regress:run`) が変わらない
  (本体 checkout に取り込んでから親が流す)。


## 決まったこと (2026-11-05、Ff。ユーザが選択肢から選んだ)

- 1 VM の基準の常駐の費用 (32 bit で 90 KB) は、今はこのまま受け入れる。C の関数の番地を表に入れてさらに減らす形 (mruby のソースに手が入る) は、S3 に入れるときに改めて考える。
- 標準のアプリ (候補: kamon・blockgame・othello・piano) に `movable = true` を付けるかは、F6 で実機の移動を確かめた後に決める。
- `rake regress:size` の TAB5 の構成が IDF の構成の段で止まる件 (Ff とは無関係の見立て) は、F6 の最初に追う。
- F6 の最初の端末は Tab5 でなく P4-Nano (NARYAv4)。

## 決まったこと (2026-11-04、F5 の検収)

- ESP32 の flash の増え (カーネルの移す段取りの分、+27〜30 KB、内蔵 RAM +0) は、gem の無いビルドでもこのまま入れる。
- 移し先を受け入れる設定 (`/home/asterism/receive.toml` の `from = [...]`) が無い端末は、何も受け取らない (この既定のまま)。
  Console の許可の表と結ぶのは、Console の GUI のときに考える。
- ブラウザ版の公開のビルドに gem を入れる前に、ヘッドレスのブラウザで待ちが 17〜34 倍に遅くなる原因を先に調べる
  (F5 の変更の無いビルドでも同じ。F4 でも 20 倍の遅れを見ている)。
  → 調べた (2026-11-04): ヘッドレスのせいでなく、ブラウザ版の port の時計が Worker ごとにずれていた (Linux の Chromium、WSL2)。直した。doc/wasm/report/worker_clock.md
- (F5 とは別) ESP32 のビルドが mDNS の部品 1.15.0 で落ちるので、`~1.8.0` に留めた。新しい版への追従は別の作業。

## 未確定事項

- (なし。下の決まったことを参照)

## 決まったこと (2026-11-03、F1 の検収。ユーザが選択肢から選んだ)

- D10 の入口は F1 の形 (HAL の `mrb_hal_task_slice_get/set` と gem の埋め込む側のセクション `EMB0`)、
  D11 は gem の `mrb_checkpoint_set_allocator` (作業の領域だけ) で確定。
- 基準の常駐の費用 (F1 の計測で 64 bit の 1 VM あたり約 1 MB) は**両方やる**: まず F1b で gem の表を
  小さくし (内訳を測ってから)、実機 (F6) の前に名前の表をファームごとに flash に置く形 (D12 の (c)) も作る。
- `rake regress:checkpoint` を `rake regress:full` に入れる。
- 止める入口を増やす: 開発の道具 (`tools/fmrb_native.rb` と MCP の `native_app`)、Shell のコマンド、
  アプリが自分で頼む API (`on_event` の外で)。
- hook の関数ポインタと切り替えの要求の数は、実機では `mrb_state` の中に置く (mruby-checkpoint の plan、
  2026-11-03)。F6 で入れる。

## 決まったこと (2026-11-04、F2 の検収)

- 移した後に止めた側 (A) のアプリを閉じるとき、`on_destroy` は呼ばない (今のまま。アプリは壊れたのでなく移った)。
- 着いた側 (B) が移す頼み (デバッグサーバの `restore`、つまり移す・復元の頼み) のイメージを断ったとき、
  B のデスクトップに「起動できない」の窓を出さない。理由は頼んだ側に返り、A は続ける。B のログには残る。
  頼んだ側のいない失敗 (普通の起動、F1 の同じプロセスでの起動し直し `restart: true` / カーネルの
  `restart_from_checkpoint`) は今までどおり窓を出す (理由を受け取る相手がいないので)。
  窓を出さないのは C の側で決める: `fmrb_app_checkpoint_resume` が、断りを `restore` の答えの表に
  書けた (頼んだ者がいた) かを返し、そのときは `app_error` をカーネルへ送らない。
- 止めておく既定の `hold` (10 秒) と運ぶ速さは、F5 (Asterism) まで今のまま。

## 決まったこと (2026-11-04、F3 の検収)

- 描いた中身はイメージに入れず、アプリが `on_restore` で描き直す (report/f3.md §3 の (b))。基底クラスは空の窓 (枠つき) を用意し、`on_restore` の後に `present` する。
- 移すために止めている間 (F2 の形、`hold`)、移動元の窓のタイトルバーに「移動中」の印 (テーマの色の小さな札 `moving...`) を出す。アプリは何もしない (C の側が窓の上の小さな画面に描く)。移った (閉じる) ときは窓ごと消え、続けるときは印を消して窓は止める前のまま。同じプロセスで起動し直す形 (F1、`restart`) は止めておく時間が無いので出さない。
- 標準のアプリに `movable = true` を付けるのは、基準の表が flash に移ってから (Ff)。それまでは試験のアプリだけ。pub/sub の見本は `movable` を付けず、`on_restore` の描き直しだけを持つ。

## 決まったこと (2026-11-04、F4 の検収)

- ブラウザ版の公開のビルドに gem を入れるのは F5 (Asterism の受け口) の後。測るための別のビルド
  `FMRB_WASM_CHECKPOINT=1` はそのまま残す。
- 呼ぶメソッドの差 (report/f4.md §1) は**両方やる**:
  - (a) ブラウザ版の gem の並びを、ソケット・TLS・OS に依らないものだけネイティブ版に揃える。
    `mruby-pack` (`Array#pack`・`String#unpack`/`unpack1`) と `picoruby-base64` を足した
    (wasm +18,754 B、+0.3%)。`URI` は net-http の、`EOFError` はソケットの gem のものなので入れない。
  - (b) gem がイメージに「アプリのコードが呼ぶメソッドの名前」を入れる (形式のセクション `SND0`)。
- 足りない名前の扱い (ユーザ): 運べるアプリは、すべての端末に共通でないメソッドは無いかもしれない
  と思って書く (`respond_to?`・`rescue NoMethodError`、端末の違いはアプリの窓口のオブジェクトに集める)。
  だから着いた側は**名前が足りないだけでは断らない**。復元して続け、足りない名前を頼んだ側に知らせる
  (移す頼みの答えの `missing`、着いた側のログ)。断るのは、アプリが `.app.toml` の
  `requires = ["名前", ...]` に書いた名前が無いときだけ。申告が無ければ名前で断らない。
  移った後の `NoMethodError` は、移ったアプリの普通の実行時の誤り。
- 名前の確認の限り (名前だけで受け手のクラスを見ない、eval・名前を計算する send・method_missing・
  define_method・C からの呼び出しは見えない、`respond_to?` で備えた名前は足りないと出ても問題ない) と、
  アプリの書き方の決まりは `movable_apps.md` にまとめる。
- **F5 では止める前に確認する** (ユーザ): アプリを止める前に、移動元は運ぶコードが呼ぶメソッドの名前の
  一覧 (`SND0` と同じもの。gem の `mrb_checkpoint_sent_names` がイメージを書かずに出す) を相手に送り、
  相手は自分の表 (Ff の後は flash・wasm の基準の表。メソッドの一覧も持つ、`flash_table.md`) で引いて
  「全部ある」か「足りない: ...」を返す。`requires` の名前が足りなければ止める前に断る。各ビルドは
  自分の表だけを持ち、ほかの端末の表は持たない。着いた側の確認 (`SND0` と `requires`) は土台・最後の
  守りとして残す。
- `rake regress:checkpoint` の既定はネイティブ版の場面だけ (作り終えていれば 3 分ほど)。`SIM=1` で sim の
  場面も足し、`rake regress:full` は `SIM=1` で流す。

## ほかの計画とのつながり

- F5 の Asterism の受け口 (`begin` / `chunk` / `commit`、D13) は、wasm のアプリのインストールと共用にする
  (`doc/wasm_apps/plan.md` の 4・W5)。先に必要になった方で作る。
