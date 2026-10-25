# R6 指示書: FmrbText・FmrbButtons を広げ、FmrbUI も FmrbText で詰める

> 状態: 指示 | 更新: 2026-10-25 | plan.md の R6。R5 で作った FmrbText (`lib/add/picoruby-fmrb-app/mrblib/fmrb-text.rb`)・FmrbButtons (`fmrb-buttons.rb`) を既存のアプリに広げ、FmrbUI のラベル・ボタンが長い文字を FmrbText で詰めるようにする。使い分けを文書にする (2026-10-25 ユーザ決定、選択形式)。**実機は使わない**

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 決まっていること

- **載せ替えるアプリ**: オセロ (直接) `flash/app/test/othello.app.rb` (fit・wrap・button・button_at)、MML `flash/app/demo/mml.app.rb` (button_at)、StackChan Remote `flash/app/demo/stackchan_remote.app.rb` と動画の再生 `flash/app/modern/video_play.app.rb` (draw_button)、rover_brain `flash/app/test/rover_brain.app.rb` (wrap)。ピアノの鍵盤の当たり判定は対象外。**見た目は変えない** (形が違って寄せられないものは寄せない)。
- **FmrbUI も FmrbText で詰める**: FmrbUI のラベル・ボタン (と、文字を出すほかの部品で同じ扱いが自然なもの) が、枠に収まらない文字を FmrbText で詰める (印の有無・種類は FmrbUI の見た目に合わせて決め、report に)。FmrbUI は Spinel のプログラム (デスクトップ・エディタ・シェルほか) も取り込むので、**FmrbText を Spinel のプログラムでも使えるようにする**。Spinel のプログラムごとに内蔵 RAM が約 8 B 増えるのはユーザ承認済み (これ以上増えるなら止まる)。
- **使い分けの文書**: FmrbUI = 状態を持つ部品 (作ったら残る、フォーカス・キー・押して離したときの動作・テーマの色、設定・道具の画面、Spinel と共有)、FmrbText = 状態を持たない文字の道具 (測る・詰める・折り返す)、FmrbButtons = 毎回描き直す画面のボタン (描くたびに押せる範囲を登録)。置き場は doc/reference/ (現状を書く資料、例 `doc/reference/screen_helpers.md`) と、3 つのファイルの冒頭のコメント。.claude/skills/fmrb-app-new に 1〜2 行の案内を足すのは親がする (指示書の範囲外)。

## 2. 作るもの

1. FmrbText を Spinel で使える形にする: Spinel のプログラムが取り込む仕組み (fmrb-ui.rb が Spinel に入る道筋、FmrbGfx の Spinel 版の有無と text_width・FONT_METRICS の扱い) を読み、FmrbText の 3 つの口が Spinel で型が決まって通るようにする (Spinel の罠: 型が絞れない呼び出しで黙って 0、逃げる proc、強制インライン。memory の Spinel の節と doc/reference/ の Spinel の資料)。
2. FmrbUI の Label・Button ほかで、収まらない文字を FmrbText で詰める。収まる文字の見た目は 1 画素も変えない。
3. 5 つのアプリを載せ替える。行数の前後。
4. 使い分けの文書。

## 3. 確認

- sim の Modern と Retro、ブラウザ版で、5 つのアプリの前後の画面を並べる (差 0 が目標、差は report に)。
- FmrbUI: デスクトップ (Config・ランチャーの確認の画面など)・エディタ・シェル・NSF/SMF プレイヤ・appstore などで、ふだんの文字の画面が前と同じ (画素で比べる)。わざと長い文字 (言語を ja/en で切り替える、長いファイル名など) を出して詰まることを確かめる。
- Spinel: 標準構成 (Spinel のカーネル・エディタ) で起動し、エディタを 1 回起動して 1 打鍵。全 mruby の構成でも同じ (engine_policy: 標準+全 mruby の 2 点)。
- `rake test`、`rake mahjong:overflow` (前と同じ)、`rake spinel:doctor` があれば指摘の前後。
- 内蔵 RAM: Tab5・Retro・StickS3 の DIRAM の前後 (Spinel のプログラムの分、約 8 B × 本数までの増え)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の picoruby-fmrb-app の gem、Spinel のプログラムの取り込みの設定 (要る範囲)、5 つのアプリ、試験、doc/reference/ の新しい資料、report。lib/ を触ったら `rake clean`、ターゲットの切り替えは `rake clean_all`、Spinel の生成物は作り直す (wasm も)。
- **触らないもの**: 実機、sdkconfig、asterism-games・asterism の gem 本体・console、VPS、`~/asterism_keys/`、麻雀の手描きの牌の絵。
- push・タグはしない (親がする)。`git commit -a` は使わない。件名 `app:` `ui:` `spinel:` など履歴に合わせ、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc/app_refactor の指示書・report はコミットしない (親がする)。doc/reference/ の新しい資料はコミットしてよい。
- 終わったら sim・web を片付ける (前から動いていたものは残す)。
- **止まる条件**: Spinel の内蔵 RAM が 1 プログラムあたり約 8 B を超えて増える、FmrbUI の収まる文字の見た目が変わる、Spinel で FmrbText が型の決まらない形にしかならない (選択肢で返す)。
- **report**: `doc/app_refactor/report/r6.md` (常体): Spinel で使えるようにした方法、FmrbUI の詰め方、5 アプリの前後の行数と画面、RAM、残件、コミット。
