# R1 指示書: Asterism に「繋がり続ける口」を作り、step_sensor・walk_scroller・asterism_demo を載せ替える

> 状態: 指示 | 更新: 2026-10-24 | plan.md の R1。各アプリが自前で書いている「WiFi を待つ、繋ぐ、繋ぎ直す、前の自分が網に残っていて断られたら待つ、状態を出す」を、fmrb-asterism の共通の口に移す。振る舞い (画面・網に出す値・操作) は変えない

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 今の作り (読んでから設計する)

- `flash/app/test/step_sensor.app.rb` の `network_ready?`・`connect`・`network` (WiFi 待ち WIFI_WAIT_MS、画面の準備 SCREEN_WAIT_MS、RETRY_MS ごとの繋ぎ直し、「already on the network」の待ち、`Asterism.reconnecting?`、切れたときの close と理由、接続の前に「AST ..」を描く)。
- `flash/app/demo/walk_scroller.app.rb` と `flash/app/test/asterism_demo.app.rb` の同じ役の部分 (少しずつ違う。違いを表にする)。
- オセロ・ボード・麻雀の `network_ready?`・`connect`・`reconnecting?` (R4 で載せ替えるので今回は触らないが、口の形はこれらにも合うように設計する: `on_join` を繋ぐたびに登録している、など)。
- `lib/add/picoruby-fmrb-asterism/mrblib/fmrb-asterism.rb` (`FmrbAsterism.settings`) と asterism の gem の `Asterism.connect` / `close` / `connected?` / `reconnecting?` / `lost_reason` / `poll` / `expose` / `on_join`。

## 2. 作るもの

1. **共通の口** (例 `FmrbAsterism::Link`。名前と形は読んでから決めてよい): アプリは「繋がったらこれをする (expose・購読・on_join)」を一度書き、毎回の更新で 1 回呼ぶだけ。口が受け持つこと:
   - WiFi を待つ (ブラウザ版・sim のように WiFi の情報が無ければすぐ)、待ちすぎたら諦めて試す (今の WIFI_WAIT_MS の扱い)。
   - 任意で「画面の準備ができるまで待つ」(step_sensor の SCREEN_WAIT_MS の扱い)。
   - 接続が止まる (TLS の握手で 10〜20 秒) 前に、アプリが状態を描けるよう、接続の直前に知らせる (コールバックか、状態が「接続中」になった更新を 1 回挟む)。
   - 失敗・切断で決まった間隔で繋ぎ直す。「already on the network」は待ってから繋ぎ直す。
   - poll を回す。切れたら close し、理由を残す。
   - 状態を 1 つの値で返す (例 `:waiting_wifi` `:connecting` `:connected` `:reconnecting` `:retrying` と理由の文字列)。
   - 繋ぎ直すたびに、アプリの「繋がったら」の処理が流れる。
2. **載せ替え**: step_sensor → walk_scroller → asterism_demo の順に、自前の接続の処理を口に置き換える。アプリの行数の前後を report に。
3. **試験**: 口の状態の移り変わりを、ホストの CRuby で偽の Asterism を相手に試す (既存の試験の流儀に合わせる、例 tool/imu/test や test/)。

## 3. 確認

- **step_sensor (StickS3 の実機)**: tab5_fs で置き換えて起動し (ip 192.168.10.19、焼かない、シリアルを開かない)、画面が「WiFi ok / AST ..」→「AST ok」になる。いま VPS のルータに mTLS で繋がっている (`/home/asterism/config.toml` と証明書の tar には触らない)。asterism の MCP (VPS 側) で値と `state` が読め、歩数を送り、`tune`・`save` が前と同じに動く (保存してあるしきい値 130 mg / 250 ms は変えない。試したら必ず 130 / 250 に戻して保存しておく)。アプリを kill して起動し直すと「already on the network」を待って繋がる。
- **walk_scroller (ブラウザ版)**: 手元の zenohd (親の compose に lan と ws を重ねる、private-notes にある手順を参照してよい) と `tool/imu/step_send.rb` で、歩数で歩く、zenohd を止めると赤、戻すと繋がり直す、が前と同じ。T の調整の画面も動く。
- **asterism_demo**: sim で手元の zenohd に繋がる (H3 の確認と同じ)。
- `rake test` と新しい試験。Retro・Tab5・StickS3 のビルドで内蔵 RAM が増えない (共通の gem を変えたら、Retro と Tab5 の `idf.py size` の D/IRAM を前後で比べる)。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の `lib/add/picoruby-fmrb-asterism/`、3 つのアプリ、試験、report。gem を変えたら `rake clean` (lib/ を触ったら) の決まりを守る。
- **触らないもの**: オセロ・ボード・麻雀 (R4)、asterism の gem 本体・asterism-games・console (要るなら止まる)、sdkconfig、Tab5 (焼かない)、VPS、`~/asterism_keys/`、StickS3 の証明書と config.toml。
- push・タグはしない。`git commit -a` は使わない。件名 `asterism:` `app:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。実在の値 (IP・ドメイン・端末名) をコミット・doc に書かない。doc (この指示書・report) はコミットしない (親がする)。
- 終わったら sim・web を片付ける。StickS3 は新しい step_sensor で VPS に繋がった状態で残す。
- **止まる条件**: asterism の gem 本体の変更が要る、内蔵 RAM が増える、振る舞いを変えないと載せ替えられない、StickS3 を VPS に戻せない。
- **report**: `doc/app_refactor/report/r1.md` (常体): 3 アプリの違いの表、口の設計、載せ替えの前後の行数、確認の結果、RAM、R4 (卓のアプリ) で口をどう使うかの見通し、残件、コミット。
