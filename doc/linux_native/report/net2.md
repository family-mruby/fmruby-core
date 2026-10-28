# NET2 報告: WebSocket の受信の見直しを 5 → 10 → 30 → 100 ms に

> 状態: 完了 | 更新: 2026-10-28 | 指示書は instruction_net2.md。ブランチは feature/linux-native (未 push)

## したこと

`lib/patch/picoruby-net-websocket/mrblib/client.rb` の `receive` だけを直した
(rakelib/setup.rake は NET1 で client.rb を写す行が入っているので変えていない)。

- Linux 版では、何も読めなかったときの待ちを 1 回目 5 ms、2 回目 10 ms、
  以降 30 ms、その待ちが始まってから 1 秒たったら 100 ms にした。
- 待ちの始まりは `receive` の呼び出しごとと、待ちを終えないフレーム
  (ping・pong) を受けた後。そこで回数と始まりの時刻を戻すので、また 5 ms から。
- 実機は一定の 30 ms のまま。間隔は私用のメソッド `poll_interval_ms` に
  まとめ、先頭で `return 30 unless stepped` としている。
- 先頭の "Family mruby patch" のコメントに、この段階のことを英語で足した。

途中で親から変更があり (同じ日のユーザ決定)、最初の指示の「30 ms のまま」に
「1 秒で 100 ms」を足した形で入れた。

## Linux 版の見分け方

`FmrbConst::PLATFORM == "linux"` を `receive` の頭で読む (`::FmrbConst` と
トップから引く)。

- 既にある仕組みで、C の追加が要らない。値は picoruby-fmrb-const の
  `ports/esp32/const.c` が `CONFIG_IDF_TARGET_LINUX` の有無で決める
  (あれば "linux"、無ければ "esp32")。
- `CONFIG_IDF_TARGET_LINUX` は sim (IDF の linux ターゲット) とネイティブ版
  (native の CMake が `-DCONFIG_IDF_TARGET_LINUX=1` を渡す) にだけある。
  ESP32-S3・P4 の IDF ビルドには無いので、実機では PLATFORM は "esp32" で、
  `stepped` は false、`poll_interval_ms` は必ず 30 を返す。前と同じ 1 回 30 ms。
- websocket の gem を入れている 4 つの構成 (family_mruby_linux / _native /
  _esp32 / _esp32p4) はどれも gembox `family_mruby` を通して
  picoruby-fmrb-const を持つので、どの VM でも定数がある。
- ブラウザ版も PLATFORM は "linux" だが、この gem を持たないので関係しない。
- 定数は gem の読み込み時でなく `receive` の呼び出し時に読む。gem の
  初期化順で picoruby-fmrb-const が後になっても困らないようにするため。
- ビルド時の定義 (mrblib の Ruby には `#ifdef` が無い) や、実行時の問い合わせ
  (新しい C の関数) は使っていない。

## 確認

- `rake clean` → `rake regress`: host 19/19、描画 205/205。
- `rake regress:size` (NARYAv3・TAB5): 内蔵 RAM は DIRAM・IRAM・その他すべて +0。
  アプリのイメージは NET1 の後の値から NARYAv3 4,579,408 → 4,579,792 (+384 B)、
  TAB5 6,761,680 → 6,762,032 (+352 B)。fmruby-core では NET1 から今までの間に
  文書だけのコミットしかないので、この差は今回の分。
- `rake setup` 後、submodule 内の client.rb が patch と同じこと、ネイティブ版の
  libmruby に `poll_interval_ms` が入っていることを確認した。
- ネイティブ版 (`rake native:core`、標準と `NATIVE_ENGINE=mruby`):
  - `test/native/net/l2n_check.rb --only http`: 両構成とも 6/6。
    WS echo は 115 ms (標準)・118 ms (全 mruby)。NET1 は 133 ms。
    残りの大半は close の待ち (100 ms 刻み、変えていない)。
  - CDP の通し: Playwright の chrome-headless-shell (1243) を専用の
    `--user-data-dir` (scratchpad) と空いたポート 9352 で起動し、検査アプリを
    `fmrb_native.rb up --user-dir` の `/home` に置いて起動した。終わった後、
    自分で起動した chrome と native だけ止めた。検査アプリは受信の待ちには
    手を入れず、`Net::WebSocket::Client#sleep_ms` を包んで、呼ばれた時刻と
    引数を記録するだけ (super で元の sleep_ms を呼ぶ)。
  - sim (Modern = TAB5 の .env、`rake build:linux`、ELF が x86-64 であることを
    確認): `flash/app/test/net_test.app.rb` の WS ECHO が ok 118 ms。
    送ってから受け取るまで 5 ms (NET1 は 30 ms)。相手は PC の 0.0.0.0:8765 に
    立てた Ruby の echo。build/ は `tools/regress/build_tree.rb` の stash! で
    退避してから作り、`ruby tools/regress/size.rb --restore` で戻した
    (戻した後の build/ は元の NARYAv4 (esp32p4))。

### 1 往復の時間 (CDP、Runtime.evaluate "1+2" を 20 回)

| | 20 回の平均 | 1 回ごと |
|---|---|---|
| NET1 (標準) | 29 ms | 31・31・29・31・39 ms (先頭 5 回) |
| NET1 (全 mruby) | 30 ms | 30・31・31・30・38 ms (先頭 5 回) |
| NET2 (標準) | 5 ms | 7,10,6,5,4,6,5,5,4,6,5,5,5,5,5,5,5,5,5,5 |
| NET2 (全 mruby) | 6 ms | 11,5,16,10,10,6,4,5,5,5,5,5,6,4,5,5,5,6,4,5 |

ほとんどの往復は最初の 5 ms の見直しで返事を拾っている。10 ms・16 ms の回は
2 回目・3 回目の見直しまで着いていなかった分。`Page.captureScreenshot`
(base64 3.6 KB) は 20 ms (標準)・51 ms (全 mruby)。

### 何も届かない待ち (receive(timeout: 3))

両構成とも同じ形になった。

| | 標準 | 全 mruby |
|---|---|---|
| 戻り値・かかった時間 | nil・3019 ms | nil・3018 ms |
| 見直しの回数 | 55 | 55 |
| 間隔 (sleep_ms の引数) | 5, 10, 30 × 33, 100 × 20 | 同じ |
| 最初の 100 ms | 待ちの始めから 1013 ms (35 回目) | 1013 ms (35 回目) |
| 実際の間隔 (記録の差) | 30 ms の所は 26-36 ms、100 ms の所は 96-104 ms | 30 ms は 25-35 ms、100 ms は 96-103 ms |

最初の 2 回 (5・10 ms) の後は、最初の 1 秒が 30 ms 刻み、それ以降は 100 ms
刻み。3 秒の待ちで起きる回数は、一定の 30 ms なら約 100 回のところ 55 回。

## 見立てと違った点

- なし。指示の途中で 100 ms への段が足された (上記)。

## 撤回した仮説

- なし。

## 残件

- 実機 (S3・Tab5) は一定の 30 ms のまま。短い見直しを入れるかは、実機で
  ほかのタスクへの負担を測ってから。入れるときは `poll_interval_ms` の
  `return 30 unless stepped` の条件を変えるだけで済む。
- `close` の待ち (100 ms 刻みで最大 1 s) は変えていない。WS の通し 1 回
  (115-118 ms) の大半はここ。
- client.rb が上流の写しであることは NET1 の残件のまま。
