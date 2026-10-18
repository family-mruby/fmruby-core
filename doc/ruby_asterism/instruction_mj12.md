# MJ12 指示書: BGM だけの音量、麻雀の BGM を 切/小/中/大

> 状態: 指示 | 更新: 2026-10-19 | 麻雀の BGM (NSF を APU の MAIN で鳴らす) が効果音 (SUB) と同じ大きさで混ざり、大きすぎる。アプリから変えられるのは端末全体の音量だけ。BGM (MAIN 側) だけを下げる口を作り、麻雀の BGM ボタンを 切/小/中/大 にする

前提: fmruby-core の音声処理 (`main/drivers/audio_p4/audio_p4_task.c`: NSF・FMSQ を MAIN、note_on を SUB で鳴らし、`apuif_process_mix` で 2 つを混ぜ、WAV を足し、出力段で端末全体の音量 `s_out_gain_q16`)、`apuif_process_mix` の実体 (どのコンポーネントか読んで確かめる)、アプリの口 `FmrbAudio` (`lib/add/picoruby-fmrb-app/mrblib/fmrb-audio.rb`、`sig/fmrb_audio.rbs`、`flash/help/FmrbAudio.*.md`、kernel への `MSG_TYPE_APP_AUDIO` の命令 → 音声のタスク)、ブラウザ版 (wasm) は同じ C を使う、Retro (S3 + WROVER) は fmruby-graphics-audio の音声のタスク (`main/tasks/audio_task.c` など) で同じ 2 つの APU を混ぜる。`doc/reference/audio_output.md`。麻雀アプリの音 `flash/app/game/mahjong/mahjong_sound.rb` (MJ11: BGM の入り切り `bgm = true|false` を `/home/mahjong.toml` に保存、ボタン「BGM 入/切[B]」)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。画面の言葉は日本語。

## 1. 決まっていること (2026-10-19 ユーザ、選択形式)

- **BGM だけ下げる口**: APU の MAIN 側 (NSF と MAIN の FMSQ) に掛ける音量を足し、アプリから設定できるようにする。SUB 側 (効果音・note_on) と WAV、端末全体の音量はそのまま。
- **麻雀の BGM を 切/小/中/大**: 待ち合わせ (と卓の一覧) の「BGM」ボタンを押すたびに 切→小→中→大→切 と巡回 (B キーも同じ)。**既定は「小」**。`/home/mahjong.toml` に記憶。今の `bgm = true` は「小」、`bgm = false` は「切」として読む (前の保存を壊さない)。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- 口の形: 例 `FmrbAudio#main_volume=(0..100)` (百分率、既定 100 = 今と同じ)、またはそれに近い名前。端末全体の値で、アプリを閉じたら既定に戻す (麻雀は閉じるときに戻す、または kernel がアプリの終了で戻す: どちらか安全な方)。
- 段階の値は麻雀アプリの定数 (例 小 30%・中 55%・大 100%)。数は聞いて決める前提で、仮の値でよい (report に書く)。
- **Retro**: fmruby-graphics-audio にも同じ口を入れる (命令が届いて MAIN の音量が変わる)。入れるのが大きければ、Retro では命令を受けて何もしない (落ちない) でもよい。どちらにしたかを report に書く。
- 出力の飽和を起こさない (掛け算は飽和なし、整数の Q16 など、`s_out_gain_q16` の流儀に合わせる)。内蔵 RAM を増やさない (決まり: 内蔵 RAM の増分 0)。

## 3. 作るもの

1. C: MAIN 側の音量 (混ぜる前の MAIN の出力に掛ける、または apuif の混ぜ方に係数)。命令の口 (kernel → 音声のタスク)。Modern・ブラウザ版・(Retro)。
2. Ruby: `FmrbAudio` の口、`sig/fmrb_audio.rbs` (doc コメントの 1 行目は 112 バイトまで: sig/README.md)、`flash/help/FmrbAudio.*.md`。sig を直したら `rake clean` してからビルド (fmruby-core/CLAUDE.md)。
3. 麻雀アプリ: BGM の段階、ボタンの表示 (例「BGM 小[B]」)、保存と読み込み (前の true/false を読む)。はみ出し検査 (`rake mahjong:overflow`) に場面を足し、失敗 0。
4. `doc/reference/audio_output.md` に MAIN 側の音量を一節。

## 4. 受け入れ条件

1. `rake test` (ネイティブの host テスト) と `rake mahjong:overflow` が通る。
2. Linux sim (標準構成。エディタを 1 回起動して 1 打鍵するところまで含める) で、BGM を鳴らしたまま段階を変えると、音の数値 (`ruby tools/fmrb_audio_probe.rb`: ピーク・RMS) が段階どおりに下がり、効果音の大きさは変わらない (同じ効果音を段階ごとに鳴らして比べる)。アプリを閉じると MAIN の音量が既定に戻る (別のアプリの NSF プレーヤが大きさ 100% で鳴る)。
3. ブラウザ版 (`rake wasm:web`) で落ちないこと (音の大きさの数値はブラウザ版で測れれば測る、無理なら sim の結果で代える)。
4. ESP32 のビルド (`rake build:esp32`、ターゲットは `.env` の TAB5) が通る。**焼くのは親** (Tab5 には触らない)。build ディレクトリの切替 (linux ⇔ esp32 ⇔ wasm) には `rake clean_all` の罠があるので fmruby-core/CLAUDE.md と root の CLAUDE.md に従う。最後は esp32 (TAB5) のビルドを残して終える (親がすぐ焼けるように)。
5. Retro (fmruby-graphics-audio) を直したなら、そのビルドが通る。

## 5. 範囲・決まり

- **触ってよいもの**: fmruby-core (音声処理の C、`lib/add/picoruby-fmrb-app`、sig、help、`flash/app/game/mahjong/`、`tools/mahjong_overflow/`、doc)、fmruby-graphics-audio (音声のタスクの命令と MAIN の音量)。develop に直接コミットしてよい (fmruby-graphics-audio は develop か main か、履歴に合わせる)。
- **触らないもの**: 牌のシート `tiles_hand.bmp` / `tiles_river.bmp` (ユーザの手書き)、asterism-*、VPS、GitHub の設定、Tab5 (シリアル・WiFi)。
- push・タグはしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。件名は履歴に合わせる (`audio, game: ...` など)。
- 起こしたもの (sim、ブラウザ) は片付ける (sim_down)。docker の prune はしない。
- **止まる条件**: 1・2 章に無い仕様の選択が要る、内蔵 RAM が増える、Retro の作りが大きく違う。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/ruby_asterism/report/mj12.md`、常体): 口の形、C の変更の場所、段階の値、受け入れ条件の結果 (音の数値)、Retro の扱い、親が焼くときの注意。
