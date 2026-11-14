# アプリの書き方とリポジトリの置き場の整理

> 状態: 進行中 | 更新: 2026-11-14 | アプリの決まった書き方 (末尾の起動コード・座標の足し算・FmrbApp のクラスメソッドの混在・古い契約の残り) と、二重管理のヘッダ、置き場の名前 (tool/tools・試験用アプリ・prebuild_scripts・lib/add・config) を整える。段ごとに、利用者に見える選択は選択肢で決めてから進める

## 目的

- アプリを書く人が毎回書く決まり文句を減らし、忘れたときに黙って何も起きない落とし穴を無くす。
- 入力イベントの型を足すたびに 3 か所を直す、のような二重管理を無くす。
- 置き場の名前を中身に合わせ、初めて読む人が迷わないようにする。

## 調べた数 (2026-11-14、fmruby-core develop)

| 項目 | 数 |
|---|---|
| `flash/app` の `.app.rb` | 106 本。末尾に起動コード (`X.new` → `start` を begin/rescue で包む) があるもの 105 本 |
| `@user_area_x0` / `@user_area_y0` | 101 ファイルで 541 回 |
| `flash/app/test/` の試験用アプリ | 64 本 (配布の木に入っている) |
| アプリのメタデータ | `.app.toml` 110、`#---fmrb` ヘッダ 7 |
| `fmrb_hid_event.h` | 3 か所 (core の main/include・components/fmrb_msg、graphics-audio の main/include) |
| FmrbApp のクラスメソッド | `sig/fmrb_app.rbs` で一般用と system 用 (wifi_*・linux_*・ble_*・reboot ほか) が混在 |
| 古い契約の残り | skill とテンプレート (window.rb・fullscreen.rb) の `super(ev)`、window.rb の `draw_window_frame`、`set_timer` の対が `clear_time` |

## 方針

1. **互換は考えない** (使っているのは作者だけ)。ただし一度に全部は変えず、段ごとに全アプリを入れ替え、回帰の道具で確かめてから次へ。
2. **Spinel の標準構成と全部 mruby の構成の両方で動く形**にする (エンジン方針)。Spinel の型推論・AOT に合わない書き方は採らない。
3. **checkpoint (アプリを移す) と矛盾しない**こと: 起動の仕方を変えるときは、移った先での起動し直し (`on_restore`) と二重に起動しないことを確かめる。
4. **内蔵 RAM は増やさない**。描画の原点を C 側で持つ場合も静的な増分 0。
5. 置き場の名前の変更 (T6) は、ほかの作業が並んでいない時期に 1 回でまとめて行い、文書・skill・memory・CLAUDE.md の参照を全部追従させる。
6. 段ごとに instruction → サブ (Opus) が実装と report → 親が検収 (fmrb-dev-flow 4 章)。

## スコープ (段)

| 段 | 内容 | 状態 |
|---|---|---|
| T0 | 調べ: 枠なし・全画面・高解像度のアプリと、Window の外や枠の上に描くアプリの一覧、座標を取る API (描画・当たり判定・透ける区画・スプライト・キャンバス・FmrbUI) の一覧と今の原点、起動の流れ (カーネルがアプリの .rb を読んでから `start` まで、mruby・Spinel・MicroPython・checkpoint の復元)、`@gfx` の座標の通り道と全画面・枠なしアプリの例外、FmrbApp のクラスメソッドの一覧と使っている所、両リポジトリで重複するヘッダの全一覧と差分、`#---fmrb` ヘッダの読み手、tool/・tools/・ルートの tools/ の中身と呼び出し元、試験用アプリを参照している所 (regress の道具・文書) | 済 (report/t0.md、69ad37b7) |
| T1 | 古い契約の残り (安い): skill とテンプレートの `super(ev)` を消す、window.rb の不要な `draw_window_frame` を消す、`clear_time` を対になる名前に、メタデータを 1 形式に寄せる | 済 (44c6771c・8598a5f8・9a33293c、report/t1.md。launcher の画面の基準が古いキャッシュに乗っている件は残り) |
| T2 | プロトコルヘッダの一本化: 正を 1 か所に置き、ほかは生成かビルド時の参照にする。違いがあるものは中身を揃える。足し忘れを `rake` の検査で止める | 進行中 |
| T3 | アプリの自動起動: ランタイムが起動の決まり文句を引き受け、末尾の起動コードを全アプリから消す。起動に失敗したときは理由をログと画面に出す (黙って何も起きない、を無くす) | 未着手 |
| T4 | 描画の座標: `@gfx` が user area の原点を持ち、アプリは `@user_area_x0 +` を書かない。全画面・枠なし・高解像度のアプリの扱いを決める。全アプリを入れ替え、画面の回帰の基準で絵が同じことを確かめる | 未着手 |
| T5 | FmrbApp のクラスメソッドの分割: 一般アプリ用と system 用 (Wi-Fi・BLE・Linux・電源など) をモジュールに分け、sig とヘルプも分ける | 未着手 |
| T6 | 置き場の整理 (まとめて 1 回): tool/ と tools/ を一本化、試験用アプリを配布の木の外へ (開発のビルドと回帰では今どおり置く)、`main/prebuild_scripts/` を中身に合う名前に、`lib/add/` を上流への差し込みと自作 gem に分ける、`ports/` の名前、`config/` の名前を端末名に揃える | 未着手 |

## 受け入れ条件

- 全段で `rake regress` が通り、画面に関わる段 (T3・T4) は `regress:screens`・`regress:run`・`regress:checkpoint` も通る。T4 は画面の基準を**更新せずに**通る (絵が同じ)。
- `regress:size` で内蔵 RAM の静的な増分 0 (S3・P4)。
- T3 の後、末尾の起動コードを書かないアプリが起動し、書き損じたアプリ (クラスが無いなど) は理由がログと画面に出る。
- T2 の後、入力イベントの型を 1 つ足すのに直すヘッダが 1 か所。
- T6 の後、古いパスを grep しても文書・skill・memory・CLAUDE.md・ソースのコメントに残らない。

## 決めたこと (2026-11-14 ユーザ)

- T3 自動起動: ファイルを読んだあと、新しく定義された FmrbApp の子クラスを 1 つ見つけて起動する。子が 2 つ以上あるときだけ `.app.toml` の `class =` で名指す。末尾の起動コードが残っていても二重に起動しない。
- T4 座標: `@gfx` の原点は「そのアプリが描いてよい区画」の左上 (0,0)。枠のある Window は枠の内側 (user area)、
  枠のない Window (`window_frame = "none"`、doc/frameless_window) は Window の左上、全画面・高解像度の全画面は画面の左上。
  アプリはどの形でも同じ書き方で描く。全アプリを一度に入れ替える (2026-11-14 ユーザ)。
  守ること (2026-11-14 ユーザ「枠がないアプリでも不都合ないように」):
  - 区画の形は実行中に変わりうる (枠の有無の切り替え、全画面への出入り、checkpoint で別の大きさの画面へ移る `on_resize`)。
    原点は基底が持ち直し、アプリは描き直すだけでよい。
  - マウス・タッチの座標も同じ原点にそろえる (描く座標と当たり判定の座標が食い違わない)。FmrbUI (今も user area 相対) も同じ。
  - 区画の外 (枠の上など) に描きたいアプリのために、Window 全体の座標で描く手段を残す (例: 枠を自分で描く枠なしアプリ、
    独自のタイトル)。枠を描く基底クラスもこれを使う。
  - 透ける区画 (`set_composite_regions`)・スプライト・キャンバスなど、座標を取る API を全部数えて、同じ原点に揃えるか、
    Window 座標のままにするかを API ごとに決める (T0 で一覧)。
- T1 メタデータ: `.app.toml` と `#---fmrb` ヘッダの**両方を使えるまま**にする。読み手を 1 つにし、項目の名前と意味を両形式で同じにする。両方あるときの優先 (どちらが勝つか) を決めて文書に書く。
- T2 ヘッダ: 正は fmruby-core の 1 か所。graphics-audio と sdl2-display はビルドのときにそれを読む (隣の checkout)。写しが要る所は rake が写し、差があれば警告する。

- T0 の後の決定 (2026-11-14 ユーザ。根拠は report/t0.md):
  - T1: `.app.toml` と `#---fmrb` の両方があるときは、項目ごとに `.app.toml` が勝つ。読み手は C の spawner 1 つにまとめ、
    ランチャー・desktop・Shell・checkpoint はそこに問う。`clear_time` は `clear_timer` にする。
  - T2: 共有するのは線の形のヘッダ 5 本。正は fmruby-core、graphics-audio は `../fmruby-core` を読み取り専用で mount して
    CMake で直接 include、sdl2-display は docker の `additional_contexts` で core を足す。`MAX_TEXT_LEN` は 128 にそろえる。
    `components/fmrb_msg/fmrb_hid_event.h` は同じ名前の別物なので名前を変える。
  - T3: mruby は `FmrbApp.inherited` で子を覚え、読み込みの後に起動。Spinel は生成のときに末尾を足す。二重起動は C の ctx の旗
    (PSRAM)。起動に失敗したらダイアログとログ。末尾の起動コードは全部消す。Python は後回し。
  - T4: 原点は C の gfx 構造体 (VM の pool) に持つ。Window 座標で描く口は 2 つめの描き手 `@window_gfx`。イベントの Window 座標は
    `ev[:wx]`・`ev[:wy]` で残す。スプライトも区画相対。画面の基準に載らないアプリのために、前後で全アプリを撮って比べる一回きりの道具を作る。
  - T5: system 用 (wifi_*・ble_*・linux_*・reboot など 34 本) は `FmrbSystem` にまとめる。読み取り系の境目は `FmrbApp` に残す。
    mruby と Spinel のずれ (25 本) と sig の欠け (37 本) もこの段で揃える。
  - T6: tool/ と tools/ を tools/ に、`main/prebuild_scripts/` を `main/ruby/` に、`lib/add/` を `lib/gems/` (自作 gem) と
    `lib/upstream/` (差し込み・patch・replace) に、`config/` の端末の名前を `FMRB_HW_TARGET` の小文字にそろえる。これに伴う
    sdkconfig.defaults の中のパスの書き換えは承認済み。試験用アプリは置き場を変えず、配布物からだけ外す。

## 未確定事項

なし (段の中で新しく出たら選択肢で聞く)。

## 関連

- doc/app_model/plan.md (継承の契約と予約名、完了)、doc/app_exit/、doc/gfx/、doc/checkpoint/movable_apps.md。
- skill fmrb-app-new (アプリの書き方の雛形)。T1・T3・T4・T5 の後に必ず直す。
