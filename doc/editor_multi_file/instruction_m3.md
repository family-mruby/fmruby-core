# M3 指示書: 外から開く、外から読む・書く (AI 連携の I/F)

> 2026-11-17 | 親セッションからサブ (Opus) への指示。計画の正は plan.md (4.4・4.5)、M1・M2 の結果は
> report/m1.md・m2.md、知らせの形の正は doc/ai_coding/report/a0.md の 3 章・4 章 (全部先に読む)。
> ユーザは就寝中で、親が朝まで自律で進めている。

## 1. この段で作るもの

### 1.1 外から開く (plan 4.4)
- エディタが動いているときに、エディタでファイルを開く起動が来たら、カーネルが 2 つ目を起こさず、
  動いているエディタに `on_control` の知らせ `{"cmd" => "open_file", "path" => ...}` を送り、エディタを
  前に出す。パス無しの起動 (デスクトップのメニュー・アイコンからエディタ) は、前に出すだけ。
- 起動の入口を全部調べて同じ経路にする: ファイルマネージャの関連付け、デスクトップのアイコン・メニュー、
  シェルの「エディタで開く」、debug の spawn。今どこで 2 つ目が断られ、何が出るかを先に確認し、
  report に書く。
- エディタはそれを M2 の File > Open と同じ決まりで開く (追加で開く・空の無題ならそこに・同じファイルなら
  切り替え・上限や領域切れなら開かずに知らせる)。
- エディタがダイアログなどを出している最中に来た `open_file` は、**捨てずに、ダイアログが閉じた後に開く**
  (1 つだけ覚える。後から来たものが勝つ)。
- sim・ネイティブ版・ブラウザ版・ESP32 で同じ経路 (カーネルの Ruby と spawner の範囲。端末ごとの分岐を
  作らない)。エディタが Spinel でない構成 (全部 mruby) でも同じ動きにする (2 つ目を起こさず渡す)。

### 1.2 文書の版 (a0.md 3.2)
- `editor_core.c`: 全体の通し番号 `g_edit_seq` と、`ed_doc_t` の `ver`。本文を変える公開の口の成功の
  たびに `d->ver = ++g_edit_seq`。`ec_doc_version(slot)`。PSRAM に置く (内蔵 RAM を増やさない)。
- Spinel の FFI (`fmrb_editor_ffi.rb`・`fmrb_spx_editor.c`) と mruby の束ね (`editor_core_mrb.c`) に
  `EditorCore.doc_version`。

### 1.3 外から読む・書く (plan 4.5、a0.md 3.2 の表のとおり)
- `on_control` に `ai_read`・`ai_replace` を足し、`ai_doc`・`ai_done` を `send_message(re, ...)` で返す。
  項目・断る理由 (`busy`・`help`・`ver`・`io`・`nomem`) は a0.md 3.2 の表のとおり。知らせは 176 B までの
  平らな Hash。本文は `/tmp/ai/doc.rb`・`/tmp/ai/new.rb` を C の `save_file`・`load_file` で出し入れし、
  エディタの VM に本文の文字列を作らない。
- `ai_replace` は、版が今と同じときだけ今の文書を入れ替える。ファイル名はそのまま、変更の印を付け、
  診断を消し、カーソルを渡された位置 (範囲に収める) に置き、全体を描き直す。
  **入れ替えた直後の版を `ai_done` で返す**。
- 複数の文書があるとき、読む・書くの相手は「今の文書」。版は通し番号なので、読んだ後にタブを切り替え
  られていたら `ver` で断られる。
- 「行の上にコメントを差し込む」口・「コメントを数える・消す」口は**作らない** (AI 連携アプリと gem の
  側で文書全体を作って `ai_replace` する。a0.md 3.2)。

### 1.4 試験用のアプリ
- `flash/app/test/` に、読む・書くを試す小さな mruby のアプリを 1 つ置く (`launcher_visible = false`。
  今ある試験のアプリの書き方に合わせる)。エディタを `FmrbApp.ps` で探し、`ai_read` → `/tmp/ai/doc.rb` を
  読んで先頭に `# AI: test` の行を足した `/tmp/ai/new.rb` を書く → `ai_replace`、を行い、結果を
  ログに出す。古い版で `ai_replace` して断られることも試す。

## 2. 受け入れ条件

標準構成 (Spinel) と全部 mruby の構成の両方、sim の Modern で確認する (Retro は外から開くを 1 回)。

1. エディタが動いているときに、ファイルマネージャからファイルを開くと、2 つ目が起きずに、動いている
   エディタのタブに足される。同じファイルをもう一度開くと、そのタブに切り替わる。6 つ目は開かずに
   知らせる。デスクトップからのパス無しの起動は前に出るだけ。
2. エディタが動いていないときの起動は今までどおり (ファイル付き・無し)。
3. ダイアログを出している最中に来た `open_file` が、閉じた後に開かれる。
4. 試験用のアプリで: `ai_read` の答えの項目 (版・行の数・バイト・カーソル・変更の印・ファイル名) が
   正しく、`/tmp/ai/doc.rb` が今の文書と同じ中身。`ai_replace` の後、エディタの文書の先頭に `# AI: test`
   があり (保存したファイルで確認)、変更の印が付く。1 文字打った後に古い版で `ai_replace` すると
   `why: "ver"` で断られ、文書は変わらない。ダイアログ中は `busy`、ヘルプ中は `help`。
5. 文書の版が、打つ・消す・貼る・読み込むで増え、カーソルの移動・ハイライトの切り替え・タブの切り替えで
   増えない。
6. ネイティブ版とブラウザ版でも 1 と 4 が通る (ブラウザ版はファイルマネージャから開くまで。試験用の
   アプリが storage に無ければ、できる範囲で)。
7. `rake regress` が通る。段の最後に 1 回 `rake regress:screens FULL=1` が通る。
8. `git status` に取りこぼしが無い (前からある `m components/esp_littlefs`・
   `m components/picoruby-esp32/picoruby`・`?? wasm/vendor/`・`?? doc/ai_coding/` は除く。触らない)。

確認の型: 直すたびに `rake regress` だけ。`regress:size`・`regress:run`・`regress:checkpoint`・
`regress:wl`・`regress:full` は流さない。ESP32 のビルドはしない (親が後で行う)。

## 3. 範囲と止まる条件

### 触ってよい所 (fmruby-core)
- エディタ: `main/ruby/default_app/editor.app.rb`・`editor/*.rb`、`main/app/fmrb_spx_editor.c`、
  `main/ruby/spinel/fmrb_editor_ffi.rb`、`lib/gems/picoruby-fmrb-editor-core/` (lib/ を直したら `rake clean`)。
- 外から開くの経路: カーネルの Ruby (`main/ruby/kernel/`) と `main/app/fmrb_app_spawner.c`・`fmrb_app.c` の
  うち、エディタの起動を断る・渡す所だけ。**カーネルは Spinel。内蔵 RAM とスタックを増やさない。
  ほかのアプリの起動の動きを変えない。**
- `flash/app/test/` に試験用のアプリ 1 つ。エディタの案内の文書に外から開くの説明。
- `doc/editor_multi_file/report/m3.md`、`rake docs:index` の結果。
- **触らない**: 回帰の道具の本体、sdkconfig、submodule の中、graphics-audio、直下 (family-mruby) の
  リポジトリ、`wasm/vendor/`、`doc/ai_coding/`、`/home/kishima/fmrb/family-mruby/fmrb-tutor/`。
  plan.md は親が直す。

### 作業場所と git
- 本体の checkout、今いるブランチ `editor-multi-file` の上に続けてコミットする (この指示書を最初の
  コミットに入れる)。**push しない。develop へマージしない。** `git add -A` を使わない。
- 終わったら sim・ブラウザ・開発サーバ・ネイティブ版を止める。ESP32 の `build/` には触らない。
- P4-Nano (`/dev/ttyACM0`) に触らない。

### 止まる条件 (止まったら report に状況を書いて返す)
- ユーザの作業が要る。触ってよい範囲の外の変更が要る。
- カーネルの変更が、エディタの起動の所に収まらない (起動の仕組み全体・ほかのアプリの動きに及ぶ)。
- 見立てと食い違う事実 (例: Spinel のエディタで平らな Hash が読めない・`send_message` で返せない、
  知らせが 176 B に収まらない)。小さく直せるなら直して理由を書く。形を大きく変えるなら止まる。
- **利用者から見た動きを、この指示書・plan に無い形で変える必要が出たとき**。選ばずに止まる。
- 今の不具合を見つけた (この段と関係なければ直さず、再現の手順を書く)。

## 4. report に書くこと (doc/editor_multi_file/report/m3.md)

- 起動の入口の一覧と、それぞれを今どう変えたか。2 つ目が来たときの動きの前と後。
- 知らせの最終の形 (cmd・項目・断る理由)。a0.md 3.2 から変えた所と理由。知らせの大きさの実測。
- 受け入れ条件 1〜8 の結果と根拠。試験用のアプリのログ。
- カーネルの変更の大きさ (スタック・ヒープへの影響の見立て)。
- AI 連携アプリ (A2・A3) を作る人への引き継ぎ: 使い方の見本 (数行)、気をつけること。
- 残件、未確認のこと、親・ユーザに決めてほしいこと。コミットの一覧。
