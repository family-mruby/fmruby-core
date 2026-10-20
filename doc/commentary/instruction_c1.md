# C1 指示書: PC の実況アプリの土台

> 状態: 指示 | 更新: 2026-10-20 | `asterism-games commentate mahjong`: 観戦用の様子 (GS14) を読み、手元のブラウザに卓を描き (Family mruby の牌のアセット)、決まりの実況を字幕と読み上げで流す。計画は doc/commentary/plan.md

前提:
- asterism-games main (0.18.0、677b714)。観戦用の様子 `asterism/<host>/spectate/<卓>/state` (README「Spectate」、`app/asterism_games/mahjong/table.rb` の `spectate_view`: 各席の hand / drawn / river / melds / score / wind / riichi、round、dora_indicators、last_discard、result (役・符・翻・点・changes)、delay_s)。play の作り (`lib/asterism_games/play.rb` の接続と `ASTERISM_CREDENTIALS`、`play/mahjong_text.rb` の牌の名前、`play/keys.rb`)。
- 牌のアセット: fmruby-core `flash/app/game/mahjong/tiles_hand.bmp` (420x224、8bit) と `tiles_river.bmp` (240x128)。切り出しの座標は麻雀アプリのコード (`flash/app/game/mahjong/*.rb`) を読んで合わせる (読むだけ)。シートはユーザの手描き: **描き直さない・作り直さない・変換で中身を変えない** (写すだけ。ブラウザは BMP をそのまま読める)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-games の文書は英語。画面と実況の言葉は日本語 (麻雀アプリの決まり: 東1局、本場、供託、ドラ、リーチ、ポン …)。

## 1. 決まっていること (2026-10-20 ユーザ、選択形式)

- `asterism-games commentate mahjong` として asterism-games に足す (play と同じ gem)。WSL と macOS で動く。
- **ブラウザで見る形**: アプリが localhost に小さな Web サーバを立て、ブラウザの canvas に牌のアセットで卓を描く。音もブラウザで鳴らす。
- 牌のシートの写しを gem に入れる。gem の中では **MIT** (作者の決定)。
- 実況は決まりで作る (LLM は C3)。声は、この段ではブラウザの読み上げ (Web Speech API)。WAV と VOICEVOX は C2。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **つなぎ方**: play と同じ (`--router` / `--credentials` / `--namespace` / `--table`、無ければ `ASTERISM_CREDENTIALS` / `ASTERISM_ROUTER`)。読むのは観戦用の様子 (`<host>/spectate/*/state`) で、viewer か spectator の役割の証明書で読める。卓の一覧は観戦用のキーから作る (届いたもの。`--table` で指定もできる)。卓を操作しない (call しない)。
- **Web サーバ**: 依存を増やさない (標準ライブラリの `socket` で HTTP を最小限に書く。`webrick` などの gem を足さない)。`127.0.0.1` だけで listen (外から見えない)。番号は `--port` (既定は決まった番号、使用中なら次)。ページ・JS・牌の画像を返し、卓の様子と実況は **Server-Sent Events** でブラウザへ押す。起動時にブラウザを開く (macOS `open`、WSL は `wslview` か `explorer.exe`/`cmd.exe /c start`、`--no-open` で開かない)。
- **画面**: 卓 (4 人の手牌・ツモ・河・鳴き・点数・風・リーチ・局・本場・供託・残り・ドラ表示牌) を canvas に描く。自分の視点は無いので、4 人を同じ大きさで並べる (麻雀アプリの観戦の並べ方を参考にしてよい)。下に実況の字幕 (時刻つき、新しいものが下)。上に状態 (卓の名前、声の on/off、遅れ)。キー: Space 一時停止、M 声を切る、H 手牌を隠す。幅の狭い窓でも崩れないように。
- **出来事と実況**: 前回の様子との違いから出来事を拾う (局の開始、リーチ、ポン・チー・カン、ドラ・赤の打牌、テンパイ (手牌から判定。重ければ、向聴数・待ちは C5 でホストに持たせる前提で、この段は「リーチ時の待ち」と「和了」に絞ってよい)、和了 (役・点数は result から)、流局、順位の入れ替わり、残り枚数 (例: 10 枚)、オーラス)。出来事ごとに言い回しを数通り、乱数で選ぶ。盛り上がり (満貫以上、逆転、オーラス) で強い言い回し。出来事の判定と言い回しは**純粋な関数**にして試験する (C5 でサーバへ移せる形)。
- **読み上げ**: ブラウザの Web Speech API (日本語の声があれば)。待ち行列で順に読み、追いつかないときは古い小さな出来事を飛ばし、和了などを先に。ブラウザが音を鳴らすには最初に 1 回クリックが要る (自動再生の制限) ので、その案内を出す。
- **ネタばれ**: `--public` と H キーで、手牌を描かず、実況も手牌に触れない (打牌・鳴き・和了だけ)。卓の遅れはホストがかけている (アプリは何もしない)。
- **止まらない**: ホストが去る・戻る、卓が終わる・次の局、で止まらない。
- `--json` (出来事と文を 1 行ずつ標準出力へ。字幕を他の道具に渡す) はあってよい (簡単なら)。

## 3. 作るもの

1. `asterism-games commentate mahjong` (`play` と並ぶ入口、`--help`)。Web サーバ、ページ (HTML/JS/CSS、外部の CDN を使わない)、牌のシートの写し (`lib/asterism_games/commentate/assets/` など、gemspec の files に入れる) と、その MIT の注記 (作者・元は Family mruby の麻雀アプリの手描き)。
2. 出来事の判定と言い回し (純粋な関数) と試験 (記録した様子の並びを入れて、出る出来事と文を見る。手牌を隠すときに手牌の話が出ないこと)。HTTP の試験 (ページ・画像・SSE が返る、127.0.0.1 だけで listen)。
3. README (commentate の節: 使い方、役割、ブラウザ、手牌を隠す、OBS への載せ方)、CHANGELOG **0.19.0**、VERSION。gem に BMP が入ることを gem の試験で確認。

## 4. 受け入れ条件

1. `MJ_GAMES=4 bundle exec rake test` が通る。
2. 手元の zenohd とゲームホスト (作業コピー、CPU 4 人の卓、遅れ 0。外の AI の API は呼ばない) に `commentate mahjong` を当て、ヘッドレスのブラウザ (コンテナの Playwright など) でページを開き、卓が牌のアセットで描かれていること、字幕が流れることを画面の写しで確認する (局の途中・リーチか鳴き・和了の場面)。`--public` で手牌が描かれないこと。写しを report に。
3. 実際のブラウザでの見え方と、読み上げの声は親とユーザが確認する (report に「確認してほしいこと」)。

## 5. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は report だけ (`doc/commentary/report/c1.md`、ファイルとして書くだけ、git の操作はしない)。牌のシートは**読んで写すだけ**。
- **触らないもの**: fmruby-core のコードとアセット、asterism・asterism-console・riichi_engine、VPS、GitHub の設定、Tab5。ユーザの `~/asterism_keys/` の証明書を使わない (VPS につながない)。
- 外の API を呼ばない。push・タグ・gem push はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。
- 起こしたもの (zenohd、ホスト、ブラウザのコンテナ) は名前で止めて消す。docker の prune はしない。pid で止める (`pkill -f` は使わない)。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/commentary/report/c1.md`、常体): コマンドと画面、出来事と言い回しの一覧、Web サーバの作り、試験と確認の結果 (写し)、確認してほしいこと。
