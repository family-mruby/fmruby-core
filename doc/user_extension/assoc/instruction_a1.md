# 実装指示書 A1: ファイルの関連付けと「開く」の一般化 (ideas.md 案 3)

対象: 実装担当セッション。作業リポジトリ: fmruby-core のみ。
進め方の約束は services/instruction_s1.md のとおり。報告は
doc/user_extension/assoc/report/a1.md。**Retro / Modern 共通の機能**
(サービスと違い Modern 限定にしない)。desktop に触るので、書き方は
dual-safe (system_desktop は Spinel 生成の対象。doctor 新規指摘 0 を維持)。

## 下調べ済みの事実 (引数渡しは既にある)

- kernel の spawn 要求は **`open_path`** を受け、新アプリへ
  `{"cmd"=>"file_selected", "path"=>...}` の control メッセージとして
  届ける (fmrb_kernel.rb の "spawn" 分岐。新アプリの queue ができるまで
  tick_process で繰り延べ、fullscreen で要求側が suspend される難所も
  解決済み)。shell の `spawn_app(app_name, open_path)` も対応済み。
- つまり**新しい配管は不要**。本件は「`file_selected` を関連付け起動の
  引数の契約として公式化し、表と消費者を揃える」仕事。
- file_manager の「edit」だけが open_path 以前の自前実装
  (`@fmgr_pending_edit_path` + カウンタ待ち、file_manager.rb 660 行付近)。
  open_path に置き換えて自前実装を消す (Legacy は残さない)。

## T1: 関連付けの表と解決 API

- 二層: システム既定 `config/associations.toml` → `/etc/associations.toml`
  (rakelib/build.rake の system_conf と同じ cp)、ユーザ上書き
  `/home/associations.toml`。**拡張子ごとにユーザが勝つ**。
- 書式は素朴に:

  ```toml
  md  = "/app/tool/picorabbit.app.rb"
  nsf = "/app/tool/nsf_player.app.rb"
  rb  = "run"     # 特別値: そのファイル自身をアプリとして spawn
  txt = "edit"    # 特別値: エディタで開く
  ```

  表に無い拡張子の既定は `edit`。ディレクトリは対象外。
- 解決の実装は **gem に置く** (`lib/add/picoruby-fmrb-app/mrblib/` に
  FmrbAssoc 等の小さな 1 ファイル): `FmrbAssoc.resolve(path)` →
  `["run"] | ["edit"] | [アプリパス]`。desktop と shell とユーザアプリが
  同じものを使う (**ここが案 3 の公開 API**)。toml の読解は services の
  素朴な部分集合の読みを流用できるなら流用 (置き場所をどうしたかは
  report に)。
- 表の読み込みはアプリ起動時に 1 回 (キャッシュ)。ホットリロードは
  やらない (開き直せば効く、と文書化)。

## T2: file_manager と shell

- file_manager のダブルクリック (ファイル): `FmrbAssoc.resolve` に従う —
  `run` は従来の spawn、`edit` とアプリパスは `spawn_app(対象, open_path:
  そのファイル)`。**fmgr_edit_file の自前繰り延べを open_path に置き換えて
  削除**。右クリックメニューの Run / Edit は従来の明示操作として残す。
- shell に `open <path>` を追加 (resolve して spawn。`open` 単体の usage
  1 行)。既存の `run` はそのまま。
- 検収は両機種ぶんの sim (Modern 426 / Retro 320): .md をダブルクリック →
  PicoRabbit がそのデッキで開く、.nsf → NSF Player、.txt → エディタ、
  .rb → 実行、表に無い拡張子 → エディタ。/home の上書きで .md を
  エディタに変えられる。
- 対象アプリが `file_selected` を扱わない場合はただ起動するだけになる
  (それで良い。契約に明記)。

## T3: 受け側の実例 — PicoRabbit

- PicoRabbit の `on_control` に `file_selected` を足す: 渡された .md を
  メニューを飛ばして直接開く (Esc でメニューへ、は従来どおり)。
  これで「file manager で .md をダブルクリック → 発表が始まる」が通る。
- nsf_player 等がもともと `file_selected` を扱うなら何もしない (確認だけ
  report に)。

## T4: 契約の文書化

- ideas.md 案 3 を「計画済み → 実装済み」に更新し、契約を 1 か所に書く:
  「関連付けで起動されたアプリには、起動直後に `file_selected` が届く。
  受けたければ `on_control` で拾う。受けなければ何も起きない」。
  fmrb-app-new skill にも 3 行で追記 (アプリ作者向け)。

## 検収まとめ

- 上記 T2 の表 + PicoRabbit の直接起動を、**両機種向け sim** で。
- 実機 (Tab5): .md ダブルクリック → 発表、の 1 本だけ (書き込み前の
  fmrb_rd_ps 単独確認を忘れずに)。
- 2 構成ビルド + doctor 新規指摘 0 + rake test (FmrbAssoc.resolve の
  host テストを追加: 上書き・特別値・既定・大文字拡張子)。`.env` 復元。
- コミット 2 本: (1) FmrbAssoc + 表 + file_manager/shell、(2) PicoRabbit +
  docs (本書含む)。英語、ユーザ確認のうえ。

## やらないこと

- MIME 型・内容判定 (拡張子のみ)、複数引数・引数の一般形 (open_path の
  1 本で足りている)、「アプリを選んで開く」ダイアログ、
  関連付けの GUI 編集。
