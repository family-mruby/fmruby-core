# 実装指示書 M1: アプリ作成の摩擦削減 (コメント toml + /tmp RAM FS + テンプレート)

対象: 実装担当セッション。多重 VM 計画 (plan.md) の段階 3 と段階 2 の実行
+ エディタのテンプレート挿入機能。report は report/m1.md へ。
タスクごとにコミット。T1 → T3 → T2 の順 (T2 が T1 の書式と T3 の /tmp を
テンプレート内で使うため)。

動機 (ユーザ 2026-08-10): 全体的な使いやすさに効く 3 点を多重 VM 計画から
抜粋して先行実装する。

## 進め方の約束

エディタ P1-P5 と同じ (instruction_p1.md 冒頭 + p2 の Spinel カーネル注意)。
検証は Linux sim、esp32 は S3/P4 ビルド通過まで (実機はユーザ確認待ちに
積む)。**エディタに触る T2 は標準構成 (editor=spinel) と互換構成 (mruby) の
両方で動作確認する** (エディタは P5 から単一ソース二重バックエンド)。

## T1: コメント埋め込み toml (多重 VM 計画 段階 3)

### 仕様 (plan.md 3.2 で設計済み)

`.rb` の先頭コメントに実行属性を書けるようにする (PEP 723 と同発想):

```ruby
#---fmrb
# default_window_mode = "background"
# task_stack_kb = 32
#---
```

- フェンス `#---fmrb` 〜 `#---` の中身の行頭 `# ` を剥ぎ、**既存の
  fmrb_toml 解析器へそのまま渡す** (新文法を作らない)。
- **`.toml` サイドカーが存在する場合はコメントを一切見ない** (予測可能性)。
- 読むのは spawn 時に先頭 512 バイトのみ。フェンスはファイル先頭付近
  (最初の非コメント行より前) にあるものだけ有効。
- 対象は `.rb` のみ (v1)。`.bas` / `.lua` / `.py` はコメント文法が違うので
  将来 (report に一言残す)。
- **ランチャー表示系メタデータ (名前・アイコン・launcher_visible) は
  従来どおり `.toml` 限定**。起動時スキャンが全 `.rb` を開く事態を避け、
  起動時間を守るため。コメント側に書かれていても無視する (警告ログ可)。

### 実装点

- main/app/fmrb_app_spawner.c の spawn_user_app: サイドカー `.toml` が
  無い場合の後段として、`.rb` 先頭を読んでフェンスを抽出 → 行頭 `# ` を
  剥いだバッファを fmrb_toml に渡す → 既存の属性反映コードを共用する
  (窓モード / large_memory / task_stack_kb / resizable / fullscreen 等、
  .toml と同じキーが同じに効くこと)。
- 事前に現状確認: `.toml` 無しの `.app.rb` がランチャーに出て既定属性で
  起動できること (出ないなら報告。仕様上は出るはず)。

### 受け入れ条件

- `.toml` 無し + フェンス付きの test アプリが、ランチャー/shell から
  指定属性 (例: fullscreen、background) で起動する (sim スクリーンショット)。
- `.toml` と両方あるファイルでは `.toml` が勝つ。
- フェンス無しの従来アプリ・`.toml` 持ちの全既存アプリに退行なし
  (代表: rpg_demo / raycaster / pub_demo)。
- 壊れたフェンス (閉じ忘れ・不正 toml) は警告ログ + 既定属性で起動
  (spawn を失敗させない)。
- S3/P4 ビルド通過。

## T3: /tmp RAM FS (多重 VM 計画 段階 2)

### 仕様 (plan.md 3.1 で設計済み)

- マウントポイントは `/tmp` 一つ。**再起動で消える** (RAM 実体なので自然に
  そうなる)。/home = 永続、/tmp = 一時・VM 間受け渡し、の役割分担。
- Ruby 側は既存 File / Dir API がそのまま使える (新 API なし)。
  create / read / write / append / delete / list / stat / (可能なら rename)。
- 容量上限あり。超過時は書き込みエラー (ENOSPC 相当) を返し、
  **firmware を落とさない**。

### 実装点

- **確保元は専用プール** `POOL_ID_TMPFS` を新設 (editor-core P4 の前例
  どおり fmrb_mem_config.h + fmrb_mempool.c の 2 ファイル同期、
  fmrb_mem_create_handle で自前ハンドル)。サイズは 512KB から
  (S3 の PSRAM 残 = 3.2MB - editor_doc 1MB に対して安全。P4 は余裕。
  将来ターゲット別に増やせるよう定数は分けておく)。
  mrb_malloc / fmrb_sys_malloc を使わない理由は P4 指示書と同じ
  (アプリプールの天井 / SYSTEM 500KB 共有プールに大物は置けない)。
- esp32: PSRAM 後ろ盾の小さな esp_vfs ドライバ (数百行) を登録し、
  fmrb_hal_file_esp32.c の s_path_aliases に `/tmp` を 1 行追加。
  仮想ディレクトリ扱い (`/` の直下に見える) も既存の virtual mount-point
  機構に倣う。
- posix (Linux sim): fmrb_hal_file_posix で `/tmp` をホスト側の作業
  ディレクトリ (例: flash/tmp。**起動時に中身を消す**ことで揮発性を再現)
  へマップ。ホストの本物の /tmp は使わない (コンテナ間で見え方が
  変わるため)。
- **Spinel 側の経路も同じ HAL を通る** (sp_io の VFS フックは
  fmrb_hal_file_* 配線済み) ので追加作業は無いはずだが、Spinel desktop /
  Spinel editor からの /tmp 読み書きを必ず 1 ケース検証する。

### 受け入れ条件

- sim: アプリ A が /tmp にファイルを書き、アプリ B が読む (VM 間受け渡しの
  最小実証。pub/sub でパスを通知する形。既存の pub_demo/sub_demo の改造で
  良い)。
- エディタで /tmp のファイルを開いて保存できる。
- 容量超過で ENOSPC 相当のエラーになり、エディタ/アプリが生存する。
- 再起動 (sim の down/up) で /tmp が空になる。
- ls 相当 (Dir) で /tmp の一覧が見える。/ 直下の一覧に /tmp が出る。
- S3/P4 ビルド通過 (実機の動作はユーザ確認待ち)。

## T2: エディタのテンプレート挿入

### 仕様

エディタから「GUI アプリの雛形」を挿入して、すぐ書き始められるようにする。

- テンプレートは**ファイルとして** `/lib/templates/*.rb` に置く (ファーム
  埋め込みではなくユーザが自分のテンプレートを追加できる形。配置は
  flash/lib/templates/ → sync で実機へ)。
- v1 で同梱する 3 種:
  1. **window アプリ** — FmrbApp 継承、on_create / on_update / on_event の
     骨格、canvas への基本描画、コメント toml フェンス (T1 の書式) 付き
  2. **fullscreen アプリ** — フェンスに fullscreen +
     fullscreen_switchable、ゲームループの骨格
  3. **headless worker** — フェンスに background、/tmp へ結果を書いて
     publish で通知する骨格 (T3 の実例を兼ねる。多重 VM 計画 形態 B の
     最小形)
- **全テンプレートの末尾に起動トレーラ必須**:
  `begin / MyApp.new.start / rescue => e / ...` が無いとユーザアプリは
  何も動かない (既知の落とし穴。テンプレートの主目的の一つがこれを
  忘れさせないこと)。
- エディタ UI: メニュー File に Template 項目を追加 → 一覧 (既存の
  picker UI の型を再利用) → 選択でカーソル位置に挿入 (editor-core の
  insert_multiline)。空バッファへの挿入が主用途だが、カーソル挿入で良い。
- クラス名等の置換 (雛形の MyApp を入力名に置換) は v1 ではやらない
  (挿入だけ。report に将来案として残す)。

### 受け入れ条件

- sim (マウス + キーボードのみ): エディタ起動 → Template → worker 雛形を
  挿入 → /app/test/ 配下に保存 → F5 (または shell run) で起動し、
  **toml 無しで background 属性が効き** (T1)、/tmp に結果が書かれ (T3)、
  publish 通知まで動く — **3 タスクの統合デモがこの 1 本で成立する**。
  スクリーンショットを report へ。
- window / fullscreen 雛形も保存 → 起動して表示されること。
- 標準構成 (Spinel editor) と互換構成 (mruby editor) の両方で挿入動作。
- 既存メニュー動作に退行なし。S3/P4 ビルド通過。

## 範囲外

- `.bas` / `.lua` / `.py` のコメント toml (文法が違う)
- テンプレートのクラス名置換・対話ウィザード
- /tmp の rename が HAL に無い場合の追加 (書いてから通知の規約で足りる)
- 多重 VM 計画の段階 1 デモ・段階 4 (Request/Response) — T2 の worker
  雛形がその入口になるが、本格版は別フェーズ

## 完了報告

report/m1.md に: 実施内容、T1 のフェンス解析の実装形、T3 のドライバ構成と
プールサイズ根拠、統合デモの手順と画面、既存アプリ退行確認の一覧、
ユーザ確認待ち (実機での /tmp・コメント toml・テンプレート起動)、
段階 1/4 (worker デモ本格版・Request/Response) への引継ぎ。
