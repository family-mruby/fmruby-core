# RG3 報告: 相手のいる通しを、相手を道具が立てて自動で流す

> 状態: 完了 | 更新: 2026-10-26 | `rake regress:live` を作り、`rake regress:full` に入れた。相手 (ゲームホスト・歩数の送り手・MIDI のモニタ・音の探り) を道具が立て、Modern の sim で 6 場面を流して相手の側の状態を基準 (JSON 18 個と PNG 1 枚、7 KB) と比べる。3 回続けて全部通った (1 回 2.5 分)。実機・VPS・`~/asterism_keys/` は使っていない

## 1. 作り

| ファイル | 役目 |
|---|---|
| `tools/regress/live.rb` | 入口。RG2 の screens の Runner を継ぎ、場面の一覧・基準・結果の場所と Session を差し替える。`--scene`・`--update`・`--list`・`--attach`・`--keep` |
| `tools/regress/counterparts.rb` | 相手の形 (`Base`: start・state・stop・resume・close) と登録、CRuby の Asterism の接続 1 本 (`Probe`、node `rgprobe`) |
| `tools/regress/counterparts/games.rb` | ゲームホスト。asterism-games の作業コピーを `bundle exec bin/asterism-games` で、卓 1 つの設定と空の状態のディレクトリ (tmp の下) で起こす。node `rghost`。AI の鍵・資格情報・外の bundle の環境は外し、ルータは手元の zenohd に固定。stop は SIGTERM (保存して去る)、resume は同じ状態で起こし直す |
| `tools/regress/counterparts/steps.rb` | `tool/imu/step_send.rb --measure` (node `rgsteps`)。walk_scroller が描いた後に置く progress を拾う |
| `tools/regress/counterparts/midi.rb` | ルートの `fmrb_midi_monitor.rb` を起こし、出力の行からバイト列と時刻、音の組 (鳴り始め・止め) を作る |
| `tools/regress/counterparts/audio.rb` | ルートの `fmrb_audio_probe.rb`。読みながら台本の操作を流す (`listen(秒) { ... }`) |
| `tools/regress/screens.rb`・`screens/dsl.rb` | 継げるように小さく直した: 場面の一覧・Session の型・出力と基準の場所・落ちた行の書き方をメソッドに、「何も比べなかった」の判定を `compared?` に。振る舞いは同じ (`rake regress:screens ONLY=modern` で 10 passed を確認) |
| `rakelib/regress.rake` | `regress:live` (`ONLY=`・`SCENE=` で場面を絞る)・`regress:live:update`。`regress:full` の最後に足した |
| `test/regress/live/helpers.rb`・`10_tables.rb`・`20_streams.rb` | 卓のアプリの操作と要約、場面 |
| `test/regress/live/baseline/modern/` | 基準 (JSON 18、PNG 1) |

- 台本は RG2 の DSL にそのまま乗る。足した言葉は `s.counterpart`・`s.check (名前, 要約, variant:)`・`s.poll`・`s.expect`。相手は場面の後に道具が閉じる (落ちた場面でも)。
- 指示書の例の `counterpart :games, table: :othello` の形にした。
- asterism-games・ルートの tools/・step_send.rb は読むだけ (コマンドとして呼んだ)。直す必要は無かった。
- 鍵: sim は手元の zenohd (認証なし、loopback) に平文の tcp で繋がるので、鍵は要らなかった。ゲームホストも資格情報なしで起こす。

## 2. 場面

| 場面 | 相手 | 流れ | 比べるもの |
|---|---|---|---|
| `othello` | ゲームホスト (othello1、CPU 200 ms) | 一覧 → Enter → s 座る → c CPU → Enter 始める → 端末の番に最初の合法手をクリック → ホストを止めて戻す → もう 1 手 → Esc・y で投了 → Esc で出る | `lobby`・`move`・`after_host`・`resigned`・`left`: 席 (端末・CPU・空き)、卓の主、盤 (端末の石 D・相手 O)、手数、手番、結果 |
| `board` | ゲームホスト (board1) | 入る → 1 本描く → ホストを止めて戻す (絵の読み直しをログで待つ) → 2 本目 → Esc で出る | `stroke`・`after_host`・`left`: 線の数、会員 (主か)、各線の色・幅・始点・終点 |
| `mahjong` | ゲームホスト (mahjong1、東風、CPU 300 ms) | 入る (名前を聞かれたら Enter) → 1 座る → c CPU 3 人 → 始める → 端末の番に Enter で打牌 → ホストを止めて戻す → もう 1 打牌 → Ctrl+Q で閉じる | `lobby`・`discard`・`after_host`・`closed`: 状態、会員の数、席の種類、端末の席が人で河に牌があること (端末の札で `look` を呼ぶ) |
| `walk_scroller` | 歩数の送り手 | 120 歩/分で 10 歩 | `walked`: 送った数、描かれた progress の数、最後の count (10)、通った印。歩いた後の画面 (`walked.png`) |
| `midi_serial` | MIDI のモニタ | MIDI APU で 7 (serial) → 1 (音階) | `scale`: 8 音の音名とチャンネル、前の all off、長さと間の中央値 (10 ms に丸め: 200・260)、1 音ずつ ±40 ms に収まるか |
| `apu_sound` | 音の探り | 鳴らす前に 1 秒、1 (音階) を押しながら 3 秒 | `scale`: 前は無音、鳴った (ピーク > 256)、鳴った窓の数を秒に丸めた値 (2) |

- 「ホストを止めて戻す」はアプリのログ (`<tag>: rghost left the network`・`is back`) で待ち、戻った後に次の手・線・打牌がホストに届くことで「アプリが繋ぎ直して卓に戻った」を確認する。
- 待つのは相手の状態 (`poll`) とアプリのログで、時間では待たない。`pause` はアプリを開いた直後の 0.3〜0.5 秒だけ。

## 3. 揺れと、その扱い

- **オセロの色**: ホストが始めるときに席を混ぜる (SecureRandom) ので、端末が黒か白かで盤が 2 通りになる。要約を `variant:` で色ごとに分け、基準を 2 組 (`othello.move.black.json` と `.white`) 持つ。盤は端末の石を D、相手を O にして比べる。出た後の要約 (`left`) は席を並べ替え、盤を入れない (色によらない)。
- **名前**: 端末の名前はプロフィール・`/home/mahjong.toml` (sim の /home はユーザの `flash/home/`) から来るので、要約に入れない。麻雀は /home に名前が無いと最初の join の前に名前を聞くので、`join_table` は Enter で joined が出なければもう一度 Enter を押す (出された名前で入る)。今の sim の /home には名前があるので、この枝は流れていない。
- **麻雀の牌**: 山は毎回違うので、河の牌・手牌は比べない (端末が打ったか、席の種類だけ)。CPU の鳴きや流局の結果で打牌の番が来ない間は、`mahjong_discard` が鳴きを Esc で見送り、結果を Enter で進める。
- **ボードの点の数**: 動きを何点で拾うかは入力の速さで変わりうるので、線の始点と終点だけを比べる (3 回とも同じ)。
- **MIDI の時刻**: モニタの時刻は PC に届いた時刻。長さ・間は中央値を 10 ms に丸めて基準と比べ、1 音ずつは ±40 ms の範囲で真偽だけを比べる。
- **serial の口**: MIDI のポートは起動して最初に開いたときだけログ (`MIDI serial out ready`) と GM の初期化を出すので、それは待たず比べない (`--attach` で 2 回目に流すと出ないため)。比べるのはアプリが鳴らす前に送る all off (16 チャンネル分) だけ。
- 3 回の通しの中で揺れて落ちたものは無かった。作る途中の `--attach` の通し (10 回ほど) でも、場面の作りの誤り以外では落ちていない。

## 4. 確認

### 4.1 3 回続けて

`rake regress:live` を 3 回続けて流し、3 回とも `16 passed / 0 failed`。

| 回 | 全体 (rake の壁時計) | ビルド | sim と場面 |
|---|---|---|---|
| 1 | 153 秒 | 78 秒 | 67 秒 |
| 2 | 156 秒 | 83 秒 | 67 秒 |
| 3 | 153 秒 | 81 秒 | 66 秒 |

- 場面ごと (3 回目): othello 6.7 秒、board 4.8 秒、mahjong 13.6 秒、walk_scroller 12.0 秒 (歩く 5 秒と、画面が落ち着くのを待つ 2.5 秒ほど)、midi_serial 4.1 秒、apu_sound 5.3 秒。ゲームホストの起動は 1〜2 秒。
- オセロの色は、基準を作るときに両方を出した (白の形を先に取り、黒が出るまで流し直した)。3 回の通しでどちらが出たかは最後の回 (白) しか残していない。
- `rake regress:full` はこれまでの 17 分ほどに 2.5 分を足して 20 分ほど (通しては流していない)。regress:screens と regress:live はそれぞれ Modern をビルドする (合わせて 1 回にはしていない)。

### 4.2 わざと壊す

`rake regress:live ONLY=board,midi` で 2 つ同時に壊した (ファームのコードは変えていない):

- ホストを止めたまま戻さない: `helpers.rb` の `restart_host` の `games.resume` を消した → `FAIL modern board: timed out after 30s waiting for the log to say /board: rghost is back/`。
- 相手の側の基準をいじる: `midi_serial.scale.json` の `step_ms` を 260 → 250 → `FAIL modern midi_serial.scale: step_ms: 250 -> 260`。
- 出力は 4 行 (build/ の行、FAIL 2 行、要約) と `regress:live: failed`。どちらも戻し、戻したファイルが元と同じことを確認した。落ちた場面の後もゲームホスト・モニタは止まっていた (`pgrep` で残り無し)。

### 4.3 出力の長さ

- 通ったとき: 2 行 (`build/: TAB5 (esp32p4)` と `live: 16 passed / 0 failed (modern 16/16; 2.5 min: modern build 81s + run 66s)`)。
- 落ちたとき: 落ちたものを 6 つまで 1 行ずつ (要約の違いは最初の 3 つの葉を `path: 前 -> 今` で)、残りは数と results.json の場所。
- 要約・相手のログ (ホスト・モニタ・送り手・探り)・コアのログは `tmp/regress/live/` にあり、場所だけ出す。

## 5. R4a/R4b で手でやった確認のうち、置き換わったもの

| R4a/R4b の手の確認 | 置き換わったか |
|---|---|
| オセロ: 卓の一覧、Enter で入る、S で座る、C で CPU、始める | `othello` (lobby) |
| オセロ: 打つ (打ち手の台本で最初の合法手をクリック) | `othello` (move、1 手ずつ 2 回) |
| オセロ: ホストを止めて戻す (卓の中)、戻ると続きから | `othello` (after_host) |
| オセロ: 立つ・出る (Esc)、ホストの会員から消える | `othello` (resigned・left。投了してから出る形) |
| ボード: 一覧、入る、描く | `board` (stroke) |
| ボード: ホストを止めて戻すと絵を読み直す | `board` (after_host、`picture read: 1 strokes`) |
| ボード: Esc で一覧 (出る) | `board` (left) |
| 麻雀: 一覧、入る・座る・CPU で埋める・始める、打つ | `mahjong` (lobby・discard) |
| 麻雀: ホストを止めて戻す、続き | `mahjong` (after_host) |
| 麻雀: Ctrl+Q で閉じる | `mahjong` (closed) |
| オセロ: CPU や Jev と終局まで、端末のクライアント (`asterism-games play`) との対局 | 置き換わっていない (終局まで打つと 30 秒ほどかかる。Jev は AI の偽物の設定が要る) |
| ホストの状態を消して戻す (札を忘れたホスト → 一覧に戻る) | 置き換わっていない (相手に「状態を消して戻す」を足せば入る) |
| zenohd を止めて戻す | 置き換わっていない (zenohd は sim の compose の中。相手にすると sim の片付けと絡む) |
| ボード: 別のクライアントの線が届く、U・C・R | 置き換わっていない (相手から `line` を呼べば入る) |
| 1-1・1-2 (join の待ちを捨てる、誤りの答え) と SIGSTOP で「ホストが答えない」 | 置き換わっていない (相手に SIGSTOP/SIGCONT を足せば入る) |
| 観戦 (麻雀の W)、アシストの段 | 置き換わっていない |
| sim の Retro、ブラウザ版 (ws の経路) | 置き換わっていない (Modern の sim だけ) |
| エディタの 1 打鍵 | RG2 の `editor_type` |

- 一覧 → 入る → 打つ/描く → ホストを止めて戻す → 出る、の骨組みは 3 アプリとも道具になった。R4a/R4b の 5.2 の表の行のうち半分ほど (卓の基本の流れとホストの再起動) が置き換わり、残りは異常系 (状態を消したホスト、zenohd、SIGSTOP、誤りの答え) と相手を増やす確認。

## 6. 指示との違い・残件

- 指示との違い:
  - 構成は Modern の sim だけ (卓のアプリ・Asterism・walk_scroller・麻雀の 640x360 がそろう。Retro は卓のアプリを実機で持たない)。
  - `ONLY=` は場面の名前で絞る (9 章の `ONLY=` は構成で絞るのと違う。live の構成は 1 つなので)。`SCENE=` も同じ意味で受ける。
  - 麻雀の「出る」は Ctrl+Q で閉じる形 (対局中は席を立てない。席はホストに残る、R4b の作り)。オセロは対局中に Esc すると投了を聞くので、投了してから出る。
  - 音の確認は音高まで見ていない (鳴ったか・鳴った長さ)。MIDI のバイト列で音名と間隔は見ている。
- 残件:
  - HTTP の偽の相手: 天気のアプリは決まった外の HTTPS (Open-Meteo) に繋ぐ作りで、向け先を変える口が無い。入れるにはアプリか設定の側に口が要る (アプリのコードは触っていないので止めずに外した)。
  - 相手を増やす候補: ホストの SIGSTOP/SIGCONT (「答えない」)、状態を消して戻す、別の参加者 (CRuby で join して `line`・`move`)、zenohd の止めて戻す。
  - 麻雀で /home に名前が無いときの枝 (名前を聞かれて Enter) は流れていない。ユーザの `flash/home/mahjong.toml` は触っていない。
  - 道具が kill されたとき、ゲームホスト・モニタは自分のプロセスグループで残る (次の通しでは同じ node で起こすのでぶつかる)。`pkill -f bin/asterism-games` で消す。
  - regress:screens と regress:live を同じビルドで流せば `regress:full` が 1.5 分ほど縮む。今は別々にビルドする。
  - `doc/README.md` の索引の更新は、doc/regression の文書をコミットするときに親が行う。

## 7. コミット (fmruby-core develop、push していない)

- 7b5e8887 tools: rake regress:live, scenes with their counterparts started by the tool (RG3)
- 7c87b51e test: live scenes and their baselines for rake regress:live
- 0839dccd doc: rake regress:live, the scenes with counterparts, in the regression usage (usage.md だけ。件名はこれまでの usage.md のコミットに合わせて `doc:`)

指示書 (instruction_rg3.md) とこの report はコミットしていない。わざと壊した変更は戻した。build/ は始める前と同じ TAB5 (esp32p4)。sim・ゲームホスト・モニタ・送り手は止めた (zenohd は sim の compose と一緒に下りた)。ブラウザ版・port 8006 は触っていない。P4-Nano (/dev/ttyACM0) には触っていない。
