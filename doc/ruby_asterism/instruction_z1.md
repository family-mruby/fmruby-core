# Asterism Z1 指示書: sim で Zenoh の最小の疎通

> 状態: 指示 | 更新: 2026-10-06 | plan.md の Z1 を実装する。sim の mruby アプリ ⇔ 親リポジトリの docker で動く zenohd の put / subscribe の往復

前提として plan.md (同じディレクトリ) の「決まっていること」「Z1」「決定事項」を読むこと。
zenoh-pico の調査結果は zenoh_idea.md の 5 章 (ポーリング) と 8 章 (版・機能の現状)。

## 1. 作るもの

### 1.1 zenoh-pico の取り込み

- `lib/add/ZENOH_PICO_PIN` を作る。書式は `lib/add/PICORUBY_TI_PIN` と同じ (`repo:` / `branch:` または tag /
  `commit:`、先頭に # の説明)。版は **1.10.0 以上の最新のリリースのタグ**の commit に固定する。
- Rakefile の既存の `read_pin_file` を使い、`rake zenoh:setup` で `vendor/zenoh-pico` (gitignore) に取得する
  (`rakelib/ti.rake` の流儀にそろえる)。`rake build:linux` の前に未取得なら取得されるようにする
  (ti / spinel と同じ扱い)。
- zenoh-pico のビルド構成: **Z_FEATURE_MULTI_THREAD=0** (読み取りのタスクを立てない。受信は zp_read /
  zp_spin_once 相当をアプリの側から回す)。不要な機能 (serial、TLS、UDP multicast scouting など) は
  Z1 に要らなければ切る。設定はビルドの定義で与え、vendor/ の中を書き換えない。

### 1.2 picoruby-zenoh gem (`lib/add/picoruby-zenoh/`)

- **Family mruby に依存しない** (fmrb_* のヘッダ・API を include / 呼び出ししない。素の PicoRuby でも
  ビルドできる形)。確保は mruby の allocator (mrb_malloc) か zenoh-pico 自身のもので、fmrb_mem は使わない
  (gem の独立性を優先。gem の README に理由を一行書く)。
- Ruby の名前は `Zenoh` (決定事項 2)。最小の API の目安 (細部の名前はサブが決めてよい。ただし report に
  一覧を書く):
  ```ruby
  s = Zenoh::Session.open("tcp/zenohd:7447")   # mode は client
  s.put("fmrb/test/out", "hello")              # payload は String
  sub = s.subscribe("fmrb/test/in")            # 購読
  s.poll                                       # 受信の処理を進める (アプリの _spin から呼ぶ)
  sub.each_pending { |key, payload| ... }      # 溜まった値を取り出す (無ければ何もしない)
  sub.close; s.close
  ```
- 受信のコールバック (zenoh-pico の中から呼ばれる C の関数) では **mruby の VM を触らない**。C のリング
  (上限付き) に key と payload を写すだけにし、Ruby への受け渡しは each_pending の中で行う。リングが
  溢れたら古いものを捨て、捨てた数を数えて取れるようにする。
- 後始末: `close` を呼ばずにアプリが終わっても漏れないこと (mruby のオブジェクトの解放でセッションと
  購読を閉じる)。Z1 のゴール 3 (開き直し) はこれの確認。
- **Linux だけでリンクする**: `lib/add/family_mruby_linux.rb` に足す。ESP32 / wasm のビルド構成
  (`family_mruby_esp32*.rb` / `family_mruby_wasm.rb`、`components/picoruby-esp32/CMakeLists.txt`) には
  入れない。

### 1.3 zenohd (親リポジトリの docker compose)

- 親リポジトリ (family-mruby) の `docker-compose.yml` に zenohd のサービスを足し、sim と一緒に上がるように
  する。公式のイメージ (`eclipse/zenoh`) を **zenoh-pico と同じ版のタグ**に固定する。REST プラグインを
  有効にし (PC から HTTP で確かめるため)、7447 (zenoh) と 8000 (REST) をホストに出す。
- core のコンテナから名前 (`zenohd` など) で届くこと。既存のサービスのネットワークの設定を壊さない
  (headless / wsl / vnc などの重ね合わせの compose ファイルでも sim が上がること)。
- MCP の sim_up / sim_down (`tools/mcp/`) が 3 コンテナを前提にしている箇所があれば、zenohd が増えても
  動くことを確かめる。直す必要があれば最小限に直し、report に書く。

### 1.4 PC 側の確かめ方 (Ruby)

- `tools/fmrb_zenoh.rb` (Ruby、標準ライブラリだけ): REST プラグイン経由で
  `get <key>` (最新の値を読む。REST の GET) と `put <key> <value>` (REST の PUT) と `watch <key>`
  (一定間隔で GET して変化を出す、で可) ができる。`--help` を付ける。

### 1.5 試しのアプリ (`flash/app/test/zenoh_echo.app.rb` など)

- ランチャーには出さない (`flash/app/test/` の既存のアプリの置き方にそろえる)。
- 起動で接続し、`fmrb/test/out` に 1 秒ごとに連番を put する。`fmrb/test/in` を購読し、受け取った最新の
  値と受信数・捨てた数を画面に出す。接続に失敗したら画面にそう出して、落ちない。

## 2. 受け入れ条件

1. `rake build:linux` が**標準構成 (Spinel カーネル + Spinel エディタ) と互換構成 (全 mruby)** の両方で
   通る (`file build/fmruby-core.elf` が x86-64)。
2. sim で試しのアプリを起動し、`ruby tools/fmrb_zenoh.rb get fmrb/test/out` で連番が増えていくのが読める。
3. `ruby tools/fmrb_zenoh.rb put fmrb/test/in hello` を打つと、試しのアプリの画面に `hello` が出る
   (sim_screenshot で確認)。
4. 試しのアプリを閉じて開き直す、を 5 回くり返しても 2-3 が通り、core のログに異常 (クラッシュ、
   確保の失敗、VM プールの減り続け) が無い。開く前と 5 回後の VM プールの値を report に書く。
5. 1-4 を標準構成と互換構成の両方で。
6. 標準構成で、エディタを 1 回起動して 1 打鍵するところまで動く (fmruby-core/CLAUDE.md のテストの節)。
7. `rake test` が通る。
8. ESP32 のビルドに影響しないこと: ESP32 向けの構成ファイルに変更が無いことを差分で示す (ESP32 の
   ビルドは回さなくてよい)。
9. zenohd を止めた状態で試しのアプリを起動しても core が落ちない (画面に接続の失敗が出る)。

## 3. 触ってよい範囲

- fmruby-core: `lib/add/ZENOH_PICO_PIN`、`lib/add/picoruby-zenoh/`、`lib/add/family_mruby_linux.rb`、
  Rakefile / `rakelib/` (zenoh の取得とビルドの追加だけ)、`.gitignore` (vendor/zenoh-pico)、
  `flash/app/test/` の試しのアプリ、`doc/ruby_asterism/report/z1.md`。
- 親リポジトリ: `docker-compose*.yml` (zenohd の追加)、`tools/fmrb_zenoh.rb`、必要なら `tools/mcp/` の最小の修正。
- 触らないもの: `.env` (ユーザの手元の変更が入っている。読むのは可、書き換え・コミット禁止)、
  sdkconfig / sdkconfig.defaults*、submodule の中、fmruby-graphics-audio、ESP32 / wasm の構成。

## 4. 止まる条件 (report に状況を書いて返す)

- zenoh-pico をアプリの側からのポーリングだけで回せない (読み取りのタスクが要る) と分かったとき。
  案を書いて返す。
- gem を Family mruby から独立させたままでは作れない事情が出たとき。
- 上の範囲の外の変更が要るとき (graphics-audio、カーネル、sdkconfig、ESP32 の構成など)。
- API の形 (上の目安) から大きく外れる必要が出たとき、利用者から見た動きの選択が要るとき。
- sudo やユーザの作業が要るとき。
- docker のネットワークの都合で zenohd に届かず、既存の compose の作りを変えないと解決できないとき。

## 5. report (`report/z1.md`) に書くこと

- 採用した zenoh-pico と zenohd の版 (タグと commit)、zenoh-pico のビルドの定義。
- API の一覧 (メソッド、引数、戻り値、例外)。
- 受け入れ条件 1-9 の結果 (コマンドと出力、画面の確認)。VM プールの値。
- gem が増やした大きさ (Linux の elf の text / bss の増分の目安。ESP32 の見積もりは Z2)。
- 見立てと違った点、撤回した仮説、踏んだ罠、残件 (Z2 で ESP32 に載せるときに気をつけること)。

## 6. 作業の決まり

- 作業は本体の checkout で直接 (worktree は使わない)。ブランチ `feature/asterism-z1` を fmruby-core と
  親リポジトリの両方に切ってコミットする (英文、`<領域>: <要約>`、Co-Authored-By を付ける)。
  develop へのマージと push はしない。
- lib/ を編集したら `rake clean`、構成を切り替えるときは必要に応じて `rake clean_all` (してよい)。
- sim の検証は MCP の sim_* ツールで。終わったら sim_down。
