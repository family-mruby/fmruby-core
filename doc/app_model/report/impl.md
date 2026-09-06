# 完了報告: FmrbApp 基底の締め直し (M1 / M2 / M2b)

対象: `doc/app_model/plan.md`。全段階 (M1 / M2 / M2b / M3) 実施済み。

| 段階 | commit | アプリ改修 |
|---|---|---|
| M1 `super(ev)` 契約の廃止 + 全画面 reload ガード | `54cabe03` | 0 本 |
| M2 非公開 ivar 13 個の `@_` 化 + アクセサ手書き化 | `ce10f295` | 0 本 (mixin 1 件) |
| M2b `@running` / `@name` の回収 | `eaf2ce14` | 6 ファイル |
| (副産物 1) タスク例外の握りつぶし修正 | `b4aba4a1` | - |
| (副産物 2) 互換構成エディタの即死修正 | `ac347b42` | - |
| M3 `super(ev)` の掃除 | `c14e6936` | 41 本 (削除のみ) |

---

## M1: ディスパッチの分離

計画どおり。呼び先を `_dispatch_event` に変え、基底処理 (`_frame_event`) →
アプリの `on_event` (基底では空) の順で必ず両方走る。既存の `super(ev)` は
空フックを呼ぶ無害な行になった。

- 触った場所は計画の表どおり 3 か所: mruby 基底 / `app.c` の 1 行 /
  Spinel 基底 (別実装なので同じ分離をもう 1 度)。
- 付随修正 (タイトルバー右クリック reload の `!@fullscreen` ガード) も
  計画どおり入れた。**基底が必ず走るようになった瞬間に踏む穴**なので、
  M1 と同時でなければならない。

## M2: ivar の改名

**計画の一覧から 1 個減って 13 個** (`@param` は現物に存在しなかった。
計画の実測が doc コメントの `# @param` を拾ったもの)。

計画に無かった発見:

- **`app.c` が対象 ivar 5 個を C から書いている** (`_init` の
  `@name` `@fullscreen` `@rounded_corners` `@canvas` `@bg_canvas` と
  リサイズ経路の `@fullscreen`)。計画の「触る箇所」表に C 側が
  載っていなかった。改名は 3 か所 (mruby 基底 / Spinel 基底 / app.c) で
  1 セット。
- アクセサはすべて手書き化 (`attr_reader :fullscreen` は `@fullscreen` を
  読む reader を生やすため、ivar 改名と同時に必須)。真偽は
  `fullscreen?` / `closable?` / `rounded_corners?` に統一。
- mixin の直接参照は計画どおり `editor/menu.rb` の 1 件だけ
  (`fullscreen?` に置換)。

## M2b: `@running` / `@name`

読んでいたのは計画どおり 6 ファイル (shell 3 行、kamon / stackchan /
stackchan_remote 各 1 行、taskbar.rb と debug_pane.rb の自分比較 2 件)。
すべて読み取りだけなので `running?` / `name` への置換で済んだ。

---

## 副産物: 互換構成のエディタが「静かに」死んでいた

検証手順 (両構成 + エディタ 1 打鍵) がそのまま先制した。**互換構成
(全 mruby) のエディタが、起動直後に何のログも出さず消える**。

### 切り分け

自分の 3 コミットを疑い、変更 10 ファイルだけを計画前 (`365716ce`) に
戻して再ビルド → **同じ即死**。つまり既存リグレッション。

### 原因は 2 段重ね

1. **診断の穴**: mruby-task はタスクの未捕捉例外を `t->result` に移して
   `mrb->exc` を消す (task.c)。`fmrb_app.c` は `mrb->exc` しか見ないので
   「No exception detected」と誤報し、例外は闇に落ちる。
   → `mrb_task_value()` で取り出して通常の例外報告に載せた (`b4aba4a1`)。
2. **本体**: 例外が見えたら 1 行だった。
   `uninitialized constant EditorConst::SEL_BG (NameError)`。
   Colors 対応で入った `EDITOR_COLOR_VALUES` 配列が、**同じモジュール内で
   後方に定義される定数** (SEL_BG ほか) を参照していた。module 本体は
   上から実行されるので mruby では即死。**Spinel は定数をプログラム全体で
   コンパイル時解決するため、標準構成では絶対に発現しない**。
   → 配列 2 本をモジュール末尾へ移動 (`ac347b42`)。

### 教訓

- **「Spinel でだけ検証した Ruby」は mruby の定義順バグを素通しする**。
  互換構成の検証は形式的な儀式ではなく、この種のバグの唯一の網。
  今回どれだけ前から壊れていたかは特定していないが、Colors 対応
  (`1ba4776a` 前後) 以降のどこかで、以後の互換構成検証がエディタ起動を
  含んでいなかったことになる。
- 「例外なしで即終了」は例外が無いことを意味しなかった。診断の穴は
  塞いだので、今後この形の死に方は必ず例外文面が出る。

### 作業上の事故 (記録)

切り分けで `git checkout <旧> -- <files>` した内容が index に残ったまま
副産物 1 をコミットし、**M1-M2b を巻き戻した内容が混入**した。未 push
だったので `reset --soft` で 2 コミットを作り直して解消。
`git checkout <rev> -- <paths>` は **index も書く**ので、直後のコミットは
`git add` 対象以外も乗る。切り分けの戻しは stash / worktree の方が安全。

---

## 検証

標準構成 (Spinel kernel + Spinel editor) と互換構成 (全 mruby) の両方。

| 確認 | 標準 | 互換 |
|---|---|---|
| デスクトップ・エディタ・シェル起動 | OK | OK |
| エディタ 1 打鍵 (必須手順) | OK | OK (修正後) |
| shell の閉じるボタン | OK | OK |
| **全画面 picorabbit の上端右クリックで reload しない** | OK (ログに reload_confirm なし) | - |
| 窓アプリのタイトルバー右クリック reload は生きている | OK (kamon で Confirm 表示) | - |
| F11 全画面の出入り (バッファ保持) | OK | OK |
| View メニューの Full チェック (fullscreen?) | OK | - |
| Ctrl+Tab 巡回 | OK | - |
| kamon / stackchan が止まらない (`running?`) | OK (kamon は操作反応まで) | - |
| 窓タイトル (`@_name` + C 書き込み) | OK (FM-Shell / Kamon / StackChan / FM-Editor) | OK |
| ビルド | Linux / S3 (残 34%) / P4 (残 5%) | Linux |

S3/P4 の残量は本テーマ前後の比較ではない (このツリーはパーティション構成
ごと動いている)。ビルド通過の確認として記録する。

## M3: `super(ev)` の掃除

41 本すべてから除去した (計画の実測どおりの本数)。中身が super だけだった
override 3 本 (sub_demo / require_test / gpio_viewer) はメソッドごと削除、
inspector の「キー以外のときだけ super」は 1 行に畳み、mic_spectrum の
「super を先に呼べ」という説明コメントも役目を終えたので消した。

空メソッドへの呼び出しの削除なので挙動は変わらない (mrbc / Spinel とも
標準ビルドで再コンパイル済み)。sim で editor 打鍵・quit ダイアログ・
kamon 描画・**shell の閉じるボタン (かつて super が守っていた当のもの)** を
確認した。

## 残り

- 実機 (S3 / Tab5) での閉じるボタン・全画面出入りの確認はユーザ。
  確認が取れたらテーマを archive へ。
