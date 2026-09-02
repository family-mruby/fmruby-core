# 実装指示 P2: ブラウザ版に取得手段を足す (`FmrbNet.request`)

計画は plan.md、形式の正は spec.md。P1 の記録は report/p1a.md と p1b.md、
**最初に作って捨てたページ側の店**の記録は report/p2_discarded.md。

## なぜ作り直すか

**店は OS の中のアプリにする。** ページ (`wasm/web/`) は機械の筐体であって
OS の一部ではない。そこに店を置くと、ブラウザにしか無い機能になり、実機用の
店と同じものを 2 つ実装することになる。配布の看板が「同じ Ruby が 3 環境で
動く」である以上、店自身がそうでないのは筋が通らない。

そのために足りないものが 1 つだけある: **ブラウザ版に通信手段が無い**
(`family_mruby_wasm.rb:71`。ソケットが無いので picoruby-socket /
net-http が入らない)。これを足すのが P2。

## 作るもの: `FmrbNet.request`

`FmrbNet` は既にある (`ports/esp32/net.c`。`connected?` / `ip_address` /
`hostname` / `ssid` / `wait_for_ip`)。**3 環境すべてでコンパイルされている**
(esp32 の CMake も wasm の CMake も `ports/esp32/*.c` を集める)。ここに
取得を足す。

### 形は問い合わせ型 (コールバックにしない)

```ruby
def on_create
  @req = FmrbNet.request("https://.../registry.json")
end

def on_update
  if @req && @req.done?
    @req.ok? ? load_list(@req.body) : show_error(@req.error)
    @req = nil
  end
  50
end
```

**コールバックにしてはいけない。** 保存して後から呼ぶブロックは、Spinel が
外側のローカル変数の捕捉で黙って壊す形そのものである (`set_timer` と同じ
地雷)。アプリは既に `on_update` を持っているので、そこで見る形が素直。

### API

| | |
|---|---|
| `FmrbNet.request(url)` | `FmrbNet::Request` を返す。**待たない** |
| `Request#done?` | 済んだか (成否は問わない) |
| `Request#ok?` | 済んで status が 2xx |
| `Request#status` | 整数。まだなら nil |
| `Request#body` | 文字列。まだなら nil |
| `Request#error` | 失敗の理由 (文字列)。無ければ nil |
| `Request#cancel` | 捨てる (結果を待たない) |

**`get` という名前にしない。** `Net::HTTP.get_response` は応答を返すが、
これは要求を返す。呼んだ行を見ただけで区別がつくようにする。

### `Net::HTTP` は残す・触らない

| | `Net::HTTP` | `FmrbNet.request` |
|---|---|---|
| 動く場所 | Retro / Modern / sim | **3 環境すべて** |
| 形 | 止まって待つ | 止まらない |
| 使うとき | 実機専用の道具 | **配布するアプリ** |

## 実装

### Ruby 側 (`mrblib/`)

`Request` は Ruby で書き、環境で分岐する (`::FmrbConst::BOARD == "wasm"`)。

- **wasm**: C の橋 (下記) を呼ぶ。本当に非同期。
- **それ以外**: `initialize` の中で `::Net::HTTP.get_response` を呼んで
  結果を持つ。`done?` は最初から true。

実機で `initialize` が待つのは正直に書く。**止まるのはそのアプリのタスク
だけ**で (プリエンプティブなので画面は動き続ける)、今の weather と同じ挙動。
アプリのコードは変わらないので、後で補助タスクへ移しても呼ぶ側は無傷。

picoruby の地雷に注意: **クラスの中の裸の定数は解決されない**ので
`::Net::HTTP` `::URI` `::FmrbConst` と書く。`Regexp` は無い。

### C 側 (`ports/esp32/net.c` に `#if defined(__EMSCRIPTEN__)` で足す)

**取得は main スレッドで走らせる。** 機械の各タスクは pthread (= Worker)
で、`emscripten_thread_sleep` で止まっている間その Worker の JS は 1 行も
動かない。Worker で `fetch` を投げても promise が解決しない。

ページ側 (main スレッド) の event loop は回っているので、そこへ回す。
ファイル系のシステムコールが既に同じ形 (`proxyToMainThread`) なので、
作りとして一貫している。

```
FmrbNet._fetch_start(url)  -> MAIN_THREAD_EM_ASM_INT  -> id
FmrbNet._fetch_poll(id)    -> 0 pending / 1 done / 2 error
FmrbNet._fetch_status(id)  -> int
FmrbNet._fetch_body(id)    -> String
FmrbNet._fetch_error(id)   -> String
FmrbNet._fetch_free(id)
```

JS 側の表は `globalThis` に置く (`Module` は Worker ごとに別)。

## 受け入れ条件

- [ ] Linux sim で、`FmrbNet.request` を使う小さなアプリが応答を取れる。
- [ ] **ブラウザで同じアプリが同じコードのまま動く。**
- [ ] 取得中に**画面が止まらない** (ブラウザ。時計が進み続けることで見る)。
- [ ] 届かない URL で `error` が入り、`done?` が true になる (固まらない)。
- [ ] `Net::HTTP` を使う既存アプリ (weather) が壊れていない。
- [ ] `family-mruby-apps` の `validate.rb` が、`app_env` に `web` を含む
      アプリの `Net::HTTP` 使用を落とす。

## 気をつけること

- **`lib/` を編集したら `rake clean`**。しないと古いものがリンクされる。
- `rake wasm:web` の前に `source ~/emsdk/emsdk_env.sh`。
- wasm の生成物は作り直されないことがある (`wasm:mrb` / `wasm:mruby`)。
  直したのに動かないときの最初の疑い。

## report に残すこと

`report/p2.md`。特に、main スレッドへ回す必要が本当にあったか (Worker で
直接 `fetch` して駄目だったなら、その観測)、そして実機側の「中で待つ」が
実用上どう見えたか。
