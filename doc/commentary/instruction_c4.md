# C4 指示書: 端末の麻雀アプリで宣言の声 (ずんだもん、各自が作って入れる)

> 状態: 指示 | 更新: 2026-10-21 | Family mruby の麻雀アプリで、リーチ・ポン・チー・カン・ツモ・ロンの宣言をずんだもんの声 (WAV) で鳴らす。声は git に入れず、各自が手元の VOICEVOX で作って flash_local/ に置き、端末のビルドで入る。計画は doc/commentary/plan.md、規約は doc/reference/voicevox_terms.md

前提:
- fmruby-core develop。麻雀アプリ `flash/app/game/mahjong/` (音 `mahjong_sound.rb`: 宣言のジングル `snd_jingle` と `@shout`、BGM・効果音の切り替えボタンとキー (`snd_buttons`、`snd_key`、B・E)、設定の保存 `snd_load_conf`)。WAV を鳴らす口 `@audio.play_wav(path)` (16bit・モノラル・8000〜48000Hz・2MB まで、1 本ずつ、Retro では false) と `stop_wav` (`sig/fmrb_audio.rbs`)。読み上げで WAV を鳴らした実績 (doc/user_extension/voice/)。
- **flash/ の不変条件** (fmruby-core/CLAUDE.md): flash/ の git 追跡分 = 配ってよいもの。手元だけの中身は **flash_local/** (gitignore) に置く。実機の storage は flash/ + flash_local/ の合成。ブラウザ版の束は追跡分だけ。
- asterism-games の実況アプリの VOICEVOX の口 (`lib/asterism_games/commentate/voicevox.rb`: `/speakers` で名前から番号、`audio_query` → `synthesis`) と言葉の読み (`SPEECH_READ`) は参考 (読むだけ)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。画面の言葉は日本語。周辺の道具は Ruby (ルートの CLAUDE.md)。

## 1. 決まっていること (2026-10-21 ユーザ、選択形式)

1. **声は各自が作って入れる**。git に入れない。手元の VOICEVOX で WAV を作る道具で **flash_local/** に作り、端末のビルドで入る。声の無い端末 (と公開のブラウザ版) では今のまま。
2. **言葉は宣言だけ**: リーチ・ポン・チー・カン・ツモ・ロンの 6 つ。
3. **全員ずんだもん** (席で声を変えない)。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **作る道具**: fmruby-core の rake (例 `rake voice:mahjong`) と Ruby の道具 (`tools/` に)。`VOICEVOX_URL` (既定 `http://127.0.0.1:50021`)、`SPEAKER` (既定 ずんだもん)、`STYLE` (既定 ノーマル) を受ける。番号は `/speakers` から名前で引く。`audio_query` の `outputSamplingRate` を 16000 にして、16bit モノラルの WAV を作る (端末の口に合わせる。大きさを抑える)。置き場所は `flash_local/usr/share/sounds/voice/mahjong/` (ファイル名は `riichi.wav` `pon.wav` `chi.wav` `kan.wav` `tsumo.wav` `ron.wav`)。同じ場所に `CREDITS.txt` (「VOICEVOX:ずんだもん」と、規約の要点、各自が作ったもので配らないこと)。読み (例 リーチ → 「リーチ!」) は道具の中の表に持つ。VOICEVOX が無ければ、入れ方を案内して止まる。
- **焼かずに入れる道** (Tab5): 作った WAV を `tools/fmrb_rd_fs.rb` (か MCP の tab5_fs) で端末の `/home/voice/mahjong/` に送る手順も用意する (rake か README に 1 行)。アプリは `/home/voice/mahjong/` を先に、無ければ `/usr/share/sounds/voice/mahjong/` を見る。
- **アプリ**:
  - 誰かが宣言したとき (今ジングルを鳴らしている契機)、その宣言の WAV があれば `play_wav` で鳴らす。ジングル (APU) と重なってよい (WAV は APU に重なる)。ほぼ同時の宣言 (ポンとロン) は優先の高い方 (ロン > ツモ > カン > ポン > チー、リーチは自分の番なので重ならない想定) を鳴らす。
  - 設定に「声 入/切」を足す (BGM・効果音の隣、キーは空いているもの。既定は入、声のファイルが無ければボタンを出さないか「声: なし」)。設定は今の保存の仕組みで覚える。
  - `play_wav` が false (Retro など) なら何もしない。ファイルが無ければ何もしない (エラーを出さない)。
  - **クレジット**: 声のファイルを使っているときは、アプリのどこか (設定の近く、または直近の結果・遊び方の画面) に「声: VOICEVOX:ずんだもん」を出す (規約: アプリの紹介画面など、探せば見つかる場所)。
  - 内蔵 RAM を増やさない (Ruby だけの変更のはず)。640x360 と 1280x720、Retro (320x240) で崩れないこと (`rake mahjong:overflow`)。
- **ブラウザ版**: 束は追跡分だけなので声は入らない (何も変えない)。声が無いことでボタンが崩れないこと。
- 外の API を呼ばない (VOICEVOX は自分のコンテナ)。

## 3. 作るもの

1. 作る道具と rake、Tab5 へ送る手順。
2. アプリの再生・設定・クレジット。
3. 試験: 道具の部分 (名前から番号、読みの表、置き場所、VOICEVOX が無いときの案内) を host の試験で。アプリの「どの宣言でどのファイル」の対応 (純粋な関数にできれば)。
4. 文書: 道具の使い方 (fmruby-core の doc か README の該当箇所。ASSETS.md に「声は flash_local に各自が作る、配らない」を 1 行)。
5. 確認:
   - 自分の VOICEVOX のコンテナ (`voicevox/voicevox_engine:cpu-0.25.0` は pull 済み、名前とポートは自分のもの) で 6 つの WAV を作る (大きさ・長さ・サンプリング周波数を report に)。
   - Linux sim (標準構成、fmrb MCP の sim_*) で、flash_local の声が入ったビルドの麻雀アプリを動かし、宣言のときに WAV が鳴ることを確かめる (`sim_audio` やログ、`tools/fmrb_audio_probe.rb` で鳴った証拠が取れるか試す。取れなければ、`play_wav` が true を返したことのログで代える)。手元の zenohd とゲームホスト (CPU の卓) につなぐ。エディタを 1 回起動して 1 打鍵する確認も含める。画面の写し (声の設定とクレジット)。
   - 声の無いビルドで今のままであること。

## 4. 受け入れ条件

1. fmruby-core の `rake test`、`rake mahjong:overflow` が通る。
2. 上の確認が通り、report に写す。
3. **生成した WAV が git に入っていない** (`git status` と、flash_local が gitignore されていることを確認)。
4. Tab5 での聞こえ方は親とユーザが確認する。

## 5. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop: 麻雀アプリ、tools/ の道具、Rakefile の rake の追加、ASSETS.md の 1 行、report)。
- **触らないもの**: asterism-games・asterism・asterism-console・riichi_engine・fmruby-graphics-audio、牌のシート、NSF、VPS、GitHub の設定、Tab5 (焼かない・送らない。手順だけ書く)。ユーザの VOICEVOX (127.0.0.1:50021)・`~/asterism_keys/`・API キーを使わない。
- push・タグはしない。`git commit -a` は使わない。件名は `game:` / `tools:` など履歴に合わせる、本文英語。末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。fmruby-core の doc (この指示書・report) はコミットしない (親がする)。**flash_local/ に作った声は確認の後に消す** (ユーザが自分の VOICEVOX で作り直す)。
- fmruby-core の build/ は sim のために作り直してよい (report に書く)。
- 起こしたもの (VOICEVOX のコンテナ、zenohd、ホスト、sim) は名前で止めて消す (sim は sim_down)。docker の prune はしない。pid で止める。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/commentary/report/c4.md`、常体): 道具と rake、置き場所、Tab5 へ送る手順、アプリの鳴らし方・設定・クレジット、試験と確認の結果、ユーザが自分の VOICEVOX で作って入れる手順。

## 6. 追加の決定 (2026-10-21 ユーザ、作業の途中で)

- **初期の声を同梱する**: Open JTalk と MMDAgent の声「Mei」(CC BY 3.0) で 6 つの宣言の WAV を作り、**flash/** に入れる (全端末と公開のブラウザ版に入る)。作る道具はコマンド 1 つで作り直せる形にし、声と辞書のライセンス・版・digest を CREDITS に書く。ASSETS.md に載せる。
- **探す順**: `/home/voice/mahjong/` (各自が送った声、例 ずんだもん) → flash_local で各自が作った声 → 同梱の初期の声。クレジットは実際に使った声に合わせる (「声: Open JTalk (Mei, CC BY 3.0)」/「声: VOICEVOX:ずんだもん」)。
- Mei の声のファイルを公式の配布元から、はっきりしたライセンスで得られなければ、別の声に替えずに止まって選択肢を返す。
- 各自が VOICEVOX でずんだもんを作って入れる道は、そのまま残す。
