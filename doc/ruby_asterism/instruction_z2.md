# Asterism Z2 指示書: P4 実機 (Tab5) で Zenoh の往復

> 状態: 指示 | 更新: 2026-10-06 | Z1 の picoruby-zenoh gem を ESP32-P4 のファームに載せ、Tab5 のアプリと PC の zenohd が WiFi 越しに put / subscribe で往復する

前提として plan.md (同じディレクトリ) と report/z1.md を読むこと。Z1 の作り (ポーリング、gem の独立、
受信のリング) はそのまま使う。

## 1. 作るもの

### 1.1 ESP32-P4 のビルドに gem を入れる

- `lib/add/family_mruby_esp32p4.rb` に picoruby-zenoh を足す。**S3 (family_mruby_esp32.rb) と wasm には
  入れない** (S3 は Z4)。
- zenoh-pico の ESP-IDF のポート (`src/system/espidf/`) を使う。ESP32 / FreeRTOS のヘッダを使う C は
  `components/picoruby-esp32/CMakeLists.txt` の `PICORUBY_SRCS` で管理する決まり (fmruby-core/CLAUDE.md)。
  P4 の分岐だけに足す。
- gem は Family mruby に依存しないまま (fmrb_* を呼ばない)。機体ごとの差は gem の中の platform の選択で
  吸収する (Z1 の mrbgem.rake の `ZENOH_PICO_PLATFORM` の考え方を広げる)。

### 1.2 ESP-IDF の TCP 層と確保先

- Z1 で POSIX の TCP 層を差し替えた理由 (データの無い poll が止まる、接続の時間制限が無い) が ESP-IDF の
  ポートにもあるかを読んで確かめ、あれば gem 側の実装で差し替える (vendor/ は書き換えない)。
- **内蔵 RAM を増やさない** (プロジェクトの方針。速度より RAM を優先する)。zenoh-pico の `z_malloc` は
  ESP-IDF のポートでは `heap_caps_malloc(size, MALLOC_CAP_8BIT)` で、内蔵 RAM から取られうる。
  gem 側の platform の実装で PSRAM (`MALLOC_CAP_SPIRAM`) から取る形にする。静的な領域 (.bss / .data) も
  増やさない。
- zenoh-pico がタスクを立てないこと (シングルスレッドの構成) を確かめる。

### 1.3 PC 側を LAN に開く

- 親リポジトリの `docker-compose.yml` の zenohd は 127.0.0.1 だけに出している (ユーザ決定)。これは
  変えない。**LAN に開くための重ね合わせのファイル** (`docker-compose.zenoh-lan.yml` など、ポートを
  0.0.0.0 に出す) を足し、使い方を `tools/fmrb_zenoh.rb --help` か文書に書く。
- この PC の WSL2 はミラーモード (LAN 側の IP は 192.0.2.2)。Windows のファイアウォールで届かない
  場合は止まる条件 (4 章)。

### 1.4 試しのアプリ

- `flash/app/test/zenoh_echo.app.rb` が接続先を変えられるようにする。接続先の文字列 (`tcp/<IP>:7447`) を
  `/home/zenoh_echo.txt` に 1 行書いておけばそれを使い、無ければ今の既定 (`tcp/zenohd:7447`) を使う。
  画面に接続先を出す (今と同じ)。

### 1.5 Z1 の残り

- `Subscriber#each_pending` で文字列の確保が例外になると、取り出した 1 件のバッファが解放されずに残る。
  例外が出ても解放されるように直す。

## 2. 受け入れ条件

1. `rake build:esp32` (.env の TAB5) が通る。flash の区画 (factory 7M) に収まり、残りを report に書く。
2. Tab5 に焼いて (MCP の flash)、起動のシリアルログに crash の印 (`Guru|abort`) が無い。
3. **内蔵 RAM**: gem を入れる前 (develop) と後で、起動直後の `M1|` 行と周期ダンプの `IRAM free:` を比べ、
   内蔵 RAM の減りが 0 であること。アプリを接続して動かしている間の内蔵 RAM の減りも測り、0 か、0 で
   ないなら何がどれだけ取っているかを書く。比べるのは同じ手順で取ったログ同士。
4. Tab5 で試しのアプリ (`tab5_fs` で送り、`tab5_app` で起動) が PC の zenohd につながり、
   `ruby tools/fmrb_zenoh.rb get fmrb/test/out` で連番が増えるのが読める。
5. `ruby tools/fmrb_zenoh.rb put fmrb/test/in hello` が Tab5 の画面に出る (`tab5_screenshot`)。
6. アプリを閉じて開き直す、を 5 回くり返しても 4-5 が通る。前後のアプリの VM プールと内蔵 RAM の値を書く。
7. zenohd を止めた状態で試しのアプリを起動しても落ちない (画面に接続の失敗が出る)。そのとき、ほかの
   アプリやデスクトップの操作が止まる時間 (接続の待ち) を測って書く。
8. Linux の sim で Z1 の受け入れ条件 2-3 が今も通る (標準構成)。標準構成でエディタを 1 回起動して 1 打鍵。
9. S3 と wasm のビルド構成のファイルに変更が無いことを差分で示す。`rake test` が通る。

## 3. 触ってよい範囲

- fmruby-core: `lib/add/picoruby-zenoh/`、`lib/add/family_mruby_esp32p4.rb`、
  `components/picoruby-esp32/CMakeLists.txt` (P4 の分岐に zenoh を足すことだけ)、Rakefile / `rakelib/`
  (zenoh の部分)、`flash/app/test/zenoh_echo.*`、`doc/ruby_asterism/report/z2.md`。
- 親リポジトリ: zenohd を LAN に開くための重ね合わせの compose ファイル、`tools/fmrb_zenoh.rb`。
- 触らないもの: `.env` (ユーザの手元の変更。書き換え・コミット禁止。TAB5 になっているのでそのまま使う)、
  sdkconfig / sdkconfig.defaults*、パーティションの表、submodule の中、fmruby-graphics-audio、
  S3 / wasm の構成、`docker-compose.yml` の zenohd のポートの設定。

## 4. 止まる条件 (report に状況を書いて返す)

- Windows のファイアウォールなど、PC の設定の変更 (管理者の権限) が要るとき。何を変えればよいかを書く。
- 内蔵 RAM が増える形しか作れないとき。何がどれだけ増えるかと案を書く。
- flash の区画に収まらないとき。
- sdkconfig の変更が要るとき (変えずに、案を書く)。
- 実機のボタン操作が要るとき、Tab5 が遠隔の操作に答えなくなって戻せないとき。
- ルータが消えたことに気づくまでの時間 (Z1 で約 20 秒) と再接続: **動きを変えない**。実機での実測値
  (WiFi が切れたとき、zenohd が落ちたとき) を report に書くだけにする。変える案は書いてよい
  (選ぶのはユーザ)。
- 範囲の外の変更が要るとき、利用者から見た動きの選択が要るとき。

## 5. report (`report/z2.md`) に書くこと

- ESP-IDF のポートで差し替えたもの (TCP 層・確保先など) と理由。
- flash の増分 (`idf.py size-components` で gem と zenoh-pico の分)、区画の残り。
- 内蔵 RAM / PSRAM の比較 (ログの行をそのまま)。
- 受け入れ条件 1-9 の結果と証拠。
- 切断の実測値 (4 章)。
- 見立てと違った点、撤回した仮説、踏んだ罠、Z3 (get / queryable / liveliness、機体どうし) と
  Z4 (S3) への申し送り。

## 6. 作業の決まり

- ブランチ `feature/asterism-z2` を fmruby-core と親リポジトリに切ってコミットする (英文、`<領域>: <要約>`、
  Co-Authored-By を付ける)。develop へのマージと push はしない。
- **実機の作業の前に `tab5_audio` でミュートにする**。Tab5 で動いているアプリは終了させてよい。
- シリアルは `serial_start` で 1 回開いて開きっぱなしにする (Tab5 は開くだけでリブートする)。焼くのは
  MCP の `flash`。ファームの焼き直しは何度でもよいが、ファイルの書き込みの繰り返しの試験はしない
  (flash の消耗)。
- ESP32 と Linux を行き来するときは `rake clean_all` (してよい)。終わったら Linux の標準構成に戻して
  ビルドしておく (`file build/fmruby-core.elf` が x86-64)。sim は sim_down して終える。
