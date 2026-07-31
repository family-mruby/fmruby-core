# Phase 2 実装レポート

## 完了条件の判定

| 条件 | 判定 | 検証内容 |
|---|---|---|
| 検証 1-5 がすべて通る | OK | 下記 |
| .rb / .lua / .bas に退行がない | OK | 下記 |
| phase1 の自己診断 (FMRB_MP_SELFTEST) を削除済み | OK | fmrb_mp.c / fmrb_mp.h / CMakeLists.txt / fmrb_kernel.c から除去 |
| 停止方式と実測スタック使用量を記録 | OK | 下記 |

### 検証 1-5 (headless, Linux シミュレーション)

起動経路はシェルの `run /app/demo/pytest.app.py`。ランチャーはまだ .py を
知らない (launcher.rb の SCRIPT_EXTS は phase3 で足す) ので、この時点では
一覧に出ない。

| 項目 | 結果 |
|---|---|
| 1. 起動 | `Detected Python script` -> `MicroPython VM created with mempool=5` |
| 2. print と正常終了 | `pytest: start` から `pytest: end` まで出力、`Task exiting normally` -> `Closing MicroPython VM` -> `MicroPython runtime closed` |
| 3. 2 本目の拒否 | `Python app 'Python test' is already running; only one at a time` -> エラーダイアログ表示。1 本目は中断されずに完走し、その後は再び起動できる |
| 4. 実行中の停止 | 長ループの chunk 20 付近で `Close button on micropython app` -> `Python script stopped on request` -> `Task exiting normally` |
| 5. 起動終了の繰り返し | 11 回 (6 回 + 5 回) 連続で成功。gc の total/used/free は毎回同一。Monitor のプール表示にも増加傾向なし |

検証 3 のダイアログ表示:

```
Error: Python test
Failed to launch. / Another Python app is already running.
Only one Python app can run at a time.
```

### 退行確認

| アプリ | 結果 |
|---|---|
| `/app/demo/lua.app.lua` | ウィンドウに "Hello from Lua! / Lua 5.4 / Running: 17s" を描画。閉じるボタンで `Lua script closed by user` |
| `/app/demo/shapes.app.rb` | 図形デモが正常に描画 |
| `/app/demo/basic.app.bas` | `Loaded 55 program lines` -> BASIC 画面キャンバス生成 -> 正常動作 |

## 確定した停止方式: (a) mp_sched_vm_abort

計画の 2 案のうち **(a) `MICROPY_ENABLE_VM_ABORT` の `mp_sched_vm_abort()`** を
採用した。理由:

1. **ゲストコードが握り潰せない**。(b) の例外方式だと Python 側の
   `except:` が停止要求を飲み込んでしまう。VM abort は Python の例外機構を
   通らず、`fmrb_mp_exec` が `nlr_set_abort()` で登録した nlr バッファへ
   直接巻き戻る。
2. **停止時に何も確保しなくてよい**。例外オブジェクトの生成は GC ヒープを
   要求するが、アプリが止まらなくなる状況はヒープが詰まっているときこそ
   起きやすい。止めたいときに止められないのでは意味がない。
3. 本家が「トップレベルまで非同期に中断する」ために用意した仕組みそのもの。

実装上の注意:

- `mp_handle_pending` は `nlr_get_abort() != NULL` のときしか
  `nlr_jump_abort()` しない。`fmrb_mp_exec` で `nlr_set_abort(&nlr)` を
  呼ばないと、abort フラグが黙って消費されてループが続く。
- 巻き戻ってきたときの `nlr.ret_val` は **NULL**。例外との区別はこれで行う
  (NULL を例外オブジェクトとして扱うと落ちる)。
- 抜けるときは成功・失敗どちらの経路でも `nlr_set_abort(NULL)` する。
  死んだスタックフレームへ飛ぶ abort が残るため。

### フックの間引き

`MICROPY_VM_HOOK_LOOP` は VM がジャンプするたびに呼ばれる。毎回
メッセージキューを浚うのは重すぎるので、100 回に 1 回だけ
`fmrb_app_poll_exit_signal` を呼ぶ (Lua の `lua_sethook(..., LUA_MASKCOUNT, 100)`
と同じ考え方・同じ桁)。`should_exit` が立った後は
`fmrb_app_poll_exit_signal` が即 true を返すので追加コストは無い。

## 実測値

### タスクスタック

| 項目 | 値 (Linux シミュレーション) |
|---|---|
| start 時点の空き | 123,752 B |
| 設定したスタック上限 | 121,704 B (空き - 予備 2,048 B) |
| 終了時の最小空き (low water) | 122,376 B |
| ピーク使用量 | 約 1,376 B |

**この数字で ESP32 の可否は判断できない**。ESP-IDF の Linux ターゲットは
FreeRTOS を POSIX スレッドで模擬するため、`FMRB_USER_APP_TASK_STACK_SIZE`
(16KB) の指定は効かず 120KB 超のスタックが割り当てられる。Xtensa / RISC-V での
再計測は phase4 の作業項目 4 で行う。

計測しやすいよう、`fmrb_mp_close` にタスクスタックの low water を出す
ログを常設した (強制 kill 経路では呼び出し元タスクの値になってしまうので、
アプリ自身が閉じるときだけ数値を出す)。

### GC ヒープ

phase1 と同一 (total 259,968 / used 192 / free 259,776)。11 回の起動終了で
値が動かないことを確認した。

## 実装中の気づき

### 1. `APP_INFO_VM_TYPES` は飾りで、実マッピングは三項演算子だった

計画の作業項目 2 は
`main/prebuild_scripts/spinel/fmrb_kernel_base_spinel.rb` の
`APP_INFO_VM_TYPES` 配列に "micropython" を足すよう指示していたが、
**この定数はどこからも参照されていない**。実際の変換は隣の
`_get_app_info` にある三項演算子の連鎖で、そちらを直さないと
vm_type が `:unknown` になる。

配列だけ直した状態で検証したところ、停止自体は効く
(`vm_type != :mruby` の判定は通る) が、ログが
`Close button on unknown app` になって気付いた。

最終形は三項演算子を捨てて配列をシンボルで引く形に整理されている
(`APP_INFO_VM_TYPES[vm_idx]`、範囲外は `:unknown`)。表が一つになったので
次に VM 種別が増えたときは配列だけ直せばよい。

### 2. アプリプールの TLSF ハンドル条件 (phase1 からの申し送り、対応済み)

`fmrb_app.c` の spawn は `vm_type` が LUA / BASIC のときだけ
`fmrb_mem_create_handle` を呼んでいた。ここに MICROPYTHON を足さないと
`ctx->mem_handle` が -1 のままで GC ヒープの確保に失敗する。

### 3. 強制 kill 経路でも排他は解放される

`fmrb_app_kill` がアプリの応答を待ちきれなかったときに通る
`force_release_resources` は `destroy_vm(ctx)` を呼ぶので、そこに足した
MICROPYTHON の case が `fmrb_mp_close` を実行し、単一インスタンスの所有権が
戻る。ここを通らないと「以後どの Python アプリも起動できない」状態が
残るところだった。ただしこの経路の `fmrb_mp_close` は**別タスクから**
呼ばれる点に注意 (だからスタック計測ログを条件付きにしている)。

### 4. 起動失敗の通知経路は既にある

`set_last_error()` + `notify_error_to_kernel()` で
`{"cmd": "app_error"}` をカーネルに送ると、カーネルがデスクトップに
`show_error` を転送してエラーダイアログが出る。mruby の例外報告に使われて
いる経路で、mrb 引数に NULL を渡せば mruby 以外からも使える。
2 本目の起動拒否をこれに乗せた。

### 5. VM 種別の追加で追従が要った箇所 (最終)

| 箇所 | 内容 |
|---|---|
| `components/fmrb_common/include/fmrb_app.h` | enum に追加 (NATIVE の後、MAX の直前)、union に `mp_active` |
| `main/app/fmrb_app.c` | create / execute / destroy / spawn unwind / メモリ統計 の 5 つの switch + TLSF ハンドル生成条件 |
| `main/app/fmrb_app_spawner.c` | `.py` の拡張子判定 |
| `main/kernel/fmrb_spx_kernel.c` | app info スナップショットの byte 2 (micropython=5) |
| `main/prebuild_scripts/spinel/fmrb_kernel_base_spinel.rb` | `_get_app_info` の三項演算子 (+ 参考表) |
| `lib/add/picoruby-fmrb-kernel/ports/esp32/kernel.c` | mruby カーネル側の vm_type シンボル |
| `main/prebuild_scripts/kernel/system_desktop/taskbar.rb` | `TASKBAR_COLORS` に index 4 |

追従漏れが無いことは `FMRB_VM_TYPE_` の全出現箇所を洗って確認した。
残る 3 箇所は追加不要:
`main/kernel/fmrb_kernel.c` (カーネル自身の spawn 属性)、
`main/drivers/debug/fmrb_debug_ctx.c` と
`main/prebuild_scripts/default_app/editor.app.rb`
(リモートデバッガを mruby アプリに限定しているのは意図どおりで、
.py は `FMRB_ERR_NOT_SUPPORTED` になる)。

### 6. `system_desktop_combined.rb` は生成物だった [phase3 への申し送り]

phase3 の計画は「`main/prebuild_scripts/kernel/mrb/system_desktop_combined.rb`
に同じコードが結合された形で存在するので、再生成手段が無ければ両方を
同じ内容に編集する」としているが、**この結合ファイルはビルド時に
`tool/debug/gen_combined_rb.py` が分割ソースから生成する**もので、
`.gitignore` にも入っている (`main/prebuild_scripts/kernel/mrb/*`)。

つまり**編集するのは分割ソース側だけでよい**。今回 taskbar.rb を直しただけで
結合ファイルにも反映されることを確認済み。Spinel 構成のデスクトップを
使う場合に `rake spinel:gen` が要る点は計画のとおり
(`rake build:linux` が自動で実行する)。

## 環境まわり (実装内容とは無関係)

- `dev_run_check.sh` が sdl2-display を起こしそこねると、core が
  `waiting host timeout!` で起動に失敗する。`docker compose ps` に
  3 コンテナ揃っているかを見て、欠けていたら `down` からやり直す。
- デスクトップのドロップダウンは `DROPDOWN_Y = 13` / `DROPDOWN_ITEM_H = 12`
  なので、n 番目の項目の中心は y = 13 + 12n + 6。Monitor (index 3) は y=55 付近。
  目分量だと隣を掴む。
- メニューを開く click と項目を選ぶ click は**同じ注入コマンドにまとめる**。
  別々に実行するとメニューが閉じてしまう。
- アプリを起動すると入力フォーカスがそちらに移る。シェルから続けて打つには
  先にシェルのウィンドウかタスクバーのアイコンをクリックして戻す。
