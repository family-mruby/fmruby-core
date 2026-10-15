# MJ-E2: 雀魂の既定（四人）に合わせる（報告）

> 状態: 完了 (検収待ち) | 更新: 2026-10-16 | riichi_engine の Fork の `fmrb` は 0.1.1.fmrb4 (056e659、push 済み。その後の修正 1 件 cc5ffce・19d7084 は push していない)。ゲームホスト (asterism-games の af27fd6) が 0.1.1.fmrb4 の新しい鍵を雀魂の既定で有効にし、卓のアプリの欄の「西入」を「延長」に、終局の理由に「サドンデス」を足した (最後の章)

## 1. 監査（四人麻雀の各行）

「済」= エンジン＋ゲームホストですでにその動き。「直した」= 今回 Fork に鍵を足した（ゲームホストで有効にしたのは最後の章）。
「ホスト」= ゲームホストで今回直した。「残す」= 理由つきで手を付けていない。

### 共通基本ルール

| 行 | 判定 | 確かめ方 | 違い |
|---|---|---|---|
| 喰い断あり | 済 | `kuitan` true（host `RULE_DEFAULTS`）、host 試験 test_mahjong_local_rules（喰い断なしでロンが出ない） | なし |
| 後付けあり | 済 | `atozuke` true。エンジンは後付けを禁じる処理を持たない（常にあり） | なし |
| 飛びあり | 済 | `tobi` true、game_flow_spec「トビで即ゲーム終了」 | なし |
| 長考時間 5+20 | 残す（提案） | host の `turn_ms` 20 s・`call_ms` 8 s（1 手ごとの持ち時間で、持ち越しの時間は無い） | 下の「長考時間」 |
| 南入・西入（場の終了時に 1 位が必要点数未満なら次の場へ、以降サドンデス） | 直した | 半荘の西入は既存（`nishi_iri`）。東風戦の南入は無かった（`should_end_game?` が東4局の後に常に終わる）。サドンデスも無かった（西場は親が流れるときだけ終わり、西4局の後は西1局に戻る） | `feat/sudden-death`: `nan_iri`・`sudden_death` |

### 共通詳細ルール

| 行 | 判定 | 確かめ方 | 違い |
|---|---|---|---|
| ローカル役は不採用 | 済 | 役の一覧（`lib/mahjong/scoring/yaku/`）に人和・大車輪などのローカル役が無い | なし |
| 役満の複合あり | 済 | `multiple_yakuman` true、multiple_yakuman_spec | なし |
| ノーテン親流れ・聴牌連荘 | 済 | `GameFlow.renchan?`（流局は親のテンパイで連荘）、game_flow_spec | なし |
| サドンデスは必要点数（供託未収）以上で終了、ダブロンで親の連荘があれば連荘優先 | 直した | `feat/sudden-death`。点数は局の後の持ち点（場の供託は数えない） | 細部は下の「決めたこと」 |
| サドンデスの最大は東風戦は南場まで、半荘は西場まで | 直した | `sudden_death` で延長は 1 つの場だけ（その場の 4 局の後に `:normal_end`）。ゲームホストは西4局の後に自分で終えていた（`"west"`） | エンジンが終えるようになる |
| マイナスで飛び終了、マイナス点数も集計、0 点は続行 | 済 | game_flow_spec（ちょうど 0 点は次局）、`GameResult.calculate` は点をそのまま使う | なし |
| ラス親トップかつ必要点数以上で自動和了り止め・聴牌止め | 一部ホスト・一部直した | host の `tenpai_yame` を true にした（今回）。エンジンの判定は「首位で 0 点より上」で、必要点数を見ていなかった | `feat/sudden-death`: 延長戦のあるルールでは親に必要点数が要る |
| 終了時の供託はトップ取り | ホスト | host が終局で `calculate_ranked_results` に渡すだけで、場の供託はどこにも行っていなかった | `Table#final_results` で 1 位に足した。試験 test_the_sticks_left_go_to_the_top_and_ties_go_by_seat |
| 同点は起親に近い方が上位 | 済（ホストの起親が席 0 のため） | `GameResult.calculate` は同点を席番号で並べる。ゲームホストの起親は常に席 0（`start_game` の `"oya" => 0`）なので、席番号順＝起親に近い順。試験は同上 | エンジンには起親を渡す口が無い（起親が席 0 でない使い方では違う）。今回は直していない |
| 常に 1 飜縛り | 済 | 役の無い和了は候補に出ない（`can_agari?`） | なし |
| 流し満貫あり、聴牌清算の代わりに満貫清算、親の聴牌で連荘 | 済 | `nagashi_mangan` true、nagashi_mangan_spec、host test_nagashi_mangan_is_paid_in_place_of_the_noten_payments、`renchan?` は流し満貫でも親のテンパイを見る | なし |
| 包（大三元・大四喜のみ）、ツモ全額・ロン折半 | 済 | host `pao` = 大三元・大四喜、pao_spec、host test_pao_on_a_ron… / test_pao_on_a_tsumo… | なし |
| 積み棒は包 | 直した | ロンのとき積み棒を放銃者が払っていた（`ScoreResult#pao_score_changes`。ツモは責任者で一致） | `feat/pao-honba`: `pao_honba` = `"liable"` |
| 複合役満の包はパオ部分のみ、ほかは通常精算 | 済 | `WinEvaluator.pao_parts` が包の役満の基本点だけを責任者に回し、残りは `pao_score_changes` でふつうに払う | なし |
| 大明槓の責任払いなし | 済 | 嶺上開花の責任払いの処理が無い。`pao` に四槓子を入れていない | なし |
| 国士無双の暗槓の搶槓あり | 直した | 搶槓は加槓だけ（`detect_chankan_actions` は `process_kakan` からだけ） | `feat/kokushi-ankan-chankan`: `kokushi_ankan_chankan` |
| 槓ドラ・槓裏あり。暗槓は即乗り、明槓・加槓は後めくり（打牌後または続く槓の直前） | 一部直した | 時機は既存の `minkan_dora_after_discard`（kan_dora_spec）で一致。**槓裏が和了の点に入っていなかった**: `WinEvaluator` は `RoundState#uradora_hyouji` を読むが、槓でそこが増えない（作業用の一時的な道具で暗槓後に 1 枚のままを確かめた） | `fix/kan-uradora`（誤りの修正、キーなし） |
| 形式聴牌あり。手持ち 4 枚の単騎待ちのみ不可 | 済（細部 1 つ残す） | `MachiJudge.find_machi` は手に 4 枚ある牌を待ちにしない（`next if work[i] >= 4`）。役の有無はテンパイの判定に入らない | ポンした牌の 4 枚目の単騎（手の内は 1 枚）はテンパイと数える。雀魂の文面「手持ち 4 枚」どおりと読んだが、確かではない |
| フリテンリーチあり、カラテンリーチあり | 済 | `can_riichi?` はフリテンも見えている牌の数も見ない | なし |
| リーチは持ち点 1000 以上でツモ番があるとき | 済 | `can_riichi?`: `scores >= 1000`、`wall.remaining >= 4` | なし |
| リーチ後のアガリ牌見逃しは以降フリテン | 済 | `process_skip` が `riichi_furiten_flags` を立て、`FuritenJudge` が見る。host は鳴きの解決で選ばれなかった席に必ず skip を当てる（`Table#resolve_calls`） | なし |
| リーチ後の暗槓は待ちが変わらないときのみ、送り槓不可、牌姿や役の増減は不問 | 直した | リーチ中は暗槓の候補が一切出なかった（`available_actions` の `unless riichi_flags`） | `feat/riichi-ankan`: `riichi_ankan` |
| 見逃し（搶槓を含む）後は同巡フリテン、次の自分のツモ番まで | 済（細部 1 つ残す） | `process_skip`（`chankan_tile` も見る）、`advance_turn` が自分の手番で解く | 見逃しはロンの候補が出た席だけで判定する。役が無くてロンの出ない聴牌の席はフリテンにならない。雀魂の扱いを確かめられないので残した |
| 連風牌の雀頭は 4 符 | ホスト | エンジンに既存の `renpuu_jantou_fu`（既定 2）。host を 4 にした | なし |
| 嶺上開花のツモは通常のツモと同じ 2 符 | 済 | `FuCalculator`: 自摸は常に +2（嶺上で 0 にする処理は無い） | なし |
| 天和は最も高得点となる牌をツモ牌とする | 直した | 最後に自摸った牌で評価していた（四暗刻か四暗刻単騎か等が配牌の順で変わる） | `feat/tenhou-best-tile`: `tenhou_best_tile` |

### 四人麻雀

| 行 | 判定 | 確かめ方 | 違い |
|---|---|---|---|
| 配原 25000 | 済 | `initial_score` 25,000 | なし |
| 一位必要点数 30000 | 済 | `nishi_iri_threshold` 30,000 | なし |
| 赤ドラ 3 枚 | 済 | `aka_dora` 1/1/1 | なし |
| 25000 点返し | ホスト | host の `oka_origin` が 30,000（1 位にオカ 20,000 が付いていた） | 25,000 にした。試験の終局の点（+22.0 / +3.0 / -7.0 / -18.0） |
| ウマ 5-15（順位ウマのみ） | 済 | `uma` 15/5/-5/-15 | なし |
| 喰い替えなし | 直した | 鳴いた直後の打牌に制限が無かった | `feat/kuikae`: `kuikae` = false |
| 途中流局（九種九牌・四家立直・四槓散了・四風連打）あり、すべて連荘 | 済 | abortive_draw_spec、`GameFlow::ABORTIVE_DRAWS`（連荘・本場 +1）、host の既定はすべて true | なし |
| 四家立直・四槓散了では流し満貫不成立・精算なし | 済 | 流し満貫は荒牌流局（`:ryuukyoku`）だけで調べる。途中流局は host の `score` の `else`（点の移動 0） | なし |
| ダブロン・トリプルロンあり | 済 | `double_ron` true、`triple_ron_draw` false（3 人とも和了） | なし |
| 積み棒・供託は上家取り | 済 | host の `score` が放銃者から順で最初の和了者だけに本場と供託を渡す（test_mahjong_rules のダブロン） | なし |
| 和了者に親が含まれれば連荘 | 済 | `renchan?` は和了者のどれかが親なら連荘 | なし |

### 長考時間（提案。今回は変えていない）

雀魂は「5+20」: 1 手ごとに 5 秒、それを超えた分は 20 秒の持ち越しの時間から減る。ゲームホストは 1 手ごとの持ち時間（打牌 20 秒・鳴き 8 秒）だけで、持ち越しは無い。

提案: 席ごとに `bank_ms`（20,000）を持ち、打牌・鳴きの締め切りを「`turn_ms`（5,000）＋残りの `bank_ms`」にし、5 秒を超えて使った分を `bank_ms` から引く。見え方には締め切りに加えて残りの持ち越しを送る（卓のアプリに表示を足す）。持ち越しをいつ戻すか（局ごと・半荘を通して）は確かめていないので、ユーザに決めてもらう。鳴きの時間も同じ持ち越しから引くかも同じ。

### 決めたこと（文面で決まらない細部）

- サドンデスの「ダブロンで親の連荘がある場合は連荘を優先」は、親を含む複数人のロンのときだけ続けると読んだ。親だけの和了・親のテンパイ流局の連荘で誰かが 30,000 以上なら終わる。
- 延長戦の 4 局目（半荘の西4局・東風戦の南4局）もオーラスとする。首位の親が 30,000 以上で親を含む複数人のロンのときは、アガリ止めより連荘を先にする（2026-10-16 ユーザの決定「ダブロンで親の連荘がある場合は連荘を優先」。最初はアガリ止めを先にしていたので、Fork で直した。最後の章）。親だけの和了はアガリ止め。
- 延長戦のある（`nishi_iri`・`nan_iri` が真の）ルールでは、アガリ止め・テンパイ止めに必要点数を求める。延長戦の無いルールは 0.1.0 のまま（0 点より上の首位）。
- 喰い替えは現物（鳴いた牌と同じ種類）と筋（チーの反対側）の両方を禁じ、切れる牌が残らない鳴きは出さない。嵌張のチーは鳴いた牌だけ。

## 2. Fork（riichi_engine）

各ブランチは `main`（0.1.0）か、依存するブランチから作った。どれも修正前に落ちて修正後に通る spec を持つ（下の「落ちた数」は lib を戻して新しい spec だけ回したもの）。鍵の既定はどれも 0.1.0 の動き。

| ブランチ | 土台 | コミット | 鍵（既定） | spec | 落ちた数 |
|---|---|---|---|---|---|
| `fix/kan-uradora` | `fix/dora-indicators` | 1dbbd04 | （誤りの修正） | flow/rounds/kan_uradora_spec.rb（新規、3） | 3/3 |
| `feat/kuikae` | main | 0bd7eea | `kuikae`（true） | flow/rounds/kuikae_spec.rb（新規、6） | 3/6 |
| `feat/riichi-ankan` | main | 73269ee | `riichi_ankan`（false） | flow/rounds/riichi_ankan_spec.rb（新規、5） | 2/5 |
| `feat/kokushi-ankan-chankan` | main | 6292aa9 | `kokushi_ankan_chankan`（false） | flow/rounds/kokushi_ankan_chankan_spec.rb（新規、3） | 2/3 |
| `feat/pao-honba` | `feat/pao` | 3e0e663 | `pao_honba`（`"discarder"`、`"liable"` で責任者） | scoring/evaluators/pao_spec.rb（+2） | 1/13 |
| `feat/tenhou-best-tile` | `feat/double-yakuman` | 326bcf2 | `tenhou_best_tile`（false） | scoring/evaluators/tenhou_best_tile_spec.rb（新規、5） | 2/5 |
| `feat/sudden-death` | `feat/agari-yame` | 459ea20 | `nan_iri`・`sudden_death`（false） | flow/games/game_flow_spec.rb（+9） | 5/32 |

- `fix/kan-uradora` を `fix/dora-indicators` の上にしたのは、`main` ではドラを表示牌そのもので数える誤りがあり、試験の点が変わるため。
- `feat/sudden-death` の互換の注意: `nishi_iri` を真にしているときだけ、南4局のアガリ止め・テンパイ止めに必要点数が要るようになる（下書きに書いた）。
- `feat/kuikae` は `DahaiSelector.select(..., allowed:)` を足した（CPU が打牌の候補から選ぶ）。公開 API はメソッドとキーワードの追加だけで、壊していない。

`fmrb` への merge: 7 本を merge（衝突は `RuleConfig::DEFAULTS` と api_reference.md の表の末尾、`reveal_kan_dora`・`process_ankan`・`finalize_kakan`・`BaseAi`・`DahaiSelector`・`ActionValidator`。どれも両方を残した）。版を 0.1.1.fmrb4 に、CHANGELOG と下書き 7 本（`pr_drafts/`、日本語）と索引（土台・隣り合う変更・ルールの選び方・未着手）を足した（056e659）。

| 確認 | 結果 |
|---|---|
| `RBENV_VERSION=3.2.6 bundle exec rspec`（`fmrb`） | 440 → 473 examples、0 failures（突き合わせの spec 1 件を含む） |
| Python `mahjong` 2.0.0 との突き合わせ | 代表 61 + 乱数 2000（seed 1）で違い 0・決まりの違い 0。`DOUBLE_YAKUMAN=1`、乱数 2000（seed 7）でも 0 |
| CPU 4 人の対局（作業用の一時的な道具、新しい鍵をすべて有効＋雀魂の既定） | 半荘 20 回 248 局、東風 20 回 118 局で例外なし、毎局の点の合計 100,000、北場に入らない。サドンデスで終わった対局: 半荘 1（西場）、東風 2（南場）。喰い替えで候補が絞られた手番 558 / 291。槓裏が乗った和了 3 / 1 |

各ブランチの spec はそのブランチの上で全体も通した（kan-uradora 310、kuikae 310、riichi-ankan 309、kokushi 307、pao-honba 317、tenhou 324、sudden-death 319、どれも 0 failures）。作業用の worktree は消した（ブランチは残っている）。

## 3. ゲームホスト（asterism-games）

### 今回したこと（2f12d04 のエンジンで動く分。757392d）

- `RULE_DEFAULTS`: `oka_origin` 30,000 → 25,000（25000 返し）、`renpuu_jantou_fu` 4、`tenpai_yame` true。
- 終局: `Table#final_results` が場に残った供託を 1 位に足してから順位を出す（`@game["scores"]` と `kyoutaku` も直す）。同点は席番号順＝起親（席 0）に近い順。
- 試験: 鍵の既定（`tenpai_yame`・`oka_origin`・`renpuu_jantou_fu`）、終局の供託と同点（23,000 が 2 人、席 0 が上）、ローカルルールの既定の行（テンパイ止めあり）。`MJ_GAMES=8 bundle exec rake test`: 88 runs、0 failures（ネットワークの試験 1 件は飛ばす）。
- README の "Rules" の表を雀魂の既定に合わせ、fmrb4 を待つ行を足した。

ローカルルールの画面: 意味の変わった項目は無い（テンパイ止めは既定値が変わっただけ）。fmruby-core（卓のアプリ）は変えていない。連風牌の 4 符・2 符は欄に足していない（15 行で埋まっている）。

### push の後に残っていたもの（最後の章ですべて行った）

1. `bundle update riichi_engine`（前回、`bundle lock --update` は revision を残したので `bundle update` を使う）。
2. `RULE_DEFAULTS` に足す: `"kuikae" => false`、`"riichi_ankan" => true`、`"kokushi_ankan_chankan" => true`、`"pao_honba" => "liable"`、`"tenhou_best_tile" => true`、`"nan_iri" => true`、`"sudden_death" => true`。
3. `Table#next_round` の `west_over`（西4局の後に終える）は、`sudden_death` でエンジンが終えるので残しても害は無い。終局の理由に `sudden_death` が増えるので、`fr` と卓のアプリの終局の見出しに名前を足す（「終局 (サドンデス)」など、fmruby-core の `mahjong_panels.rb`・`mahjong_xl.rb`）。エンジンが延長戦の 4 局目の後に返す理由は `normal_end`（今の「西4局まで」の代わり）。
4. ローカルルールの項目 9（`nishi_iri`、「西入」）は、東風戦では南入の意味にする（`["nishi_iri", %w[nishi_iri nan_iri]]` のように 2 つの鍵を動かす）。名前を「延長戦」などに変えるかは卓のアプリの表示と一緒に決める。
5. CPU の打牌: 喰い替えの禁止で CPU が候補外の牌を選ぶことは無くなった（エンジンの `BaseAi`）。`Engine.cpu_choice` はそのままでよい。
6. 試験を足す: 喰い替え（候補に出ない・act が断られる）、リーチ後の暗槓、国士の暗槓の搶槓（ロンの候補と結果）、包のロンの積み棒、東風戦の南入とサドンデスの終局（理由が見え方に出る）、`test_the_hosts_rule_keys_reach_the_engine` に新しい鍵。
7. README の表の「not yet」「until fmrb4」を外し、"riichi_engine" の節（版、`fix/kan-uradora` を 0.1.0 の誤りの表に 23 として）を直す。

## 4. ユーザに決めてもらうこと

- 長考時間を 5+20 にするか（上の提案。持ち越しをいつ戻すか）。
- 役の無い聴牌でアガリ牌を見逃したときの同巡フリテン、ポンした牌の 4 枚目の単騎のテンパイ（どちらも今は残した）。

## 5. 片付け（Step 1-2 の時点）

ゲームホスト・zenohd・ページのサーバは起動していない（止まったまま）。sim・ブラウザ版も使っていない。fmruby-core は触っていない（develop のまま）。作業用の worktree（作業用の一時ディレクトリ）は消した。

## 6. ゲームホストと卓のアプリへの組み込み

Fork の `fmrb`（0.1.1.fmrb4、056e659）が push された後に、3 章の残りをゲームホストと卓のアプリに入れた。

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| riichi_engine（Fork） | `feat/sudden-death` → `fmrb` | cc5ffce（ブランチ）、0a5b33d（merge）、19d7084（下書きと CHANGELOG） | 延長戦の 4 局目の、親を含む複数人のロンの連荘をアガリ止めより先にする（下の「Fork の修正」） |
| asterism-games | main | af27fd6 | Gemfile.lock を 056e659 に、`RULE_DEFAULTS` に 7 鍵、ローカルルールの項目 9 を「延長」に、試験、README |
| fmruby-core | feature/mj-e2 | この報告と同じコミット | 卓のアプリ（`mahjong_panels.rb`）: 項目 9 の名前、終局の理由「サドンデス」 |

push・タグ・PR はしていない。

### Fork の修正（1 件）

ユーザの決定「ダブロンで親の連荘がある場合は連荘を優先」を確かめたところ、`GameFlow.judge_next` はサドンデスより先に連荘を優先していた（延長戦の 1-3 局目は決定どおり）が、延長戦の 4 局目（半荘の西4局・東風戦の南4局）では、首位の親が 30,000 以上ならアガリ止め（`all_last_oya_stop`）が先に効いてゲームを終えていた（1 章の「決めたこと」で最初にそう決めていた）。

- 直し: 延長戦の場で、和了者が 2 人以上かつ親を含むときは、アガリ止めもサドンデスも見ずに連荘にする。親だけの和了は今までどおりアガリ止め。延長戦でない南4局（半荘）は、複数人のロンでもアガリ止めのまま。
- spec: `game_flow_spec.rb` の「サドンデスの延長戦の 4 局目は、首位の親の複数人ロンでもアガリ止め」を「…首位の親を含む複数人ロンならアガリ止めより連荘を先にする」（半荘の西4局・東風戦の南4局の 2 つ）に書き換え、「親だけの和了はアガリ止め」「延長戦でない南4局はアガリ止め」の 2 例を足した。lib を戻すと書き換えた 1 例が落ちる。`feat/sudden-death` 上で 321 examples、`fmrb` で 475 examples、0 failures。
- 書いたもの: `api_reference.md` の `sudden_death`、`state_machine.md`、`pr_drafts/feat-sudden-death.md`、CHANGELOG（0.1.1.fmrb4 の欄に足した。版の名前は変えていない）。
- **ゲームホストの Gemfile.lock は 056e659 のまま**（この修正は push していないので取れない）。延長戦の 4 局目で、首位の親が 30,000 以上で親を含む複数人のロンという場面だけ、ゲームホストは今もアガリ止めで終える。Fork の push の後に `bundle update riichi_engine` で取り込む。

### 規則の既定

`Engine::RULE_DEFAULTS` に足した鍵（Fork の `RuleConfig` で意味を確かめた）:

| 鍵 | 値 | 意味 |
|---|---|---|
| `kuikae` | `false` | 喰い替えの禁止（`true` が「喰い替えあり」で、Fork の既定） |
| `riichi_ankan` | `true` | リーチ後の暗槓（待ちが変わらないとき） |
| `kokushi_ankan_chankan` | `true` | 国士無双の暗槓の搶槓 |
| `pao_honba` | `"liable"` | 包のロンの積み棒は責任者 |
| `tenhou_best_tile` | `true` | 天和は最も高い点になる牌を和了牌とする |
| `nan_iri` | `true` | 東風戦の南入 |
| `sudden_death` | `true` | 延長戦のサドンデス、延長は 1 つの場まで |

`Table#next_round` の「西4局の後にゲームホストが終える」（理由 `west`）は残した。`sudden_death` ではエンジンが先に終えるので通らない。卓の設定で `sudden_death` を外したときだけ効く。

### ローカルルールの項目 9「延長」

- 項目 9 は `["extra", %w[nishi_iri nan_iri]]`。値を変えると 2 つの鍵を同じ値にする。見え方の値は、選ばれている対局の鍵（半荘は `nishi_iri`、東風戦は `nan_iri`）。
- 「ゲームホストの値に戻す」は鍵ごと（途中流局のような「項目の値がゲームホストの値と同じなら全部外す」にすると、ゲームホストの設定と対局の種類が違うときに片方の鍵が戻らない）。
- 見え方の鍵の並びは変えていない（`ru` は 11 個、項目の番号も同じ）。古い卓のアプリ（「西入」）と新しいゲームホストの組では、「西入」を切り替えると 2 つの鍵が動き、東風戦では南入の値が出る。新しい卓のアプリ（「延長」）と古いゲームホストの組では、「延長」が `nishi_iri` だけを動かす（古いゲームホストには南入が無い）。どちらの組でも欄は描けて、切り替えも通る。
- 卓のアプリは、東風戦のときに項目 9 を薄く描いていた（西入は半荘だけだったため）のをやめた。古いゲームホストで東風戦を選ぶと、「延長」が効かない値のまま普通の色で出る。

### 終局の理由

見え方の `fr` に `sudden_death` が増えた（エンジンの理由がそのまま来る）。卓のアプリは `over_head`（640x360 と 1280x720 で共通）で「終局 (サドンデス)」と出す。延長戦の 4 局目の後はエンジンの理由が `normal_end`（「終局 (最後の局まで)」）。

### 試験と確認

| 確認 | 結果 |
|---|---|
| asterism-games `MJ_GAMES=8 bundle exec rake test` | 93 件、失敗 0（ネットワークの試験 1 件は飛ばす）。東風戦の試験の対局は南場に入った（`winds ["ton", "nan"]`） |
| 新しい試験（`test_mahjong_round_ends.rb`） | 新しい 7 鍵がエンジンに届く、包のロンの積み棒（2 本場: 和了 +32,600、責任者 -16,600、放銃者 -16,000）、チーの後の喰い替え（45m で 3m をチーした後に 3m・6m が候補に無く、6m の打牌は `not offered`）、西1局の子のツモで 30,000 に届いてサドンデスで終わる（`fr` が `sudden_death`）、東風戦の東4局の後に南場へ（「延長」なしなら終わる） |
| 試験が効いているか | 鍵を Fork の既定に戻す（`pao_honba` `"discarder"`、`kuikae` `true`、`sudden_death` `false`）と、それぞれの試験が 1 件ずつ落ちることを確かめた |
| ローカルルールの試験（`test_mahjong_local_rules.rb`） | 「延長」が 2 つの鍵を動かす、値は選ばれた対局の鍵、ゲームホストの設定が片方だけ（`nan_iri` なし）のときの戻り方。東風戦の対局の試験は「東場だけ、または南場まで」に直した |
| 1 通の大きさ | 変わらない（試験の最悪 2,110 B。理由の文字列は前からある `all_last_oya_tenpai` より短い）。CPU の半荘で見え方の最大 1,019 B、試しのゲームホストで実際に送った最大 455 B |
| fmruby-core `rake test` | 通った（golden 103 passed、MicroPython の smoke） |
| fmruby-core `rake wasm:web` | 通った（`emsdk_env.sh` を読んでから） |
| sim の標準構成（Modern 426x240、x86-64） | 試しのゲームホスト（ノード `gamesmje2`、卓 mahjong7・mahjong8、状態は作業用の一時ディレクトリ）に、卓のアプリが入った。mahjong8 は試験と同じ手順で作ったサドンデスの終局を保存したファイルから起こしたもので、観戦者として「終局 (サドンデス)」を見た。エディタを開いて 1 打鍵（`Ln 1, Col 2 *`）。core のログに例外なし |
| ブラウザ版（開発サーバ 8016） | 1280x720 で mahjong7 の卓の主として対局を東風にし、「延長」をクリックで なし → あり（ゲームホストの保存に `nishi_iri`・`nan_iri` が付いて外れ、`game_type` だけ残る）。426x240 のページ（卓のアプリは 640x360）で上下キーで「延長」を選び、右キーで なし（保存は `{"game_type"=>"tonpuu", "nishi_iri"=>false, "nan_iri"=>false}`）。mahjong8 の終局を両方の大きさで |

### 画面の写し

`C:\Users\Public\asterism_deploy\screenshots\mj3\`:

| ファイル | 中身 |
|---|---|
| `s12_web1280_lobby_extra_tonpuu.png` | 1280x720、卓の主の欄。対局 東風、延長 あり（南入）を選んだところ |
| `s12_web640_lobby_extra_off.png` | 640x360、キーで「延長」を選んで なし にしたところ |
| `s12_web1280_over_sudden_death.png`・`s12_web640_over_sudden_death.png` | 西1局の子のツモ（30,100 点）で「終局 (サドンデス)」 |
| `s12_sim_over_sudden_death.png` | sim（426x240）の同じ終局 |
| `s12_sim_editor_one_key.png` | sim のエディタの 1 打鍵 |

### 確かめていないこと

- 実際の対局で喰い替えの禁止・リーチ後の暗槓・国士の暗槓の搶槓・天和の和了牌が画面に出る場面は見ていない（ゲームホストの試験と Fork の spec で確かめた）。
- 古い卓のアプリと新しいゲームホスト、新しい卓のアプリと古いゲームホストの組は、コードを読んで確かめただけで、実際には動かしていない。
- 端末（Tab5 ほか）では見ていない。ESP32 のビルドもしていない（C を変えていない）。
- Fork の修正（cc5ffce）はゲームホストに入っていない（上の「Fork の修正」）。

### 片付け

試しのゲームホスト（SIGTERM。状態は作業用の一時ディレクトリ）、開発サーバとブラウザ（`web_down`。ページの解像度は `w=426&h=240` で開き直して 426x240 に戻した）、sim（`sim_down`）を止めた。sim_up が作り直した zenohd は、ブラウザ版の確認に使った後に止めた（作業の前と同じく止まったまま）。fmruby-core の build/ は Linux の標準構成（x86-64）のまま、作業コピーは develop に戻した。
