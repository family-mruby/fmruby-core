# OP1 報告: オセロの卓のアプリの右の欄の文字

> 状態: 完了 (検収待ち) | 更新: 2026-10-24 | instruction_op1.md の結果。文字は窓からはみ出していたのではなく、`fit` が欄の桁数で印なしに切っていた。席の行は名前の側を「…」で詰め、案内などは 2 行に折り返すようにした。sim の Modern と Retro の 4 場面と長い名前で、欄の文字がすべて窓の内側に収まることを確認した

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| fmruby-core | develop | `d04cf86d` | `flash/app/game/othello/othello.app.rb` だけ |

## 1. 切れていた原因

窓の大きさ・字の幅・座標系はどれも原因ではなかった。

- Modern の窓 (372x222) の利用域は幅 370・高さ 210 (枠 1 px とタイトルバー 11 px)。`layout` は `c = 25`、`@px = 210`、`@pw = 159` で、欄の右端は `@px + @pw = 369` (窓の内側の右端 371 の 2 px 手前)。計算は正しい。
- 字の幅: `FmrbGfx::FONT_METRICS` の `[:ja, 12]` は半角 6 px・全角 12 px で、アプリの `text_w` / `fit` の数え方と同じ。sim の画面でも合っていた: 直す前の待ち合わせの案内「席に着いた (色は始めるとき」の右端は画面 x 389 (窓の内側の右端は 397)。26 桁 × 6 px で計算どおりに止まっていて、窓の端より 8 px 手前で終わっている (`op1/before_modern_lobby.png`)。
- 実際の原因は `fit` が欄の桁数 (`n = @pw / 6` = 26) で印を付けずに切っていたこと。切れた位置がたまたま窓の端の近くなので、端で切れたように見えた。
  - 案内の文は 35 桁あり、26 桁で切れて「席に着いた (色は始めるとき」になる (Tab5 で見えたものと一字一句同じ)。
  - 席の行は名前と印を合わせて `fit(label, n - 6)` (20 桁) で切っていた。「fmruby-90bce8* (自分)」は 21 桁なので、最後の「)」が落ちる (Tab5 で見えたものと同じ)。名前が長いと印まで消える作りだった。

止まる条件 (窓や描画の基盤の側の原因) には当たらない。

## 2. 直し方

`othello.app.rb` の `draw_panel` と、小さな助け 3 つ。

- `fit_dots(s, cols)`: 収まらなければ `fit(s, cols - 2) + "…"`。「…」は `text_w` の決まり (多バイト = 全角) で 2 桁に数えるが、efont は半角の幅で描くので、詰め方は 6 px 短い側に外れる (はみ出す側には外れない)。
- `wrap2(s, cols)`: 収まらなければ 2 行に分ける。1 行目の最後の空白で切って残りが 1 行に収まるならそこで、だめなら桁で切る。2 行目も長ければ「…」。
- `text_lines(x, y, s, cols, color)`: `wrap2` の行を 15 px 間隔で描き、次の y を返す。

使い方:

- **席の行**: 印 (`*`、` [AI]`、` (自分)`、` 途切れ`) と石の数を先に測り、残りの幅 (`(@pw - 14 - text_w(印 + 数)) / 6` 桁) で名前だけを `fit_dots` で詰める。印と数は必ず全部出る。空席に CPU がいるときの「CPU」も同じ扱い。
- **卓の名前と状態**・**最後の手**: `fit_dots`。
- **席の下の 3 つの枠** (待ち合わせ: 案内 / 「卓の主 (*) が始める」 / 空き、対局中: 手番 / 残り時間 / 最後の手、終局: 結果 / 勝ち負け) と **@note**: `text_lines` で、折り返した分だけ下の行が下がる。何も折り返さないときの行の位置は前と同じ (案内 y+50、次 +15、最後の手 +30、@note +45)。
- 文言・ボタン・キー・色は変えていない。`draw_list` (卓の一覧) は欄の外なので触っていない。

ボタンとの余裕 (いちばん行が増える場合):

| 画面 | 欄の幅 (桁) | 待ち合わせ・終局で最も下の行 | 最も上のボタン | 対局中で最も下の行 | 投了の確認文 |
|---|---|---|---|---|---|
| Modern 窓 | 159 px (26) | y 139-151 (6 行) | `Jev[A]` y 161 | y 154-166 (7 行) | y 183 |
| Retro 全画面 | 141 px (23) | y 128-140 | `Jev[A]` y 180 | y 143-155 | y 202 |

Retro でも折り返しの余地は十分あり、止まる条件には当たらない。

## 3. 確認

構成: sim の zenohd (127.0.0.1:7447) に、asterism-games の作業コピーから自分で起こしたゲームホストをつないだ (node `op1games`、othello1 だけ、`turn_seconds` 30、`cpu_ms` 300、状態は scratchpad、`ASTERISM_GAMES_AI_FAKE=1`・`_FAKE_DELAY_MS=1500`、`ANTHROPIC_API_KEY` などは環境から外して起動)。VPS にはつないでいない。~/asterism_keys も使っていない。asterism-games の作業コピーは変えていない (設定は scratchpad の YAML を `--config` で渡した)。

各画面の右端は、写しの欄の範囲を行ごとに走査して背景でない画素の一番右を測った (scratchpad の小さな Ruby、`tools/fmrb_png.rb` を使用)。

### 3.1 Modern (426x240、窓 372x222、標準構成)

窓の内側の右端は画面 x 397。

| 場面 | 結果 | 文字の右端 (最大) | 写し |
|---|---|---|---|
| 直す前の待ち合わせ (席に着いた) | 案内が 26 桁で印なしに切れる | 389 | `op1/before_modern_lobby.png` |
| 席に着いた | 案内が「席に着いた」/「(色は始めるときに決まる)」の 2 行、「卓の主 (*) が始める」がその下 | 379 | `op1/modern_1_seated.png` |
| Jev を入れた | `Jev [AI]`、ボタン `立つ[S]`・`CPU[C]`・`始める`・`出る` と重ならない | 379 | `op1/modern_2_jev.png` |
| 対局中 (Jev の番) | 「Jev が考え中 (1 手目)」、残り時間が「残り 30 秒 (切れたら CPU」/「が打つ)」 | 380 | `op1/modern_3_play_jev.png` |
| 対局中 (自分の番) | 上に加えて「最後: d3」が 2 行目の下 | 380 | `op1/modern_3_play_myturn.png` |
| 終局 (投了) | 「終局 4-1 投了」「あなたの負け」 | 339 | `op1/modern_4_over.png` |
| エディタを開いて `x` を 1 打鍵 | `Ln 1, Col 2` | - | `op1/modern_editor_one_key.png` |

### 3.2 Retro (320x240 全画面、小さい盤)

`.env` の `FMRB_HW_TARGET` を一時的に NARYAv3 にして `rake clean_all && rake build:linux` で作り直し、sim で見た (`.env` は元に戻し、`git status` で差分なし)。画面の右端は x 319、欄の右端は 318。

| 場面 | 結果 | 文字の右端 (最大) | 写し |
|---|---|---|---|
| 観戦中 | 「観戦中:」/「空いた席に座れる」の 2 行 | 295 | `op1/retro_0_spectator.png` |
| 席に着いた | 「席に着いた (色は始める」/「ときに決まる)」(空白で切ると残りが 24 桁で 1 桁あふれるので桁で切る) | 309 | `op1/retro_1_seated.png` |
| Jev を入れた | ボタン 4 つと重ならない | 309 | `op1/retro_2_jev.png` |
| 対局中 (Jev の番 / 自分の番) | 残り時間が 2 行、「最後: d3」がその下 | 310 | `op1/retro_3_play_jev.png`、`op1/retro_3_play_myturn.png` |
| 終局 (投了) | 「終局 4-1 投了」「あなたの負け」 | 309 | `op1/retro_4_over.png` |
| エディタを開いて `x` を 1 打鍵 | `Ln 1, Col 2` | - | `op1/retro_editor_one_key.png` |

Retro の 4 場面は、自分の名前を長くした状態 (3.3) で撮った。

### 3.3 長い名前

試験用の名前の与え方:

- **sim の自分の名前**: sim の /home は `fmruby-core/flash/home/` (gitignore)。`flash/home/asterism/config.toml` に `name = "OwnLongName-abcdefgh"` (20 字) を一時的に書いた (`FmrbAsterism.settings` の名前で、アプリの `my_name` がこれを join に使う)。終わったら `flash/home/asterism/` ごと消した (前は無かった)。
- **相手の名前**: asterism-games の端末のクライアントを `bin/asterism-games play othello --router tcp/127.0.0.1:7447 --table op1games/games/othello1 --node op1long --name "LongPlayerName-abcdefghijklmnopq" --commands "sit,start"` で起こした (32 字)。その後クライアントを kill して、ホストに「途切れ」と判断させた。
- 全角 20 字の名前 (「ながいなまえのプレイヤーさんabcdefg」) も試したが、名前は「ながいなまえのプレイ」(10 字、30 バイト、20 桁) まで詰められて卓に届いた (どこで詰めたかは追っていない)。そのため、欄に収まって「…」は出なかった (`*` 付きで 21 桁、Modern の名前の枠は 24 桁)。そこで ASCII の長い名前で試した。

| 場面 | 表示 | 写し |
|---|---|---|
| Modern、待ち合わせ、自分 (卓の主) | `OwnLongName-ab…* (自分)` (右端 387) | `op1/modern_long_own.png` |
| Modern、対局中、自分の番 | `OwnLongName-…* (自分) 2` (右端 388) | `op1/modern_long_own_play.png` |
| Modern、対局中、相手 (卓の主) | `LongPlayerName-abcd…* 1` (右端 387) | `op1/modern_long_other_play.png` |
| Modern、相手が途切れた | `LongPlayerNa…* 途切れ 2` (右端 388) | `op1/modern_long_other_away.png` |
| Retro、相手が途切れた、対局中 | `LongPlay…* 途切れ 14` (右端 310) | `op1/retro_long_other_away_play.png` |
| Retro、自分 (卓の主) | `OwnLongName…* (自分)`、対局中は `OwnLongNa…* (自分) 2` | `op1/retro_1_seated.png`、`op1/retro_3_play_myturn.png` |

どの場合も印と石の数は全部見え、名前の側だけが「…」で詰まった。

勝った人の名前が長い終局の文 (観戦者から見た「白 (長い名前) の勝ち」) は撮っていない。相手の人を投了させる手段が手元のクライアントだけで、組むと時間がかかるため。この行も `text_lines` で折り返すので、ほかの行と同じ扱いになる。

### 3.4 検査と試験

- `rake mahjong:overflow`: `255 drawings: 0 failed, 55 warnings (426x240 is a warning only)`。
- `rake test`: 全部通った (golden 103 passed 0 failed、MicroPython smoke、mahjong overflow・voice、WAV・tts・secret_path・CA bundle ほか、終了コード 0)。
- sim の core のログに `E (` の行は 0 (Modern・Retro とも)。

## 4. 麻雀の卓で同じ切れがあるか

直していない (範囲外)。読んだかぎりでは同じ形ではない。`mahjong.app.rb` の `fit(s, w)` は幅を px で測り、切ったときは末尾に `..` を付ける。だから端で切れたようには見えない。Modern では高解像度の全画面 (640x360) で動くので、372x222 の窓の幅の問題もない。`rake mahjong:overflow` も失敗 0 (426x240 では警告 55: 欄どうしの重なりと、牌譜の結果が欄の外に出るもの。前からの警告で、今回の変更とは関係ない)。

## 5. 気づいたこと

- sim の写しが 1 つ前の描画のことがあった (GS13 の報告と同じ)。投了の確認のつもりのクリックが、終局のあとの同じ位置の `始める` に当たって 2 局目が始まった (ホストのログに 2 局目の開始と投了が残っている)。判断はホストのログか 2 枚目の写しで行った。
- 卓の一覧 (`draw_list`) も `fit` で印なしに切っている。今回の範囲 (右の欄) の外なので触っていない。

## 6. 後片付け

- sim は `sim_down` で落とした。ゲームホストと端末のクライアントは pid で止めた。ホストの状態は scratchpad の中だけ。
- `.env` は元に戻した。`flash/home/asterism/` は消した。
- build/ は `.env` どおり TAB5 の Linux で作り直した (`file build/fmruby-core.elf` で x86-64 を確認)。TAB5 の esp32 ビルドは置き換わっている。焼く前に `rake clean_all && rake build:esp32` が必要。
- fmruby-graphics-audio は触っていない (前から Linux のビルド)。
- この報告と `op1/` の写しはコミットしていない (親がする)。
