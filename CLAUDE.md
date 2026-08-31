# Family murby Core firmware

## 基本的な注意事項

- コミュニケーションは日本語
  - ただし指示は英語の場合もある。私の指示が英語の場合も返答は日本語にしてください。
- 勝手にgit操作しない
- ASCII以外の絵文字等は使用しない。✓など使用しない。
- 問題を解決する際に、ソースコードをビルド対象から外すことは本質的ではないので、禁止
- ソースコード上に記載するコメントは英語で記述する
- ソースコードのファイルは勝手に削除しない

### コミット規約

- コミット・push は求められたときだけ行う。
- 件名は `<領域>: <要約>`。領域は変更した機能・サブシステム名
  (doc / desktop / editor / spinel / services / tools / wasm など。
  履歴の実例に合わせる)。文書だけの変更は `doc:`、複数領域にまたがるときは
  `services, doc:` のようにカンマ区切り。
- 本文は英文で記述する。要点が伝われば、変更の背景や理由をある程度詳しく
  書いてよい。
- Co-Authored-By トレーラは、Claude が主な作業を行ったコミットに付ける
  (人間が主体で Claude の関与が軽微な場合は付けない)。

### 開発時の注意

- **flash/ は「git 追跡分 = 配布してよいもの」を不変条件とする**。ローカル
  専用の中身 (音楽、個人のデッキ、認証情報の生成物) は flash_local/
  (gitignore) に置く — 実機の storage は flash/ + flash_local/ の合成、
  web (wasm) の bundle は追跡分 + config/system_conf_wasm.toml だけを束ねる。
  **/home は空で出荷する** (2026-08-31)。/home はユーザの物だけを置く場所で、
  更新で入れ替わってよいもの (手本・サンプル・案内) は /usr/share/ 配下
  (samples/ services/ doc/) に置く。/home 自体は起動時に file HAL が作る。
- .gitsubmoduleに含まれるディレクトリは直接編集禁止
- .gitsubmoduleに含まれるディレクトリ以下を編集する際は、lib/add lib/patch lib/replace 以下にファイルを配置して、 Rakefile で、対象のファイルやディレクトリを追加、上書き、削除する。
- sdkconfigおよびsdkconfig.defaults は編集禁止
  - sdkconfig に関する変更が必要なときは、編集せずに、提案すること
- mrbgem で ESP32やFreeRTOSのヘッダを利用するものは、`components/picoruby-esp32/CMakeLists.txt` の `set(PICORUBY_SRCS` でビルド管理する。
- main/以下のコードの関数の戻り値定義は `fmrb_err.h` を標準とする
- GPIOのPinアサインは、 `fmrb_pin_assign.h` を参照する
- esp_* を直接 include してよいのは、各 fmrb 抽象の esp32 platform 実装と
  実機専用 driver のみ。共有コード (components/ と、linux・wasm もコンパイル
  する main/ の範囲) は fmrb_* 経由 (ログは fmrb_log.h / fmrb_log_port.h、
  時刻は fmrb_hal_time.h、配置属性は fmrb_attr.h。doc/archive/idf_seam/)。
- 素のmallocは使わず、fmrb_mem.h の関数を利用する。もし少量のメモリならファイルスコープのstatic配列変数を利用することを検討する
  - mruby実行タスクでは、fmrb_mallocを利用して、その他のmain/以下のOS関連ではfmrb_sys_mallocを利用する。
- シンボリックリンクの仕様は原則禁止
- 編集をした場合、Legacyコードは残さず消す
- Doxygenコメントは、基本的に他のモジュールから参照されるヘッダのみに記載。Cソースには不要。（メンテナンスしきれないため）
- `sig/*.rbs` (エディタの型支援の元) の doc コメントは、**1 行目 (要約) が
  両言語あわせて 112 バイトまで**。超えた分はエディタが黙って切る
  (`ET_DOC_MAX`)。長い説明は 2 行目以降の長文ヘルプに書く。書き方の詳細と
  ビルド時の注意 (**sig を直したら `rake clean` してからビルド**。しないと
  古い型 db がリンクされ続ける) は `sig/README.md`。

## ビルド方法

```
rake build:linux  # Linuxターゲットビルド
rake build:esp32  # ESP32ターゲットビルド
rake -T # その他のコマンドの使い方
```

### 注意

- lib/ 以下のファイルを編集した場合は、ビルド前に `rake clean` を実行すること
- ターゲットをlinux - ESP32で切り替えてビルドするときは、ビルド前に `rake clean_all` を実行すること
- プログラムの実行確認は、リポジトリルート (family-mruby) の自律検証ツールで headless に行える
  (起動+画面キャプチャ+入力注入。使い方はルートの CLAUDE.md 参照)。音声・実機・操作感の最終確認はユーザが行う。

## テスト

テストは 2 層に分かれている。

- **ネイティブ host テスト**: `rake test` で picoruby-ti / BASIC ゴールデン /
  MicroPython smoke をまとめて実行する。docker も実機も不要で、ランナー
  (ホスト) の gcc/g++/ruby/python で走る。**コード変更後はまずこれ**。
  個別には `rake ti:test` / `rake basic:test` / `rake micropython:smoke`。
  CI (.github/workflows/build.yml の test-host ジョブ) はこれを回す。
  型データベース生成に rbs gem が要る (`gem install rbs`)。
- **sim / 結合テスト**: docker の Linux ビルドを headless で駆動する
  (`tools/dev_run_check.sh` + `fmrb_screenshot` / `fmrb_input`)。画面挙動に
  関わる変更のときに回す。重いので CI では回していない。
  **標準構成 (Spinel カーネル + Spinel エディタ) の sim 検証には、エディタを
  1 回起動して 1 打鍵するところまでを必ず含める**。デスクトップが上がっても
  エディタだけが死ぬ壊れ方が実在する (基底クラスに ivar を 1 個足しただけで
  Spinel の ivar レイアウトが親子で食い違った例:
  doc/spinel_aot/reports/editor_ivar_layout_bug.md)。
- lint (`rake spinel:doctor`) は上記とは別枠 (Spinel コンパイラ checkout が要り、
  pass/fail ではなく指摘を出す)。

## ハードウェア構成

### 本番構成

fmruby-coreは、ESP32-S3で実行する
映像出力(NTSC)と音声出力（APUエミュレータを利用したI2S）は子マイコン（ESP32-WROVER）で実行する
S3とWROVERの間はSPI通信

### 開発環境構成（Linux）

fmruby-coreは、WSL2で動くコンテナで実行する
映像出力と音声出力は、WSL2側で動く別プロセスにソケット通信で通信して実現する。その別プロセスでは、SDL2を動かす

### サポート中断中のターゲット

**ATOM_DISPLAY (M5 AtomS3 + Atom Display, n8r8) はサポートを中断している**
(2026-08-03 時点)。ESP32-P4 対応でハードウェア分岐を再編した際に追随して
おらず、現在はビルドも通らない (`fmrb_pin_assign.h` の ATOM 分岐に
`FMRB_PIN_RESTRICTED_BOOT` / `_JTAG` が無く、pin manager がコンパイル
エラーになる)。

- ATOM のビルドが通らないことを不具合として追わない。再開する時にまとめて
  直す。
- 一方で**ATOM を意図的に除外している箇所は壊さないこと**。WiFi 関連
  (`lib/add/family_mruby_esp32.rb` の networking gem、`components/
  picoruby-esp32/CMakeLists.txt` の socket ポート、`FMRB_HAS_WIFI`) は
  ATOM を除外する条件で書いてある。ATOM は WiFi を無効にしたままなので、
  再開時もこの区別は必要。

## 参考情報

- **doc/README.md が索引** (`rake docs:index` で自動生成)。文書の置き方の
  規約 (参照資料は doc 直下 / 企画は doc/<テーマ>/ に plan.md + report/、
  状態行の書式、完結したら doc/archive/ へ) も README 冒頭にある。
- 新しい設計・計画文書はこの規約に従って作り、追加・移動したら
  `rake docs:index` を実行する。
