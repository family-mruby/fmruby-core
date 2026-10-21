# C3b 指示書: 実況のページに麻雀アプリの BGM と効果音

> 状態: 指示 | 更新: 2026-10-21 | 実況のページ (`asterism-games commentate mahjong`) で、Family mruby の麻雀アプリと同じ BGM と効果音を鳴らす。Family mruby の APU の音源で前もって WAV (BGM はループ用) にして gem に入れ、ページで鳴らす。計画は doc/commentary/plan.md

前提:
- asterism-games main (0.20.1、e717c71)。実況アプリ `lib/asterism_games/commentate*` (ページ `assets/commentate.js`、声の待ち行列、出来事 `mahjong_events.rb`、牌のシートの写しと `TILES_LICENSE.txt`、gem の試験 `test_gem.rb`)。
- fmruby-core develop。麻雀アプリの音 `flash/app/game/mahjong/mahjong_sound.rb` (場面ごとの BGM (`:lobby` `:play` …、`play_slot` の MAIN)、効果音とジングル (SUB の `note_on` / `note_off` の並び、`snd_play(name)` の名前: `discard` `discard_other` `draw` `turn` `round` `ryuukyoku` ジングル (`snd_jingle`) ファンファーレ (`snd_fanfare`) …)、BGM の段階 `BGM_LEVELS [0,30,55,100]`)。BGM の NSF `flash/usr/share/sounds/nsf/` (after_the_rain / spring_thing / encounter、shiru8bit、**CC BY 3.0**、`CREDITS.md`)。APU の音源 `components/apu_emu` (nes_apu)、NSF の再生 (graphics-audio 側にある)。sim の音を数値で見る道具 `tools/fmrb_audio_probe.rb` (`--dump out.wav`)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-games の文書は英語。画面の言葉は日本語。

## 1. 決まっていること (2026-10-21 ユーザ、選択形式)

1. **WAV で鳴らす** (前もって音のファイルにして gem に入れる。ページで APU を動かさない)。
2. **効果音は gem の中では MIT** (作者の決定。fmruby-core 側は GPL-3 のまま)。**BGM は元の CC BY 3.0** をそのまま付ける (作者 shiru8bit、曲名、改変なし (WAV/OGG にしただけ) の旨)。
3. **BGM の既定は小さめで入** (麻雀アプリの「BGM 小」相当)。ページで 切/小/中/大 を切り替えられる。効果音も入/切。
4. **ダッキング**: 声 (VOICEVOX・ブラウザの声) が鳴っている間だけ BGM を半分ほどに下げ、終わったら戻す。効果音は下げない。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **音のファイルの作り方**: Family mruby の音源で作る (麻雀アプリと同じ音にする)。
  - 効果音・ジングル: `mahjong_sound.rb` の音符の並びを、APU の音源 (`components/apu_emu` をホストで組んだ小さな道具か、sim を動かして `fmrb_audio_probe.rb --dump`) で鳴らして WAV にする。どちらの道を取ったかと理由を report に。並びをホスト側で再現するなら、音符の表を読み出す (写し間違えない) 仕組みにする。
  - BGM: NSF を同じ音源で鳴らし、ループの切れ目が目立たない長さで WAV にし、gem には **OGG (Vorbis)** などの圧縮でもよい (大きさを report に。ページの `<audio loop>` か WebAudio のループ)。ブラウザで鳴る形式にする (Chrome・Edge・Safari)。
  - 作る道具は、どこで動かすか決めて (fmruby-core の tools/ か asterism-games の tools/)、**再現できる手順** (コマンド 1 つ) にする。作った音のファイルの digest を記録し、作り直したときに変わったかが分かるようにする。
- **ページ**: 麻雀アプリと同じ場面で鳴らす (打牌 (自分の視点が無いので打牌は 1 種類か、手番の席で変える)、ツモ、手番、局の始まり、鳴き・リーチ・ロン・ツモのジングル、流局、和了のファンファーレ)。BGM は場面 (待ち合わせ・対局中) で麻雀アプリと同じ曲を選ぶ。最初のクリックの後で鳴らし始める (自動再生の決まり)。上の帯に BGM と効果音の切り替え (キーも: 例 B と E、麻雀アプリと同じ)。設定はページの localStorage に覚える (try/catch)。
- **重ね方**: 効果音と声 (VOICEVOX の WAV) が同時に来たとき、効果音は待たせずに鳴らす (声の待ち行列とは別)。決まり文句の WAV (「リーチ」) とジングルが重なりすぎるなら、ジングルを少し先に鳴らす・音量を下げるなどの調整を report に。
- **クレジット**: ページの隅に BGM のクレジット (例「BGM: shiru8bit (CC BY 3.0)」) を VOICEVOX のクレジットと並べて出す。gem に `assets/SOUNDS_LICENSE.txt` (効果音は MIT、BGM は CC BY 3.0 と出典) を置く。README に。
- **大きさ**: gem が大きくなりすぎないよう、BGM の圧縮と長さを選ぶ (目安 3 曲で数 MB 以内)。gem の試験で、音のファイルが入っていること、ライセンスの注記があることを見る。
- 依存は増やさない (gem の実行に新しい gem を要しない。音を作る道具がホストの ffmpeg などを使うのはよい)。版は **0.21.0**。

## 3. 作るもの

1. 音のファイルを作る道具と手順、作った WAV/OGG、ライセンスの注記。
2. ページの再生 (BGM、効果音、ダッキング、切り替え、覚える)。
3. 試験 (gem に入る、注記、場面 → 音の名前の対応 (純粋な関数にする))。
4. README (音の節)、CHANGELOG 0.21.0、VERSION。
5. 確認: 手元の zenohd とゲームホスト (CPU 4 人) に当て、録画の手順 (doc/commentary/report/recording.md、録画用のイメージ `fmrb-rec:local` があればそれ) で 60 秒ほど録り、BGM・効果音・声が入っていること (音量と、ダッキングで BGM が下がる区間) を数値で確かめて report に。録画のファイルは scratchpad に置き、パスを返す (親がユーザに渡す)。

## 4. 受け入れ条件

1. `MJ_GAMES=4 bundle exec rake test` が通る。
2. 上の確認が通り、report に写す。
3. 実際の聞こえ方は親とユーザが確認する。

## 5. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。音を作る道具を fmruby-core の tools/ に置くなら fmruby-core (develop、その道具だけ)。fmruby-core の report (`doc/commentary/report/c3b.md`、ファイルとして書くだけ)。
- **触らないもの**: 麻雀アプリのコードと音の定義 (読むだけ)、NSF のファイル (読むだけ、改変しない)、牌のシート、asterism・asterism-console・riichi_engine、VPS、GitHub の設定、Tab5。ユーザの `~/asterism_keys/`、API キーを使わない。ユーザの VOICEVOX (127.0.0.1:50021) は使わない (要るなら自分のコンテナ)。
- 外の API を呼ばない (実況の AI は `--ai off`)。push・タグ・gem push はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。fmruby-core の doc はコミットしない (親がする)。
- 起こしたもの (zenohd、ホスト、sim、コンテナ) は名前で止めて消す (sim は sim_down)。docker の prune はしない。pid で止める。sim を使うなら fmruby-core の build/ を作り直してよい (report に書く)。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/commentary/report/c3b.md`、常体): 音の作り方 (道と理由、手順)、ファイルと大きさ、ページの鳴らし方、ダッキング、クレジット、試験と確認の結果 (録画の数値)、確認してほしいこと。
