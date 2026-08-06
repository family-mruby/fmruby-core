# 高速な PicoRuby アプリを書くための知見

fmruby 上で動く mruby VM アプリ (ユーザアプリ・desktop) を速く・止まらずに
動かすための定石集。**実測に基づくものは数値と出典を付けてある**。GC の
観測・調整そのものは [gc_monitoring.md](gc_monitoring.md)、Spinel でコンパイル
されるカーネル側は [spinel_aot/spinel_gc_notes.md](spinel_aot/spinel_gc_notes.md)
と [spinel_aot/ruby_writing_constraints.md](spinel_aot/ruby_writing_constraints.md)
が正 (Spinel はブロックが for にインライン化されるなど、事情がかなり違う)。

## 0. 大原則

1. **速くしたいなら、まず割り当てを減らす**。この環境の「遅い」の大半は
   処理そのものでなく GC である (P6 実測: 処理時間の 7/8 が GC。
   gc_monitoring.md 1.4)。割り当ての単価表は gc_monitoring.md 7 章。
2. **sim の時間は実機に外挿できない** (GC 支配的な処理で実機 = sim の
   ~330 倍。gc_monitoring.md 5.3)。sim で意味があるのはオブジェクト数と
   GC 回数。時間は実機で測る。
3. **推測せず数える**。`Machine.uptime_us` で区間を測り、
   `measure_alloc` (gc_monitoring.md 1.3) で 1 呼び出しあたりの
   オブジェクト数を数え、`GC.disable` A/B (同 1.4) で GC のせいかを切る。

## 1. 制御構造: ブロックを回すな、while で回せ

**ホットループは `while` + インデックスで書く。** `loop do` /
`Array#each` / `Integer#times` は毎周ブロック呼び出しが入る:

- `Kernel#loop` は mrblib の **Ruby 実装**である
  (`picoruby-mruby/lib/mruby/mrblib/kernel.rb:27`): `loop` メソッドの
  呼び出し + 毎周の `yield` + StopIteration 用の **rescue フレーム**が
  素の `while true` に上乗せされる。
- `each` / `times` も毎周 OP_SENDB でブロックを起動する。`while` は
  ジャンプ命令だけで回る。

```ruby
# 遅い                          # 速い
arr.each do |e|                 i = 0
  work(e)                       n = arr.size
end                             while i < n
                                  work(arr[i])
                                  i += 1
                                end
```

- **`arr.size` はループ外に持ち上げる** (毎周のメソッド呼び出しを消す)。
- 実例: launcher / taskbar / desktop の熱経路は全部この形で書いてある
  (`main/prebuild_scripts/kernel/system_desktop/launcher.rb` ほか)。
- 例外: 1 度しか回らない・数要素だけ・コールドパス、なら `each` で良い。
  読みやすさを捨てるのは熱いところだけ。

## 2. リテラルと定数

**メソッドに渡す・比較に使う文字列リテラルは定数に持ち上げる。**
`key == "literal"` は `key == CONSTANT` の **~40 倍**のコストだった
(実測、再現性あり。`launcher.rb:151-161` のコメントと
[boot_performance.md](boot_performance.md))。リテラルは評価のたびに
RString スロットを作るが、定数参照は作らない。

```ruby
S_SLASH = "/"
...
path.split(S_SLASH)   # 毎回 "/" と書かない
```

起動スキャンはこれ (+ gsub 除去 + 索引キャッシュ) で **12.9 秒 → 0.29 秒**
になった (boot_performance.md)。

## 3. 文字列処理

- **`gsub` をホットパスで使わない**。picoruby では `gsub` は Ruby 実装で
  極端に重い (スキャンの行ループから外しただけで 13.0 → 7.8 秒。
  `launcher.rb:378` コメント)。引用符剥がし程度なら index + スライスで書く。
- **`+=` でなく `<<`**。`s += x` は毎回新しい String を作る。`<<` は
  その場に追記する。
- **バイト処理は `getbyte` / `setbyte`**。`s[i]` は 1 文字 String を
  割り当てる。入力ルータや i18n の幅計算が getbyte 走査で書かれているのは
  このため (`fmrb-i18n.rb:42` の text_width が手本)。
- **作業バッファは ivar に持って使い回す** (`@buf = "\x00" * 6` を
  initialize で 1 回、あとは setbyte)。同期的に送信/消費されるなら安全。
- 補間 `"#{x}"` はセグメントごとに割り当てる。**毎フレーム/毎イベントの
  経路に補間を置かない** (ログも含む! Log.debug は C 側でレベル無効でも
  **引数の補間は毎回実行される**)。
- 12 バイト以上の String は実体バッファも heap に載る (embed 上限 11B)。

## 4. データ構造

- **Hash のキーは Symbol** (代入 0 割り当て)。String キーは複製+凍結で
  **2 個**割り当てる (gc_monitoring.md 7 章の表)。
- 毎イベントの `{ ... }` リテラルは 1 個 + 実体配列 (~128B) を作る。
  受信側がイベントをまたいで保持しないなら **1 個を使い回す**
  (マウス移動の `$fmrb_move_ev`、カーネル `_poll_message` が実例)。
- `[a, b]` を返すのも 1 個。ivar に片方を置く・整数にパックする等で回避
  できる (gc_monitoring.md 7 章)。
- 周期ポーリングで Hash を返す API を毎秒呼ばない。値 1 つで足りるなら
  bool/fixnum 返しの API を足す (`FmrbApp.wifi_connected?` /
  `pool_usage` / `ps_gen` 方式)。

## 5. 描画・UI

- **変わったものだけ描く**。キャンバスは present 後も内容が保持される
  ので、毎フレーム全再描画は割り当てと GFX 帯域の両方を無駄にする。
  desktop は 1 秒ティックで時計セルだけ再描画に変えて、ランチャー表示中の
  GFX コマンドが数十/s → 6/s になった (コミット 4d434ce)。
- レイアウト計算 (文字列の折返し・センタリング等) は**描画のたびに
  やらずキャッシュする** (launcher の `:label_layout` が実例)。
- 毎秒の定型描画 (時計など) は文字列を組む時点で負けなので、**C メソッドに
  降ろす**手がある (`Graphics#draw_wallclock` / `draw_free_iram`。これで
  desktop の定常割り当ては厳密にゼロになった)。

## 6. GC との付き合い方 (アプリ側の要点だけ)

詳細は gc_monitoring.md。アプリ作者が知るべき最小セット:

- リズムを持つアプリ (演奏・アニメ) は `self.idle_gc = true`。収集が
  アイドル時間に逃げる。
- バースト割り当てをするアプリは `FmrbApp.pool_usage` の
  **ウォーターマーク** (70% で `GC.start`) を frame 境界に置く
  (gc_monitoring.md 3.3)。プール天井のストームより、選んだ瞬間の
  1 回が必ず安い。
- ソースのコンパイルだけで 500KB プールの 6 割が埋まる (gc_monitoring.md
  5.1)。大きいデータを扱うなら .toml に `large_memory = 1`。
- `NoMemoryError` は `rescue => e` を**素通りする** (StandardError では
  ない)。落ちる可能性のあるアプリは `rescue Exception => e`。

## 7. ファイル I/O

- ディレクトリ走査・toml パースは秒単位で高くつく。**結果をファイルに
  キャッシュして再走査を明示操作に限定する** (launcher_index 方式。
  boot_performance.md)。
- 拡張子の存在確認のような情報は、**列挙が既に持っている** —
  ファイルを 1 つずつ open して試さない (`launcher.rb:304-312`
  find_script_ext の教訓)。
- `File#read` は読み込みバッファと String の 2 つ分が同時に生きる
  (gc_monitoring.md 5.1)。

## 8. 測ってから信じる

このドキュメントの「速い/遅い」も環境が変われば揺れる。1 回の呼び出しが
高いか安いかを疑ったら:

```ruby
t0 = Machine.uptime_us
i = 0
while i < 1000
  target_work
  i += 1
end
Log.info("#{(Machine.uptime_us - t0) / 1000}us/call")
```

と `measure_alloc` (gc_monitoring.md 1.3) を併用する。**短いマイクロ
ベンチは GC が 1 回挟まるだけで 100 倍揺れる**ので、集計値だけを信じる
(boot_performance.md の教訓)。

## 9. 参考

- 割り当て単価の実測表: [gc_monitoring.md](gc_monitoring.md) 7 章
- 起動 12.9s → 0.29s の全過程: [boot_performance.md](boot_performance.md)
- 演奏中の GC 停止を消した過程: midi/report/p6.md, p7.md
- Spinel (カーネル) は別世界: ブロックはインライン化され proc を作らない
  一方、Hash/Array の単価が大きい。
  [spinel_aot/spinel_gc_notes.md](spinel_aot/spinel_gc_notes.md) 2.1 章
