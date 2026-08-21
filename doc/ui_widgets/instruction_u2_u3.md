# 実装指示書 U2 / U3: midi_apu と Spinel、monitor と大きさ変更

対象: 実装担当セッション。作業リポジトリ: fmruby-core。plan.md と
instruction_u1.md、report/u1.md を先に読むこと。**U1 の T3 がユーザに
承認されるまで着手しない**。進め方の約束と書き方の規則は
instruction_u1.md と同じ。report は report/u2.md、report/u3.md。

## U2: midi_apu と Spinel

### T1 (独立コミット): Spinel 基底の `_send_audio_note`

`FMRB_APP_ENGINE_DESKTOP=spinel` のビルドが develop で落ちている:

```
system_desktop_combined.rb: undefined method '_send_audio_note'
  for an instance of SystemDesktopApp (NoMethodError)
```

`lib/add/picoruby-fmrb-app/mrblib/fmrb-audio.rb` が
`@app._send_audio_note(on, ch, freq, vol, duty, sweep)` を呼ぶが、
`main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb` の FmrbApp に無い。
`respond_to?` の分岐は Spinel では静的に解決され、呼び出しが残る。

- Spinel 基底の FmrbApp に `_send_audio_note` を足す。mruby 側
  (`lib/add/picoruby-fmrb-app/ports/esp32/app.c` の
  `mrb_fmrb_app_send_audio_note` → `fmrb_app_build_audio_note_msg`) と同じ
  バイト列をカーネルへ送る。FFI の追加が要れば `fmrb_app_ffi.rb` と
  `main/app/fmrb_spx_app.c` に対で足す (既存の `fmrb_spx_*` の流儀に従う)。
- 確認: `FMRB_APP_ENGINE_DESKTOP=spinel rake build:linux` が通り、sim で
  デスクトップのクリック音 (あれば) か、Spinel 化された音を出すアプリが
  鳴る。`tools/fmrb_audio_probe.rb` で波形が出れば良い。
- これは FmrbUI と無関係の修正なので単独でコミットする (ユーザ確認)。

### T2: midi_apu の書き換え

`flash/app/demo/midi_apu.app.rb` の `BUTTONS` / `button_rect` /
`button_at` / `draw` のボタン部分を FmrbUI に置き換える。

- 7 行のボタン列: Scale / Chord / Drums は Button、BGM / SMF / Fast / Out は
  Toggle (Out は「serial に切替」の表示を `on_text` で)。mode 表示の排他は
  group で。
- キー操作 (1-7) は従来どおり `on_event` の `:key_down` で受け、
  `@ui.set_on(id, ...)` で表示を合わせてから同じ処理に流す。
- 上部の状態文字列 (`@status`) は Label にして `set_text` で更新する。
  1 秒ごとの再描画で `@status` が同じなら何も描かれないこと。
- `gc_line` のログはそのまま残す (このアプリは GC 計測の実例でもある)。
- 確認: sim で 7 ボタンとキーの両方。`fmrb_audio_probe.rb` で音が出る。
  midi_apu のログの `[gc live=...]` が、放置中に増えないこと。

### T3: Spinel でコンパイルを通す

- `tool/spinel/gen_app_combined.rb` の `system_desktop` と `editor` の
  `libs` に `fmrb-ui.rb` を足す。使っていなくても取り込まれるので、
  構文・型推論の穴がここで出る。
- `FMRB_KERNEL_ENGINE=spinel FMRB_APP_ENGINE_DESKTOP=spinel
  FMRB_APP_ENGINE_EDITOR=spinel rake build:linux` が通ること。
- `rake spinel:doctor` の fmrb-ui.rb に対する指摘を 0 にする。
- Spinel で引っかかった書き方は、直した上で
  doc/spinel_aot/ruby_writing_constraints.md の B 表に追記する (恒久制約か
  fork の弱点かを分類して)。
- 実際に Spinel で動かす確認は U3 以降 (Spinel 化されたアプリで FmrbUI を
  使うものがまだ無い)。report にその旨を書く。

### 受け入れ条件 (U2)

- midi_apu が FmrbUI で動き、`button_rect` / `button_at` が残っていない。
- 上記 3 構成のビルドが通る。
- mruby 全構成と Spinel カーネル構成の sim 回帰。

## U3: monitor と大きさ変更、仕上げ

### T1: monitor の Tasks ページ

`main/prebuild_scripts/default_app/monitor.app.rb` の 3 ページ目 (`[X]` と
二段クリック) を FmrbUI に乗せる。

- 行数は可変だが**部品は作り直さない**。`FMRB_MAX_APPS` 分の Button を
  `on_create` で作り、`set_visible` で出し入れする。1 秒ごとの再描画で
  一覧に変化が無ければ present されないこと (presents/s が 0 になる)。
- 二段クリック (`[X]` → `[?]` → 送信) は `set_text` で文字を変え、アプリ側が
  `@kill_armed_pid` を持つ従来の形を保つ。
- ページ切替時は他ページの部品を `set_visible(false)`。ページごとに別の
  FmrbUI を持ってもよい (flush するのは表示中のものだけ)。
- 1 ページ目・2 ページ目 (棒グラフ・折れ線) は部品化しない。

### T2: 大きさ変更

- kamon を一時的に `resizable = true` にし (toml、コミットしない)、
  `on_resize` で `@ui.move` を各部品に回して右側の列を追従させ、
  `invalidate_all` → `draw_kamon` → `flush` で崩れないことを確認する。
  feedback: resizable アプリは毎 redraw で `draw_window_frame` を呼ぶ。
- この手順を plan.md の「使い方」に 1 例として足す。

### T3: 文書と型支援

- `sig/fmrb_ui.rbs` を最終 API に合わせる。editor で `@ui.` の補完に
  メソッドが出ること (sim で確認)。
- ルートの `.claude/skills/fmrb-app-new` (アプリの書き方の skill) に
  FmrbUI の節を足す: いつ使うか、使い方の最小例、禁止事項 (毎フレーム
  描かない、ブロックを渡さない)。
- plan.md を確定結果で更新し、段 2 の候補 (Slider、キーボード焦点、
  自動レイアウト、デスクトップのダイアログ移植) を「やらないこと」から
  「次の候補」に移す。

### 受け入れ条件 (U3)

- monitor の Tasks ページが FmrbUI で動き、無変化時に present が出ない。
- kamon の大きさ変更で部品が追従する (一時変更は戻す)。
- rbs と skill が更新されている。
- mruby 全構成と Spinel カーネル構成の sim 回帰、Tab5 で monitor から
  kill できる (U1 の機能の回帰)。
