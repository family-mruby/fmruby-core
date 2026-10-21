# C3b 報告: 実況のページに麻雀アプリの BGM と効果音

> 状態: 完了 (検収待ち) | 更新: 2026-10-21 | instruction_c3b.md の実装と確認の記録。麻雀アプリの効果音・ジングル・場面の BGM を Family mruby の APU エミュレータ (components/apu_emu をホストで組んだもの) で前もってファイルにし、asterism-games 0.21.0 の実況ページで鳴らすようにした。BGM は既定「小」、声の間は半分に下げる。効果音は gem の中で MIT、BGM は CC BY 3.0 のまま。止まる条件には当たらなかった (手番・ツモの音の鳴らし方など、確認してほしい判断は 9 章)

## 1. 結果の要約

- asterism-games main に 1 コミット: `1b13f19 games 0.21.0: the mahjong app's BGM and effects in the commentary page`
  (e717c71 = 0.20.1 の上)。版は 0.21.0。push・タグ・gem push はしていない。
- fmruby-core develop に 1 コミット: `7f8b198e tools: render the mahjong app's sounds with the APU (mahjong_sounds.rb)`
  (道具だけ。この report はコミットしていない)。
- 試験: `MJ_GAMES=4 bundle exec rake test` 405 runs / 0 failures / 4 skips、`bundle exec rake test:gem` 164 runs /
  0 failures / 10 skips。新しい試験 `test/test_commentate_sounds.rb` (12 件、gem の試験にも入れた)。
- 確認: 自分の zenohd・VOICEVOX・ゲームホスト (CPU 4 人の東風戦) に当て、録画用のイメージ `fmrb-rec:local` で 60 秒録った。
  BGM・効果音・声が入っていること、ダッキングで BGM が下がることを数値で確認した (6 章)。
  録画: `/tmp/claude-1000/-home-kishima-fmrb-family-mruby/ac2e25be-21b2-46ea-8f7c-511bdcdbea0d/scratchpad/verify/out/c3b_sound.mp4`
- 外の API は呼んでいない (`--ai off`、`ANTHROPIC_API_KEY` 空)。ユーザの API キー・`~/asterism_keys/`・VPS・ユーザの VOICEVOX
  (127.0.0.1:50021) は使っていない。麻雀アプリ・NSF・牌のシートは読むだけで変えていない。sim は使っていない (fmruby-core の
  build/ は触っていない)。
- 実在の値の検査: 両リポジトリの差分と新しいファイルに IP (127.0.0.1 以外)・ホームのパス・キー・ドメインが無いことを確認して
  からコミットした。gem の依存は増えていない (asterism だけ)。

## 2. 音の作り方

### 2.1 道と理由

**ホストで組んだ APU の道具** を取った (sim + `fmrb_audio_probe.rb --dump` ではない)。

- Tab5 のファームウェアが使うのと同じソース (`components/apu_emu` の `apu_if_p4.c`・`apu_helper.c`・nofrendo の `nes_apu.c`・
  `nes_cpu.c`・`nsf_player.c`) をそのまま gcc でホスト用に組む。ESP-IDF の依存は 2 つの小さな差し替えヘッダ
  (`heap_caps_malloc` → `malloc`、`fmrb_hal_time_get_ms`) だけ。鳴らし方も `audio_p4_task.c` と同じ: 1 秒 60 フレーム、
  フレームごとに NSF の tick (MAIN) → `apuif_process_mix` (MAIN は等倍、SUB を足す)、15720 Hz・1 フレーム 262 サンプル。
  効果音は `audio_p4_engine_note_on/off` と同じ写像 (SUB、パルスは duty と音量、三角波、ノイズは周期と mode ビット)。
- 理由: (1) **決定的**で、作り直すと同じバイト列になる (digest で変化を見られる)。sim の録りはタイミングの揺れと他の音が混ざる。
  (2) 1 回 3 秒で全部できる (sim は両リポジトリのビルドと起動が要る)。(3) ループ点を APU のレジスタ書き込みから厳密に求められる。
  (4) ソースは sim (graphics-audio 側) と Tab5 で同じ nofrendo の APU (差は時刻のヘッダ 1 行だけ)。
- 音符の表は**写さずに読む**: `mahjong_sound.rb` の `SFX` / `BGM` / `BGM_LEVELS` / `BGM_NAMES` / `BGM_DEFAULT` / `TENSE_LEFT`
  を、定数の括弧の範囲を切り出して JSON として読む (`name:` → `"name":`。評価はしない)。並べ方は `snd_begin` と同じ (各声部の
  音符を 0 ms から続けて置き、休符 (-1) は note off、最後の音符の後に note off)。アプリでは音符がフレームの間に届くので、
  その時刻以後の最初のフレームに置いた (1/60 秒単位)。

### 2.2 BGM のループ

- 曲を 720 秒鳴らし、フレームごとの APU レジスタ書き込みのハッシュから「S フレーム目から周期 P で最後まで繰り返す」最短の P
  (4 秒以上、2 周期以上で確認) を探す。3 曲とも見つかった。

| 曲 | 前奏 S | ループ P |
|---|---|---|
| after_the_rain (待ち合わせ) | 0.02 秒 | 153.6 秒 |
| spring_thing (対局中) | 1.92 秒 | 115.2 秒 |
| encounter (緊迫: リーチか残り 10 枚以下) | 0.00 秒 | 128.0 秒 |

- 同じ書き込みでも発振器の位相とノイズの LFSR は周回ごとに違うので、そのまま切ると継ぎ目に段差が出うる (実測で、同じ譜面の
  位置どうしの波形の差が信号と同程度)。そこで継ぎ目を S+0.5〜10.5 秒の範囲で「前後半フレームの波形が 1 周期後と最もよく
  一致するフレーム」に置いた (encounter は無音の位置、spring_thing は差 0、after_the_rain は差が信号の半分ほどに下がる)。
- ファイルは前奏から 1 周 + 1 秒。sounds.json の `loop_start` / `loop_s` でページが Web Audio のループ (`loopStart`/`loopEnd`)
  にする。`loop_start` は S から 0.5 秒以上後なので、デコーダの頭の遅れ (MP3 の priming) が残るブラウザでもループは繰り返し
  部分の中に収まる (区間の長さが P ちょうどなら継ぎ目は変わらない)。

### 2.3 手順 (コマンド 1 つ) と digest

```
cd fmruby-core
ruby tools/mahjong_sounds.rb ../asterism-games/lib/asterism_games/commentate/assets/sounds          # 作る
ruby tools/mahjong_sounds.rb --check ../asterism-games/lib/asterism_games/commentate/assets/sounds  # 作り直して比べる
```

- 要るもの: gcc、ffmpeg (libmp3lame)。WAV は Ruby が書く (ffmpeg のメタデータが入らない)。MP3 は `-fflags +bitexact`、ID3 なし。
- `sounds.json` に各ファイルの SHA-256 と大きさ、作った元の digest (`mahjong_sound.rb` の SFX の文字列、NSF 3 つ)、ループ点、
  アプリの段階と場面を書く。`--check` は別の場所に作り、各ファイルに same / CHANGED / new / GONE を付け、全部同じなら 0 で終わる。
  2 回作って同じになることを確認した (`the files are the same`)。

## 3. ファイルと大きさ

`lib/asterism_games/commentate/assets/sounds/` (計 3,573,628 バイト。gem は 3.5 MB):

| ファイル | 大きさ | 中身 |
|---|---|---|
| after_the_rain.mp3 | 1,262,208 | 待ち合わせの BGM、157.7 秒 (前奏 + 1 周 + 1 秒)、24 kHz モノ 64 kbit/s |
| spring_thing.mp3 | 987,648 | 対局中の BGM、123.4 秒 |
| encounter.mp3 | 1,041,600 | 緊迫の BGM、129.1 秒 |
| 効果音 19 個 (.wav) | 1,616〜37,404 (計 282 KB) | discard, discard_other, draw, turn, round, yaku, points, riichi, pon, chi, kan, ron, tsumo, kyuushu, ryuukyoku, fanfare1〜4。モノ 16 bit 15720 Hz |
| sounds.json | 約 7.7 KB | 一覧と digest |

- 形式: BGM は **MP3** にした。指示書の「OGG などの圧縮でもよい・Chrome・Edge・Safari で鳴る形式」のうち、Ogg Vorbis は
  Safari の古い版で鳴らないことがあるため、どのブラウザでもデコードできる MP3 を選んだ。24 kHz は APU の帯域 (7.86 kHz まで)
  に十分で、1 フレームがちょうど 400 サンプルになる。
- ページで使わない効果音 (discard = 自分の打牌、yaku、fanfare2〜4 = 自分が 2〜4 位) も道具は作る。アプリの音の全部で、計 100 KB ほど。
- 音量は正規化していない (APU の出力そのまま)。BGM の素の平均は約 -20 dBFS、ジングルのピークは -8〜-11 dBFS、打牌は -27 dBFS。
  ページで BGM に段階の倍率 (小 0.30) を掛けるので、アプリと同じ釣り合いになる。

## 4. ページの鳴らし方

- **何をいつ鳴らすか** は Ruby の純粋な関数 `commentate/mahjong_sounds.rb` (`MahjongSounds.effects(prev, cur)` →
  `[[名前, 遅れ ms], ...]`、`MahjongSounds.scene(view)`)。ハブが各 `state` イベントに `bgm` (場面) と `sounds` (効果音) を付ける
  (新しく開いたページへの最初の state は場面だけ)。`--public` でも手牌を使わない (ツモは山の残りが減ったことで見る)。
- 場面と音 (アプリの `snd_view` と同じ時点):

| 起きたこと | 音 |
|---|---|
| 局の始まり (局・本場が変わる、対局が始まる) | round |
| チー / ポン / カン (大明槓・暗槓・加槓) / リーチ | chi / pon / kan / riichi のジングル |
| 打牌 (河が増える) | discard_other (アプリの観戦者と同じく、誰の打牌でも「他家の打牌」の音) |
| ツモ (山の残りが減る) | draw |
| 手番が移る | turn (70 ms 後、アプリと同じ) |
| ロン / ツモ和了 | ron / tsumo のジングル、1 秒後 (アプリの宣言の表示時間) に points (和了の点の音) |
| 流局 (荒牌・四風連打・四開槓・四家立直・三家和) / 九種九牌 | ryuukyoku / kyuushu |
| 終局 | fanfare1 (アプリの観戦者は 1 位の曲)。和了のジングルと同じ様子なら 1 秒後 |
| 最初の様子 | 何も鳴らさない (アプリと同じ) |

- BGM の場面: 待ち合わせ (卓がまだ無いときも) → after_the_rain、対局中と結果 → spring_thing、リーチ者がいるか残り 10 枚以下 →
  encounter、終局 → 無音。曲が変わるときは頭から (アプリと同じ)。最初のクリックで AudioContext を作り、その時の場面の曲から
  鳴らす。AudioContext は 24 kHz で作る (デコード後の曲のメモリが半分。作れなければ既定のレート)。
- **重ね方**: 効果音は声の待ち行列と別で、来たらすぐ鳴らす。APU の 4 チャンネルの取り合いはアプリに倣った (sounds.json の
  channels と priority: 優先度の高い音が鳴っているチャンネルは取れず、1 つも取れなければ鳴らさない。取れたら前の音を止める)。
- 声とジングル: 声が鳴り始めるときにジングル (優先度 3 以上) が鳴っていれば、その大半 (終わりの 0.12 秒前まで、最大 0.6 秒) を
  待ってから声を出す。実際の卓では声がたいてい数秒遅れて読まれるので、ジングルと「リーチ」の WAV がぶつかる場面は少なかった
  (録画ではロンのジングル 33.3 秒 → 「ロンなのだ」35.2 秒)。音量は下げていない。
- **切り替え**: 上の帯に「BGM 小 [B]」「効果音 入 [E]」(クリックでも切り替わる)。B で 切→小→中→大→切 (アプリと同じ順)、E で
  効果音の入/切 (切ると鳴っている効果音も止める)。localStorage (`asterism-games.commentate.bgm` / `.se`) に覚える (try/catch。
  読めなければ既定)。Space の一時停止では AudioContext を止める (BGM は続きから)。キーの行も「B BGM / E 効果音」を足した。
- 鳴らしたものは `window.commentate.sound.log` とコンソール (`sound sfx ron <時刻>` など) に残る。

## 5. ダッキングとクレジット

- 声 (VOICEVOX の WAV・ブラウザの読み上げ・最初のあいさつ) の待ち行列が話している間、BGM の段階の値を 0.5 倍にする (下げは
  時定数 0.05 秒)。話し終えて 0.6 秒たっても次の行が始まらなければ戻す (時定数 0.25 秒)。行と行の間の 0.12 秒では戻さない。
  効果音は下げない。
- クレジット: ページの右下に「VOICEVOX:ずんだもん」と並べて「BGM: shiru8bit (CC BY 3.0)」を出す (BGM が切のときは出さない。
  `?obs=1` でも出る)。`assets/SOUNDS_LICENSE.txt`: 効果音 (19 個を名前で列挙) は MIT (作者の決定、fmruby-core では GPL-3.0)、
  BGM は MIT ではなく shiru8bit の CC BY 3.0 (曲名・出典・ライセンスの URL・改変なし (NSF を APU で鳴らして MP3 にし、
  1 周で切っただけ、ページがループする) の旨・再配布や配信でのクレジット)。README に「The sound」の節、`--help` に音の段落、
  CHANGELOG 0.21.0。サーバは `/sounds/<名前>.wav|mp3|json` (assets/sounds/ のファイルだけ) と `/SOUNDS_LICENSE.txt` を返す。

## 6. 確認の結果

### 6.1 構成

どれも作業用の名前と番号で起こし、終わったら pid で止め、コンテナは名前で stop / rm した (prune はしていない):

- zenohd: `docker run -d --name c3b-zenohd -p 127.0.0.1:27771:7447 eclipse/zenoh:1.10.1`
- VOICEVOX: `docker run -d --name c3b-voicevox -p 127.0.0.1:27721:50021 voicevox/voicevox_engine:cpu-0.25.0` (自分のコンテナ)
- ゲームホスト: 作業コピーの `bin/asterism-games --config <scratch>/games.yml` (mahjong1、humans 0、cpu_ms 700、result 8 秒、
  東風戦、API キーは空)。
- 実況: `commentate mahjong --router tcp/127.0.0.1:27771 --no-open --port 27780 --ai off --voicevox http://127.0.0.1:27721`
  (XDG_CACHE_HOME / XDG_STATE_HOME は作業場所)。
- 録画: recording.md の record.sh を写し、Chromium に `--enable-logging=stderr` を足してページのコンソールを保存、ffmpeg の
  開始時刻を記録 (コンソールの時刻と合わせるため)、クリック後に押すキー (`KEYS`) を足したもの。コンテナ名 `c3b-rec`。
- ゲインの直接の確認: 手元の headless Chromium (Playwright の chrome-headless-shell、`--remote-debugging-port=27790`) を
  Node から CDP で操作し、BGM の GainNode の値を 0.1 秒ごとに読んだ。

### 6.2 録画 (60 秒、全部入)

`/tmp/claude-1000/-home-kishima-fmrb-family-mruby/ac2e25be-21b2-46ea-8f7c-511bdcdbea0d/scratchpad/verify/out/c3b_sound.mp4`
(1280x720・15fps、1.55 MB、57.9 秒。東 1 局の途中から東 2 局の始まりまで)

- 全体: ピーク -2.1 dBFS、平均 -25.7 dBFS。
- 声 (VOICEVOX): 決まり文句 5・その場の文 7 を読んだ。声の区間の RMS 平均 -25.6 dBFS (計 45.5 秒。CPU の速い卓なので声が
  ほぼ途切れない)。
- BGM: spring_thing (段階「小」)。行の間 (声の無い区間) の下限 -30.5 dBFS。コンソールに `sound bgm spring_thing`、
  デコードの失敗 0 (Playwright の Chromium で MP3 も WAV も読めた)。東 2 局の録画 B では encounter → spring_thing の
  切り替えも出た。
- 効果音: 169 回 (打牌 57、ツモ 53、手番 53、ポン 3、ロン 1、点 1、局の始まり 1)。チャンネルの取り合いで鳴らさなかった手番の音 3。
  録画の中の 157 回について、鳴った直後 0.25 秒のピークは平均 -23.5 dBFS、直前の下限 -33.2 dBFS (約 10 dB 上に乗る)。
- 写し (30 秒の 1 枚): 帯に「BGM 小 [B]」「効果音 入 [E]」、キーの行に「B BGM / E 効果音」、右下に「VOICEVOX:ずんだもん」と
  「BGM: shiru8bit (CC BY 3.0)」が並ぶ。

### 6.3 ダッキングの数値

- **GainNode の値** (CDP、30 秒、298 サンプル): 声が話している間 0.150 (中央値、258 サンプル)、話していない間 0.285〜0.300
  (戻りの途中を含む 10 サンプル)。段階「小」0.30 の半分に下がり、戻ることを確認した。
- **録った音で** (録画 C: BGM「大」・効果音切 (`KEYS="b b e"`)、46 秒、`.../verify/out/c3b_duck_bgm_high.mp4`): 声が止んだ直後の
  0.6 秒 (声は無く、BGM はまだ下がっている) と、戻った後の 0.6〜2 秒とで、BGM の大きさを曲のファイルのスペクトルに対する比で
  比べた: **-6.7 dB** (設定は -6.0 dB)。
- 限界: 声の重なる区間で録った音から BGM だけの大きさを出すのは、声のエネルギーが BGM の周波数にも乗るため偏る (同じ方法で
  声の下を測ると逆に大きく出た)。また「小」の BGM は声より 10 dB ほど小さく、区間ごとの音量では曲そのものの強弱に埋もれる
  (録画 A・B の区間ごとの音量では -1〜+8 dB とばらついた)。ダッキングの判定は GainNode の値と録画 C の -6.7 dB による。
- 測った道具は scratchpad の `verify/measure.rb` (区間の音量・効果音)、`verify/bgm_gain2.py` (曲のファイルとの比)、
  `verify/cdp_gain.js` (GainNode)。どれもコミットしていない。

## 7. 試験

`test/test_commentate_sounds.rb` (12 件):

- 効果音の対応 (手で作った様子): 最初の様子は無音、局の始まり (次の局・本場・次の対局)、打牌・ツモ・手番 (70 ms)、チー・ポン・
  大明槓・暗槓・加槓・リーチ (ジングルが先)、ロン (+1 秒後に points)・ツモ和了・流局 5 種・九種九牌、同じ結果の再送は無音、
  終局 (ジングルと同時なら 1 秒後)。
- 場面: 卓なし・待ち合わせ → lobby、対局中、残り 11 枚は play・10 枚は tense、リーチ者 → tense、結果 (リーチなし play、あり
  tense)、終局 → none。
- C1 の東風戦の記録 (383 の様子) を通す: 局の始まりは局の数 - 1 (最初の様子の局は鳴らさない)、ファンファーレ 1 回、和了の数 =
  points の数、名前はすべてページが知る名前、遅れは 0・70・1000 だけ、最後の場面は none、tense を含む。
- ファイル: ページが使う名前がすべて sounds.json とディスクにある、tense_left・段階 [0, 0.3, 0.55, 1.0]・名前・既定「小」が
  アプリの値、3 場面の曲があり CC BY 3.0・shiru8bit、ループが繰り返し部分とファイルの中に収まる。sounds.json の一覧 = ディスクの
  ファイル、大きさと SHA-256 が一致、WAV / MP3 の頭、合計 5 MB 未満。
- 注記: MIT・CC BY 3.0・shiru8bit・曲のファイル名と曲名・効果音の名前がすべて書いてある、ページに BGM のクレジットがある。
- ハブ: 各様子の state に bgm と sounds が付き、あとから開いたページには場面だけ。
- サーバ: sounds.json / WAV / MP3 / SOUNDS_LICENSE.txt の種類と中身、無い名前・`..`・`%2e%2e`・ディレクトリは 404。
- `test_gem.rb`: 音のファイルは `assets/sounds/` の sounds.json に載ったものだけ (声は入らない)、SOUNDS_LICENSE.txt が gem に入る、
  assets の下のファイルがすべて gem に入る。

## 8. 決めたこと (指示書の範囲内の判断)

1. 道はホストの APU (2.1)。BGM は MP3 (3 章)。継ぎ目を選ぶ (2.2)。
2. 打牌はアプリの観戦者と同じく全員 discard_other。ツモと手番は、各席の打ち手がアプリで聞く音として全席で鳴らす (9 章 1)。
3. 「和了のファンファーレ」は、アプリの結果の数え上げの最後に鳴る points (和了者の点の音) を和了のジングルの 1 秒後に鳴らす形に
   した。アプリのファンファーレ (fanfare1〜4) は終局の音なので、終局に fanfare1 を鳴らす。役ごとの yaku の音は、ページに数え上げの
   表示が無いので鳴らさない (アプリでも人のいない卓では数え上げが無い)。
4. 効果音の取り合いは、アプリの「声部ごと」ではなく「ファイルごと」(取れるチャンネルが 1 つでもあれば全体を鳴らす)。
5. 一時停止で音も止める (AudioContext を止める)。BGM のクレジットは BGM が切のときは出さない。

## 9. 確認してほしいこと

1. **手番とツモの音の多さ**: CPU の卓では 1 手ごとに「打牌・ツモ・手番」の 3 つが鳴る (60 秒で各 50 回ほど)。アプリでは
   自分の手番にだけ鳴る音を、全席ぶん鳴らしているため。うるさく感じるなら選択肢:
   1. (推奨) 手番の音は人の席の手番だけにし、ツモの音は今のまま全席 (CPU の卓では打牌とツモだけになる)
   2. 今のまま (全席で手番とツモ)
   3. 手番もツモも鳴らさず、打牌だけ (アプリの観戦者と同じ)
2. 実際の聞こえ方: BGM「小」と声・効果音の釣り合い、ダッキングの深さ (半分) と戻りの速さ、BGM のループの継ぎ目
   (after_the_rain は 156.7 秒、spring_thing は 122.4 秒、encounter は 129.1 秒の位置)。
3. Safari (macOS / iOS) で MP3 と 15720 Hz の WAV が鳴ること、`sampleRate: 24000` の AudioContext が作れること (作れなければ
   既定のレートに戻る)。ここでは Chromium でしか確認していない。
4. sim や実機の音との聞き比べ (同じ APU のソースだが、アプリでは音符が 1/60 秒単位でずれうる。ここでは音符をフレームの頭に
   揃えている)。

## 10. 気づき

- CPU の東風戦では声の待ち行列がほとんど空かない (60 秒のうち 45 秒話している) ので、BGM はほとんどの時間ダッキングされた
  大きさで鳴る。「小」(0.30) の半分 (0.15) は声に比べてかなり小さい。
- 録画を数値で見るには、ページのコンソールの時刻と ffmpeg の開始時刻を合わせるのが効いた (record.sh に 2 行足しただけ)。
  recording.md に足してよいかもしれない。
- 最初のホストを止めたつもりで、記録していた pid が bundle の外側のものだったため、ホストが残っていた (2 つ目が「already on the
  network」で待った)。`ps` で見つけて pid で止めた。
