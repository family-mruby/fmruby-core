# MJ-E: riichi_engine の品質作業（報告）

> 状態: 完了 (検収待ち) | 更新: 2026-10-15 | riichi_engine の Fork の品質作業 (0.1.1.fmrb3、`fmrb` は 2f12d04) と、そのゲームホスト (asterism-games) と卓のアプリへの組み込み (最後の章)。新しいルールの鍵 (途中流局 3 種・流し満貫・包・明槓の槓ドラの後乗り・ダブル役満・テンパイ止め) を雀魂の既定でゲームホストに入れ、待ち合わせのルールの欄に 4 項目を足し、局の結果と終局の画面に流し満貫・途中流局の名前・包・ダブル役満・終局の理由を出した

対象は riichi_engine の Fork（`/home/kishima/fmrb/family-mruby/riichi_engine`）。push・tag・PR は行っていない。
各修正・機能は `main`（上流 0.1.0）から（または依存するブランチから）切った 1 ブランチ 1 件で、spec を持ち、
すべてを `fmrb` に merge した。`fmrb` の版は 0.1.1.fmrb3。ルールのキーはどれも、既定値で 0.1.0 の動きを保つ。

## 結果のまとめ

- `fmrb` の最新: `2f12d04`（Add the drafts of the new branches in Japanese, mark 0.1.1.fmrb3）、版 `0.1.1.fmrb3`
- spec: 362 → 440 examples、0 failures（`RBENV_VERSION=3.2.6 bundle exec rspec`。`.ruby-version` の 4.0.2 は
  この機械に無いので 3.2.6 で回した）
- Python `mahjong` 2.0.0 との突き合わせ: 違い 0、決まりの違い 0（代表 61 + 乱数 2000 seed 1、乱数 5000 seed 7、
  `DOUBLE_YAKUMAN=1` で両方のダブル役満を有効にして代表 61 + 乱数 2000 も 0）
- 向聴数の速さ: 乱数の 14 枚の手 300 で全打牌の向聴数＋受け入れ: 中央値 63.6 ms → 2.8 ms、95 % 点 191 → 5.4 ms、
  最大 342 → 18 ms
- CPU 4 人の半荘 40 回（double_ron、ほかは既定ルール）: 下の表

| 版 | レベル | 局数 | 荒牌流局 | リーチ/局 | 鳴き/局 | 1 手の判断 中央値 / 95 % / 最大 |
|---|---|---|---|---|---|---|
| fmrb2（0912a7a） | 3 (hard) | 108（半荘 10） | 18.5 % | 0.04 | 6.76 | 31.0 / 472 / 2825 ms |
| fmrb3 | 1 (easy) | 412 | 33.3 % | 1.17 | 0.54 | 2.7 / 4.6 / 134 ms |
| fmrb3 | 2 (normal) | 449 | 15.1 % | 0.57 | 3.02 | 2.1 / 4.6 / 387 ms |
| fmrb3 | 3 (hard) | 419 | 13.6 % | 0.80 | 3.04 | 1.9 / 4.5 / 413 ms |

（最大値は、色の牌が多い並びを初めて数えるときのもの。並びは覚えておくので 2 回目からは速い。計測の道具は
作業用の一時ディレクトリの `sim.rb`、host と同じ手順で API を呼ぶ。）

## 項目ごと

### 1. 向聴数と副露 — `fix/shanten-melds`（d88ab6d）

- 誤り: `Shanten.calculate` が手の内の牌を手のすべてとして 4 面子を求めていた。副露 1＋3 面子＋単騎（テンパイ）が
  2 向聴。鳴いた後の打牌（`DahaiSelector`）と鳴きの判断（`NakiJudge`、「副露を除いた手の向聴 − 2」で見積もり）が誤る。
  探索の最良値をクラスのインスタンス変数に置いていた（スレッドで共有される）。
- 直し: `Shanten.calculate(tiles_34, melds: 0)`・`regular_shanten`・`TileEvaluator.evaluate_all_discards(..., melds:)`・
  `count_ukeire(..., melds:)`。`DahaiSelector.select(..., seat:)`（既定は手番の席、`BaseAi` が渡す）、`NakiJudge` は
  鳴く前後を副露込みで数える。通常形は 4 つの色に分け、色ごとの「面子・搭子・雀頭」の組を並びごとに 1 度だけ求めて
  （上限 50,000 の表）組み合わせる。探索の手は従来と同じで、門前の手は同じ値（spec で手全体の探索と乱数 400 手を比較）。
- spec: shanten_spec（副露の 5 例＋参照探索との比較）、tile_evaluator_spec、dahai_selector_spec（鳴いた後に 6p を切る、
  `seat:`）、naki_judge_spec（副露のある 1 向聴で鳴かない／向聴が下がる鳴き／面子を崩すだけのチーは選ばない）。
  既存の「中級は向聴が下がる鳴きを選ぶ」は旧来の数え方に頼っていたので手を替えた（下書きに理由）。

### 2. ルール

エンジンのルールの扱い: `RuleConfig::DEFAULTS` のキーごとに読み出しメソッド（真偽なら `?` も）が生える。局の進行は
`RoundFlow`、局の後は `GameFlow`、点数の割り振りは `ScoreResult#score_changes`（host が呼ぶ）、流局の支払いは
API の関数（host が呼ぶ）。どのルールもこの形に収まったので、公開 API を壊さずに入れた（メソッドとキーワードの追加のみ）。

| 規則 | ブランチ | キー | 既定 | 試験 |
|---|---|---|---|---|
| 四風連打・四槓散了・四家立直 | `feat/abortive-draws`（5848af9） | `suufon_renda`・`suukaikan`・`suucha_riichi` | すべて `false` | abortive_draw_spec（12 例: 成立、既定で不成立、風牌でない／三元牌、ロンだけ残し見送りで流局、1 人の 4 槓は続く、GameFlow の連荘・本場） |
| 九種九牌 | （既存。キーなし） | — | — | 調べたところ子が宣言できなかった（下の `fix/first-turn-flags`） |
| 流し満貫 | `feat/nagashi-mangan`（d903890） | `nagashi_mangan` | `false` | nagashi_mangan_spec（成立する席・しない席、既定で出さない、支払い 3 例） |
| 包（大三元・大四喜・四槓子） | `feat/pao`（90d6d0e） | `pao`（役満名の配列） | `[]` | pao_spec（記録・既定で記録しない・JSON、大四喜・四槓子の記録、自摸全額・ロン折半・包の席からのロン・既定・記録なし） |
| 明槓の新ドラの時機 | `feat/kan-dora-after-discard`（73844ec） | `minkan_dora_after_discard` | `false`（直後、0.1.0） | kan_dora_spec（既定は直後、大明槓・加槓は打牌時、次の槓で一緒にめくる、暗槓は直後、JSON） |
| ダブル役満 | `feat/double-yakuman`（05ea9e4、`feat/multiple-yakuman` の上） | 既存の `double_yakuman` を効かせた | `false` | double_yakuman_spec（4 つの手、既定で 1 倍、純正でない形は 1 倍、`multiple_yakuman` と足し合わせ 3 倍・5 倍、偽なら最大の 1 つ）＋ Python 突き合わせ |
| アガリ止め・テンパイ止め | `feat/agari-yame`（0e8687b、`fix/tonpuu-game-type` の上） | `agari_yame`・`tenpai_yame` | `true`・`false`（0.1.0 の動き） | game_flow_spec（4 例） |

決めた細部:

- 途中流局は、成立させる打牌がロンされずに通ったとき（その打牌にはロンだけを残す）。`result_type`・`draw_reason`
  にその種類（`:suufon_renda`・`:suukaikan`・`:suucha_riichi`、state_machine.md にあった名前）。親の連荘・本場 +1、
  供託は残る（`GameFlow::ABORTIVE_DRAWS`）。
- 流し満貫: 荒牌流局の `round_end_info[:nagashi_seats]`（ルールが真のときだけキーが出る）。支払いは
  `API.calculate_nagashi_mangan_payments(seats, oya_seat:)`（満貫の自摸と同額、本場なし）。ノーテン罰符の代わり。
  連荘は親のテンパイ。
- 包: `RoundState#pao_seats` に記録（ルールにある役満だけ）。四槓子は 4 つ目の槓が大明槓のときだけ（加槓は対象外）。
  自摸は責任者がロン相当を全額＋積み棒、他家ロンは放銃者と折半（積み棒は放銃者）。包でない役満の分はふつうに払う。
  `ScoreResult#score_changes` が自動で割り振り、`winner_info[:pao]` に `[{seat:, yaku:}]`。`payments` は手の値のまま。
- ダブル役満の項目は `han: -2`（名前 `name` は変えず、表示名が 四暗刻単騎・国士無双十三面・純正九蓮宝燈）。
  `multiple_yakuman` が真なら足し、偽なら最大の 1 つ。

### 3. 0.1.0 の残り

- 槓と牌山 — `fix/kan-live-wall`（f9f97a5）: 嶺上牌を取っても牌山が短くならず、海底・流局が遅れていた。
  槓のたびに牌山の最後の牌を除く。海底牌の後は暗槓・加槓を出さない。spec: kan_wall_spec（2 例）。
- トビの境目 — `fix/tobi-doc`（0d46da5）: キーにはせず文書をコードに合わせた（0 点未満で終了、0 点は続行。
  一般的な決まり）。spec: ちょうど 0 点は次局。
- 知らない `game_type` — `fix/unknown-game-type`（a4dac58、`fix/tonpuu-game-type` の上）: `RuleConfig.new` が
  `EngineError`（`RuleConfig::GAME_TYPES`）。`should_end_game?` も知らない値で `EngineError`。spec 3 例。
- easy の CPU: 半荘 40 回を最後まで打ち、不正なアクションで止まることは無かった（エンジンは不正なら例外を出す）。
  打牌は向聴数を保つ候補のうち么九牌を先・受け入れの少ない方を切る作りで、弱くしてあると見てそのままにした。
  調べる途中で全レベル共通の明らかな誤りを 2 つ見つけて直した:
  - `fix/cpu-riichi`（dd08fd0）: `RiichiJudge` が打牌前の 14 枚の手でテンパイを見ていたため CPU が一度もリーチしなかった
    （fmrb2 の計測でリーチ 0.04 回/局）。切った後の手で判断し（`should_riichi?(..., tile:)`）、切る牌は待ちの残り枚数で
    選ぶ（`RiichiJudge.riichi_tile`）。spec 6 例。
  - `fix/first-turn-flags`（b43f392）: 打牌のたびに 4 席の第一巡を消していたので、子の九種九牌・ダブル立直・地和が
    起きなかった。打牌は自分の第一巡だけ、鳴き（暗槓を含む）は全員の第一巡を終える。spec: first_turn_spec（5 例）。

### 4. 試験と計測

上の「結果のまとめ」。突き合わせの道具に `DOUBLE_YAKUMAN=1` を足した（`spec/compat/python_mahjong_eval.py` が
環境変数を読み、`python_mahjong_compat.rb` の RULE も合わせる。`fmrb` のみにあるファイル）。

### 5. PR の下書き

`pr_drafts/` の 15 本の英語の下書きを日本語に書き直し、新しい 12 ブランチの下書きを足した（全 29 本、すべて日本語）。
`README.md` は日本語の索引（土台、隣り合う変更、ルールの選び方、未着手）。理由は正しさ・既存の作り・文書との一致・
一般的な決まりで書き、host 側の事情には触れていない。古い下書きにあった `fmrb` ブランチへの言及と PyPI の名前は消した。
ルールの名前（天鳳、M リーグ）は既存の下書きに残っているもののみで、新しい下書きでは確かめられない「天鳳と同じ」は書いていない。

## やらなかったこと・見つけたが残したもの

- 喰い替えの禁止が無い（鳴いた直後に同じ牌を切れる）。今回の指示の規則一覧に無いので未着手。雀魂は禁止のはず。
- リーチ宣言牌がロンされたときのリーチ棒の返却（リーチ不成立）が無い。
- normal・hard の CPU は役の有無を見ずに鳴く（役の無い鳴き手で和了れない局がある）。CPU の強さの話なので残した。
- 三家和の扱い（host は `triple_ron_draw: false`）は今回触れていない。三家和は 3 人とも和了とする (2026-10-15 ユーザの決定、雀魂に合わせる)。
- 1 手の判断の最大値（約 0.4 s）は、牌の多い色の並びを初めて数えるときの探索。これ以上は手を入れていない。
- fmrb2 のレベル 1・2 と、レベル 3 の半荘 40 回の計測は、旧来の向聴数が遅く 1 時間を超えそうだったので途中で止めた。比較は fmrb2 のレベル 3・半荘 10 回だけ（局数が少ないので荒牌流局率の差は目安）。作業用の worktree は消した。

## host（asterism-games）側で要る変更

`lib/asterism_games/mahjong/engine.rb` の `RULE_DEFAULTS` に足すキーと、雀魂の既定と思われる値:

| キー | 雀魂の既定（推定） | 確かさ |
|---|---|---|
| `suufon_renda` | `true` | ほぼ確か |
| `suukaikan` | `true` | ほぼ確か |
| `suucha_riichi` | `true` | ほぼ確か |
| `nagashi_mangan` | `true` | ほぼ確か |
| `pao` | `["daisangen", "daisuushii"]` | 確か (段位戦の既定は四槓子に包をかけない。2026-10-15 ユーザ) |
| `minkan_dora_after_discard` | `true` | ほぼ確か |
| `double_yakuman` | `true` | ほぼ確か |
| `agari_yame` | `true` | ほぼ確か |
| `tenpai_yame` | `true` | 不確か |

コードの変更:

- `score` の `:ryuukyoku`: `info[:nagashi_seats]` が空でなければ `API.calculate_nagashi_mangan_payments(info[:nagashi_seats], oya_seat: state.oya_seat)`
  をノーテン罰符の代わりに使う（`res["tenpai"]` はそのまま連荘の判定に渡す）。
- 新しい局の終わり `:suufon_renda`・`:suukaikan`・`:suucha_riichi` は今の `else`（点数の移動なし）で足りる。画面の表示名だけ要る。
  `next_round` は `result_type` をそのまま渡せば連荘・本場 +1 になる。
- 包は `sr.score_changes` が割り振るので計算はそのまま。`winner[:pao]` を画面に出すなら表示を足す。
- ダブル役満の役は `han: -2`。`yaku_han` を表示しているところで `-2` の扱い（「ダブル役満」など）を決める。
- ゲームの終わりの理由に `:all_last_oya_tenpai` が増える（`tenpai_yame` のとき）。
- `RoundState` の JSON に `pao_seats`・`pending_kan_dora` が増える（古い JSON も読める）。
- 自前の `Engine.shanten` は `Mahjong::Cpu::Analysis::Shanten.calculate(t34, melds: melds)` に置き換えられる
  （副露を数え、同じく色ごとに覚えておく方式）。今の比較の試験はそのまま通るはず。
- CPU がリーチするようになり、鳴きが減る。槓のある局は牌山の残りが槓の数だけ少なく表示される。
- `RuleConfig.new` は知らない `game_type` で例外になる。

## ゲームホストと卓のアプリへの組み込み

上の「host 側で要る変更」をゲームホストと卓のアプリに入れた。

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `d011fbc` | Gemfile.lock を 2f12d04 (0.1.1.fmrb3) に、`RULE_DEFAULTS`、点数 (流し満貫・包)、見え方の `pa`・`ng`・`fr`・`rx`、ローカルルール 4 項目、向聴数をエンジンのものに、試験 `test/test_mahjong_round_ends.rb`、README の "Rules"・"Local rules"・"Views"・"riichi_engine" |
| fmruby-core | feature/mj-engine3 | この報告と同じコミット | 卓のアプリ (`mahjong_panels.rb`・`mahjong_xl.rb`): ルールの欄の 4 行、局の結果と終局の画面 |

push・タグはしていない。riichi_engine の Fork は読んだだけ。

- `bundle lock --update riichi_engine` は版の名前だけ fmrb3 にして revision を 0912a7a のまま残した (bundler 4.0.6。その後の `bundle install` が「その版が無い」で止まる)。`bundle update riichi_engine` で revision ごと 2f12d04 にした。動いているユーザのゲームホストは別の checkout (0912a7a) を読んでいるので影響しない。

### 規則の既定

`Engine::RULE_DEFAULTS` に足した値 (雀魂の既定):

| 鍵 | 値 |
|---|---|
| `suufon_renda`・`suukaikan`・`suucha_riichi` | `true` |
| `nagashi_mangan` | `true` |
| `minkan_dora_after_discard` | `true` |
| `double_yakuman` | `true` |
| `agari_yame` | `true` |
| `pao` | `["daisangen", "daisuushii"]` |
| `tenpai_yame` | `false` |
| `triple_ron_draw` | `false` のまま (三家和は 3 人とも和了。ユーザの決定) |

**ユーザに確かめてほしいもの** (雀魂の既定が確かでない): `tenpai_yame` を `true` にするか (今は `false`)。鍵 1 つの変更で、待ち合わせの欄でも卓ごとに変えられる。`pao` は段位戦の既定どおり大三元・大四喜だけで確定した (2026-10-15 ユーザ。四槓子にはかけない)。

### 点数と見え方

- 荒牌流局で `nagashi_seats` が空でなければ、ノーテン罰符の代わりに `API.calculate_nagashi_mangan_payments` で払う。テンパイの席はそのまま連荘の判定に渡す。見え方の結果に `ng` (成立した席)。
- 途中流局 (`suufon_renda`・`suukaikan`・`suucha_riichi`) は点の移動なし。結果の `k` がその名前。連荘・本場 +1 はエンジンの `GameFlow` がする。
- 包: エンジンの `winner_info[:pao]` を和了ごとの `pa` (`[[払う席, 役満の表示名], ...]`) にした。点の割り振りは `score_changes` のまま。
- ダブル役満: `yh` の値 -2 をそのまま送り、画面で「ダブル役満」と出す。点の欄はエンジンの表示 (`2倍役満 16000/32000点`)。
- 終局の理由 `fr`: エンジンの理由 (`tobi`・`normal_end`・`all_last_oya_agari`・`all_last_oya_tenpai`) と、ゲームホストが決める `west` (西 4 局のあと)・`rounds` (局数の上限)。保存のファイルに `final_reason` を足した (版の番号は 2 のまま)。
- 保存した古い卓: 局の JSON に `pao_seats`・`pending_kan_dora` が無くても読めて打ち続けられることを試験で確かめた (`test_a_table_saved_before_the_new_rules_still_loads`)。ユーザのゲームホストの保存ファイル 2 つ (どちらも待ち合わせ) の写しも新しいコードで読めた (写しは消した)。

### 向聴数

ゲームホストの `Engine.shanten` (色ごとに分けて数える自前の通常形) を、エンジンの `Shanten.calculate(t34, melds:)` の呼び出しに置き換えた。既存の比較の試験 (門前 3000 手でエンジンの数と、副露ありでテンパイを待ちの判定と) はそのまま通る。

段階 3 の助け (`tenpai_help`、打牌ごとの向聴数と受け入れ) の時間を、CPU の半荘の同じ局面で古い数え方と新しい数え方の両方で測った (作業用の一時ディレクトリの道具。1 回目は古い方を先、2 回目は新しい方を先):

| | 局面 | 古い (中央値 / 95 % / 最大) | 新しい |
|---|---|---|---|
| 1 回目 (半荘 6 回) | 2,905 | 2.28 / 4.60 / 93.4 ms | 2.02 / 4.07 / 98.6 ms |
| 2 回目 (半荘 6 回) | 3,718 | 1.64 / 4.40 / 57.3 ms | 1.47 / 3.91 / 58.5 ms |

助けの中身は全局面でバイト単位で同じ。新しい方が約 1 割速い (最大値は GC と、並びを初めて数えるときのもの)。`rake test` の `assist:` の行は、置き換え前 (エンジンは fmrb3) 1.42 / 6.00 / 28.6 ms、置き換え後 1.33 / 5.10 / 42.5 ms (半荘ごとに局面が違うので目安)。

### ローカルルールの画面

| # | 画面の名前 | 鍵 |
|---|---|---|
| 11 | 途中流局 | `suufon_renda`・`suukaikan`・`suucha_riichi` をまとめて (3 つともありのときだけ「あり」。ゲームホストの設定が一部だけのときは「なし」と出し、「なし」を選び直すと設定の値に戻る) |
| 12 | 流し満貫 | `nagashi_mangan` |
| 13 | ダブル役満 | `double_yakuman` |
| 14 | テンパイ止め | `tenpai_yame` |

- 4 項目とも入った。1 行 18 px (640x360) で 15 行が y 324 まで、行の帯の下端は 341 で 360 に収まる。1280x720 はその 2 倍。sim の小さい画面 (426x240) は欄を出さず 1 行の要約のまま (変えていない)。
- 名前は画面の他の言葉 (「テンパイ」) に合わせて「テンパイ止め」にした。
- **見え方の鍵を分けた**: 11-14 は新しい鍵 `rx` に入れ、`ru` は 11 個のまま。MJ3b の卓のアプリは `ru` の数だけ行を描くので、`ru` を伸ばすと名前の無い行 (`nil` を描こうとして落ちる) が出る。分けたので、古いアプリと新しいゲームホスト、新しいアプリと古いゲームホストのどちらの組でも動く (新しいアプリは `rx` が無ければ 11 行)。`rule(token, item, value)` の項目番号は 0-14 の通し。
- 出さなかったもの: `pao` (役満の一覧で、あり・なしの切り替えではない)、`minkan_dora_after_discard`・`agari_yame` (16 行目は 640x360 で画面の下端にかかる。どちらも雀魂の既定で固定してよいと見た)。`atozuke` は使われていないので前のまま出さない。
- 試験: 新しい 4 項目がエンジンの `RuleConfig` に届くこと、途中流局の 1 項目が 3 つの鍵を動かすこと、設定が一部だけのときの扱い。ゲームホストの全部の鍵がエンジンに届くこと (`test_the_hosts_rule_keys_reach_the_engine`)。

### 局の結果と終局の画面

- 流局の見出し: 「四風連打で流局」「四槓散了で流局」「四家立直で流局」(九種九牌・三家和は前から)。流し満貫は「流局  流し満貫: <名前>」。
- 包: 点の行の右に「包: <名前> (大三元)」(橙)。行は増やしていないので箱の高さは変わらない。
- ダブル役満の役は「四暗刻単騎 ダブル役満」。
- 終局: 「終局 (トビ)」「終局 (アガリ止め)」「終局 (テンパイ止め)」「終局 (西4局まで)」「終局 (最後の局まで)」「終局 (局数の上限)」。理由の無いゲームホストでは「終局」だけ (MJ3b の残り「終局の画面が終わった理由を書かない」を閉じた)。
- アプリの大きさ: `mahjong_panels.rb` 12.5 KB (前は 10.6 KB)、`mahjong_xl.rb` 25.1 KB、`mahjong.app.rb` 52.4 KB (変えていない)。

### 試験と確認

| 確認 | 結果 |
|---|---|
| asterism-games `MJ_GAMES=8 bundle exec rake test` | 87 件、失敗 0 (ネットワークの試験 1 件は飛ばす)。CPU の半荘 8 回で 94 局、四風連打 2 回 |
| 新しい試験 `test_mahjong_round_ends.rb` (12 件) | 鍵がエンジンに届く、流し満貫 (子の満貫 8000、なしにすると 4 人ノーテンで移動 0)、包 (ポンで記録、ロンは 16,000 ずつ折半、ツモは責任者が 32,000)、ダブル役満 (-2、16000/32000。なしなら -1、8000/16000)、四風連打 (親が続き本場 +1、なしなら続く)、終局の理由、古い保存。15 回続けて回して失敗 0 |
| 直した試験 2 件 | fmrb3 の CPU の打ち方で揺れるようになった: 手番の前に鳴きの時間切れが 1 回あると数が合わない (前後の差で見るように)、東風の対局が短いと観戦の確認の回数が 100 に届かない (20 に) |
| 1 通の大きさ | 試験の最悪の形: 結果 (3 人の和了・役 14 個・包 2 つ・終局の理由) 2,110 B、受け入れの助け 2,100 B、リーチの助け 1,498 B。CPU の半荘で見え方の最大 1,059 B。試しのゲームホストで実際に打った中で 510 B。3800 B の内 |
| fmruby-core `rake test` | 通った (golden 103、MicroPython の smoke) |
| fmruby-core `rake wasm:web` | 通った (`emsdk_env.sh` を読んでから) |
| ブラウザ版 (開発サーバ 8016) | 試しのゲームホスト (ノード `gamesmje`、卓 mahjong7-12、状態は作業用の場所) に入った。mahjong7 で卓の主として 4 項目を切り替え (クリックと上下左右キー)、CPU で埋めて対局を始め、打牌 (応答 64 ms)。始めたときのログ `rules [0, 1, 3, 1, 0, 1, 1, 0, 1, 1, 20, 1, 1, 1, 0]`。mahjong8-12 は試験と同じ手順で局面を作って保存したファイルから起こし、観戦者として結果を見た |
| sim の標準構成 (Modern 426x240、x86-64) | 卓のアプリが起きて、ユーザのゲームホストの mahjong2 の待ち合わせ (sim が座ったままの席) に戻り、`rx` の無い古いゲームホストで今までどおり出た。エディタを開いて 1 打鍵 (`Ln 1, Col 2 *`) |

### 画面の写し

`C:\Users\Public\asterism_deploy\screenshots\mj3\`:

| ファイル | 中身 |
|---|---|
| `s11_web1280_lobby_rules_owner.png` | 卓の主のルールの欄 15 行 (途中流局なし・テンパイ止めありに変えたところ) |
| `s11_web640_lobby_rules_owner.png` | 640x360 の同じ欄 (途中流局をクリックであり、選んでいる行) |
| `s11_web1280_result_nagashi.png`・`s11_web640_result_nagashi.png` | 流し満貫 (子 +8000、親 -4000、子 -2000 ずつ) |
| `s11_web1280_result_pao.png`・`s11_web640_result_pao.png` | 大三元のロンと包 (責任者と放銃者が 16,000 ずつ) |
| `s11_web1280_result_double_yakuman.png`・`s11_web640_result_double_yakuman.png` | 四暗刻単騎 ダブル役満、2倍役満 16000/32000点 |
| `s11_web1280_result_suufon_renda.png` | 四風連打で流局 |
| `s11_web1280_over_tobi.png`・`s11_web640_over_tobi.png` | 終局 (トビ) |
| `s11_sim_lobby_old_host.png` | sim: 古いゲームホストの待ち合わせ (今までどおり) |
| `s11_sim_editor_one_key.png` | sim のエディタの 1 打鍵 |

### 確かめていないこと・気づいたこと

- 実際の対局で途中流局 (四開槓・四家立直)・流し満貫・包が起きる場面は画面で見ていない (試験と、作った局面で見た)。CPU の半荘で四風連打は起きた。
- 端末 (Tab5 ほか) では見ていない。ESP32 のビルドもしていない (C を変えていない)。
- ブラウザ版のページを `w=640&h=360` で開くと、デスクトップも卓のアプリも真っ黒のまま描かれなかった (426x240・852x480・1280x720 は出る。426x240 のページで卓のアプリが 640x360 に切り替わる形も出る)。この作業の変更は卓のアプリだけで、アプリ無しのデスクトップでも黒いので別の問題と見て、追っていない。開発サーバ (8016) の保存領域を消して 426x240 に戻した。
- sim の core のログに、error などの語に当たる行が 1 つあったが、中身を見る前に sim_down で container ごと消えた。卓のアプリとエディタは画面の上では正常だった。
- 3 人の和了の結果が 640x360 の箱に入りきるかは MJ3b から引き続き確かめていない。

### 片付け

試しのゲームホスト (SIGTERM、状態は作業用の場所)、開発サーバ (8016) とブラウザ (`web_down`)、sim (`sim_down`) を止めた。sim_up・sim_down のあとに `tools/mahjong_local.rb up` で zenohd の ws 7449 を戻し、`status` で zenohd・ユーザのゲームホスト (pid 418264)・8006 のページのサーバが動いていることを確かめた。卓 mahjong1 には入っていない。fmruby-core の build/ は Linux の標準構成 (x86-64) のまま、作業コピーは develop に戻した。
