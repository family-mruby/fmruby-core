# 運べるアプリの書き方 (movable なアプリの前提・制約・作法)

> 状態: 進行中 | 更新: 2026-11-06 (テーマの色、platform) | 動いているアプリを止めてイメージに書き、別の VM・別のプロセス・別の端末で続ける (doc/checkpoint) ときに、アプリを書く人が知っておくこと。前提 (mruby の VM のアプリだけ、`.app.toml` に `movable = true`、運べるビルド)、制約 (断られるもの、戻らないもの、Task のブロックの self、時計)、作法 (hook でしてよいこと、端末ごとに違うメソッドへの備え、`requires` の申告、移す頼み方)

計画と決まったことの正は `doc/checkpoint/plan.md`。この文書はそこで決まったことを、アプリを書く側から見てまとめ直したもの。
API の説明はエディタの型支援 (`sig/fmrb_app.rbs` の `on_checkpoint`・`on_restore`・`on_checkpoint_refused`・`checkpoint`) にもある。

## 1. 前提

- **mruby の VM で動くアプリだけ**が運べる。Spinel で動くもの (標準構成のカーネル・エディタ・Shell など) は対象外 (D16)。頼むと「only an app on the mruby VM can be written」で断られる。
- **`.app.toml` に `movable = true`** と書いたアプリだけ (D6)。書いていないアプリは、頼まれても断って動き続ける。
- 運べるビルド (gem mruby-checkpoint を持つもの) は、今は **sim (Linux のシミュレーション)・ネイティブ版・P4 の実機 (P4-Nano・Tab5、F6)**。ブラウザ版の公開のビルドに入れるかは未決 (測るための別のビルド `FMRB_WASM_CHECKPOINT=1` にはある)。S3 はまだ持たない。
- `movable = true` のアプリは、起動したときに「基準」(VM を開いた直後の組み込みのクラス・メソッドの名前の表) を取る。名前・メソッドの一覧・クラスの写しはビルドが作ってリンクした表 (Ff、`flash_table.md`) にあり、VM ごとの費用は 32 bit で約 90 KB、64 bit で約 111 KB (`doc/checkpoint/report/ff.md`。前は約 322 KB・356 KB)。作る間だけ一時的に 32 bit で約 173 KB 多く要る。標準のアプリに `movable = true` を付けるかはユーザが決める (候補は report/ff.md の 12 章)。
- 移す先は、同じビルドどうし (ネイティブ版 2 つ) のほか、ビルドの違う sim とネイティブ版の間 (画面の大きさも違ってよい)。

## 2. 何がイメージに入り、何が入らないか

| もの | 扱い |
|---|---|
| インスタンス変数、局所変数、Task (数えている途中も)、タイマ、Fiber、アプリが作ったクラス・メソッド | **そのまま運ばれる** |
| `Machine.board_millis` | 書いたときの値から続く (D9)。止めていた時間・運んだ時間は数えない |
| タイムスライスの残り・Task の tick | 運ばれる (D10) |
| Window (位置・大きさ・枠・角丸・題)、`FmrbGfx` (どの画面に描いていたか)、フォント、`GfxBlock`、`subscribe` した知らせ | 基底クラス (`FmrbApp`) が作り直す |
| テーマの色 (`FmrbConst::THEME_*`、`theme_bg` などの `theme_*`) | 着いた側の値になる (基底クラスの枠も着いた側のテーマで作り直す)。アプリがインスタンス変数や自分の `GfxBlock` の手順に控えた色は、書いた側の色のまま運ばれる。描くときに読むか、`on_restore` で作り直す |
| 端末で決まる値 (`platform`、`FmrbConst::PLATFORM`・`BOARD`・`LANGUAGE` などの定数、`FmrbApp.language`、ファイルの関連付けの表 `FmrbAssoc`) | 着いた側の値になる (`platform` は読むたびに求め、関連付けの表は復元のときに読み直す)。アプリがインスタンス変数に控えた値 (`@esp = platform == :esp32` など) は書いた側の値のまま運ばれる。描く・決めるときに読むか、`on_restore` で求め直す |
| `FmrbI18n.t` の言語、`FmrbUI` の部品の地の色 (`bg:` を省いたときのテーマの色) | 今は書いた側の値のまま運ばれる (`FmrbI18n` は最初に引いた言語を控え、部品は作ったときのテーマの色を控える) |
| **描いた絵** | **戻らない**。Window は枠だけの空の状態で着く。アプリが `on_restore` で描き直す (3 章) |
| スプライト、画像 (`create_image`)・マスク・動画 | 戻らない。アプリが `on_restore` で作り直す (スプライトの古い ID は基底クラスが忘れる。`move` すると例外になる) |
| 鳴っている音、MIDI の予定、マイク | 戻らない。鳴らし直す・開き直すのはアプリ |
| 届いて未処理のメッセージ | 捨てる (止めている間に来たものは、続けるときだけ 16 個まで戻す) |

## 3. 制約

### 3.1 断られるもの (アプリは動き続ける)

- **開いたままのファイル**、**ソケット**、**Asterism / Zenoh のセッション**、そのほか運び方の登録の無い C のデータを持っているとき。書く瞬間 (セーフポイント) に gem が理由つきで断る (例: `C data of class File (type 'IO') has no entry in the checkpoint data table`)。閉じてから頼めば書ける。
- C から呼ばれたブロックの中で待っているときは書けない。アプリが `on_event`・`on_update` の中から頼むのはよい (書くのは、その呼び出しから戻ってメッセージ待ちに入った後の、次のセーフポイント)。
- 着いた側で、アプリが `.app.toml` の `requires` に書いた名前のメソッドがそのビルドに無いとき (5.3)。

### 3.2 Task のブロックの self はアプリでない

mruby-task の Task は、作った所の self ではなくトップレベルの self で動く。ブロックの中でインスタンス変数を読むと nil、クラスの中の定数は引けないことがある。要るものは局所変数に入れてからブロックを作る (例: `flash/app/test/ckpt_counter.app.rb` の `t0 = @t0`)。運ぶかどうかに関係なく Task の決まりだが、運べるアプリの試験で最初に踏んだ。

### 3.3 移った側での画面

- Window は同じ位置・大きさで作り直される。着いた側の画面に収まらない Window は、画面に収まるように縮められ、左上へ寄せられる。全画面のアプリは着いた側の全画面の大きさになる (Retro 320x240 ⇔ Modern 426x240)。大きさが変わったときは `on_resize(w, h)` が先に来て、その後に `on_restore`。
- フォントは着いた側にあるものから選び直す (日本語のフォントの無い端末では小さいものになり、戻しても元の大きさには戻らない)。

### 3.4 移した側

- 移した側 (移動元) のアプリには `on_destroy` が来ない。アプリは終わったのでなく移った。`on_checkpoint` が最後。
- 移すために止めている間 (着いた側が起動できたと返すまで)、移動元の Window のタイトルバーに「移動中」の札 (`moving...`) が出る。アプリは何もしない。着いた側が断ったとき・返事が無いときは、札が消えて止めた所から続く。

## 4. hook でしてよいこと

| hook | いつ | すること |
|---|---|---|
| `on_checkpoint` | イメージが書けた直後、閉じる前 (移すときは止める前) | ログ、後片付けの準備。**ここでしたことはイメージに入らない** |
| `on_restore` | イメージから起動した側で、続ける直前 | Window に描き直す (`on_update` で毎回全部描くアプリは何もしなくてよい。何かあったときだけ描くアプリはここで描く)、スプライト・画像を作り直す、音・マイク・つなぎ直し |
| `on_resize(w, h)` | 着いた側で Window の大きさが変わったとき、`on_restore` の前 | 配置を計算し直す (描くのは `on_restore`) |
| `on_checkpoint_refused(reason)` | 自分で頼んだイメージが断られたとき | 理由を見て、続ける (アプリは止まらない) |

## 5. 端末ごとに違うメソッド

### 5.1 考え方

ビルドごとに持っているメソッドが違う (ネイティブ版だけの `FmrbApp.linux_settings` など、ブラウザ版に無い通信の gem、S3 の gembox)。**運べるアプリは、すべての端末に共通でないメソッドは無いかもしれないと思って書く**。

- `respond_to?` で確認してから呼ぶか、`rescue NoMethodError` で受ける。
- **おすすめ**: 端末の違いをアプリの小さな窓口のオブジェクト (facade) に集め、そこで `respond_to?` か、`method_missing` と `respond_to_missing?` で足りないメソッドを吸収する (mruby はどちらも持つ。アプリが定義したメソッドは普通に運ばれる)。
- **おすすめしない**: 組み込みのクラス (`Array` など) に `method_missing` を足すこと。そのクラス全体で関係のない書き間違いまで飲み込んでしまう。
- 移った後に出る `NoMethodError` は、移ったアプリの普通の実行時の誤りとして扱われる (ほかのアプリの例外と同じ)。

### 5.2 着いた側が教えること (断らない)

イメージは「アプリのコードが呼ぶメソッドの名前」を持っている (形式のセクション `SND0`)。着いた側は、そのうち自分のビルドのどのクラスも持たない名前を**足りない名前**として、

- ログに出す (`[Ckpt Calls] the program calls 1 method(s) this build does not have: linux_settings (it goes on; ...)`)、
- 移す頼みの答えに入れる (`{status: moved, pid, missing: ["linux_settings"]}`、デバッグサーバの `restore` の答えの `missing`)。

**それだけでは断らない**。アプリが `respond_to?` で備えていることがあるため。

名前の確認は名前だけで、受け手のクラスは見ない。次のものは見えない、または誤って出る:

- 見えない: `eval` したコード (まだコンパイルしていないもの)、名前を計算する `send`、`define_method` で作ったメソッドの中身、C から呼ぶもの、`super`、VM が自分で扱う演算子 (`[]`・`+` など)。
- 出るが問題ない: `respond_to?` で備えた呼び出し、facade の `method_missing` で受ける名前。足りないと出ても、アプリが備えていれば動く。
- 出ない: アプリが自分で定義した名前 (自分のクラスのメソッド、組み込みのクラスに足したメソッド)。そのビルドに別のクラスの同じ名前のメソッドがあれば、そのクラスのものでなくても通る。

### 5.3 どうしても要るメソッドの申告 (`requires`)

それが無いと続けられないメソッドは、`.app.toml` に名前で書く。

```toml
movable = true
# このメソッドが無いビルドへは移さない
requires = ["pack", "linux_settings"]
```

- 着いた側は、イメージを読む前に名前を 1 つずつ自分のビルドで引き、どのクラスも持たない名前があれば**断る** (`the app requires the method 'linux_settings' (requires in its .app.toml), which this build does not have`)。移動元は止めた所から続ける。
- 申告が無ければ、名前を理由に断ることはない (5.2)。
- 書くのはメソッドの名前 (今のところ)。値は文字列の配列。イメージの横の `.toml` にも写るので、移った先からさらに移しても申告は続く。
- 読むのは横の `.app.toml` だけ (`.rb` の中のコメントの toml からは読まない)。

### 5.4 止める前の確認 (F5)

Asterism で移すときは、止める前に、移動元が同じ名前の一覧 (`mrb_checkpoint_sent_names`、イメージを書かずに取れる) と `requires` を相手に送り、相手が自分の表で引いて「全部ある」か「足りない: ...」と答える。

- `requires` の名前が相手に無ければ、**止める前に断る** (イメージも書かない)。アプリは何も気づかずに動き続け、頼んだ側に理由が返る (`on_checkpoint_refused`、Shell の `move` の答え、道具の `{status: stayed, here: "not stopped"}`)。
- ほかの足りない名前は警告として頼んだ側に返り (`missing`)、移す。
- 着いた側の確認 (5.2・5.3) は最後の守りとして残る。

## 6. 移す・書く頼み方

| 入口 | 使い方 |
|---|---|
| アプリ自身 | `checkpoint(restart: true)` (`on_event`・`on_update` の中から呼んでよい)。書けたら `on_checkpoint` の後に閉じ、`restart: true` ならすぐに起動し直す。断られたら `on_checkpoint_refused`。ほかのアプリは `FmrbApp.checkpoint(pid)` |
| Shell | `checkpoint [-r] <pid>`、`restore <image>` |
| 開発の道具 | `ruby tools/fmrb_native.rb app checkpoint PID [--restart] [--hold MS]`、`app restore PATH`、`app move PID --to <run dir か port>` (sim は port 5555)。MCP の `native_app` も同じ |

イメージは `/tmp/<名前>.ckpt`、横に起動の属性の `/tmp/<名前>.toml` (`movable`・Window の形・`requires`) が書かれる。

### 6.1 別の端末へ移す (Asterism、F5)

移す先は **Asterism の端末の名前** (ノード ID。`asterism_nodes` や `/home/asterism/config.toml` の `node` に出るもの) で指す。

| 入口 | 使い方 |
|---|---|
| アプリ自身 | `move_to("ノード ID")`。相手で起動できたら `on_checkpoint` の後に閉じ (相手では `on_restore`)、移らなかったら `on_checkpoint_refused` で理由を受けて続ける。ほかのアプリは `FmrbApp.move(pid, "ノード ID")` (答えは `on_control` に `checkpoint_result` で、`to`・`there` (相手での PID)・`missing` か `reason`) |
| Shell | `move <pid> <ノード ID>` |
| 開発の道具 | `ruby tools/fmrb_native.rb app move PID --node ノードID [--hold MS]`。MCP の `native_app` の `move` に `node` |

- 段取りは、相手に聞く (5.4) → 止めてイメージを書く (移動中の印) → 運ぶ → 相手で起動 → 起動できたと返ってきたら閉じる。断られた・返事が無い (既定の `hold` 10 秒) ときは止めた所から続ける。返事が遅れて相手で起動したときは、相手の側を止める (2 つにならない)。
- 相手の端末は、受け取ってよい相手を `/home/asterism/receive.toml` に書く (`from = ["ノード ID", ...]`。`"*"` は網に入れる全員)。この file が無い端末は何も受け取らない。この file がある端末は、起動のときに受け口 (転送のアプリ `/usr/share/transfer/transfer.app.rb`) を動かす。
- 移す側の端末は、Asterism につながる設定 (`/home/asterism/config.toml`) があればよい。転送のアプリは移す頼みのときに起動する。

## 7. 試験のアプリ (手本)

`flash/app/test/` の `ckpt_counter` (Window なし、Task 2 つ)、`ckpt_window` (Window あり、何かあったときだけ描き、`on_restore` で描き直す)、`ckpt_wide`・`ckpt_full` (画面の大きさが変わる)、`ckpt_calls` (共通でないメソッドを `respond_to?` で備えて呼ぶ)、`ckpt_requires` (それを `requires` に書く)。動きの確認は `rake regress:checkpoint` (doc/regression/usage.md 12 章)。
