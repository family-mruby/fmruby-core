# Phase B3.5 報告: 実行ループ基盤とエディタ RUN ボタン

対象指示書: `phase_b3_5.md`。検証は Linux シミュレーション (headless、
mruby kernel / mruby desktop、`keyboard_layout = "jp"`) で実施。

## 1. 変更ファイル

| ファイル | 内容 |
|---|---|
| `main/app/fmrb_app.c` | kill を協調終了 -> 強制の 2 段に。強制時の資源解放と kernel 通知を追加。`fmrb_app_note_control_payload()` を切り出し |
| `components/fmrb_common/include/fmrb_app.h` | `fmrb_app_note_control_payload()` 宣言 |
| `components/basic/fmrb_basic.cpp` | tick の HID drain が APP_CONTROL を捨てていた不具合を修正 (下記 3) |
| `main/app/fmrb_app_spawner.c` | user app spawn 時に kernel へ `{"cmd":"spawned"}` を通知 |
| `main/prebuild_scripts/kernel/fmrb_kernel.rb` | spawn 後処理を `after_spawn` に集約。`spawned` / `run` コマンドを追加 |
| `main/prebuild_scripts/kernel/fmrb_kernel/app_lifecycle.rb` | `after_spawn` / `run_path_allowed?` / `reply_run_result` を追加。終了時のキーボード戻し。reload も `after_spawn` 経由に |
| `lib/add/picoruby-fmrb-app/mrblib/fmrb-app.rb` | `FmrbApp#request_run(path, prev_pid)` を追加 |
| `main/prebuild_scripts/default_app/editor.app.rb` | Run メニュー / F5 / Alt-R、`run_current_file`、`run_result` 受信、status バッジ汎用化 |

## 2. T3.5-1: kill の協調終了化

### 状態遷移 before / after

| 段階 | before | after |
|---|---|---|
| 要求 | 即 `STOPPING` に遷移し `task` を null 化 | `should_exit` を立て、APP_CONTROL `{"cmd":"stop"}` を送り、notify で待ちを起こす。**状態は RUNNING のまま** |
| 終了 | `fmrb_task_delete()` で強制削除 | アプリ自身が `app_task_main` の cleanup を通り `STOPPING` へ。kill は最大 1000ms (10ms 間隔) 待つ |
| 資源 | **リーク** (mem handle / semaphore / canvas / msg queue) | アプリ自身の cleanup が全部解放。kill は `fmrb_app_reap()` を呼んでタスク削除 + `FREE` |
| 応答なし | (該当なし) | 1000ms 経過で強制削除 + `force_release_resources()` + kernel へ exit 通知 |

`fmrb_app_reap()` / `STOPPING` / `FREE` / `gen` / task ハンドル null 化の意味は
変えていない。kill が reap を呼んだ後に kernel が同じ pid を reap しても、
reap は RUNNING のスロットを拒否するので再利用済みスロットは安全。

### 強制フォールバックの発動条件と残リスク

発動するのは「1000ms 以内にアプリが自分の cleanup を終えなかった」場合のみ。
具体的には (a) tick / on_update に戻らない無限ループ、(b) 応答しない
ブロッキング呼び出し、(c) 優先度の高いタスクに CPU を奪われ続けている場合。

強制時に解放するもの: VM (tick manager からの登録解除)、canvas 一式、
メッセージキュー、hw_proxy 資源、メモリプール handle、semaphore、LARGE プールの
使用フラグ。**回収できないもの**: そのタスクのスクリプトバッファ (タスクの
ローカル変数)、アプリが自前で確保した OS 資源のうち ctx から辿れないもの、
graphics 側に残る sprite image (canvas 破棄でカスケードされる前提)。
つまり強制経路は「以前の kill よりは漏らさない」だけで、無漏洩ではない。
BASIC / Lua / mruby の通常のアプリでは今回の検証で 1 度も発動していない。

既知の副作用と残リスクを 2 点:

- **`fmrb_kernel_stop()` (`fmrb_kernel.c`) は `fmrb_app_kill(PROC_ID_KERNEL)` を
  呼ぶが、kernel は自分宛の `"stop"` を処理しない**ため、毎回 1000ms の猶予を
  使い切ってから強制経路に入る。シャットダウンが最大 1 秒遅くなるだけで、
  解放処理は強制経路が行うので実害は小さい。直すなら kernel のメッセージループに
  自分宛 stop の処理を足すか、この呼び出しだけ猶予を 0 にする。
- **理論上のレース**: kill が `g_ctx_lock` を離してから `request_app_exit()` を
  送るまでの間にスロットが解放・再利用されると、新インスタンスに `"stop"` が
  届く窓がある (`should_exit` も同様)。窓はマイクロ秒オーダーで、その間に
  終了・reap・再 spawn まで進む必要があるため実用上は起こらない。閉じるには
  メッセージ送信を `g_ctx_lock` の内側に入れる (キュー満杯時に最大 10ms
  ロックを保持することになる) か、送信直前に gen を再確認して窓を狭める。
  待機ループと強制経路は gen を照合しているので、この窓以外では取り違えない。

## 3. 途中で見つけた不具合: BASIC が stop メッセージを捨てていた

kill を協調化しても BASIC アプリが止まらなかった。原因は
`fmrb_basic.cpp` の `host_on_tick`:

```c
while (fmrb_msg_receive(ctx->app_id, &msg, 0) == FMRB_OK) {
    if (msg.type != FMRB_MSG_TYPE_HID_EVENT) continue;   // <- ここで捨てていた
```

HID イベントを取るためにキューを drain しており、APP_CONTROL も一緒に
消費して破棄していた。後段の `fmrb_app_poll_exit_signal()` はキューが空の
ため `should_exit` しか見られない。C から直接フラグを立てる kill では
動いていたので、**メッセージ経由の停止要求 (デスクトップの閉じるボタン、
input_router の非 mruby アプリ向け stop、今回の RUN) は BASIC アプリに
一度も届いていなかった**。

修正: 判定を `fmrb_app_note_control_payload()` に切り出し、HID 以外の
メッセージを捨てる前にこれへ渡す。`fmrb_app_poll_exit_signal()` も同じ
関数を使う。

## 4. T3.5-2: canvas 残留

協調終了なら `app_task_main` の cleanup が canvas を削除するので残らない。
強制経路でも `force_release_resources()` が canvas を削除し、さらに kernel へ
exit 通知を送るのでウィンドウリストも更新される。検証: RunTest (windowed) を
kill した直後の画面にキャンバスの残骸はなく、下のエディタとデスクトップが
正しく見えた (`C cleanup: deleting canvas 14` -> 画面クリーン)。

## 5. T3.5-3: spawn 時のキーフォーカス

原因は spawn 経路の非対称性だった。kernel の `spawn` メッセージ処理だけが
window list 更新 / fullscreen 遷移 / `_set_hid_target` を行っており、
debugd のように C API (`fmrb_app_spawn_app`) を直接叩く経路はそれを通らない。

- 後処理を `after_spawn(pid)` に集約し、`spawn` / `run` / reload の全経路が使う
  (reload はこれまで fullscreen 遷移を忘れていた)。
- `spawn_user_app()` が成功時に kernel へ `{"cmd":"spawned","pid":N}` を送り、
  kernel は自分が処理済みの pid (= 現 HID target) 以外について `after_spawn` を実行。
- アプリ終了時にキーボードを宙に浮かせない: Run の親 (エディタ) が生きていれば
  そこへ、いなければデスクトップへ戻す。以前は `0xFF` (誰にも行かない) だった。

検証: debugd から Dodge を spawn し、**クリックせずに** 左キー 8 回を注入 ->
プレイヤースプライトが 152-167px から 130-145px へ移動 (ログにも
`Spawn notification for pid=4 (spawned outside the kernel)` ->
`HID target set to new app pid=4`)。

### B2 でスキップしたキー入力ケース (`.keys`)

`208_screen_inkey` を sim で試した。フォーカスはもう障害ではないが、
**依然として決定的に回せない**。アプリ起動から最初の `INKEY$` 到達までが
10ms 台で、ホストからのキー注入がそれに間に合わない (注入前に INKEY$ が空を
返して `DONE` まで進んでしまう)。回すにはアプリ側が「入力待ちに入った」ことを
ログに出し、ハーネスがそれを待ってから注入する仕組みが必要。B4 以降の課題。

## 6. T3.5-4: エディタ RUN

- メニューバーに `Run` を追加 (File / Edit / Search / **Run** / Hilight / Debug)。
  240px 幅のウィンドウに収まる (実測: ラベル終端 232px < user area 238px)。
- 起動キーは **F5**。デバッグセッション中の F5 は従来どおり Continue なので、
  `@dbg_active` でない時だけ RUN。`Alt-R` も同じ動作 (Linux sim では Alt が
  取れないので実機用)。
- 動作列: 無題なら保存ダイアログ -> 保存 -> `request_run(path, @run_pid)` ->
  kernel が前回インスタンスを停止 -> spawn -> 新アプリへフォーカス。
  結果は `run_result` で戻り、`@run_pid` を更新して status に `Run pid N` を出す。
- app -> kernel の経路は `FmrbApp#request_run` (APP_CONTROL `{"cmd":"run"}`) を
  新設し、kernel 側は既存の spawn / reload 実装に合流させた。前回インスタンスの
  停止は既存の `@pending_reload` 機構をそのまま使う (停止完了の通知で spawn する
  ので、メモリプールが解放されてから次が確保する)。
- パス制限: `/app/` と `/home/` 配下のみ。組み込み名 (`default/editor` 等) は
  対象外。停止できるのも「まだ生きていて、パスが上記配下で、要求元自身でない」
  アプリだけ (スロット再利用で無関係なアプリを止めないため)。
- 拡張子は spawner のディスパッチに任せるので `.rb` / `.bas` 両対応。

### RUN したアプリからエディタへ戻る導線

| RUN したアプリ | 戻り方 |
|---|---|
| windowed (toml で `default_window_mode = "window"`) | エディタのウィンドウをクリックすれば前面 + キーボードが戻る。そのまま F5 で置き換え再実行 |
| fullscreen (`.bas` の既定) | **キーボードからは戻れない**。キーは全部そのアプリに行き、kernel/desktop に予約キーが無い。プログラムが END / エラーで終わるか、外部 (debugd) から止めるしかない。終了時はキーボードがエディタへ戻る |

fullscreen の停止手段が無いのは B3.5 の範囲外だが、体験としては穴なので
B4 の検討事項に挙げる (実機の STOP キー相当を kernel 側で予約する案)。

### E2 デバッガとの関係

エディタの Debug メニュー (Ctrl-D) -> `Attach...` -> ターゲット選択 -> Enter で
spawn 済みアプリに attach できることを確認 (`dbg_ctx: attached pid=5` /
`FM-Editor: Editor debug: attached pid=5`)。新機能は入れていない。

## 7. 検証結果

| 項目 | 結果 |
|---|---|
| INKEY$(0) 待ちの BASIC アプリを debugd から kill -> 同名再 spawn | 3 サイクル成功。毎回 `Destroyed pool handle` -> 同じ `pool_id=4` を再確保。強制発動なし |
| エディタ RUN ループ (F5 -> エディタをクリック -> F5) | **10 サイクル成功**。プール生成 11 / 破棄 10 (11 個目は実行中)、`pool_id=5` と `6` を交互に 512000 バイトで再確保、alloc 失敗 0、強制 kill 0 |
| RUN 直後のキー到達 | `HID target set to new app pid=N` が毎サイクル出る。クリック不要 |
| kill 後の canvas 残留 | なし (上記 4) |
| `rake basic:test` | 86 passed, 0 failed |
| `tools/basic_screen_check.py` | 11 passed, 0 failed |
| ランチャからの通常起動・終了 | Dodge を起動 -> fullscreen + HID target、stop -> cleanup + `HID target back to pid=2` + Reaped |
| E2 attach | 上記 6 のとおり成功 |
| 音声・実機 | 未確認 (headless の範囲外) |

## 8. 受け入れ基準の対応

1. kill -> 再 spawn でプール安定 -> **達成** (10 サイクルで 11/10、同一サイズ、失敗 0)
2. canvas 残留なし -> **達成**
3. spawn 直後にクリックなしでキーが届く -> **達成** (debugd spawn / RUN の両方)
4. Run メニュー / F5 で `.bas` が起動し再 RUN できる -> **達成** (`.rb` は spawner の
   ディスパッチ任せで経路は同一。今回の実測は `.bas` と、attach 用に spawn した `.rb`)
5. 既存回帰 green -> **達成** (golden / screen check / ランチャ / attach)
6. 本レポート -> 本ファイル

## 9. 疑義・申し送り

| # | 内容 | 扱い |
|---|---|---|
| 24 | fullscreen の `.bas` を実行中、キーボードから抜ける手段が無い | B4 で予約キー (STOP 相当) を検討 |
| 25 | `.keys` ゴールデンを sim で回すには「入力待ちに入った」通知が必要 | B4 以降 |
| 26 | `tools/fmrb_input.py` の `text` は US 配列前提だが sim は `keyboard_layout = "jp"`。`"` が `*`、`=` が `^`、`:` が `+` になる | 記号を含むプログラムを注入する検証では回避が必要。ツール側に layout 対応を入れるのが本筋 |
| 27 | エディタのメニューバーをマウス注入で開けなかった (キーボードの Ctrl-D / F5 は動く)。ウィンドウ相対座標の換算が不明 | 検証手順の制約として記録。UI 自体は実機/手動では従来どおり動作 |
| 28 | sim のコンテナは 3 つまとめて再起動しないと framebuffer が繋がらない (core だけ `up -d` すると sdl2-display が SHM を待ち続ける) | 検証手順メモ。`docker compose down` -> `tools/dev_run_check.sh` を使う |
