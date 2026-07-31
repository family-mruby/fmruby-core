# Phase 2: fmrb_app 統合 (.py の起動・停止・排他)

## 目的

.py ファイルを launcher / shell から通常のアプリとして起動できるようにする。
描画バインディングはまだ無いので、print と計算だけのスクリプトが
起動・実行・終了・強制停止できることがゴール。

## 作業項目

1. **VM 種別の追加**: components/fmrb_common/include/fmrb_app.h
   - enum fmrb_vm_type_t に FMRB_VM_TYPE_MICROPYTHON を追加する。
     **FMRB_VM_TYPE_NATIVE の後、FMRB_VM_TYPE_MAX の直前に置く**
     (既存の番号を変えない。番号は下記 3 箇所の対応表に影響する)。
   - タスクコンテキストの union に MicroPython 用フィールドを追加。
     実体はグローバル状態なのでポインタは不要だが、所有の印として
     `bool mp_active` 相当を持たせ、他 VM と同じ null チェックの形を保つ。

2. **番号対応表の追従** (enum に足しただけでは壊れる箇所):
   - main/kernel/fmrb_spx_kernel.c の vm_type -> 数値変換 (buf[2] を埋める
     switch) に case を追加。
   - main/prebuild_scripts/spinel/fmrb_kernel_base_spinel.rb の
     APP_INFO_VM_TYPES 配列に "micropython" を追加。
   - 他に FMRB_VM_TYPE_ を switch している箇所を grep で洗い、default に
     落ちて誤動作するものが無いか確認する (タスク一覧のメモリ統計
     fmrb_app.c 内の switch は Lua と同じ処理を追加)。

3. **拡張子判定**: main/app/fmrb_app_spawner.c の拡張子判定
   (".lua" / ".rb" / ".bas" の並び) に ".py" を追加。

4. **fmrb_app.c の 4 系統に case 追加** (Lua の case を手本に):
   - create_vm_micropython: fmrb_mp_acquire + fmrb_mp_start。
     acquire 失敗 (既に Python アプリが動作中) はエラーログを出して
     タスクを正常系の cleanup へ抜けさせる。ユーザから見え方が
     「何も起きない」だけにならないよう、既存のアプリ起動失敗の通知経路が
     あればそれに乗せる (無ければログのみで可、README の制約に明記済み)。
   - execute_micropython_script: ファイル読み込みは execute_lua_script が
     使っている読み込み処理と同じ形にし、fmrb_mp_exec に渡す。
   - destroy_vm と、停止経路 (fmrb_app.c 内のもう一つの VM close switch) に
     fmrb_mp_close を追加。
   - タスク一覧のメモリ統計 switch に Lua と同一の処理を追加。

5. **強制停止**: ctx->should_exit による停止を実装する。
   - MICROPY_VM_HOOK_LOOP (VM のバイトコードループから定期的に呼ばれる
     フック) で ctx->should_exit を見て、立っていたら実行を中断して
     mp_embed_exec_str から戻す。中断手段は次の 2 案を試して確定する:
     a. MICROPY_ENABLE_VM_ABORT の mp_sched_vm_abort() を フック内から呼ぶ
     b. フックから SystemExit 相当の例外を raise する
     確定した方式と理由をこのファイルに追記する。
   - `time.sleep` 相当をまだ提供しないので、このフェーズの検証スクリプトは
     ビジーループでよい (フックはバイトコード実行中なら必ず回る)。

6. **タスクスタック量の確認**: .py の parse/compile は C スタック再帰を使う。
   spawn 時の stack_words (Lua アプリと同じ値のはず) で足りるかを
   stack_high_water で確認し、不足なら MICROPYTHON の場合のみ増やす。

## 検証手順 (headless)

検証用スクリプトを flash/app/demo/pytest.app.py (仮) として置く:
print 数回 + 長いループ + 最後に print、という内容。

1. rake build:linux 後、tools/dev_run_check.sh --keep で起動。
2. ランチャーまたは shell から pytest.app.py を起動し、docker ログで
   print 出力と正常終了 (タスクの cleanup ログ) を確認。
3. 実行中にもう 1 本 .py を起動し、拒否ログを確認。1 本目の終了後は
   再び起動できることを確認 (排他の解放漏れ検出)。
4. 長いループ実行中にアプリを kill (タスクモニタまたはウィンドウ側の
   既存の停止経路) し、タスクが正常に回収されることを確認。
5. 起動と終了を 5 回以上繰り返し、プールのメモリ統計 (タスク一覧) で
   リークがないことを確認。

## 完了条件

- 上記検証 1-5 がすべて通る。
- .rb / .lua / .bas アプリの起動に退行がない (lua デモアプリの起動を確認)。
- phase1 の自己診断コード (FMRB_MP_SELFTEST) を削除済み。
- 停止方式 (5 の a/b どちらか) と実測スタック使用量をこのファイルに追記済み。
