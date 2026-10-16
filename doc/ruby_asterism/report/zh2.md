# ZH2 報告: zenohd が新しいセッションを受けなくなったら console が起こし直す (console 0.3.2)

> 状態: 完了 | 更新: 2026-10-17 | instruction_zh2.md の実装と確かめの記録。見張りは bridge のプロセスの中の別スレッドで、30 秒ごとに新しいセッションを開いて閉じ、2 回続けて失敗したら Apply と同じ道 (`restart.request`) で zenohd を起こし直す。手元の構成 (deploy/vps と同じ docker、zenohd 1.10.1) で負荷をかけて 3 回止め、3 回とも見張りの最初の失敗から 40.3-40.7 秒で起こし直し、止まってから 51-87 秒で新しいセッションが開けるようになった。負荷なしの 30 分と Apply の直後で自動の起こし直しは 0 回。asterism-console 3cbe318 (main、push・タグなし)

## 1. 見張りの形

| 項目 | 中身 |
|---|---|
| どこで動くか | bridge のプロセスの中の別スレッド (`lib/bridge/router_watch.rb`、`Bridge::RouterWatch`)。bridge のつなぎ直しの輪 (`serve`) の外で始めるので、bridge が router を失ってつなぎ直している間も動き続ける。docker の権限は使わない |
| 試し方 | bridge と同じ locator と証明書 (`Runner.zenoh_config`、JSON5 のファイルのときは `config_file:`) で、client のセッションを**毎回新しく**開いてすぐ閉じる。`connect/timeout_ms` と `transport/unicast/open_timeout` を上限にし、さらに Ruby の側で上限 + 5 秒待っても戻らないスレッドは失敗と数える (戻るまで次の試しを重ねない) |
| 起こし直し | Apply と同じ `Relay::Settings.restart_command` (既定 `bin/relay_restart_request` が `restart.request` を書き、`deploy/vps/zenohd/run.sh` がそれを見て起こし直す)。run.sh は変えていない |
| 数えない間 | `restart.request` の時刻と最新の RelayApply の開始・終了のうち新しい方から `GRACE` 秒、および Apply の実行中。この間は試さず、数えかけの失敗も捨てる |
| 繰り返しの抑え | 自分の起こし直しのあと、試しが一度も通らないまま次の起こし直しになるたびに待ちを倍にする (60 → 120 → 240 → ... 最大 1800 秒)。試しが 1 回通れば元に戻る |
| 動く条件 | console が router の設定を書いている所 (`Relay::Settings.config_path` がある)。それ以外 (開発機の bridge など) では何もしない |
| グラフに出さない | 試しのセッションの zid を見張りが先に決めて (`id` に乱数の 16 バイト) 開く。bridge は `read_admin` で `@/*/router` の sessions からその zid を落としてからグラフを作る。開いている最中の問い合わせにも当たらない。直近 8 個を覚えておく |
| ログ | `bridge: router watch: ...` (開始時の設定、失敗ごとの回数と誤り、起こし直しの理由、戻ったこと) |
| 画面 | `router_watch.json` (restart.request の隣) に状態を書き、Admin > Relay のタブの下 (Registry・Roles・Applies の各ページ) に「Last automatic restart of the cloud router: 時刻. 理由」を出す。1 日以内は notice の帯、それより前は hint の 1 行 |

既定の値と設定 (環境変数、秒):

| 変数 | 既定 | 選んだ理由 |
|---|---|---|
| `ASTERISM_RELAY_WATCH_EVERY` | 30 | 試しのセッションの出入り自体が zenohd の書き錠を取る (行き詰まりの引き金側の操作) ので、頻度は低めに。0 で止まる |
| `ASTERISM_RELAY_WATCH_TIMEOUT` | 10 | 健全な時の open は 3-11 ms (手元の実測)。遠い VPS や TLS の握手の遅れを見ても 10 秒は十分に長い |
| `ASTERISM_RELAY_WATCH_FAILS` | 2 | 1 回の一時的な失敗 (網の揺れ) で起こし直さない最小 |
| `ASTERISM_RELAY_WATCH_GRACE` | 60 | run.sh が要求に気づくまで最大 2 秒 + zenohd の起動数秒 + 客のつなぎ直し。Apply の待ち (WAIT_BACK 20 秒) より長く取った |

この値で、最初の失敗から起こし直しまでは TIMEOUT + EVERY + TIMEOUT 弱 (実測 40 秒台)。止まってから起こし直しまでは 50-80 秒 (止まった時が試しの直前か直後かで変わる)。

compose: bridge に `generated` の volume (読み書き) と、Apply と同じ restart の設定 (`ASTERISM_RELAY_GENERATED_DIR` ほか) を足した。DB の変更なし、`./update.sh 0.3.2` だけで上がる (bridge は作り直される)。

## 2. 受け入れ条件ごとの結果

### 2.1 試験と rubocop

- 全試験 435 件・4283 assertions 成功、rubocop 247 ファイル違反なし。
- 足した試験 (zenohd 無し、試し・起こし直し・時計は差し替え): 続けての回数と、間に通った試しで数え直すこと / 要求の直後と Apply の実行中は数えないこと / 待ちが倍に伸び、試しが通れば戻ること / 待ちの上限 / 戻らない試しは失敗で、重ねないこと / 設定 0 と、router の設定が無い所で止まること / 設定の既定・環境変数・誤った値 / 試しのセッションの zid の形と admin space からの除き方 / 試しの設定 (id・時間の上限・config_file) / 要求の書き方 (`bin/relay_restart_request` が `restart.request` を書く) と最新の Apply を数えること / 誤りの文から zenoh のソースの場所を落とすこと / Relay の画面の表示 (帯・hint・無し)。

### 2.2 起こし直しが効くこと (3 回)

手元の構成: deploy/vps の compose に build と小さな上書き (zenohd を 127.0.0.1:27448、console を 23000) を重ね、project 名 `asterism-zh2` で起こした (別に動いている `zh-pr` とは名前もポートも別)。zenohd は公式の `eclipse/zenoh:1.10.1`、run.sh はそのまま。客は CRuby: 出入りの負荷 (4 本並行、player-a と部屋の席 wasm-0116 の証明書で、購読・queryable・liveliness token・見張り・get をして閉じる)、2 秒ごとに新しいセッションを開く計測の客、ずっとつないでいる Asterism のノード player-b。

| 回 | 止まった (計測の客の最後の成功〜最初の失敗) | 見張りの最初の失敗 | 起こし直しの要求 | 新しいセッションが開けた | 止まってから戻るまで | 見張りの検出から要求まで |
|---|---|---|---|---|---|---|
| 1 | 22:27:49-51 | 22:28:26.1 | 22:29:06.8 | 22:29:09 | 約 80 秒 | 40.7 秒 |
| 2 | 22:30:36-38 | 22:30:47.9 | 22:31:28.4 | 22:31:28 | 約 51 秒 | 40.4 秒 |
| 3 | 22:32:28-30 | 22:33:09.7 | 22:33:50.1 | 22:33:55 | 約 87 秒 | 40.3 秒 |

(時刻は UTC。「止まってから戻るまで」は計測の客の最後の成功から次の成功まで。要求から戻るまでは 0-5 秒で、残りは止まってから見張りが気づくまでと 2 回目の試しまでの時間。)

- 負荷をかけ始めて約 2 分で 1 回目が止まり、その後も負荷を続けたので戻るたびに 1-2 分でまた止まった。3 回とも見張りは「the router takes sessions again」を出して数え直し、待ちの倍化は起きなかった。zenohd のログの `run.sh: restart requested` は Apply 1 回 + 見張り 3 回の 4 回。
- 1 回目の後に、bridge のグラフに新しい zid の cloud ルータと、CRuby の客 player-b (`a_node:player-b`) が戻っていることを確かめた (console の DB の GraphState)。ブラウザ版は使っていない (CRuby の客で代えた)。

### 2.3 誤って起こし直さないこと

- 負荷なし (ずっとつないでいる player-b だけ) で 22:38:50 から 23:09:36 まで 30 分 46 秒 (試し約 61 回): 失敗 0 回、自動の起こし直し 0 回。
- その途中 22:49:04 に Apply を 1 回 (console の Apply と同じ `Relay::Applier#run`): zenohd は起こし直され、見張りはその後の 60 秒を数えず、余計な起こし直しはなかった。最初の Apply (22:25:14、行の追加の反映) の直後も、状態は `quiet` (「a restart was requested at ...」) になり起こし直さなかった。

### 2.4 試しのセッションがグラフに出ない

負荷なしで、見張りを止めた bridge (`ASTERISM_RELAY_WATCH_EVERY=0`) と、見張りを 1 秒ごとにした bridge (3 分、試し約 190 回)、既定の 30 秒の bridge (30 分) を比べた。どれもグラフは 10 ノード・8 線・セッション 3 (bridge の 2 つと player-b)、cloud ルータの sessions の数 3 で同じ。1 秒ごとの 3 分の間、グラフの版は一度も上がらなかった (bridge の問い合わせは 2 秒ごと)。

### 2.5 移行

DB の変更なし。compose と VERSION・env.example の版の既定を 0.3.2 にし、docs/install.md に「From 0.3.1 to 0.3.2」(`./update.sh 0.3.2` だけ、bridge が作り直される、確かめ方、戻し方) を書いた。update.sh 自体の通しは VPS に触らない決まりなので試していない (試験の update_script_test は通る)。

## 3. 文書

- CHANGELOG 0.3.2: zenohd 1.10.1 が止まる件の要点 (読み錠の再入と書き手の割り込み、自然には戻らない)、見張り、既定の値、画面とログ、compose の変更。
- docs/install.md: 1 章の表の bridge の行、21 章「The router takes no new connection」(症状・見張りの働き・ログと画面・設定・繰り返しの抑え)、20 章「From 0.3.1 to 0.3.2」。
- README.md の relay の節に見張りの項。env.example にコメントで 4 つの設定。

## 4. 見立てと違った点・気づいたこと

- 見立てどおり、起こし直せば戻った (3 回とも、要求から 0-5 秒)。負荷なしでの誤検出も無かった。
- 1 回目の確かめの時、理由の文に zenoh のソースの場所 (`at /home/runner/.cargo/.../orchestrator.rs:383.`) が入って画面で長かったので、誤りの文からそこを落とし 300 文字までにした。状態の `restarted_at` が試しを始めた時刻 (失敗まで 10 秒前) になっていたので、試しの後の時刻に直した。どちらも確かめの途中で直し、試験を足した。
- zenoh 1.10.1 の client は `id` を設定で決められ、admin space の `peer` にそのまま出る (手元で確かめた)。これで試しのセッションを開く前から除ける。
- bridge の既存のセッションは、zenohd が止まっている間もグラフ上は残り、起こし直しの後に bridge 側のつなぎ直しで新しい zid のルータに替わった。

## 5. 残ったこと

- VPS への反映 (タグ v0.3.2・image の公開・`./update.sh 0.3.2`) はしていない (push・タグは指示待ち)。
- ブラウザ版の部屋の席の戻りは、この Step では CRuby の客で代えた (起こし直し後の戻りは G1.5b で確かめ済み)。
- 根本の対策 (zenohd の直し、上流への PR と直した image) は別の作業。直った zenohd が入ったら、見張りは止めてもよい (`ASTERISM_RELAY_WATCH_EVERY=0`) が、zenohd が別の理由で止まった時の保険として残す選択もある。
- 片付け: 手元の構成 (`asterism-zh2` の container・volume・network) と作った image `asterism-console-zh2:0.3.2` は消した。前からある image は残した。prune はしていない。
