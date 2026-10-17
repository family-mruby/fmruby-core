# GS3 報告: 居る・居ないを on_join / on_leave で、卓のアプリの探し直し、case/in

> 状態: 完了 (検収待ち)・常設の卓だけ判断待ち | 更新: 2026-10-17 | instruction_gs3.md の結果。ゲームホストは Asterism の on_leave / on_join で会員の途切れを知るようにし (asterism-games 0.8.0)、卓のアプリと `bin/othello-tty` はホストの on_leave / on_join で一覧と対局中の表示を切り替えるようにした。途切れの判定は家の LAN と部屋で 10 秒前後から 20 ms 以下になった。常設の卓だけは、console の役割 `games-host` がほかのノードの liveliness を受け取れないので、今の仕組み (呼び出しの間隔) のまま。速くするには console の役割の変更が要る (止まる条件の「ACL の変更」に当たるので、8 章で判断を仰ぐ)

## 1. コミット

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| asterism-games | main | `cf68d00` games 0.8.0 | `Game#node_left` / `node_joined`、`Host` の on_leave / on_join、`bin/othello-tty`、試験、README「Who is there」 |
| fmruby-core | feature/gs3 (develop から) | `baad24ef` | 麻雀・オセロの卓のアプリ (探し直し、つながりが切れた表示、case/in) |
| fmruby-core | feature/gs3 | この報告と同じコミット | この報告と画面 (`report/gs3/*.png`) |

push・タグはしていない。VERSION は 0.8.0。コミットの前に差分から実在の IP・ドメイン・端末 ID・鍵 (`~/.openai_key` の中身) を検査して 0 件だった (例の `fmruby-aaaaaa` だけ)。

作業の前に asterism-games main は親の連絡どおり `d41e590` (GS2b の README の 2 行) を含んでいた (作業はその上)。

## 2. 居る・居ないの仕組み

### 2.1 前

ゲームホストは会員の呼び出しのたびに `seen` を今にし (`Remote#mark` → `Game#touch`)、`away_ms` (10 秒) 呼び出しが無い会員を途切れとした。卓のアプリと CRuby のクライアントは、いることの印のためだけに view を 3 秒 (tty は 2 秒) ごとに呼んでいた。

```ruby
def member_away?(node)
  m = @members[node]
  m.nil? || m["seen"].nil? || @now - m["seen"] > @opts["away_ms"]
end
```

### 2.2 後

`Host#up` で Net の on_leave / on_join を受け、全部の卓に渡す (受け手のスレッドの tick の中、`Asterism::LOCK` を持ったまま。ホストの輪と同時に卓に入らない)。

```ruby
net.on_leave { |n| node_left(n) }
net.on_join { |n| node_joined(n) }
```

卓 (`Game`) は、on_join で見えたノードを `@live` に持ち、on_leave で消す。会員は「ノードが抜けた (`left`)」ならすぐ途切れ、ノードが網に見えている間は呼び出しが無くても居る。ホストがそのノードを一度も見ていないときだけ、前の呼び出しの間隔に戻る (備え)。

```ruby
def node_left(node, now)
  node = node.to_s
  @live.delete(node)
  m = @members[node]
  return false unless m && !m["left"]
  m["left"] = now
  @log&.call("#{self.class.game_name}: #{@name}: #{node} left the network: away")
  changed
  true
end

def member_away?(node)
  m = @members[node]
  return true if m.nil? || m["left"]
  return false if @live[node]
  m["seen"].nil? || @now - m["seen"] > @opts["away_ms"]
end
```

- `node_joined` は `left` を消して `seen` を今にする。部屋の席のノードが `seat_reuse_ms` 以上いなかったなら、前と同じく `reused_seat` で前の会員を外す (新しいページ)。会員の呼び出し (`touch`) と `join` も `left` を消す。
- 忘れる処理 (`away_ms` の 6 倍) は「最後の呼び出し」か「ノードが抜けたとき」の遅い方から数える (`last_there`)。`seat_reuse_ms` も同じ。着席中の会員は今どおり忘れない。
- `left` は保存しない (`saved_members` が落とす)。起動し直したホストは会員を「今いた」とみなし、居ないノードは接続後の最初の poll で抜けたことになる。
- 卓の put は `changed` で出るので、オセロの卓 (途切れの変化を tick で見ていない) でもすぐに put される。

### 2.3 残した備えとその理由

呼び出しの間隔で数える仕組み (`seen` と `away_ms`、画面の 3 秒ごとの view) は残した。

1. **常設の卓のホストはほかのノードの liveliness を受け取れない**。役割 `games-host` は `Watch asterism/**` で liveliness の監視を宣言できるが、ルータが送る印は自分の rw の鍵 (`asterism/${node}`・`asterism/${node}/games/**`) だけで、席の端末の `asterism/<node>/game` は届かない (cloud_config.rb の `watch` は ingress の `declare_liveliness_subscriber` だけ)。deploy/local で確かめた: 常設のオセロの卓に `player-a.tar` の tty が座って kill -9 されたとき、ホストのログに `left the network` は出ず、途切れは 13.6 秒後 (呼び出しの間隔)。
2. 部屋は見える: 部屋のホストの規則 (`room_host_rules`) が `room/*/**` に `declare_liveliness_subscriber` と `liveliness_query` を許し、egress は全部許す。deploy/local で `left the network` が出て、途切れは 6-7 ms。
3. ホストに会員のノードが見えるかどうかは網の作りで決まり、ゲームの側からは分からない。だから「見えたノードは liveliness で、見えないノードは呼び出しで」と、会員ごとに自動で切り替える形にした (設定は足していない)。

## 3. 探し直しの形

卓のアプリ (麻雀・オセロ) と `bin/othello-tty`:

- 接続のたびに `Asterism.on_join` / `on_leave` を登録する (`Asterism.close` で忘れられるため)。
- ホスト (卓のパスの先頭のノード) が on_leave したら、その卓を一覧から外す (麻雀は「自分の席あり」の印と probe の済みも外す)。対局中ならその卓の画面に「ホストとのつながりが切れた」を出し、定期の view を止める (30 秒に 1 回だけ、on_join が来ない場合の備え)。
- on_join が来たら 3 秒の間 300 ms ごとに `Asterism.each` を見直す (ノードのアプリの印のあとにオブジェクトの印が来るので少し待つ)。卓のホストが戻ったなら、待っている view を捨ててすぐ view を呼ぶ。合図が来ない場合の備えとして、30 秒ごとの見直しは残した (前は 2 秒ごと)。
- on_leave の直後に、抜ける前に送った view の答えが来ることがあり、それが「切れた」を消していた (麻雀で見つけた、7 章)。切れたあとに送った view の答えだけが「切れた」を消すようにした (`@gone_at`)。
- `bin/othello-tty`: on_leave で「the connection is lost」を出し、ticker を長い待ち (30 秒) に切り替える。on_join で「the host is back」を出してすぐ view。`--table` なしのときは on_join を合図に 10 秒まで探し直す。ホストが会員を忘れていたら (view が `token`)、join し直す。

## 4. case/in にした所

| アプリ | 所 | 形 |
|---|---|---|
| 麻雀 | `collect_view` (view の答え) | `in Hash => v if v["error"]` / `in Hash => v` / `else` |
| 麻雀 | `take_view` の `dl` | `in nil` / `in [Integer => b, Integer => l, Integer => k, *]` / `in [Integer => b, Integer => l]` |
| 麻雀 | `draw_info` の局の情報 `r` | `in nil then return` / `in [wind, hand, repeats, sticks, *rest]` |
| 麻雀 | 待ち合わせの観戦者 (`mb` の 1 件) | `in [name, Integer => seat, Integer => flags, *] if seat < 0` |
| オセロ | `collect_join` / `collect_view` | 麻雀と同じ形 |
| オセロ | `status_word`、`draw_panel` の状態 | `in "wait"` / `in "play"` / `in "over"` |
| オセロ | `run_key` | `k => [sc, ch]` |

文字列の鍵の Hash は Ruby の hash pattern で書けない (鍵は Symbol だけ) ので、view の Hash の中身は今どおり `v["st"]` で読み、配列の形のもの (`dl`・`r`・`mb`) と状態の値の場合分けだけにした。

- アプリの VM (Spinel ではなく mruby) で動くことを確かめた: sim の `flash/app/test/case_in_repro.app.rb` が 6 件とも `CASEIN ok`。麻雀とオセロの卓の上の形は sim とブラウザ版で通った (待ち合わせの観戦者の名前、局の表示「東1局 0本場 供託0 残り69」、終局の「あなたの負け」)。
- **配列の pattern が nil に出会うと落ちる**: 最初の版で麻雀が待ち合わせに入った直後に `undefined method 'deconstruct' for NilClass` で終わった (`dl` は待ち合わせでは nil)。CRuby は `deconstruct` に応えない値を「合わない」とするが、このコンパイラ (lib/patch/compiler/mruby-compiler-codegen.c の配列の pattern) は値を確かめずに `deconstruct` を送る。直すのはこの Step の範囲 (卓のアプリ) の外なので、アプリで `in nil` を先に置いた (コメントつき)。hash pattern (`deconstruct_keys`) も同じ作り。
- case/in の分の VM の使用は小さい: case/in を元の if に戻した版と比べ、コンパイル後の使用は 0.6 KB 多いだけで、山はほぼ同じ (6 章)。

## 5. 席の管理を基底に上げるか

上げなかった。

- GS3 の変更は会員の層 (`Game` の `node_left` / `node_joined` / `member_away?` / `expire_members`) だけで済み、席のコード (麻雀・オセロの `sit` / `stand` / `cpu` / `forget` / `keep_member?` / `pass_owner`) は 1 行も変えずに動いた。gs2.md 4.3 で「`forget` と `keep_member?` の呼ばれ方が変わるならそこで見比べる」としていたが、呼ばれ方は変わらなかった (忘れるのは今どおり `expire_members` から、数え始めが変わっただけ)。
- 2 つを見比べると、同じなのは `seat_index`・座る席の選び方・卓の主の引き継ぎの 30 行ほど。違いは麻雀の `humans` の上限・CPU の通し番号の名前・対局中の抜けで宣言と答えを片付ける・保存の形 (`seats` と `counts`)、オセロの対局後の待ち合わせへの戻り (`back_to_lobby`) と `schedule`。上げると麻雀の保存の形と動きに手を入れることになり、それに見合う共通部分が無い。
- 3 つ目の席のあるゲーム (GS5 の AI の打ち手や GS6 の共有のボードは席を持たない) が出たときに改めて見るのがよい。

## 6. 受け入れ条件ごとの結果

### 6.1 試験 (条件 1)

| | 結果 |
|---|---|
| 前 (`d41e590`) `MJ_GAMES=8 bundle exec rake test` | 159 runs、0 failures、skip 1 |
| 後 (`cf68d00`) 同 | 169 runs、0 failures、skip 1 (別プロセスの tally 9 件も合格) |
| 後、`ASTERISM_TEST_ROUTER=tcp/127.0.0.1:17467` で test_network.rb / test_tally.rb | 1 / 9 合格 (19 / 68 assertions) |

足した試験: `test_presence.rb` 10 件 (抜けたらすぐ途切れ・戻ったらすぐ戻る、網の合図が無いときは away_ms で途切れ (備え)、見えているノードは呼ばなくても居る、呼び出しや join で戻る、忘れるのは抜けたときから数える・着席中は忘れない、保存に presence が無い、部屋の席は seat_reuse_ms のあとは新しいページ、麻雀の途切れた席、ホストが全部の卓に伝える)。tally の網の試験に「別のノードを kill するとすぐ途切れ (4 秒未満、away_ms は 10 秒)、呼び出し無しにノードが戻るだけで戻る」を足した (実測 0.2 秒ほど)。部屋の試験の Net の代わりに on_join / on_leave を足し、3 種のホストが居る・居ないを卓に伝えることを確かめた。

### 6.2 途切れの判定 (条件 2)

時間は網を見る CRuby の観測ノード (`gs3-probe`。状態の put と on_join / on_leave を時刻つきで記録) で測った。「抜けた」はその観測ノードの on_leave、「途切れ」は put された状態の `aw` (席の途切れのビット) が立った時刻。前は asterism-games `d41e590` (部屋は image `asterism-games:7c87c17`、game.rb は同じ)、後は 0.8.0。

家の LAN の卓 (この Step の zenohd `gs3-zenohd`、tcp 17467・ws 17469、sim の zenohd へ `--connect`):

| 居なくなり方 | 前 | 後 |
|---|---|---|
| CRuby の tty を kill -9 (席に着いて) | 13.7 s / 10.3 s | 16 / 11 / 10 / 11 ms |
| ブラウザ版のタブを閉じる (web_down、オセロの席) | 12.9 s | 11 ms |
| sim の麻雀の卓のアプリを kill (対局中の席) | 9.8 s | 17 ms |
| CRuby の tty を Ctrl+C | 2 ms で会員ごと消える (終わるときに `leave` を呼ぶ) | 同じ |
| sim のオセロの卓のアプリを kill | 12 ms で会員ごと消える (`on_destroy` が `leave` を呼ぶ) | 同じ |

戻ったとき (同じノードで開き直す):

| | 前 | 後 |
|---|---|---|
| CRuby の tty (同じ node で起動し直す) | 15 ms (join の呼び出しで) | 13 ms (join が on_join より先に着いた) |
| sim の麻雀 (起動し直すと一覧で `mine` を探る) | 12 ms (`mine` で) | 12 ms (on_join で。`mine` より前) |
| ブラウザ版のオセロ (開き直して一覧のまま) | 計っていない (部屋で計った、下) | 7 ms (on_join で、呼び出し無し) |

部屋の卓 (console の deploy/local、スロット 01、オセロの卓を開く部屋。ブラウザは Playwright 1.56 の Chromium):

| | 前 (`7c87c17`) | 後 (0.8.0) |
|---|---|---|
| 席のページのタブを閉じる → 途切れ | 12.0 s | 7 ms、6 ms |
| 同じ席 (`wasm-0101` / `wasm-0105`) で開き直す → 途切れが解ける | 22.2 s (一覧のままでは呼ばないので、Enter で join するまで解けない) | 12 ms、6 ms (on_join で、一覧のまま) |

常設の卓 (同じ deploy/local、`player-a.tar` の tty、観測は `player-b.tar`): kill -9 → 途切れ **13.6 s** (前も後も同じ。ホストに見えないので備え)。手元だけで役割 `games-host` に `Read asterism/*/game` を足して確かめると **13 ms** になった (確かめたあと戻した。8 章)。

### 6.3 探し直し (条件 3)

ゲームホストを 20-37 秒止めて起こした。「get の回数」はホストの卓の鍵 (`asterism/games/games/**`) に置いた見張りの queryable が受けた呼び出しの数 (答えない。ホストが居ない間は 4 秒持ってから閉じるので、呼び手には誰もいないときと同じ 3 秒の時間切れに見える)。「戻るまで」は観測ノードの on_join (ホストのアプリの印) からの時間。

| 卓のアプリの状態 | 前: 止めている間の get / 戻るまで | 後: get / 戻るまで |
|---|---|---|
| sim の麻雀、一覧 (「自分の席あり」の探り `mine`) | 36.4 s で 10 回 / `mine` の答えまで 1.08 s | 37.7 s で **0 回** / 一覧に戻るまで 17 ms (`mine` も 18 ms) |
| sim の麻雀、対局中の席 | 21.4 s で 6 回 / 答えのある view まで 0.72 s (3 秒の周期しだいで最大 3 s) | 20.3 s で **0 回** / 5 ms |
| sim のオセロ、一覧 | 0 回 (下の注) / 一覧から外れない (止まっている間も古い状態のまま) | 0 回 / 抜けて 42 ms で外れ、戻って 15 ms で一覧に戻る |
| sim のオセロ、観戦の席 | 計っていない (麻雀と同じ 3 秒の view) | 21.5 s で **0 回** / 7 ms |
| CRuby の tty、観戦の席 | 21.5 s で 5 回 / 2.97 s | 20.3 s で **0 回** / 1 ms |

注: 一覧の取り直し (`Asterism.each`) は網に get を出していなかった。ボードの Asterism (0.6 以降) の `each` は、すでに受けている liveliness の監視から作った手元の表を読むだけ (7 章)。待っている間に出ていた get は、対局中の 3 秒ごとの view と、麻雀の一覧の `mine` の探りだった。後はどちらも、ホストが居ない間は出さない。

### 6.4 今どおり遊べる (条件 4)

- 家の LAN、sim (426x240、麻雀は 640x360): 麻雀に座って CPU 3 人と対局、打牌 2 回 (応答 63 ms)、ポンの申し出が出た (Esc でパス)。オセロは CPU と終局まで (30 手をクリック、41-23、`report/gs3/othello_sim_426x240_over.png`)。
- 家の LAN、ブラウザ版 (640x360、feature/gs3 で作り直した束): 麻雀に座って打牌 2 回 (応答 65 / 64 ms、`mahjong_web_640x360.png`)。オセロは CPU と終局まで (41-23、`othello_web_640x360_over.png`)。
- 部屋の卓 (deploy/local、後): 2 つのページが座り、卓の主が始め、矢印と Enter で 60 手を終局まで (ホストの記録 `game over (count, 19-45, seat 1 wins)`、`room_othello_over.png`)。前の image でも同じ台本を回したが、29 手目で鍵の入力が食い違って止まった (台本の待ちが短かった。待ちを 2.5 秒にした後の版は 2 回とも終局まで行った)。
- 麻雀の卓のアプリのボット (`bin/mahjong-bots --players 3 --rules 0=1`) が東風戦を終局まで、同時にオセロの tty が CPU と 1 局 (CPU の勝ち 10-54)。

### 6.5 数字 (条件 5)

| | GS2 | GS3 |
|---|---|---|
| 1 通の最大 (実戦、平文、ボット 3 + CPU の東風戦とオセロ 1 局を同時に) | 531 B | 549 B (view reply。局の中身で揺れる幅) |
| ゲームホストの RSS (VmHWM、同じ負荷) | 60.8 MB | 59.6 MB |
| オセロの卓のアプリの VM (sim、普通のプール 1.5 MB、30 秒ごと) | 479-492 KB (31-32 %) | 484-500 KB (31-32 %) |
| オセロの卓のアプリの VM (ブラウザ版、部屋の中) | 344-356 KB (22-23 %) | 351-366 KB (22-23 %) |
| 麻雀の卓のアプリの VM (sim、large 3 MB) | 788-829 KB (前の版を同じ日に計った値) | 797-837 KB (25-27 %) |

コンパイル直後のプール (sim、64 ビット。`VM pool after compile`):

| | 前 | 後 | 差 |
|---|---|---|---|
| 麻雀: ソース / 使用 / 山 | 57,103 B / 566,864 B / 1,456,456 B | 60,904 B / 576,040 B / 1,487,344 B | +3.8 KB / +9.2 KB / +30.9 KB |
| 麻雀、case/in を if に戻した版 | - | 60,651 B / 575,392 B / 1,489,136 B | case/in の分は使用 +0.6 KB |
| オセロ: ソース / 使用 / 山 | 22,721 B / 464,344 B / 843,464 B | 25,809 B / 471,976 B / 869,128 B | +3.1 KB / +7.6 KB / +25.7 KB |

- 増えた分は居る・居ないの処理 (on_join / on_leave のメソッド、ログの文字列) とそのコメント。麻雀の山は sim の large (3 MB) の 47 % のまま、mj5.md の 32/64 ビットの比 1.48 で直すと Modern の large (2 MB) の 49 % ほど (MJ5 の 47 % から +2 点)。オセロの山は普通のプールの 55 % (前 54 %)。
- 減らす工夫: 足したコメントを短くした (ソースは 60,904 → 60,440 B、25,809 → 25,343 B。山はソースの大きさにほぼ比例する)。case/in は 0.6 KB だけなので残した。上の「後」の数字はコメントを短くする前の値 (短くしたあとの版はブラウザ版の部屋で動かした)。

### 6.6 エディタ (条件 6)

sim の標準構成 (Spinel のカーネル、Spinel のエディタ、vm 3) でメニューから Editor を開き、`x` を 1 打鍵: 「Ln 1, Col 2 *」と出た。

## 7. 見立てと違った点・撤回した仮説

- **一覧の取り直しは網に何も出していなかった**。指示書の「数秒ごとの一覧の取り直し → get の回数を減らす」の get は、`Asterism.each` ではなく、対局中の view と麻雀の `mine` の探りだった (ボードの `each` は liveliness の監視の手元の表を読む)。30 秒ごとの見直しにしても網の量は変わらず、変わったのは「ホストが居ない間に呼ばない」ところ。
- **sim のアプリの kill と CRuby の Ctrl+C は「途切れ」にならない**。オセロの卓のアプリは `on_destroy` で、tty は終わるときに `leave` を呼ぶので、会員ごと消える (前も後も数 ms)。途切れになるのは、タブを閉じる・kill -9・`leave` を呼ばない麻雀の卓のアプリの kill。
- 最初の見張りの queryable はすぐに閉じていたので、ホストが居ない間の呼び出しが時間切れでなくすぐ失敗し、麻雀の `mine` の探りが 60 ms ごとに繰り返された (前の数が 400 回を超えた)。ホストが居ない間は 4 秒持つ形に直して測り直した (表の値)。
- 最初の版の「つながりが切れた」が麻雀ですぐ消えた: ホストが抜ける直前に送った view の答えが on_leave のあとに着き、成功した view で「切れた」を消していた。切れたあとに送った view だけが消すようにした。
- 部屋で on_join で戻した会員が 10 秒後にまた途切れた (一覧のままだと呼び出しが無いので、備えの away_ms が効いた)。「見えたノードは呼ばなくても居る」(`@live`) にして、備えを「ホストに見えないノードだけ」にした。前の版 (`7c87c17`) ではそもそも join するまで途切れのまま (22.2 s)。
- 配列の pattern が nil で落ちる (4 章)。sim で麻雀が待ち合わせに入った瞬間に終わったので分かった。
- 部屋の卓を遊ぶ台本 (gs2room.js の写し) は、手と手の間の待ちが 1.5 秒だと 29 手目で食い違った。2.5 秒にして通した。
- 常設の卓は見えるだろうと思っていた (役割に `Watch asterism/**` があるため)。Watch は監視の宣言を許すだけで、印の egress は rw の鍵にしか許されていない (2.3)。

## 8. 判断を仰ぐこと (常設の卓)

指示書の「部屋・常設・家の LAN のどれでも同じに効くこと」は、常設の卓で満たせていない。ゲームホストの側は直さなくてよく (見えるようになれば同じに効く)、要るのは console の役割の変更で、止まる条件 (ACL の変更) に当たる。選択肢:

1. **(推奨) 役割 `games-host` に `Read asterism/*/game` を足す**。docs/rooms.md の「A game host's permanent tables」の手順と、今の VPS の行 (Admin > Relay > Roles で足して Apply) を直す。常設の卓でも途切れが 13 ms になる (手元で確かめた)。ホストが新しく読めるのは端末とブラウザ版の卓のアプリの印の鍵 1 つ (`asterism/<node>/game`) だけで、その鍵に put するものは無い。console のコードは変わらない。
2. **今のまま**。常設の卓だけ途切れは 10 秒台 (呼び出しの間隔)。README の「Who is there」の表にそう書いてある。
3. console の cloud_config で、`watch` の役割に印の egress (`liveliness_token`) も許す。どの役割の Watch でも全ノードの印が届くようになる (範囲が広い。コードの変更)。

## 9. GS4 に向けて分かったこと

- 居る・居ないはノード単位: 同じノードで Asterism につないだ別のアプリが動いていると、卓のアプリを閉じてもノードは抜けず、途切れにならない (見えているノードは備えも効かない)。今の端末 (sim・ブラウザ版) では卓のアプリのほかにつなぐものが無いので起きていない。アプリ単位で知るには Asterism にアプリの on_join が要る (内部の `@apps` はある)。網を見る CLI (GS4) でも「ノード」と「アプリ」のどちらの出入りを見せるかを決めておくとよい。
- 状態のキー `@status` (GS4) と同じく、居る・居ないも「読む側」の都合で見える範囲が ACL で変わる (部屋は見え、常設は見えない)。CLI・MCP が網の全体を出すとき、見えないものを「居ない」と言わないこと。
- 配列・hash の pattern を nil や別の型に当てると落ちる VM の制約は、AI の打ち手や MCP の橋渡しで答えの形を case/in で分けるときに踏む。コンパイラを直すなら lib/patch の配列・hash の pattern に `respond_to?` の確かめを足す (上流にも出せる形)。
- ホストを止めて起こしたときの戻りは on_join で数 ms になった。ルータが再起動したとき (全ノードが抜けて戻る) も同じ道を通る。

## 10. 残ったこと

- 8 章の判断。
- 実機 (Tab5・P4-Nano) では動かしていない (指示書の範囲外)。麻雀の山は Modern の large で 49 % ほどの見込み。
- lib/patch のコンパイラの配列・hash の pattern の直し (範囲外、9 章)。
- 後片付け: deploy/local を `GAMES=1 VOLUMES=1 rehearse down` で volume ごと消し、`deploy/local/.env` と `certs/` を消した (console の作業コピーは clean)。asterism-games の rehearsal の project を `down -v`、残っていた volume `asterism-rehearsal_rooms` と network `asterism-rehearsal_router` (この作業で作られたもの) を消した。deploy/vps の `.env` と `credentials/games.tar`、作った image `asterism-games:gs3`・`asterism-console-rehearsal:0.3.3`、zenohd `gs3-zenohd`、家の LAN のゲームホスト・観測ノード・tty、web_* の開発サーバとブラウザ、sim (音を戻してから sim_down) を止めて消した。scratchpad の鍵 (.tar)・TOTP の秘密・admin の状態も消した。前からある image (`asterism-games:7c87c17` ほか、Playwright、`eclipse/zenoh:1.10.1`) は残した。docker の prune はしていない。fmruby-core の build/ は x86-64 のまま (`file build/fmruby-core.elf` で確認)。wasm/build はこの branch で作り直したまま。
- fmruby-graphics-audio の作業コピーに前からの変更 (`flash/etc/display_conf_linux.txt`) があり、触っていない。
