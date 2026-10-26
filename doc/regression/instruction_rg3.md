# RG3 指示書: 相手のいる通しを、相手を道具が立てて自動で流す

> 状態: 指示 | 更新: 2026-10-26 | plan.md の RG3。アプリが外の相手とやり取りする場面を、相手を道具が立てて sim で自動で流し、相手の側の状態 (卓の様子・受けたバイト列・音の数値) で確認する。相手は差し替えられる形にし、Asterism はその 1 つ (Asterism に限らない、2026-10-25 ユーザと確認)。R4a/R4b でサブが手でやっていた部分を道具にする。**実機は使わない**

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 読むもの

- doc/regression/plan.md、usage.md、report/rg2.md (台本の書き方。RG3 も同じ台本の形に乗せる)・rg3b.md (sim の起こし方)。
- 相手の道具:
  - Asterism のゲームホスト: asterism-games を手元で起こす口 (`rake mahjong:up` など、rakelib を grep)、端末のクライアント `asterism-games play ...`、doc/archive/app_refactor/report/r4a.md・r4b.md (卓に入る・出る・ホストを止めて戻す、を手でやった手順)。
  - zenohd と CRuby の Asterism の相手 (歩数の送り手 step_send など、doc/headless_s3/ と walk_scroller)。
  - MIDI: `tools/fmrb_midi_monitor.rb` (ルート)。音: `tools/fmrb_audio_probe.rb`。
  - HTTP の偽の相手 (天気などのアプリが使う口があれば)。
- 端末の証明書の扱い (sim で Asterism に繋ぐとき、手元の zenohd に繋ぐ。VPS には繋がない)。

## 2. 作るもの

1. **相手の差し替えの形**: 台本 (RG2 の DSL) から `counterpart :games, table: :othello` のように相手を立て、場面の終わりに片付ける。相手ごとに「立てる・状態を読む・止める・戻す」を持つ小さなクラス (`tools/regress/counterparts/`)。
2. **場面** (少なくとも):
   - オセロの卓・ボード・麻雀: 一覧 → 入る → 打つ/描く (1〜2 手) → ホストを止めて戻す (アプリが繋ぎ直して卓に戻る) → 出る。相手の卓の様子 (誰が座っているか、手番、盤面) で確認する。
   - walk_scroller: CRuby の送り手から歩数を流し、アプリの表示 (RG2 の撮影か、アプリが出すログ) で進んだことを確認する。
   - MIDI のアプリ: 出力を serial に切り替えて音階を鳴らし、モニタの受けたバイト列 (ノートの並びと間隔) を基準と比べる。
   - 音: 内蔵音源で 1 本鳴らし、`fmrb_audio_probe.rb` の数値 (鳴ったか、音の窓数) で確認する。
3. **入口**: `rake regress:live` (名前は決めてよい、`ONLY=` で場面を絞る)。`rake regress:full` に入れる。出力は短い要約。
4. 相手の側の基準 (卓の様子・バイト列の要約) は test/regress/ に置き git に入れる。揺れる物 (時刻・ID) は比べない。

## 3. 確認

- 全場面が 3 回続けて通る。揺れた所の扱いを report に。
- わざと壊す確認: 相手を止めたまま戻さない場面で落ちる、など (ファームのコードは変えない)。
- 流した時間と出力の行数を report に。R4a/R4b で手でやった確認のうち、どれが置き換わったか。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の tools/regress/、test/regress/、rakelib/、doc/regression/usage.md。asterism-games・ルートの tools/ は読むだけ (直す必要があれば止まる)。
- **触らないもの**: 実機、VPS (手元の zenohd とゲームホストだけを使う)、`~/asterism_keys/` (読むだけでも中身を出力しない。sim 用に要る鍵の作り方は既存の手順に従い、新しい鍵を作るなら手元専用)、sdkconfig、`.env` の既定、アプリ・gem・ファームのコード。
- push・タグはしない。`git commit -a` は使わない。件名 `tools:` `test:`、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc/regression の指示書・report はコミットしない。実在の値 (IP・ドメイン・端末名・鍵) を台本・基準・コードに入れない。
- 終わったら sim・web・相手 (zenohd・ゲームホスト・モニタ) を片付ける。
- **止まる条件**: 相手を立てるのに VPS や本物の鍵が要る、asterism-games を直さないと進めない、ファームの不具合が見つかった (追わずに返す)、揺れが止められない。
- **report**: `doc/regression/report/rg3.md` (常体)。
