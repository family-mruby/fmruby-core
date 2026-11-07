# 報告: 単一アプリ起動でも boot complete を出す

> 状態: 完了 (検収待ち) | 更新: 2026-11-07 | boot_mode = "app" (StickS3・AtomS3R) と setup (BLE の設定待ち) では誰も status_led_set_boot_complete() を呼ばず、「Boot complete」がログに出ず LED が起動中の速い点滅のままだった。kernel が自分で印を付けるようにした (単一アプリは startup_app の spawn が通った時点、setup は設定画面を出した時点)。desktop の経路は変えていない。StickS3 の起動の基準を 3 回の較正 (全部 clean) から作った

## 1. 結果の要約

- 原因: boot complete を付けるのは desktop だけ (起動アニメーションの終わりに
  `FmrbKernel.boot_complete!`)。desktop の無い起動には呼ぶ所が無かった。
  そのため `rake regress:boot:calibrate BOARD=sticks3` が毎回
  「NOT CLEAN ... boot complete false」になっていた。
- 直し方: kernel (main/prebuild_scripts/kernel/fmrb_kernel.rb) で
  - `spawn_single_app`: spawn が通ったあと (`after_spawn` の後) に
    `FmrbKernel.boot_complete!`。再起動 (respawn) でも呼ぶが、C 側は 2 回目
    以降は何もしない (ログも 1 回だけ)。spawn が一度も通らず諦めた場合は
    起動中の点滅のまま (STOPPED の画面が出る)。
  - setup: `_show_setup` の後に同じ呼び出し。起動はそこで終わり、端末は
    利用者の操作を待つ状態なので、desktop の「使える状態になった」と同じ扱い。
- 2 つのエンジンで同じ書き方にした。mruby の kernel にはもともと
  `FmrbKernel.boot_complete!` (クラスメソッド、kernel.c) がある。Spinel の
  kernel には無かったので、fmrb_kernel_base_spinel.rb に同名のクラス
  メソッドを足し、kernel の FFI `fmrb_spx_boot_complete`
  (fmrb_spx_kernel.c、`status_led_set_boot_complete()` を呼ぶだけ) を通す。

## 2. 確認

- `rake regress`: host 21/21、描画の記録 205/205 通過。
- `rake regress:size ALL=1`: 5 端末とも ok。内蔵 RAM (DIRAM) は基準より
  NARYAv3 -48、STICKS3 -72、ATOMS3R -64 (増えていない。差は基準が古いぶん)。
  app image は S3 で +224〜+304 B。基準は更新していない。
- 実機 (StickS3、app の区画だけ焼いた。/home はそのまま):
  - `rake regress:boot BOARD=sticks3`: `Single-app boot: /app/test/step_sensor.app.rb`
    の直後に `status_led: Boot complete: switching to heartbeat` が出た。
    crash 0。基準が無いので FAIL no baseline (想定どおり)。
  - `rake regress:boot:calibrate BOARD=sticks3 RUNS=3`: 3 回とも clean。
    揺れは M1 internal 68 B、IRAM free 200 B、stack (fmrb_host) 992 B。
    `:apply` で test/regress/boot_baseline.json に STICKS3 を足した
    (許容幅 m1_internal 512、iram_free 512、stack_free 2,048 ほか)。
  - 焼かずにもう一度 `rake regress:boot BOARD=sticks3 FLASH=0`: ok。
- sim: desktop の経路は触っていないので流していない。

## 3. 残件

- AtomS3R は実機が無いので未確認 (同じ kernel の経路を通る)。基準も未作成。
- setup の起動は実機で通していない (setup に入れると設定のやり直しになるため)。
