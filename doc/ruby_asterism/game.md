# 家の中でオセロを遊ぶ手順 (と、実時間の測り)

> 状態: 進行中 | 更新: 2026-10-12 (G1b) | G1 のオセロ (`flash/app/test/othello.app.rb`) を家の中の網 (LAN) で、端末・sim・ブラウザ版どうしで遊ぶ手順。待ち合わせ、指し方、切れたときの扱い、console での観戦と記録。あわせて実時間の測り (`rt_probe.app.rb`) の動かし方

作りと実測は report/g1.md と report/g1b.md (接続を 1 本に、名前)。ブラウザ版を網に入れる手順は browser.md。

## 1. しくみの要点

- 各プレイヤーのゲームは、自分のノードの下にオブジェクト `<ノード>/game/othello` を出す (Asterism のアプリの名前は
  `game`)。相手のオブジェクトを呼んで、誘い・答え・手・投了を伝える。
- 呼ぶ側は、引数の最初に自分のノードの ID を渡す (Asterism は呼んだ相手を教えないため)。
- どちらの側も盤を持つ。届いた手は自分の盤で確かめ (対局・相手・手番・手の数・置ける所)、正しくなければ受けない。
  自分の手は、相手が「ok」と答えてから自分の盤に置く。
- 手を指すたびに (と、対局中は 10 秒ごとに) 状態を `asterism/<ノード>/game/othello/state` へ `put` で配る。
  console と観戦用。`state` の呼び出しでも同じものが返る。put は Asterism の接続そのもの (`Asterism.session`、
  asterism 0.6.0) で行う。アプリが開く接続は 1 本なので、1 回で使い切る合言葉の `wss://` の入口からも入れる。
- プレイヤーの名前は Asterism のプロフィールの `name`。端末ごとに `/home/asterism_profile.toml` に 1 つだけ保存し
  (ブラウザ版はブラウザの保存領域にある `/home`)、全アプリが起動するときに読む (3.1 節)。
- 使うキーは `asterism/<ノード>/game/**` の中だけ (G0 の ACL の範囲)。

| メソッド | 引数 | 返す値 |
|---|---|---|
| `info` | なし | `{"name", "status", "opponent"}` (`status` は idle / inviting / invited / playing / over) |
| `invite` | 自分の名前, 対局の ID | `"asked"` か `"busy"` |
| `answer` | 対局の ID, 受けるか, 相手の色 (`"black"`/`"white"`), 自分の名前 | 誘った側が始めたら `true` |
| `cancel` | 対局の ID | `true` / `false` |
| `move` | 対局の ID, 今までの手の数, x, y (パスは x = -1) | `"ok"` か、断った理由 (`"not your turn"`、`"illegal"`、`"out of sync"` など) |
| `resign` | 対局の ID | `true` / `false` |

呼んだ相手の ID は引数で渡さない。呼ばれた側が `Asterism.current_call.from` で受け取る (X1、asterism 0.7.0 の
protocol 1 では呼び出しのキーの最後の塊が呼んだ側の ID)。
| `state` | なし | 状態 (下) |

状態は Hash で、`game` (対局の ID)・`black`/`white` (ノードの ID)・`turn`・`n` (手の数)・`last` (最後の手、`d3` の形、
パスは `--`)・`hist` (手の並び)・`board` (8 文字の行が 8 本、`.`・`B`・`W`)・`black_count`/`white_count`・`over`・
`result`・`opponent_away`。対局していないときは `game` が nil。

## 2. 始め方

### 2.1 家のルータ (親リポジトリ family-mruby)

端末を LAN からつなぐ形と、ブラウザ版の ws/ の待ち受けを重ねて zenohd を起動する。

```
docker compose -f docker-compose.yml -f docker-compose.zenoh-lan.yml \
  -f docker-compose.zenoh-ws.yml up -d zenohd
docker logs fmruby_zenohd | grep listen     # tcp/[::]:7447 と ws/[::]:7449
```

- sim を上げた後 (sim_up は zenohd を重ね合わせなしで作り直す) は、上の行をもう一度打つ。
- ほかの PC のブラウザから入るときは browser.md 2.1 の wss:// の中継を足す。

### 2.2 端末 (P4-Nano・Tab5・AtomS3R)

1. `/home/zenoh_echo.txt` の 1 行目に `tcp/<この PC の LAN の住所>:7447` を書く (asterism_demo と同じ)。
2. アプリを送って起動する (焼き直しは要らない)。

```
ruby tools/fmrb_rd_fs.rb <端末の住所> put fmruby-core/flash/app/test/othello.app.rb /app/test/othello.app.rb
ruby tools/fmrb_rd_fs.rb <端末の住所> put fmruby-core/flash/app/test/othello.app.toml /app/test/othello.app.toml
ruby tools/fmrb_rd_launch.rb <端末の住所> /app/test/othello.app.rb
```

- アイコン (`othello.icon` と `othello.bmp`) も送ればランチャーに出る (ランチャーの窓で右クリックで読み直し)。
- ノードの ID は mDNS の名前 (`fmruby-<16 進 6 桁>`)。`/home/asterism_node.txt` で変えられる。
- AtomS3R (128x128) は遊べない。開くと、網の上の対局の盤を 1 つ映す (観戦)。`.app.toml` の `large_memory = 1` は
  このため (AtomS3R の普通のプールではプログラムを読み込めない)。

### 2.3 sim

`sim_up` のあと `sim_app` で `/app/test/othello.app.rb` を起動する。ノードの ID は `linux`。

### 2.4 ブラウザ版

```
cd fmruby-core && source ~/emsdk/emsdk_env.sh
rake wasm:web          # アプリを直したら束を作り直す (git の index を読むので、新しいファイルは git add してから)
rake wasm:serve        # http://localhost:8006/
```

`http://localhost:8006/wasm/web/index.html?app=/app/test/othello.app.rb` を開く。ノードの ID は `wasm-<16 進 6 桁>`
(ページが作ってブラウザに残す)。2 人目は別のブラウザか別のプロファイルで開く (同じブラウザの 2 つ目のタブも
使い捨ての ID で入れる)。

## 3. 遊び方

### 3.1 待ち合わせ (ロビー)

- 開くと「オセロ: 相手を選んで誘う」と、同じ網で othello を開いている相手の一覧が出る。相手ごとに
  「待っている / 誘い中 / 対戦中 / 終局を表示中」。名前は相手の profile の `name` があればそれ、無ければノードの ID。
- 自分の名前は 1 行目の右に出る (未設定ならノードの ID、薄い色)。F2 か「名前 (F2)」で変える: 打った文字が
  下の欄に出て、Enter (か「保存」) で保存、Esc (か「やめる」) でやめる。空にして保存すると名前を消す (ID に戻る)。
  日本語はかな入力 (Ctrl+Space など) で打てる。64 バイトまで。保存するとすぐプロフィールに入り、相手の一覧
  (数秒のうち) と console の図 (`名前 (ID)`) に出る。端末を起動し直しても残る。
- 上下の矢印かクリックで相手を選び、Enter か「誘う」で誘う。誘っている間は「取り消す (Esc)」。60 秒答えが無ければ
  取り消す。
- 誘われた側には「<名前> から誘い。受ける?」と「受ける (Y)」「断る (N)」が出る。Enter / y で受け、Esc / n で断る。
  誘った相手が消えたら誘いも消える。
- 受けた側が色を決める (どちらかを引く)。黒が先。対局中・誘い中の相手は誘いを `busy` で断る。

### 3.2 対戦

- 左に盤、右に自分と相手の石の数、手番、手の数と最後の手、知らせ。置ける所に小さな点、最後の手に印。
- 自分の番で、矢印でカーソルを動かして Enter、または升目をクリック (Tab5 の相対タッチは、カーソルを升目に
  動かしてタップ)。相手の番の間は置けない。置けない所は「そこには置けない」。
- 置ける所が無いときは「パス」のボタンか Enter でパスする (置ける所があるときのパスは断られる)。
- Esc か「投了」で投了 (「投了しますか?」に Enter / y)。
- 両者とも置けなくなったら終局。「終局 黒 n - 白 m。あなたの勝ち / 負け / 引き分け」。Enter か「ロビーへ」で戻る。

### 3.3 切れたとき (決め方)

| 起きたこと | 扱い |
|---|---|
| 相手の othello が網から消えた (アプリを止めた、端末の WiFi が落ちた、ルータが止まった) | 「相手が切れた / 戻るのを待つ: あと n 秒」を出し、30 秒待つ。その間はどちらも指せない |
| 30 秒のうちに相手が戻り、相手が同じ対局を持っている | 相手の `state` を問い合わせて手の並びを比べ、合えば続ける (下の「盤を合わせる」) |
| 戻った相手が対局を持っていない (アプリを開き直した) | 「相手の対局が無くなったので終わり」 |
| 30 秒たっても戻らない | 「相手が戻らなかったので終わり」 |
| 自分の側が網から切れた | 3 秒ごとにつなぎ直す。同じ 30 秒の間に戻れば相手と盤を合わせて続ける。戻れなければ「網に戻れなかったので終わり」 |
| 手を送って答えが無かった (時間切れ) | 相手の `state` を問い合わせて盤を合わせる (相手に届いていれば、その手が入った並びに合わせる) |

盤を合わせる: 2 つの手の並びが同じなら何もしない。一方が他方の始まりで、長い方のすべての手が正しく指せる
(並べ直して確かめる) なら、長い方に合わせる。どちらでもなければ「盤が合わないので終わり」。相手の側で
終わっている対局 (届かなかった投了など) は、その結果を取る。相手の番を待つ間も 10 秒ごとに比べる。

## 4. console で観戦し、記録する

- 網の図に、各プレイヤーのノード・アプリ `game`・オブジェクト `othello` が出る。
- Recordings で、キー `asterism/*/game/othello/state` (と網の構造) に印を付けて記録する。巻き戻すと、各時刻の
  メッセージに盤 (`board` の 8 行)・手番・最後の手が出る。`n`・`black_count`・`white_count` は Plot で線にできる。
- 呼び出しで `state` を見るには、Call permissions に node `fmruby-*` (や `wasm-*`)・app `game`・object `othello`・
  method `state` を足す。PC からは `ruby tools/fmrb_zenoh.rb call <ノード>/game/othello state`。

## 5. 実時間の測り (rt_probe)

遊びではなく測るための試し。2 台で同時に開く (3 台以上だと、相手は最初に届いた 1 台になる)。

- `flash/app/test/rt_probe.app.rb` を、オセロと同じように端末・sim・ブラウザ版
  (`?app=/app/test/rt_probe.app.rb`) で開く。
- 自分の点 (緑) が画面の中を動き、その位置を毎秒 25 回 `asterism/<ノード>/game/rt/pos` へ配る。相手の点 (赤) は
  届いたまま描く。`i` で補間 (青、100 ms 遅らせて前後の 2 点の間) を足す。`+` / `-` で毎秒 10・20・25・30・50 回。
  `d` で点を描くのをやめる (描く重さを見るため)。
- 下の行に、往復の遅れ (中央値・最大)、送る量、受けた数、落ちた数・入れ替わった数、1 周の時間、内蔵 RAM の空き。
- 1 秒ごとに `asterism/<ノード>/game/rt/stats` へ数字 (その 1 秒の往復の遅れの全部を含む) を配る。console の
  記録 (`asterism/*/game/rt/**`) に残る。
- 往復の遅れは時計を比べない: 相手の送る位置に「最後に受けた自分の番号」と「それを受けてから何 ms たったか」が
  入っていて、`今 - その番号を送った時刻 - たった時間` を往復とする。片道はその半分。
- 見た目 (相手の点がどれだけ遅れて・跳んで見えるか、補間でどう変わるか) は人が見て確かめる。
