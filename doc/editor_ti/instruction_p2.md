# 実装指示書 P2: エディタ補完 UI (picoruby-ti)

対象: 実装担当セッション。前提: P1 完了 (PIN = fmrb-dev 0b95b8a、rake
ti:setup / ti:gen / ti:test が動く)。plan.md と report/p1.md を先に読むこと。

report は doc/editor_ti/report/p2.md へ。タスクごとにコミット。

## P2 のゴール

エディタで **Tab キーによる補完**が動く。sim 上で `gfx.dr` + Tab で
draw_text / draw_rect が出て、選んで挿入できる。ホバー・診断は P3。

## 方針 (決定済み)

- **@gfx 問題は fork の RBS ivar 宣言対応で解決済み** (ユーザ決定で
  attr_reader 案から変更、fork 対応 2026-08-11)。エンジンが RBS の
  `@gfx: FmrbGfx` 宣言を db に取り込み、ソース中に代入が無い `@名前` を
  外側クラスの継承チェーンから解決するようになった (fmrb-dev 333beb1)。
  **`@gfx.` がそのまま補完される**。基底クラスのコード変更は不要。
  ソース中の代入が宣言より優先される (両方あるときは代入の型が勝つ)。
- **発火キーは Tab** (scancode 0x2B で判定)。カーソル直前の文字が識別子
  ([A-Za-z0-9_] または ? !) か `.` のときだけ補完を開き、それ以外
  (行頭・空白直後) は従来どおりインデント。Ctrl+Space はかなトグルに
  割当済みなので使わない。
- **要求時のみ実行** (毎打鍵の自動ポップアップはしない)。絞り込み入力も
  v1 ではなし (開き直しが十分速いことを計測で確認する)。
- エディタは単一ソース二重バックエンド。**UI コードにエンジン分岐を
  書かない**。ブリッジは editor-core と同じ「int スロット + ポインタ+長さ」
  流儀で、mruby port と Spinel FFI の両方から同じ C を呼ぶ。

## T1: PIN 更新と RBS の @gfx 宣言

1. `lib/add/PICORUBY_TI_PIN` の commit を
   `333beb15fee33908e5c027a8fe541615aca3a257` (fmrb-dev、RBS ivar 宣言
   対応入り) に更新し、`rake ti:clean` -> `rake ti:setup` で取り直す。
2. `sig/fmrb.rbs` の FmrbApp に `@gfx: FmrbGfx` を宣言する。基底の
   Ruby コード (fmrb-app.rb / fmrb_app_base_spinel.rb) は**変更しない**。
   他の ivar の宣言追加は P4 に回す (P2 の最小は @gfx だけ)。
3. `sig/gpio.rbs` に `@pin: Integer` と `@label: String` を追加する
   (上流の host_test 新テストがこの宣言を前提にするため。
   これが無いと `rake ti:test` が落ちる)。
4. 検証: `rake ti:test` 全 green (ivar 系の新テスト 5 本を含む)。

## T2: ti ブリッジ (editor-core 内、et_*)

場所: `lib/add/picoruby-fmrb-editor-core/src/` に新ファイル
(例 editor_ti_bridge.c)。公開ヘッダは include/ に追加。P1 報告のとおり
gem の中から呼ぶ分にはリンクは何もしなくてよい。mrbgem.rake に
`spec.add_dependency 'picoruby-ti'` と include path (picoruby-ti/include、
prism の include) を足す。

API は editor-core の流儀に合わせる:

- `int et_suggest(int slot, int y, int x)` -> 候補数 (負値はエラー)。
  内部で (1) 文書を連続バッファへ直列化 (2) (y,x) をバイトオフセットへ
  変換 (x は文字インデックス。UTF-8 の実バイト数を積む) (3)
  `ti_fill_suggestions_at_cursor` 呼び出し (4) **結果を自前バッファに
  コピー** (TiSuggestion のポインタはエンジンの arena を指しており、
  次の ti 呼び出しで無効になるため。上限 64 件 x label/detail/doc)。
- `const char *et_suggestion(int i, int field, int *out_len)` 形式の
  読み出し (field = label / detail / doc。分けた関数でもよい)。
- 直列化バッファと結果バッファは POOL_ID_EDITOR_DOC から確保するか
  静的にする (fmrb_malloc は使わない。editor-core の既存の流儀に従う)。
- **排他**: エンジンはグローバル状態 (arena 16KB) を持つ。ブリッジに
  static なロックを置き、et_suggest 全体を囲む (エディタ 2 枚が同時に
  叩いても壊れないように)。

### T2-0: prism アロケータの B 案実装 (決定済み 2026-08-11)

調査結果 (中間報告): xmalloc = mrb_malloc(global_mrb) の global_mrb は
mrc_ccontext_new のたびに上書きされる単一グローバルで、Spinel 構成の
起動直後は NULL (即死)、非 NULL でも他タスクの VM に GC 統計や longjmp が
飛ぶ。実バイトは呼び出しタスクの est に落ちるので安全。既存の
「2 VM の同時コンパイル」にも同じ素地がある。

**B 案を採用**: global_mrb をタスクローカルにする。

- mruby-compiler への変更は lib/patch / lib/replace の overlay で行う
  (対象: prism_xallocator.h と、global_mrb を定義/代入している
  ccontext.c 付近)。picoruby-ti fork 側は触らない。
- 形: `global_mrb` を TLS 値 (fmrb_get_current_est と同じ流儀) を返す
  関数に置き換え、mrc_ccontext_new で自タスクの TLS に set する。
  **VM のあるタスクは従来どおり mrb_malloc 経由** (GC 圧力の
  フィードバックを失わないこと。これが A 案を捨てた理由)。
- **VM の無いタスク (Spinel エディタ) は est 直結**。このとき確保失敗が
  raise ではなく NULL 返しになるので、**prism が xmalloc の NULL に
  耐えるかを確認する** (耐えないなら、ブリッジ側で「必要量の見積もり +
  上限超過なら呼ばない」で NULL を実質踏まない設計にし、その根拠を
  report に書く)。
- 全 mruby 構成でも修正の恩恵を受ける (エディタ VM != global_mrb 問題と
  同時コンパイルの素地が消える)。回帰として、既存のアプリ実行 (実行時
  コンパイル) が両構成で通ることを確認する。

### T2-1: プールと上限 (実測に基づく決定)

実測: AST の山はソースの約 18 倍 (64bit)、実機 32bit は概ね半分。
1 要求 = 3 パース。Spinel エディタタスクの est は USER_APP 500KB で、
ここに prism を乗せると UI と奪い合う。

- **ti 呼び出し中の est 直結分は POOL_ID_EDITOR_DOC (1MB) に向ける**。
  ブリッジは et_suggest のロック内で「フォールバック先ヒープ」を
  TLS に set/restore する形にする (直列化バッファ・結果コピーも同じ
  プール。UI のヒープには触れない)。
- **補完を受け付けるソースサイズ上限 = 32KB** (定数 1 箇所)。超えたら
  ti を呼ばずにステータス行へ「大きすぎる」旨を出す。根拠: 32KB で
  ホスト 24ms / AST 山 600KB (64bit) が 1MB プールに収まり、arena 16KB
  溢れ (~100KB) より手前で止められる。子供サイズには十分。
  上限の見直しは P4/P5 の実測とセットで。

## T3: バインディング

- mruby: `lib/add/picoruby-fmrb-editor-core/ports/esp32/editor_core_mrb.c`
  に et_* のラッパを追加 (既存 ec_* と同じ形)。
- Spinel: C 実装は `main/app/fmrb_spx_editor.c`、FFI 宣言は
  `main/prebuild_scripts/spinel/fmrb_editor_ffi.rb` (共有 FFI と別ファイル
  という P5 の原則は既にこの形なので、そこに足すだけ)。
- 文字列は ptr+len で返し、Ruby 側で文字列化する (ec_render_text と同じ)。

## T4: エディタ UI (editor.app.rb)

- Tab (scancode 0x2B) のハンドラ: 上記の文脈規則で補完かインデントかを
  振り分ける。**キー判定は scancode。keycode を見ない。**
- ドロップダウンは既存メニュー描画の部品/配色 (MENU_* / dropdown 系) を
  流用する。仕様:
  - カーソル行の直下に出す。画面下端に収まらなければ上に出す。
    表示は最大 8 件 + 上下スクロール。
  - 各行は候補名。横幅が許せば detail (シグネチャ) を薄色で並記
    (Retro 320px では候補名のみで可)。選択中の候補の doc コメントが
    あればステータス行に出す。
  - 操作: 上下で選択、Enter または Tab で確定、Esc で閉じる。
    それ以外のキーは閉じてから通常処理に流す (シンプル優先)。
  - 確定時はカーソル直前の識別子 prefix ([A-Za-z0-9_?!] を後方スキャン)
    を選択候補で置換する。`.` 直後 (prefix 空) はそのまま挿入。
  - 候補 0 件のときはステータス行に一言出すだけでポップアップは開かない。
- 全画面/窓、折返し ON/OFF のどちらでも座標がずれないこと (描画座標は
  既存のカーソル描画と同じ経路から取る)。
- 計測: 要求ごとに `ti_lat: N ms (M candidates, K bytes)` を DEBUG ログに
  出す (常設。edit_lat と同じ思想)。

## T5: 検証 (sim 自律 + esp32 ビルド)

標準構成 (Spinel エディタ) で、tools/dev_run_check.sh --keep +
fmrb_input + fmrb_screenshot で一巡:

1. エディタを開き、`class MyApp < FmrbApp` の def の中で `@gfx.dr` + Tab
   -> draw_text / draw_rect のドロップダウンをスクリーンショットで確認。
   Enter 確定で `@gfx.draw_text` になること (P1 積み残しの受け入れ条件
   「elf に ti_fill_suggestions_at_cursor」もここで自然に満たされる。
   nm で確認して report に書く)。
2. ローカル変数: `s = "abc"` / 改行 / `s.up` + Tab -> upcase 系。
3. 継承: `class MyApp < FmrbApp` の def 内で `self.` + Tab ->
   基底メソッドが出る。
4. 行頭で Tab -> 従来どおりインデント (補完が開かない)。
5. かなモード on (key ctrl+space) のまま Tab -> 補完が開く (Tab は
   合成層を素通しするはず。開かなければ原因を追う)。
6. 全 mruby 構成で 1 と 4 を再確認。
7. 大きい文書: 200KB を開いて Tab。ti_lat の実測値を report に記録。
   arena あふれ等で 0 件になっても UI が壊れないこと。
8. esp32 (S3/NARYAv3): ビルド通過 + サイズ記録。今回からエンジンが
   リンクされるので P1 実測 (flash +49KB / bss +16.4KB) との照合。
   内蔵 RAM が予算を割るようなら報告だけして P5 の PSRAM 化を待つ
   (ここでは対処しない)。

## 受け入れ条件

1. T5 の 1-6 が全て通る (スクリーンショット付きで report へ)。
2. ti_lat 実測: 子供サイズ (数 KB) の文書で補完要求から表示まで
   体感一拍以内 (目安 100ms 以下。超えるなら数値を報告して相談)。
3. esp32 S3 ビルド通過 + サイズ記録。
4. rake ti:test green のまま。B 案 (global_mrb タスクローカル化) の
   実装内容と NULL 耐性の確認結果が report にある。
5. B 案はコンパイル経路全体に触るので、回帰として両構成 (標準/全 mruby) で
   既存アプリの起動 (実行時コンパイル) が通ること。ランチャーから
   ゲーム 1 本 + shell の起動で良い。

## やらないこと (P2 の範囲外)

- ホバー・診断 (P3)。注意: hover.c はカーソルの外側クラスを context に
  設定していないので、def 内の `@gfx` のホバーは P3 で suggest と同じ
  外側クラス解決を hover に入れる必要がある (fork 側の宿題として記録済み)。
- 絞り込み・自動ポップアップ。
- RBS の網羅 (P4)。arena の PSRAM 化・実機確認 (P5)。
- 上流へのオーバーライド重複候補の修正 (UI 側で同名を畳む必要が出たら
  ドロップダウン側で名前重複を除去してよい。その場合 report に書く)。
