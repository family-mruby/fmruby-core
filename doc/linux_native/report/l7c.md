# L7c 報告: File Manager の削除の前に確認を出す

> 状態: 完了 (受け入れ 5 点。実機の目視は無し) | 更新: 2026-10-27 | 指示書は instruction_l7c.md。ブランチは fmruby-core の feature/linux-native (develop へは未マージ・未 push)

## 1. 作り

### 確認と削除の結び付け方

- `file_manager.rb` (`FileManagerMixin`、両方のデスクトップが共有) の削除を 3 つに分けた。
  - `fmgr_delete_file(idx)`: 消す対象の仮想パスを `@fmgr_delete_path` に覚え、
    デスクトップの `fmgr_ask_delete(問い)` を呼ぶだけ。右クリックの Delete と Delete キーは
    今どおりここを通る。
  - `fmgr_delete_confirmed`: はい の後。覚えたパスを `File.unlink` する。中身は前の
    `fmgr_delete_file` のまま (成功でログ・再一覧、失敗でログ)。
  - `fmgr_delete_cancelled`: いいえ の後。覚えたパスを捨てる。
- 問いの文は i18n の `fmgr_delete_confirm` (en `"Delete %s?"`、ja `"%s を削除しますか?"`)。
  `%s` は `index` と切り出しで差し替える (カーネルの Ruby は `sprintf` に文字列を渡す
  使い方をしていないので、両エンジンで同じに動く切り出しにした)。
- 出し方はデスクトップごとに `fmgr_ask_delete` を持つ (mixin の外の決まりは、すでにある
  `close_launcher` などと同じ形)。

### mouse のデスクトップ (`confirm_dialog.rb`)

- `open_local_confirm_dialog(問い, 記号)` を足した。はい で kernel へ命令を送る代わりに、
  デスクトップの `confirm_local_yes(記号)` を呼ぶ。いいえ・Esc・外のクリックで閉じたときは
  `confirm_local_no(記号)`。`system_desktop.app.rb` が `:fmgr_delete` を削除へ振り分ける。
- 既存の `open_confirm_dialog(問い, 命令, データ)` (電源・Reset・再読み込み) は、共通の
  `show_confirm_dialog` を通るだけで動きは同じ。開くときに `@cdlg_on_yes_local` を nil に
  戻すので、kernel の問いが後から来ても削除に化けない。
- `close_confirm_dialog(yes = false)`: はい の経路だけ `true` を渡し、`confirm_local_no` を
  呼ばない。
- **overlay の矩形を File Manager へ戻す**: kernel の overlay は 1 つなので、ダイアログを
  開くとダイアログの矩形に置き換わり、閉じると「無し」になっていた。File Manager が開いた
  ままなのに kernel が知らない状態になるので、問いが File Manager から来て File Manager が
  まだ開いているときは、閉じるときに File Manager の矩形を伝え直す。電源などの問いの閉じ方は
  今どおり「無し」。

### keys のデスクトップ (`system_desktop_keys.app.rb`)

- **確認のダイアログの mixin は使わず、電源の問いと同じ下の 1 行 (枠 + 2 行) に出す**。
  電源の描画を `draw_confirm_line(問い)` に切り出し、電源と削除の両方で使う。
  下の行は「Enter:はい Esc:いいえ」(既存の `keys_confirm_help`)。
- 理由:
  - keys には同じ用途の出し方 (電源の問い) がすでにあり、320x170 で読めることも
    確かめ済み。出し方を 1 つにそろえられる。
  - `ConfirmDialogMixin` はマウスのボタン (FmrbUI の部品)、kernel への overlay 通知、
    kernel からの `confirm_dialog` を前提にしていて、keys では部品も通知も要らない。持ち込むと
    合成の領域・ボタンの置き場を keys 用に足すことになり、使わないものが増える。
  - 200x80 のダイアログは 320x170 に収まるが、ボタンを押す手段が無いので見た目が誤解を招く。
- キー: Enter で消す、Esc で消さない。ほかのキーは問いの間は捨てる (mouse のダイアログと
  同じく、後ろの File Manager を動かさない)。電源の問いは今どおり「Enter 以外は全部いいえ」
  のまま変えていない。
- 長い名前は mouse のダイアログと同じ `FmrbI18n.truncate_to` で行の幅に切る (電源の問いも
  同じ関数を通るようになったが、短いので見た目は変わらない)。
- **Esc の長押し**: host_task は Esc を離すか長押しの時間 (1000 ms) まで止めておき、短ければ
  離したときに押し・離しとして渡す (= 問いへの いいえ)。長押しは `keys_esc_long` で
  デスクトップの `esc_held` に来る。ここで問いが出ていれば、問いだけを閉じる (いいえ と同じ。
  電源の問いと同じ扱い)。もう一度長押しすると今どおり File Manager が閉じてメニューへ戻る。

### 標準の構成のデスクトップのエンジン

標準 (`rake native:core`) でもデスクトップは mruby の VM で動いている
(`[system_desktop] mruby VM created successfully`)。全部 mruby (`NATIVE_ENGINE=mruby`) と
合わせて両方で試した。Spinel 向けの注意 (末尾の `nil` で戻り値を固定、mixin の中で
bare 定数を使わない) は守ってある。

## 2. 受け入れ条件

確認は `HOME` を一時ディレクトリ (scratchpad の下) にし、`--user-dir`・`--cache-dir` もその
下、`--set system.power_log_only=true`。一時の HOME に aaa.txt・bbb.txt・bbb2.txt・長い名前の
ファイル・空のディレクトリ・中身のあるディレクトリを置いて、File Manager で `/mnt/host` を
開いて操作した (`tools/fmrb_native.rb up/input/shot/log`)。結果はファイルの有無 (ls) とログで見た。

| 条件 | 結果 |
|---|---|
| 1. `--machine sdl` | Delete キー → 「Delete aaa.txt?」→ Esc で残る → Delete → Enter で消える。右クリックの Delete → No (クリック) で残る、Esc で残る、Enter で消える、Yes (クリック) で消える。ダイアログの外 (File Manager の中) のクリックでも閉じて残る。答えた後に一覧の行をクリックすると選べる (overlay が戻っている)。標準・全 mruby とも |
| 2. `--machine sdl_keys` | Delete キー → 下の行に「Delete aaa.txt?」「Enter:yes Esc:no」→ Esc で残る (File Manager は開いたまま) → Delete → Enter で消える。問いの間の Esc 長押し (keydown 1.5 秒) で問いだけ閉じて残り、次の長押しで今どおりメニューへ戻る。標準・全 mruby とも |
| 3. 両エンジン | 1・2 を `rake native:core` と `rake native:core NATIVE_ENGINE=mruby` の両方で同じ台本で流し、同じ結果 |
| 4. `/mnt/root` | 確認の後に今どおり失敗: `file_posix: ...: read-only (Linux's files under /mnt/root are read-only)` → `File manager: delete failed: cannot delete /mnt/root/tmp/...`。ファイルは残る (自分で /tmp に置いた試しのファイルで、終わってから消した)。標準・全 mruby とも。確認の前に断る形にはしなかった (下の 4 章) |
| 5. regress | `rake regress`: host 19/19、描画の記録 205/205。`rake regress:screens`: 29/29 (retro 8・modern 10・web 6・native 2・native-keys 3)。基準の更新は無し。`rake regress:size`: 内蔵 RAM は S3・Tab5 とも +0 |

ほかに確かめたこと:

- ディレクトリ: 空でも確認は出て、はい の後は今どおり `delete failed: cannot delete /mnt/host/ccc_dir` (消せない動きは変えていない)。
- 長い名前: mouse は「Delete a_very_long_file_name..」(184 px で切れ、`?` も切れる。今の切り詰めの決まりどおり)。keys は「Delete a_very_long_file_name_for_the_confirm_d..」。
- language = ja (一時の user dir の system_conf.toml を書き換え): 「aaa.txt を削除しますか?」が出る。

### 内蔵 RAM と flash

`rake regress:size` (基準 6f7b5848)。L7b の確認のダイアログのキーの後 (L7b 報告の 6 章の数字)
からの差。

| 端末 | 内蔵 RAM (DIRAM・IRAM・other) | app image (flash) |
|---|---|---|
| NARYAv3 (S3) | +0 | 4,537,792 → 4,539,472 (+1,680) |
| TAB5 | +0 | 6,719,504 → 6,721,184 (+1,680) |

build/ は道具が退避して戻した (終わった後も NARYAv4)。

## 3. コミット

- 7f809127 desktop: the file manager asks before it deletes
- 本報告のコミット (doc)

## 4. 見立てと違った点・判断・残件

- **/mnt/root を確認の前に断らなかった理由**: 読み出し専用かを Ruby から前もって知る口
  (`fmrb_hal_file_path_is_read_only()` は C の公開関数で、Ruby には出ていない) を使うには
  kernel か C を触る必要があり、範囲の外。パスの文字列で `/mnt/root` を決め打ちすると、
  HAL の決まり (配布物の木なども読み出し専用) と二重になる。今どおり HAL が断ってログに出る。
  なお失敗は画面には出ない (ログだけ) のも前からの動き。
- **撤回した見立て**: 最初の手試しで、問いを外 (デスクトップの上、File Manager の外) を
  クリックして閉じたら File Manager まで閉じたので、問いの閉じ方の不具合を疑った。
  File Manager の中 (問いの外) を押したときは問いだけが閉じたので、外のクリックは
  「閉じる合図 (0,0)」で問いを閉じた後、同じクリックがデスクトップにも届いて File Manager の
  外として扱われた、と見ている (File Manager の外を押すと閉じるのは今どおりの動き。kernel の
  経路は読んで確かめてはいない)。消えはしないので変えていない。調べる途中で、問いを閉じた後に kernel の
  overlay が「無し」になる食い違いに気づき、File Manager へ戻すようにした (1 章)。
- **気づき (変えていない)**:
  - 前からの食い違い: File Manager の上でファイルの選択 (file selector) やほかの
    ダイアログが開いて閉じたときも、overlay は「無し」に戻る。今回は削除の問いだけ直した。
  - keys の問いの枠は下の 30 px に重なり、一覧の最後の 2 行ほどが隠れる (電源の問いと同じ
    置き方)。
  - 削除の後は一覧が読み直され、選択の強調は出ない (前と同じ)。
  - 消した後に失敗を知らせる表示は無い (ログだけ)。必要なら error_dialog に出す案がある
    (仕様の判断なので変えていない)。
- **残件**: 実機 (Tab5・S3) の File Manager でも同じ問いが出ることの目視 (コードは同じ
  mixin なので動きは sim と同じはずだが、未確認)。
