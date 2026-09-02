# ユーザサービス: 1 つの VM に常駐を集める

doc/user_extension/ideas.md の案 2 (起動時サービス) の計画。ユーザが Ruby で
書いた常駐処理 (時報、MIDI ルータ、ウィジェットの中身、通知など) を、
**アプリ 1 本 = VM 1 つ**に集めて動かす。

## 判断の記録

- サービスごとにアプリを起動すると、1 本につき FreeRTOS タスク (内蔵 RAM
  スタック 12-16KB) + mrb_state 一式 + VM プール 1 枠 (全 9 枠) を食う。
  常駐 3 本で目に見えて他が困るので、**ホスト 1 本に集約する**。
- ホストの中身は**イベントループ方式** (Task = mruby-task は使わない)。
  比較した結果: Task の実利は「Ruby の無限ループからの保護」と「loop +
  sleep_ms で書ける快適さ」の 2 つだけで、C で塞がる呼び出しにはどちらも
  無力。一方イベントループは (1) アプリの土台 (on_update / on_control) が
  そのままループなので**新しい機構がゼロ**、(2) 配送が即時 (Task 方式は
  sleep の粒度でしか郵便箱を見ない)、(3) 逐次実行なので**共有状態の競合が
  原理的に無く**、初心者の書くサービスに安全、(4) Task ごとの mrb_context
  を持たない。
- 将来のための三段構え: **イベントループが既定、`task = true` (mruby-task
  に包む) は将来の opt-in、`own_vm = true` (単独アプリ) は最後の逃がし**。
  v1 はイベントループのみ実装し、契約は三段のどれでも成立する形にする。

## 対象機種: 当面 Modern のみ (2026-08-24 決定 / 2026-09-03 に Retro も上げる方針へ)

サービス機能は **Modern (P4) 専用**とする。Retro (S3) は flash 残 6%・内蔵
RAM 逼迫の機械であり、常駐の装備は「作る機械 = Modern」に置く方針とも
一致する。検証の行列も {Modern} x {標準, 全 mruby} に半減する。

- 門は 1 か所: kernel のブート spawn を FMRB_HW_MODERN で囲う (Retro 向け
  sim も実機と同じく無効)。
- Retro の firmware にはホストとサンプルを**入れない** (spawn しないだけで
  なくビルドから外し、S3 の flash を守る)。
- shell の svc / ps 子行は「ホスト不在」の既存挙動がそのまま働く。
- これは削除ではなく既定の話: 門が 1 か所なので、必要になれば Retro でも
  安く有効化できる。

### Retro もいずれ上げる (2026-09-03 決定、着手は未定)

**Retro でもサービスホストを起動する方針**に変えた。理由は常駐機能が
欲しくなったからではなく、**重い仕事の逃がし先が要る**ため:
ランチャーの `/app` 走査を優先度 8 のデスクトップから優先度 1 の
サービスへ移したいが、Retro にホストが無いと Retro だけ救われない
(reference/task_priority.md の「優先度 8 のタスクに長い仕事をさせない」、
reference/launcher_rescan_desktop_stop.md)。

**今はやらない。** 着手するときに片づける必要があるのは上に挙げた
「入れない」理由そのもの — S3 の flash 残 6% にホストとサンプルが入るか、
内蔵 RAM のスタック 1 本ぶんが出せるか。それまで Retro は刻んだ走査と
占有率 64% での逐次回収で凌ぐ。

## 置き場所と設定 (システム / ユーザ / 状態の三層)

```
/etc/services.toml         システムサービスの一覧 (config/ から生成、firmware 同梱)
/usr/share/services/*.rb   システムサービス本体 (firmware 同梱)
/home/services.toml        ユーザサービスの一覧 (ユーザが編集)
/home/services/*.rb        ユーザサービス本体 (ユーザの Ruby)
/home/services_state.toml  on/off の記録 (ホストが書く。ユーザは普段触らない)
services.app.rb            ホスト (headless、prebuild。main/prebuild_scripts/default_app/)
```

- **システムサービス**は firmware と一緒に配る常駐 (例: `clock/hour` を
  publish する時計、schedule、将来のウィジェット基盤)。一覧は /etc
  (= config/ から生成されるシステム側)、本体は /usr/share に置く。
- **ユーザサービス**は /home。ユーザが手で書き換える物は /home、という
  既存の区分に従う。
- ホストは**システム → ユーザ → 状態ファイルの順に読む**。同名の項目は
  後の層が前を上書きし、**状態ファイルが最優先**。ユーザ側は少なくとも
  `enable = false` でシステムサービスを止められること。ps の一覧には
  出どころ (sys/usr) を出す。
- **状態ファイルはホストの持ち物**で、書くのは `svc enable` / `svc disable`
  のときだけ。中身は `名前 = true/false` の行のみ。**ユーザの toml は
  ホストが書き換えない** — 手書きのコメント・並び・書き方を、フラグ 1 つの
  ために失わせないため。知らない名前が残っていても無害 (ログ 1 行で無視)。
- どちらの toml も無ければホストは spawn されない (現状と同一)。

### stop/start と enable/disable の違い

| | 効き目 | 記録 |
|---|---|---|
| `svc stop` / `kill <名前>` / `svc start` | **今のセッションだけ**。再起動で元に戻る | 無し |
| `svc disable` / `svc enable` | 今すぐ効き、**再起動をまたぐ** | 状態ファイル |

`disable` は「配送から外して `on_stop`」+ 記録、`enable` は「(未ロードなら
読み込んで) 開始」+ 記録。ps / `svc list` の状態語も分けてある:
**`stopped` は今回だけ、`disabled` は次回以降も**。

```toml
# /home/services.toml
[chime]
file = "chime.rb"          # /home/services/ からの相対
class = "Chime"
enable = true
interval_ms = 60000        # on_tick の周期。無ければ tick なし
# own_vm = true            # (将来) 単独アプリとして spawn
# task = true              # (将来) mruby-task に包む

[chime.config]             # サービス固有の設定。ctx.config で渡す
hour_only = true
```

## アプリの自動起動 (`app =` 項目)

services.toml の項目は**アプリ型**も書ける。「ブート時に立ち上がるもの」の
一覧を 1 つのファイルに揃え、立ち上がったアプリは普通のアプリとして
ps / kill の対象になる (サービスの子管理は不要)。

```toml
[my_game]
app = "/app/game/robo_explorer/robo_explorer.app.rb"   # file/class と排他
fullscreen = true          # app.toml の window_mode を上書き (省略時は toml どおり)
delay_ms = 0               # 任意。desktop が落ち着くまで待つ用
```

- 起動はホストが行う (launcher と同じ spawn 要求)。kernel は要求の答えとして
  `{"cmd"=>"spawn_result", "app"=>要求したパス, "pid"=>新 pid}` を要求元へ返す
  (`run_result` と同じ形)。ホストはこれで自分が起こしたアプリの pid を覚え、
  `app/died` の pid と突き合わせる。
- fullscreen の上書きは、spawn 属性に `fullscreen` フラグが既にあるので
  **spawn 要求で渡せるならそれが第一** (fmrb_app_spawner.c が app.toml から
  立てているのと同じ場所)。要求経路に載せられない場合は、spawn 後に
  `fmrb_app_set_fullscreen(pid, true, w, h)` (既存の実行時トグル) で寄せる。
- 用途: 起動したら即ゲーム機/発表機として使う、いわゆるキオスク的な運用。
  desktop を出さずに直接立ち上げる「真のキオスク」はカーネルの起動順の
  話になるので**本計画の外** (欲しくなったら別途)。
- **`restart = true`**: そのアプリが**異常死したときだけ**起こし直す。
  kill / stop / 正常終了は「意図した停止」なので生き返らせない (生き返ると
  kill が無意味になる)。2 秒待ってから、300 秒の窓で 3 回死んだら諦めて
  エラーログ 1 行。ホストの仕事で、判断材料は下の `app/died`。

### システム topic `net/state`

net サービス (システム) が公開する。**変化したときと、自分の起動時**に流す。

```ruby
topic "net/state"
{ "connected" => true, "ip" => "192.168.10.21", "ssid" => "Buffalo-G-F750" }
```

- 取得の作法は**2 つ**で、購読者の事情が 2 通りあるため:
  - 変化に反応したい者は購読する (publish が届く)。
  - **後から起動した者**は最初の publish を聞き逃している (Pub/Sub は保持を
    しない)。要求 topic **`net/get`** に何か流すと、net が現状を `net/state`
    として publish し直す。答えが専用の topic でなく通常の `net/state` なの
    は、遅れて来た者と最初から居た者が欲しい物は同一だから。
  - 実例: timesync は一覧で net より後に読み込まれるため、`on_start` で
    `net/get` を投げて現状を受け取る。
- `ip` / `ssid` は未接続のとき空文字。

### 同じホスト内のサービス同士の配送

**kernel は publish 元の pid には配り返さない**。ホスト内のサービスは全員が
ホストの pid を共有するので、そのままだと**隣のサービスにだけ届かない**
(clock -> hourly_chime、net -> timesync がまさにこの組)。

そこでホストは `ctx.publish` を**2 方向**に出す: kernel へ (他のアプリ向け)
と、自分の中の購読者へ (待ち行列に積み、次の `on_update` で配る)。同期的に
配らないのは、`on_event` の中からの publish が入れ子の呼び出しになり、
「サービスは順番に走る」という約束が崩れるため。1 周で配る上限を設けてあり、
自分の流した topic を自分で購読するようなループはそこで止まって警告 1 行になる。

### システム topic `app/died` (第 1 号)

kernel は**どのアプリが終わっても** publish する (ideas.md 案 6 の最初の実例)。

```ruby
topic "app/died"
{ "pid" => 5, "name" => "SubDemo", "expected" => true }
```

- `expected` が真 = **頼まれて止まった** (shell/monitor の kill、Ctrl+Q、
  閉じるボタン、スクリプトの完走)。偽 = **異常死** (例外で落ちた等)。
- 見分けは app context の `expected_stop` 1 bit。**「止めてくれ」と言われた
  ところ全部**で立てる: kernel の kill 要求、`FmrbApp#stop` (両エンジンの
  土台)、Lua/BASIC が "stop" を latch する C の口。mruby のアプリは例外で
  死んでも**スクリプト末尾の rescue が拾うのでタスクは正常終了に見える**
  — C から区別が付かないので、この印が唯一の手掛かりになる。
- 購読は任意。ホストは `restart = true` の項目があるときだけ購読する。

## サービスの契約

```ruby
# /home/services/chime.rb
class Chime
  SUBSCRIBE = ["clock/hour"]        # 任意。無ければ購読なし

  def on_start(ctx); end            # 起動時に 1 回。ctx は保持してよい
  def on_tick(now_ms); end          # interval_ms ごと (周期専用)
  def on_wake(now_ms); end          # ctx.wake_in(ms) で予約した一度きりの起床
  def on_event(topic, data); end    # 購読 topic の配信
  def on_stop; end                  # ホスト終了時 (呼ばれない死に方もある)
end
```

wake_in と on_tick を混ぜない理由: 周期より早い on_tick が届く形にすると、
「今のは周期か、頼んだ起床か」をサービスが自分で覚えて区別することになる
(隠れた状態)。呼ばれる意味は 1 メソッド 1 種にする。wake_in は周期の予定を
**ずらさない** (別枠の期限)。保留は 1 つだけで、呼び直しは置き換え。
`on_wake` の無いサービスが wake_in を呼んだら警告 1 行 (ほぼ確実にバグ)。

- 4 つとも任意 (定義したものだけ呼ばれる)。**短く返す**のが約束: 逐次実行
  なので、長居は他のサービスを待たせる (ホストが実測して警告する)。
- 定常経路の確保の規則はアプリと同じ緩さでよい (FmrbUI ほど厳しくしない)。
  ただし on_tick 1 回ごとに増え続けるとホスト全体のヒープに効くことを
  ドキュメントに書く。

### ctx (ホストがサービスに渡す唯一の口)

```ruby
ctx.publish(topic, data)   # そのまま Pub/Sub へ
ctx.wake_in(ms)            # 一度きりの起床を予約 -> on_wake (保留 1 つ、置き換え)
ctx.audio                  # ホストが 1 つ持つ FmrbAudio (note_on/off、FMSQ)
ctx.log(msg)               # "svc[chime] ..." の接頭辞つきで Log.info
ctx.now_ms                 # Machine.board_millis
ctx.config                 # services.toml の [名前.config] (Hash)
ctx.stop_self              # 自分を無効化 (次の配送から外れる)
```

## ホスト (services.app.rb) の作り

headless アプリ。既存の枠組みだけで書ける:

- `on_create`: /etc/services.toml と /home/services.toml をこの順に読んで
  併合し、enable のものを `require`、
  インスタンス化して `on_start`。SUBSCRIBE を集めて重複を除き
  `subscribe(topic)`。次の tick 期限を計算。
- `on_control(msg)`: `topic_data` を受けたら、その topic を購読している
  サービスの `on_event` を順に呼ぶ。
- `on_update`: 期限が来たサービスの `on_tick` を呼び、**次に一番近い期限
  までの ms を返す** (下限 50)。固定周期の tick は存在せず、期限のない間の
  空呼びはしない。**期限が 1 つも無ければ 30000 を返して長く寝る**:
  `_spin(timeout)` は待ち時間中もメッセージを処理する (on_control は
  その場で呼ばれる) ので、長く寝ても購読イベントや svc/ctl の応答性は
  落ちない。起床は tick のためだけにある。
- **長く寝てよい成立条件**: 配送は落ちないが、**その配送が新しい期限を
  作る場合は別**。`on_control` は `_spin` の中で呼ばれ、`_spin` は自分から
  早く帰らないので、`on_event` から頼んだ `wake_in(200)` は放っておくと
  眠りが明けるまで (最悪 30 秒) 待たされる。そこで土台に
  **`FmrbApp#request_early_update`** を置き、呼ぶと今の `_spin` がその場で
  終わって `on_update` に戻るようにした (mruby は C の `_spin`、Spinel は
  `fmrb_app_base_spinel.rb` の `_spin`。呼ばないアプリの負担はメッセージ
  1 通あたり ivar 読み 1 回)。**ホストは期限が動いたときだけ呼ぶ**
  (`ctx.wake_in` と `svc/ctl start`)。配送のたびに呼ぶと賑やかな topic で
  空回りするため。30000 が許されるのはこの離脱口があるからで、片方だけ
  取り入れると同じ穴に落ちる。
- 各呼び出しは `rescue` で包む。例外は `ctx.log` に 1 行 + そのサービスを
  無効化 (**累計 3 回で恒久無効**。1 回では消さない: 起動直後の一過性に
  やられないため)。他のサービスは続行する。
- 各呼び出しの所要を `board_millis` で挟み、**50ms を超えたら警告 1 行**
  (数えて 10 回ごとに要約でもよい。ログを埋めない)。
- 制御 topic `svc/ctl` を購読し、下の「ps / kill からの管理」の要求に
  応える。reload は**ホストごと再起動** (mruby はクラスを unload できない
  ので、部分再読み込みはしない)。

## 単発 (一回きり) の扱い

常駐だけでなく「1 回走って終わり」も居場所を決めておく。

- **ブート時に 1 回**: services.toml に `oneshot = true`。ホストは
  `on_start` を呼んだら一覧から外す (tick も購読も無し)。音量の復元や
  初期化に。ホスト側の追加は 1 分岐。
- **対話コマンド** (打ったら 1 回): サービスの仕事ではなく、shell が
  `/home/bin/<名前>.rb` を**自分の VM で eval** する (ideas.md 案 1。
  irb が同じ機構の実証)。ファイルは `def main(args)` を定義する約束。
- **時刻で 1 回** (cron 風): schedule を**システムサービス**として 1 本
  書き、`/home/schedule.toml` の期限が来たら /home/bin の作法で実行するか
  アプリを spawn する。ホストの機構追加はゼロ。
- **重い・落ちうる単発**: 従来どおりアプリとして spawn (隔離。起動費用
  数百 ms + プール 1 枠は許容)。

**ホストに「任意コードを後から実行する口」は作らない** (svc/ctl に run を
足す類)。単発の実行路は上の 4 つで足り、ホストは常駐の管理に徹する方が
無効化やエラー計数の意味論が濁らない。

## ps / kill からの管理

サービスはホストの中の存在で pid を持たないため、**名前**で管理する。
アプリ (pid) の管理系に子として現れる形にする。

### 要求/応答の約束 (Pub/Sub)

- 要求: topic `svc/ctl` へ `{"cmd"=>..., "name"=>..., "reply_to"=>...}`。
  `cmd` は `list` / `stop` / `start` / `enable` / `disable`。`reply_to` は呼び手が購読している
  応答用 topic (例 `svc/re/<自分の pid>`)。
- 応答: **1 サービス 1 通**。`{"cmd"=>"svc", "svc"=>{"name"=>..., "origin"=>
  "sys|usr", "state"=>"running|stopped|failed|disabled", "ticks"=>n, "events"=>n,
  "wakes"=>n, "errors"=>n}}` を並べ、`{"cmd"=>"svc_end", "count"=>n}` で
  締める。`stop` / `start` は `{"cmd"=>"svc_result", "ok"=>true/false,
  "name"=>..., "err"=>...}`。
  一覧を 1 通にまとめないのは、**メッセージ本体が 176 バイト**
  (`FMRB_MAX_MSG_PAYLOAD_SIZE`) でサービス 3 本で溢れ、溢れると
  `send_message` が raise するため。届いた順に印字する ps 側にも都合がよい。
- `stop` = 配送から外して `on_stop` を呼ぶ。`start` = 再開 (インスタンスは
  保持している。エラー 3 回で failed になったものも `start` で数を 0 に
  戻して再開できる)。
- ホストが起動していなければ応答は来ない。呼び手は 1 秒で諦めて
  「services host not running」を出す。

### shell

- `ps`: 従来のアプリ一覧に加え、`svc/ctl list` を投げ、応答が来たら
  ホストの行の下に子行を印字する (`  └ chime      running  ticks=123 err=0`)。
  コマンド内で塞いで待たない (アプリの土台はコールバック配送なので、
  応答は on_control から非同期に印字する。**`kill` が kernel の
  `kill_result` を受けて印字する既存の形と同じ**。shell/shell_commands.rb
  cmd_kill 付近)。
- `kill <引数>`: 数字なら従来どおり pid。**数字でなければサービス名**として
  `svc/ctl stop` を送る。ホストごと殺したいときは従来どおり pid で。
- `svc start <名前>` / `svc list` も置く (kill と対にする)。
- `svc enable <名前>` / `svc disable <名前>` は永続する方の対
  (上の表)。help で 2 対の違いを書く。
- サービス名とアプリ名の衝突は気にしない (kill の数字/非数字で経路が
  分かれるため。アプリを名前で kill する機能は無い)。

### その後 (S2 の残り)

- monitor の Tasks ページに同じ list を出し、kill ボタンで stop を送る。
- rd_http (`/app/list`) への追加は当面しない (開発は shell 経由で足りる)。

## タスク優先度

ホストのタスクは **FMRB_SERVICE_APP_PRIORITY (1) を新設**して使う
(ユーザアプリの 2 より 1 段下。kernel 9 / host 10 / desktop 8)。

- サービスは背景の存在であり、**前面のアプリをカクつかせない**のが第一義。
  2 (同格) だと同格ラウンドロビンでサービスのハンドラがゲームのフレームから
  スライスを奪う。1 なら前面が眠る瞬間 (アプリは on_update の間必ず眠る)
  にだけ走る。
- 裏返しの割り切り: 前面が 100% 回り続ける (バグった) 間はサービスが
  遅れる。前面優先の方針どおりで、許容する。
- **遅延に敏感なもの (MIDI ルータ等) はこの席に向かない**。それは
  `own_vm = true` の単独アプリ (優先度 2) へ、という使い分けの根拠になる。

## kernel の変更 (1 か所)

- ブートで desktop を spawn している場所 (fmrb_kernel.rb 606 行付近) の後に、
  **/etc/services.toml か /home/services.toml が存在すれば** services ホストを
  spawn する。どちらも無ければ何もしない (Retro がこれ)。
- kernel Ruby は Spinel でも動くので **dual-safe** で書く (保存ブロック
  なし、bare 定数なし。ruby_writing_constraints)。
- 死んだときの自動再 spawn (reaper 連携) は v1 ではやらない。手で
  launcher / shell から起動し直す。入れるなら S2。

## サンプル (同梱、launcher には出さない)

システム側 (/usr/share/services): `clock.rb` — 1 分ごとに時刻を見て、正時に
`clock/hour` を publish する。**システムサービスの型見本**であり、chime の
購読先になる。

`hourly_chime.rb` — `clock/hour` を購読して note_on 1 回 (chime の実物)。
**システムサービスとして同梱する** (2026-08-31 に /home から移した。
doc/wasm/storage_persistence.md)。

写して使う見本 (/usr/share/samples/services。既定では 1 本も走らない):

1. `heartbeat.rb` — interval_ms = 10000 で uptime を ctx.log。**最小の型見本**。
2. `broken.rb` — わざと例外を出す。**隔離の生存確認用** (verify.md の
   「検査をわざと壊す」の常設版)。/home へ写して enable = true で使う。
3. `services.toml.example` — /home/services.toml の手本。

**/home には配布物を置かない** (更新のたびに配布物とユーザの編集が
衝突するため。経緯と規則は doc/wasm/storage_persistence.md)。

## システムサービスの候補 (S1 のサンプルの先)

価値が高い順。「システムサービスが出し、別のサービスが購読する」連鎖が
そのまま Pub/Sub 契約 (ideas.md 案 6) の実例になる。

| 候補 | すること / publish | 備考 |
|---|---|---|
| clock | 1 分ごと `clock/minute`、正時 `clock/hour` | S1 の型見本。chime・schedule・省電力の土台 |
| net | WiFi の接続/切断/IP を `net/state` に。取得した IP をログ (将来はウィジェット) に出す | 「IP は毎回引く」実運用の痛みに直接効く |
| timesync | WiFi 接続で SNTP → set_hwclock + RTC へ書き戻し | Set Clock の手作業が消える。RTC 書き込みは clock_setting の実装を流用。`net/state` 購読 = サービス間連携の最初の実例 |
| flightrec | 周期で IRAM/PSRAM 残・スタック high-water・稼働アプリを /home/log/ のリングファイルへ | クラッシュ解析が「シリアル開きっぱなし限定」である弱点を補う。再起動後に前回の最期が読める |
| idle | 入力が N 分無ければバックライト休止 | Retro の NTSC 焼き付き対策も兼ねる。**入力イベントの publish (kernel 側の口) が先に要る** — 案 6 の最初の題材 |
| storage | SD の抜き差し・空き容量を `storage/sd` に | file manager や書き出しが購読して案内 |
| battery / temp | 電池残量・チップ温度を周期 publish | ウィジェット枠 (案 5) の中身の定番。Tab5 の電池残量の取得可否は要調査 |
| notify | アプリの通知を集めて表示へ回す | 表示側の差し込み口 (案 5) とセットで |

進め方の目安: S1 = clock (+net)、S2 = timesync + flightrec。idle と notify は
kernel / desktop 側の差し込み口が要るため単独では進められない (案 5・6 を
引っ張る牽引役)。

## 検収 (sim, headless)

| 項目 | 見るもの |
|---|---|
| 起動 | toml あり → ホストが spawn され、heartbeat のログが 10 秒おきに出る。両 toml 無し → 何も起きない |
| 二層 | システムの clock が動き、/home 側で `[clock] enable = false` にすると止まる (ユーザ上書き)。ps に sys/usr の出どころが出る |
| oneshot | `oneshot = true` のサービスが on_start 1 回だけで一覧から外れる |
| アプリ起動 | `app =` 項目のアプリがブートで立ち上がり、`fullscreen = true` なら全画面 (Ctrl+Tab の park も従来どおり)。ps では普通のアプリとして見える |
| 配送 | pub_demo から topic を publish → サービスの on_event が届く (ログ) |
| 隔離 | broken.rb を enable → 例外ログ 3 回で無効化、heartbeat は**止まらない** |
| 警告 | わざと 100ms 眠るサービスで警告 1 行 |
| ヒープ | 60 秒放置で GC.start 後の live が増えない (U1 の手法) |
| 費用 | M1 の spawn:services 行で内蔵 RAM 消費、fmrb_task: でスタック実測を report に |
| 終了 | kill でサービスの on_stop が呼ばれ、ホストが Reaped まで到達 |

実機 (Tab5) はブートでホストが上がることと M1 の数字だけ。Retro は sim と
同経路なので任意。

## 段割り

- **S1**: 上記全部 (ホスト + 契約 + kernel の 1 か所 + サンプル 3 本 + 検収)。
- **S1.5**: ps / kill からの管理 (上記。shell 側と要求/応答)。S1 と同じ
  検収の回で見られるなら S1 に含めてよい。
- **S2** (別途判断): reaper で自動再 spawn、monitor の Tasks ページ統合、
  enable/disable の toml 書き換え。
- **S3** (欲しくなったら): `task = true` (mruby-task に包む。ビジーループ
  保護が要る計算系向け)、`own_vm = true` (単独アプリへの逃がし)。
- ウィジェット枠 (ideas.md 案 5) と HTTP 橋 (案 10) は**このホストの上に
  乗るサービス**として、それぞれ別の計画で。

## やらないこと (v1)

- mruby-task / 単独 VM (契約だけ両立させておく)
- 自動再 spawn、部分 reload、サービス間の直接呼び出し (共有は Pub/Sub 経由)
- サービスからの描画 (headless。画面が要るものはアプリにする)
