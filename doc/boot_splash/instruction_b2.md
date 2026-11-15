# B2 指示書: 起動の和音を FMSQ で作り直す (エンベロープと低音)

> 親: plan.md、前段の結果は report/b1.md。時間の目安は doc/regression/usage.md 0 章。

## ユーザの評価と決めたこと (2026-11-15)

- 和音の流れ (G3 → D4 → G4・A4・D5) は良い。**低音をもう少し強く、エンベロープを付けてリッチな音に**。
- FMSQ で鳴らす。APU は MAIN と SUB の 2 組あるので、2 本の FMSQ を同時に始めて、矩形波 4・三角波 2・ノイズ 2 で
  鳴らす。低音は **2 つの三角波を両方 G2** にして振れ幅を倍にする (G1 は下の理由で効かない)。
- **三角波は矩形波より小さく聞こえる**ことを加味して釣り合いを決める (ユーザの指摘)。三角波は音量を持たないので、
  釣り合いは矩形波の側を下げて取る。理由は 3 つ: 振れ幅が矩形波の音量 10 前後に当たる固定の大きさ (nofrendo の
  `APU_TRIANGLE_OUTPUT`)、エミュレータの直流を除く処理 (`APU_VOLUME_DECAY`、`x -= x >> 7`) が低い音を削る、
  低い音は耳と小さいスピーカーで小さく聞こえる。**数字は勘で決めず、WAV を帯で分けて測って決める**。

## やること

1. **生成の道具 (Ruby)**: `tools/boot_chime.rb` が MAIN 用と SUB 用の 2 本の FMSQ を作る。前例は
   fmruby-graphics-audio/tools/gen_intro_fmsq.rb、形式は apu_emu/include/fmsq_format.h、確かめは
   tools/midi/fmsq_dump.rb。できた FMSQ は `flash/boot/` に置いてコミットする (道具と出力の両方)。
2. **音の作り** (時刻は B1 の絵の段に合わせる。60 Hz のコマで数える):
   - 0 ms: G3 (矩形波) と G2 (三角波)。線が伸びる間に G3 を少し明るく。ノイズで軽い「すっ」。
   - 260 ms: D4 へ (5 度上へ)。
   - 500 ms (開ききる): G4・A4・D5 の和音、G2 は 2 つの三角波で重ねる。ノイズできらめき。
   - その後: 矩形波は音量のエンベロープで伸ばして消す (立ち上がり 1〜2 コマ → 減衰 → 余韻)。和音でデューティ比を
     変えて明るさを出す、上の声に浅いビブラート、MAIN と SUB の同じ音をわずかにずらして厚みを出す、などは試して
     良いものを残す (何を入れたか report に)。
   - 1190 ms ごろに無音。三角波は音量で消せないので、矩形波が十分小さくなった所で止める (ぶつ切りに聞こえないか
     WAV で見る)。
3. **鳴らし方**: デスクトップの起動の演出で、2 本を読み込み (`load_fmsq`、インラインで足りる大きさか確かめる)、
   線が出るコマで MAIN と SUB の `play_slot` を続けて送る。今の `BOOT_EVENTS` と `tick_boot_jingle` は消す。
   スロットの番号がアプリと衝突しないこと、`boot_splash = false` と `finish_boot_animation` (途中で止めたとき
   音も止まる) が動くことを確かめる。
4. **端末の確認**: SUB の APU と FMSQ の再生が、S3 (WROVER の graphics-audio)・P4・ネイティブ版・ブラウザ版の
   全部にあることをコードで確かめる。無い端末があれば止まって返す。

## 確かめること (これだけ)

- `rake regress`、`rake regress:spinel`。
- sim (Retro) の WAV (`tools/fmrb_audio_probe.rb --dump`): 音高の表 (予定と測った値)、**帯ごとの RMS** (200 Hz
  未満と以上、段ごと) と、それで決めた矩形波の音量の理由、和音の始まりが線の画面から +500 ms であること。
  B1 の WAV (tmp/boot_splash/after_retro.wav) との帯の比も並べる。
- `rake regress:run` を最後に 1 回。カーネルへのメッセージが 27 通から数通に減るので、B1 で増えたカーネルの
  VM プールが戻るかを見る (基準は更新しない。数字を report に)。
- 出力: `tmp/boot_splash/b2_retro.wav` (と Modern)、FMSQ のダンプ。

## 触ってよい範囲

- fmruby-core (develop) の system_desktop.app.rb の起動の演出の音の部分、tools/boot_chime.rb、flash/boot/。
  graphics-audio と APU は変えない (要るなら止まって返す)。既存の未コミット (components/ のサブモジュール 2 つ、
  wasm/vendor/) には触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。
- 終わったら Linux のビルド (x86-64)、sim を片付け、裏の処理を残さない。

## 止まる条件

- 絵の段や時間を変える必要、APU や graphics-audio を変える必要、内蔵 RAM の増分、ユーザの作業が要るとき。

## report (report/b2.md)

音の設計 (声の割り振りの表・エンベロープ・デューティ比)、帯ごとの RMS と釣り合いの理由、音高の表、FMSQ の大きさ、
メッセージの数と VM プールの前後、確かめた結果。開始と終了の時刻は `date` で書く。
