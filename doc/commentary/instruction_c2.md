# C2 指示書: 声 (VOICEVOX、既定はずんだもん)

> 状態: 指示 | 更新: 2026-10-20 | C1 の実況アプリに VOICEVOX の声を足す。決まり文句は前もって作った WAV ですぐ鳴らし、その場の文は VOICEVOX で合成する。声は gem に入れず、各自の PC の VOICEVOX で作る。計画は doc/commentary/plan.md、規約は doc/reference/voicevox_terms.md

前提: C1 の後の asterism-games main (`asterism-games commentate mahjong`: 手元の Web サーバ、ブラウザの卓、決まりの実況の字幕、Web Speech API の読み上げ、出来事と言い回しの純粋な関数)。C1 の report は `doc/commentary/report/c1.md`。VOICEVOX のエンジンの HTTP (既定 `127.0.0.1:50021`: `GET /version`、`GET /speakers` (キャラクターと声の調子 (style) と番号)、`POST /audio_query?text=&speaker=`、`POST /synthesis?speaker=` で WAV)。

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントと asterism-games の文書は英語。実況の言葉は日本語。

## 1. 決まっていること (2026-10-20 ユーザ)

1. **既定の声はずんだもん**。
2. **声は gem に入れない**。gem には言葉の一覧と作る道具だけ。WAV は各自の PC の VOICEVOX で作る (再配布しない)。
3. 決まり文句 (リーチ・ポン・チー・カン・ツモ・ロン、局の名前、流局、満貫・跳満 …) は **WAV** ですぐ鳴らす。その場の文は VOICEVOX で合成する。

## 2. 決めごと (親が決めた形。変えるなら止まって返す)

- **エンジンの見つけ方**: `--voicevox URL` (既定 `http://127.0.0.1:50021`、環境変数 `VOICEVOX_URL` も)。起動時に `/version` を聞き、無ければ C1 のブラウザの読み上げに落とす (画面に「VOICEVOX が見つからない: ブラウザの声で読む」)。WSL で Windows 側のアプリを使う形 (WSL の網が mirrored なら 127.0.0.1 で届く) と、docker のエンジンの形の両方を README に書く。
- **キャラクターの選び方**: `--speaker ずんだもん` (既定)、`--style ノーマル` (既定)。番号は `/speakers` から名前で引く (番号を決め打ちしない)。見つからなければ候補の一覧を出す。`asterism-games commentate voices --list` で、エンジンにあるキャラクターと声の調子を一覧する。
- **決まり文句の WAV**: gem に言葉の一覧 (YAML など、出来事の鍵 → 文の候補) を入れる。`asterism-games commentate voices --make` (起動時にも足りない分を作る) で、エンジンに合成させて `~/.cache/asterism-games/voices/<speaker>-<style>/` (XDG_CACHE_HOME に従う) に WAV を置く。一覧が変わったら作り直す (文の digest で名前を付ける)。この場所は消してもよい (作り直せる) と README に。
- **その場の文**: 決まりの実況の文 (と C3 の AI の文) を合成し、WAV を手元の Web サーバから配る (一時的な URL)。同じ文はキャッシュから使う (上限つき、古いものから捨てる)。合成している間に、決まり文句の WAV を鳴らす。
- **鳴らす順番**: ブラウザ側の 1 本の待ち行列。決まり文句の WAV → その場の文の WAV。追いつかないときは古い小さな出来事を飛ばし、和了などを先に。字幕は声と同時に出す。M キーで声を切る (字幕は残る)。
- **口調**: 言い回しに「キャラクターの口調」の段を設け、ずんだもんのときは語尾を「〜なのだ」寄りにする (「リーチなのだ!」「満貫、8000 点なのだ!」)。ほかのキャラクターでは普通の口調。口調は言い回しの一覧の中で分けて持つ (C3 の AI への頼み方にも使う)。
- **クレジット**: 使っている声を、ページの隅に「VOICEVOX:ずんだもん」の形で出す (VOICEVOX を使っているときだけ)。README に、配信するときは説明欄にもクレジットを書くこと、規約の要点 (doc の参照資料と同じ内容を英語で短く)。
- **鳴らすのはブラウザ**: WSL・macOS とも、音はブラウザで鳴らす (C1 と同じ)。PC 側で `paplay` / `afplay` は使わない。
- 依存は増やさない (標準ライブラリの `net/http`)。版は 0.19.0 のまま (C1 と同じ版に足す。C1 が 0.19.0 を使っていなければ合わせる)。

## 3. 作るもの

1. VOICEVOX の口 (版の確認、キャラクターの一覧、名前から番号、合成)、決まり文句の WAV の作成と置き場所、その場の文の合成とキャッシュ、ブラウザの待ち行列、口調の段、クレジットの表示、`commentate voices --list|--make`。
2. 試験: VOICEVOX を真似た小さなサーバ (試験の中で立てる) で、名前から番号、合成、キャッシュ、エンジンが無いときの落ち方、口調 (ずんだもんのときの語尾)、一覧が変わったときの作り直し。
3. 確認: docker の VOICEVOX エンジン (`voicevox/voicevox_engine` の CPU 版、版を固定、自分の名前とポート) に当てて、ずんだもんで決まり文句の WAV を作る、その場の文を合成する (1 文の時間を測って report に)、ヘッドレスのブラウザで待ち行列が回る (音は聞けなくてよい。鳴らした WAV の順番をページのログで見る)。手元の zenohd とゲームホストで、CPU 4 人の半荘か東風を流す。
4. README (声の節: VOICEVOX の入れ方 WSL / macOS / docker、キャラクターの選び方、置き場所、クレジット)、CHANGELOG。

## 4. 受け入れ条件

1. `MJ_GAMES=4 bundle exec rake test` が通る。
2. 上の確認が通り、report に写す (1 文の合成の時間、鳴らした順番)。
3. 実際の声の聞こえ方は親とユーザが確認する (report に「確認してほしいこと」)。

## 5. 範囲・決まり

- **触ってよいもの**: asterism-games (main)。fmruby-core は report だけ (`doc/commentary/report/c2.md`、ファイルとして書くだけ、git の操作はしない)。
- **触らないもの**: fmruby-core のコードとアセット、asterism・asterism-console・riichi_engine、VPS、GitHub の設定、Tab5。ユーザの `~/asterism_keys/` の証明書を使わない。ユーザの Windows 側の VOICEVOX には触らない (自分の docker のエンジンを使う)。
- 外の API を呼ばない (VOICEVOX は手元のコンテナ)。push・タグ・gem push はしない。`git commit -a` は使わない。コミットの末尾は `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値の検査をしてからコミットする。生成した WAV をリポジトリに入れない。
- 起こしたもの (VOICEVOX のコンテナ、zenohd、ホスト、ブラウザのコンテナ) は名前で止めて消す。pull した VOICEVOX のイメージは残してよい (ユーザも使える)。docker の prune はしない。pid で止める。
- **止まる条件**: 1・2 章に無い仕様の選択が要る。止まったら report に、ユーザが選べる形の選択肢 (推奨を先頭に) を書いて返す。
- **report** (`doc/commentary/report/c2.md`、常体): 声の作り、置き場所、待ち行列、口調、クレジット、試験と確認の結果、確認してほしいこと。
