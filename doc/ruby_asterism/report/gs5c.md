# GS5c 報告: 麻雀の待ち合わせで、CPU・AI を席ごとに設定する

> 状態: 完了 (検収待ち) | 更新: 2026-10-18 | instruction_gs5c.md の結果。卓の麻雀のアプリの待ち合わせで、各席の行に状態のボタン (空き → CPU → Claude → Jev → 空き) を置き、下の段の `+Claude` / `+Jev` を消した。ゲームホストは `cpu` の 1 回の呼び出しで CPU・AI の席を別の種類に替えられるようにした (asterism-games の CHANGELOG に Unreleased 0.12.1)。止まる条件には当たっていない

## 1. コミット

| リポジトリ | ブランチ | 中身 |
|---|---|---|
| fmruby-core | feature/gs5c (この報告と同じコミット) | 卓の麻雀のアプリ (`mahjong.app.rb`・`mahjong_panels.rb`)、この報告と画面 (`report/gs5c/*.png`) |
| asterism-games | main `d0dddd5` | `cpu(token, seat, on)` が 1 席の CPU・AI を別の種類に替える (`table.rb`)、Remote の説明 (`remote.rb`)、試験 1 件 (`test_ai_players.rb`)、README の表と「AI players」、CHANGELOG (Unreleased 0.12.1) |

push・タグはしていない。asterism-games の VERSION は 0.12.0 のまま。コミットの前に 2 つの差分を鍵の形 (`sk-ant`・`Bearer`・`API_KEY=`・`TOKEN=`)・IP の形・メールアドレスで検査し、該当なし。

## 2. 画面の前と後

前 (GS5、`report/gs5/sim_lobby_ai_buttons.png`・`web1280_lobby_ai_buttons.png`): 席の行は「座る n」と、卓の主にだけ空き席の「CPU」/ CPU 席の「空ける」。下の段に「CPU で埋める」「+Claude」「+Jev」(最初の空き席に入れる)。

後:

- 席の行は「座る n」(空き席と CPU・AI の席だけ。自分の席は「立つ S」) と、その右に**状態のボタン** (`空き` / `CPU` / `Claude` / `Jev`)。卓の主が押すたびに 空き → CPU → (ホストの `ak` にある AI を `ak` の順で) → 空き と巡回する。人間の席には状態のボタンも「座る」も出さない。
- 卓の主でない人には、同じ場所に状態の文字だけを薄い色で出す (押しても何も起きない)。
- キーで選んでいる席は、状態のボタンに黄色の枠が付く。そのときルールの欄の選択の色は消える。
- 下の段は「CPU で埋める C」(空き席があるとき)・「始める Enter」(4 席そろったとき)・「卓を出る Esc」。`+Claude` / `+Jev` は消した。
- 文字列 `mj_cpu_off`「空ける」は使わなくなったので消した。ホストの答え `"no ai"` に「その AI は今は出せない (鍵か 1 日の上限)」を足した。
- 3 つの大きさ (426x240 の小さい画面、640x360、1280x720 は 640x360 の 2 倍) とも同じ `draw_lobby` で描く。

## 3. キーの割り当て

| キー | 前 | 後 |
|---|---|---|
| 上・下 | ルールの項目を選ぶ (640x360・1280x720) / 小さい画面では何もしない | **席とルールを 1 本の並びで選ぶ** (卓の主)。並びは 席1-4 → ルールの項目。ルールの先頭で上を押すと席4、席4 で下を押すとルールの先頭、席1 で上を押すとルールの最後、ルールの最後で下を押すと席1。ルールの無い小さい画面では席1-4 を回る |
| 左・右 | 選んだルールの値を変える | 席を選んでいれば右で次の状態、左で前の状態 (空き ← CPU ← Claude ← Jev ← 空き)。ルールを選んでいれば前と同じ |
| Tab | ルールのページをめくる | 前と同じ (席の選択は外れる) |
| 1-4・s・c・Enter・Esc・a・n | 座る・立つ・CPU で埋める・始める・出る・アシスト・名前 | 変えていない |

- 卓の主でない人のキーは前と同じ (上下はルールの選択を動かすが、表示には出ない)。
- 状態のボタンのクリックは、その席を選んだうえで次の状態に進める。ルールの行のクリックは席の選択を外す。
- 呼び出しの答えを待つ間 (前からの `@cmd_f`) のキーやクリックは捨てる (前と同じ)。

選んだ理由: 指示書の例 (上下で席を選ぶ) は 640x360・1280x720 では上下がルールの選択とぶつかる。上下を席とルールの 1 本の並びにすると、新しいキーを足さずに「上下で選び、左右で変える」というルールと同じ操作で済み、小さい画面でも同じ動きになる。

## 4. ゲームホストの変更 (あり)

- 確認: asterism-games 0.12.0 の `cpu(token, seat, on)` は空き席にしか入れられず、CPU を Claude に替えるには「出す」と「入れる」の 2 回の呼び出しが要った。2 回だと、1 日の上限などで AI が断られたとき席が空きのままになる (CPU のままで分からない)。
- 変えたこと: 1 席を指したとき、そこが CPU か別の種類の AI なら、`on` に合わせて 1 回で替える (CPU → `"claude"`、`"claude"` → `"jev"`、`true` なら AI → CPU)。`"no ai"` の判断が先なので、断られたら席は前のまま (CPU なら CPU)。席 -1 (全部) は前どおり空き席だけを埋める。
- 今の呼び方は壊していない: 前に `"ok"` だった呼び出しは同じ答え。前に `"no change"` だった「CPU・AI の席への `"claude"` / `"jev"`」と「AI の席への `true`」だけが替える動きになった (前のアプリはこの呼び方をしない)。古いホスト (0.12.0 まで) に新しいアプリを向けると、CPU → Claude の一歩が `"no change"` (画面に「変わらない」) になる。
- 試験 `test_one_call_changes_a_cpu_or_ai_seat_into_another_kind` (19 assertions): 巡回の各段、同じ種類は `"no change"`、席 -1 は空き席だけ、`ak` から Claude を外すと `"no ai"` で CPU のまま、人間の席は替わらない。

## 5. 受け入れ条件の結果

### 5.1 試験 (条件 1)

asterism-games `MJ_GAMES=8 bundle exec rake test`: **198 runs、41,930 assertions、0 failures、0 errors** (skip 3、ルータなしの試験)。

### 5.2 家の LAN の卓、偽物のつなぎの AI (条件 2)

構成: zenohd `gs5c-zenohd` (eclipse/zenoh:1.10.1、tcp 17767・ws 17769、sim の網に入れて sim の zenohd へ `-e`)、ゲームホスト (asterism-games の作業コピー、node `gs5c`、`ASTERISM_GAMES_AI_FAKE=1`、状態は scratchpad)、sim (Modern、標準構成、音はミュート)、ブラウザ版 (web_up、`?zenoh=ws/localhost:17769`、1280x720 と 426x240 のページ)。卓は mahjong1。

| 確認 | 結果 | 画面 (`report/gs5c/`) |
|---|---|---|
| 卓の主 (ブラウザ版 1280x720) が席2 のボタンを押して 空き → CPU → Claude → Jev → 空き | 合格。名前の欄も CPU1 → Claude [AI] → Jev [AI] → 空き と替わる | `web1280_owner_empty.png`・`web1280_owner_cpu.png`・`web1280_owner_claude.png`・`web1280_owner_jev.png` |
| ほかの人 (sim 640x360) の画面 | 合格。席2 は「Jev」の文字だけ、ボタンなし。人間の席1 (卓の主) には「座る」が出ない | `sim640_other_jev.png` |
| キーで巡回 (1280x720) | 合格。下下で席4、右右で 空き → CPU → Claude、左で Claude → CPU (1 回で AI → CPU)、下でルールの先頭に戻る | `web1280_keys_seat4_claude.png` |
| 「CPU で埋める」(`c` キー) | 合格。空き席2 が CPU になり 4 席そろって「始める」が出た | `web1280_left_and_fill.png` |
| 卓の主 (ブラウザ版 640x360) のクリックで CPU → Claude → Jev → 空き | 合格 | `web640_owner_claude.png`・`web640_owner_jev.png`・`web640_owner_empty.png` |
| ほかの人 (ブラウザ版 640x360) の画面、状態の文字のクリック | 合格。「Jev」「CPU」の文字だけ、クリックしても何も起きない | `web640_other_jev.png` |
| 卓の主 (sim 426x240) のキー (下下右) と巡回 | 合格。席2 が CPU → Claude、続けて Jev → 空き → CPU → Claude | `sim426_owner_claude.png` |
| CPU・AI の席に座ると入れ替わる | 合格。ほかの人が Claude の席2 に座り、席2 は人間 (ボタンなし) になった | `sim426_owner_after_swap.png` |
| 「CPU で埋める」のクリック (426x240) | 合格 | `sim426_fill.png` |

- sim の 426x240: この sim は高解像度の全画面に対応したので、麻雀は 640x360 で起きる。小さい画面の配置を写すため、sim の起動のときだけ作業コピーの `mahjong.app.toml` の `fullscreen_hires` を false にして起こし、起きたあとすぐ戻した (コミットには入っていない)。窓が 426x240 になり、小さい卓の待ち合わせ (ルールが 1 行) で描かれる。
- sim の画面は呼び出しのあとの view が届くまで前の絵のことがある (ボタンを続けて押した 2 回目が、次の写しで見えた)。打鍵が捨てられたのではなかった。
- VM (sim、LARGE 3 MB): 待ち合わせの間 817-833 KB (26 %)。GS5 の 856 KB (対局中) と同程度。`mahjong.app.rb` は 61,364 B (64 KB の上限の内、GS5 の 61,586 B より小さい)。巡回の処理は `mahjong_panels.rb` に置いた (26,672 B)。
- core のエラー行 0 (`[Mahjong] No exception detected`)。

### 5.3 鍵の無い AI を巡回に出さない (条件 3)

本物の鍵は使っていない。偽物のつなぎは Claude と Jev を両方出すので、片方だけのホストは「Jev の鍵の欄にでたらめの文字列を入れ、偽物のつなぎも Anthropic の鍵も外す」形で作った (`TYPESAFE_API_KEY=dummy-not-a-key`、`ASTERISM_GAMES_AI_FAKE` なし)。ホストは `AI players: jev` で上がり、`ak` は `["jev"]` になる。AI の席に判断を聞くのは対局が始まってからなので、待ち合わせだけなら API は呼ばれない (対局は始めていない)。

| 確認 | 結果 | 画面 |
|---|---|---|
| sim の卓の主が席1 を押す | CPU → **Jev** (Claude を飛ばす) → 空き | `sim426_onekey_cpu_to_jev.png`・`sim426_onekey_jev_to_empty.png` |
| 1 日の上限に届いたホスト (偽物、`ASTERISM_GAMES_AI_DAILY_USD=0`。ホストは起きた時点で「today's limit of $0.0 is reached」) | 空き → CPU → 空き → CPU。AI は巡回に出ない | `sim426_limit_cpu.png` |

上限で AI が断られたときに席が CPU のままであることは、試験 (4 章) で `"no ai"` のあと名前と種類が CPU のままであることを確認した。

### 5.4 sim の標準構成でエディタ (条件 4)

sim の標準構成 (Spinel カーネル + Spinel エディタ、`build/fmruby-core.elf` は x86-64) で、メニューからエディタを開いて `x` を 1 打鍵し、`Ln 1, Col 2` になった (`sim_editor_one_key.png`)。

## 6. 見立てと違った点・判断したこと

- 指示書の例 (上下で席を選ぶ) は、640x360・1280x720 では上下がルールの選択に使われている。上下を「席 → ルール」の 1 本の並びにして、左右で変える形にした (3 章)。新しいキーは足していない。左を「前の状態」にしたので、ホストの「AI → CPU」(`true`) も使う。
- ホストの変更は「1 席を指したときだけ」にした。席 -1 で CPU・AI の席まで替えると「CPU で埋める」が座っている AI を CPU に戻してしまうため。
- 状態のボタンは人間の席には出さない (指示書の 1 章の 3 と同じ扱い)。人間の席は名前と印で分かる。
- 作業の途中で、ゲームホストの使い方を見るつもりで `bin/asterism-games --help` を打ったところ、オプションを解さずにホストとして起きた (既定の `var/state` の mahjong1・mahjong2 を読み、7447 のルータにつながらないまま 2 分ほど回って、止めたときに状態を書き戻した)。書き戻したのは読んだ状態と同じ卓 (`var/state` は git の外)。以後のホストは状態の場所を scratchpad にして起こした。
- asterism-games の作業場所に、5 時間前から動いている別の `rake test` のプロセスがあった (この作業のものではない)。触っていない。

## 7. 残ったこと

- 新しいアプリを 0.12.0 までのホストに向けると CPU → Claude の一歩が「変わらない」になる。VPS のゲームホストは、この変更を含む版を出して上げるまでその状態 (公開・タグはユーザの判断)。
- 実機 (Tab5) の画面は見ていない (指示書の範囲外。描画は sim・ブラウザ版と同じ `draw_lobby`)。

## 8. 後片付け

ゲームホスト (偽物・片方の鍵・上限 0 の 3 回とも TERM で止めた)、zenohd `gs5c-zenohd` (stop・rm)、web_* の開発サーバとブラウザ、sim (音を戻して sim_down) を止めた。scratchpad のホストの状態を消した。前からある image と、ほかの人の container (`rc2-sweep`)・プロセスには触っていない。docker の prune はしていない。fmruby-core の build/ は x86-64 のまま (`file build/fmruby-core.elf` で確認)、ブラウザ版はこのブランチで作り直した (`rake spinel:gen wasm:mruby wasm:mrb wasm:web`)。
