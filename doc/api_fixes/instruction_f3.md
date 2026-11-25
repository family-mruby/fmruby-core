# F3 指示書: Kernel の定数・MIDI の予約・rescue の型・定数 2 つを sig に

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (2 章の「B の決定 2 回目」の 5〜8)。進め方と報告の形は
> F1・F2 と同じ (再現 → 直す → 試験で固定 → sig を合わせる)。型エンジンの直しは doc/archive/sig_coverage の S8
> (instruction_s8.md・report/s8.md) と同じ進め方。

## 項目

### B5. `Kernel::MSG_TASK_HOST`・`MSG_TASK_SYSTEM` を本体の番号に合わせる

- 今: `lib/gems/picoruby-fmrb-kernel/src/picoruby_fmrb_kernel.c` が HOST = 0・SYSTEM = 1 と自前で定義。本体
  (`components/fmrb_common/include/fmrb_task_config.h` の `PROC_ID_*`) は KERNEL = 0・HOST = 1・SYSTEM_APP = 2。
- 直した後: 定数の値を本体の定義から取る (自前の数字をやめる)。`Kernel::MSG_TASK_KERNEL` を足す。
  `Kernel.send_message` ほかが受け付ける番号の上限 (今 0〜15。`ports/esp32/kernel.c` の `KERNEL_MSG_TASK_ID_LIMIT`) を
  本体の上限 (`PROC_ID_MAX`) に合わせ、例外の文と sig の説明を直す。
- sig (`sig/kernel.rbs`) に 3 つの定数を要約つきで書く (今ある説明の「(0)」「(1)」も直す)。sim の irb で値を確かめる。
- 同梱のアプリ・カーネル・デスクトップがこの定数を使っていないことを grep で確かめる (使っていたら止まる)。

### B6. `SmfPlayer#drop_queued` が共有の待ち行列を空にする件

- 疑い (読み): プレーヤーを 2 つ同時に鳴らすと、片方の `drop_queued` (stop・seek・デバイスの切り替えで呼ばれる) が
  もう片方の予約も消す。`MmlPlayer` も同じ待ち行列を使うかを確かめる。
- まずホストの試験で再現する (F1 で足した MIDI の試験の形: `test/api_fixes/run.rb`)。**再現しなければ直さず、
  事実を報告する**。
- 再現したら「自分が予約した分だけ消す」形に直す。予約に持ち主の印を足すなら、予約 1 件あたりの増分 (バイト) と、
  待ち行列の上限のときの合計を報告に書く。**VM プールを KB 単位で増やす形になるなら止まって案を書く**
  (持ち主の印を足さずに済む形: 持ち主ごとの時刻の範囲・チャンネルで選ぶ、など)。
- プレーヤーが 1 つのときの動き (止めたら予約が全部消える) は変えない。sig の説明を合わせる。

### B7. `rescue Foo => e` の `e` に型を付ける (型エンジン)

- 今: エディタで `rescue ArgumentError => e` の後の `e.` に補完が出ず、`e.message(1)` が指摘されない。型エンジン
  (`vendor/picoruby-ti`、fork のブランチ `fmrb-dev`、今 `4536029`) が rescue の節を扱っていない (report/s8.md)。
- 直した後: `rescue Foo => e` は Foo (入れ子の `A::B` も)、`rescue A, B => e` は A か B (エンジンの union の作りに
  合わせる。union が持てないなら共通の親か最初の 1 つ。どうしたかを報告に書く)、`rescue => e` は StandardError。
  データベースに無いクラス (ユーザのクラス) は今までどおり型なし。`begin/rescue`・メソッドの本体の `rescue`・
  `ensure` つき・`rescue` を複数並べた形・1 行の `x rescue y` (変数なし) で落ちないこと。
- 内蔵 RAM・ヒープを増やさない。host の試験に場面を足し、`rake ti:probe` を前後で測る。
- fork にコミット (**push しない**)、`lib/upstream/PICORUBY_TI_PIN` を更新。
- sim のエディタで: `begin` / `x = 1` / `rescue ArgumentError => e` / `e.message(1)` / `end` が Ctrl+E で指摘される、
  `e.` で Tab の補完に `message` が出る、`rescue => e` でも同じ、入れ子 (`rescue JSON::ParserError => e`) でも同じ。
  今ある指摘 (`"a".byteslice(1, 2, 3)`) が今までどおり。

### B8. 定数 2 つを sig に載せる

- `FmrbApp::TRANSPARENT_COLOR` (`lib/gems/picoruby-fmrb-app/mrblib/fmrb-app.rb:41`) と `FmrbGfx::FONT_METRICS` を、
  実装を読んで sig に書く (要約 `日本語 <<en>> English`、型。`FONT_METRICS` は中身の形と、どのフォントの名前で
  引くかを長文か要約で)。見本 (`flash/app/demo/avatar.app.rb`) での使い方に合わせる。
- `CLOSE_BTN_CX_OFFSET`・`CLOSE_BTN_CY`・`CLOSE_BTN_HIT_R` は `tools/sig_coverage/ignore.yml` に理由つきで入れる
  (ユーザ決定: 枠の描き方の内側の数字。見本が読んでいる点は doc/reference/TODO.md に 1 行で課題として書く)。
- `rake sig:coverage` で、アプリが使うのに sig に無い定数が 0 になることを確かめ、基準を更新する。

## 確かめる

- 項目ごとに sim かホストで。最後に `rake regress`・`rake sig:coverage` (「3. doc gaps」が 0 のまま)・`rake ti:test`、
  `rake clean` → `rake build:linux` → sim でシェルの irb、エディタを起動して 1 打鍵 (B7 の確認を兼ねる)。
- ESP32 のビルドはしない (親が続けて P4-Nano 用に作って焼く)。ブラウザ版も作らない。

## 報告 (doc/api_fixes/report/f3.md)

F1・F2 と同じ章立て。B7 は S8 の報告と同じ項目 (直した所・足した試験・計測・sim の結果と画像のパス・解けない形)。

## 範囲と止まる条件

- 触ってよい: 4 項目の実装と試験、`vendor/picoruby-ti/` (fork)、`lib/upstream/PICORUBY_TI_PIN`、`tools/ti/test_sig/`、
  `sig/` の該当、`tools/sig_coverage/ignore.yml` (B8 の 3 行) と `baseline.json`、`sdk/`、`doc/reference/TODO.md`
  (B8 の 1 行)、`doc/api_fixes/report/f3.md`、`doc/README.md`。
- 変えない: plan.md の「B の残り」の動き (GPIO を含む)、`components/` の submodule の中、`vendor/asterism`、sdkconfig、
  エディタの Ruby と C の橋、型データベースの形。
- 各項目の止まる条件に当たったら、その項目は直さず、ほかの項目を進めて報告する。
- 利用者から見た動きが、上に書いた以外に変わる直し方が要るときは止まって報告する。
- push しない・マージしない (fmruby-core はブランチ `api-fixes-f3`、fork は `fmrb-dev` にコミットまで)。
  ユーザに質問しない。終わったら sim を止める。コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
- **シリアルのポートと実機には触らない** (親が P4-Nano をつないでシリアルの記録を開いている)。
