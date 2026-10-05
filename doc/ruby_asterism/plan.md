# Asterism: 計画

> 状態: 進行中 | 更新: 2026-10-06 | 異なる Ruby・機体・Web を一つのオブジェクトの網として扱う構想の実装計画。Z1 (sim の mruby アプリ ⇔ zenohd の put / subscribe) 完了、次は Z2 (P4 実機)

## 目的

CRuby、Family mruby (mruby / Spinel)、マイコン、Web、外部のサービスを、Ruby から一つの網 (分散した
オブジェクトの空間) として扱えるようにする。下の通信には Zenoh を使う。構想と使い方は同じディレクトリの
ruby_unified_discussion_summary.txt、usecases.md、pet_design.md、node_variants.md、remote_window.md、
名前は naming.md、Zenoh の調査は zenoh_idea.md。

## 決まっていること (これまでの議論)

- 名前は Asterism (2026-08-27)。下の通信層の名前として Silk を予約。gem 名 `asterism` は rubygems.org で
  取得済み (2026-10-06、0.0.0 は名前の確保だけ)。
- **使う人が触る層は mruby のアプリの VM** で動かし、ブロックやメタプログラミングを自由に使う
  (method_missing ほか 13 項目が sim で使えることを確認済み: report/metaprog_check.md)。**下回り
  (zenoh-pico、データの符号化、受信の取り出し) は C の gem** で速くする。Spinel にはしない。
- 受信は**ポーリング** (アプリの `_spin` から取り出す)。コールバックのスレッドを立てない (mruby の VM を
  スレッドをまたいで触らないため)。遅延の下限は `_spin` の周期 (約 50 ms) で決まり、用途上は十分。
- 高頻度のメッセージ (毎フレームの制御など) は対象外。
- zenoh-pico は **1.10.0 以上** (ESP-IDF のシングルコアでのビジーループの修正が入っている版)。1.x 系の中は
  ワイヤの後方互換が保証されるが、組み合わせの表は無いので、使う版を固定して記録する。
- **gem は Family mruby に依存しない作り**にする (素の PicoRuby / R2P2 でもビルドできる)。機体の差は
  zenoh-pico のポート層に任せる。
- セキュリティ: v1 は「信頼できる LAN の中」が前提 (ESP32 の zenoh-pico は TLS がまだ使えない)。
- 内蔵 RAM を増やさない方針は Asterism にも適用する (専用のタスクを立てない、バッファは PSRAM に置けるか測る)。

## 全体の段階 (案)

| 段階 | 内容 | 目に見える成果 |
|---|---|---|
| **Z1** (完了) | sim で最小の疎通: picoruby-zenoh gem (zenoh-pico をリンク)、mruby アプリから put / subscribe、PC の zenohd と往復 | sim のアプリが送った値を PC で読める、PC から送った値がアプリの画面に出る |
| Z2 | 実機 (P4: NARYAv4 / Tab5) で WiFi 越しに同じ往復。P4 (RISC-V) で zenoh-pico が動くことを確定、flash / RAM の実測 | P4 の機体と PC が話す |
| Z3 | get / queryable (問い合わせと応答) と liveliness (生存の監視)。2 台の機体の間で直接 (peer) | 機体どうしが問い合わせる |
| Z4 | Retro (S3)。flash の区画の見直し (factory の拡張) とセット | Retro も網に入る |
| A1 | Asterism の本体の最初: 遠くのオブジェクトの代理 (method_missing で呼び出しを get / queryable に載せる) と、キー空間の命名規則 | `home.lamp.on` のような呼び出しが別の機体で動く |
| A2 以降 | usecases.md の最小で成立する案 (家を each する、部品を借りる) → ペット (pet_design.md) など | デモ |

ブラウザ版 (wasm) から網に入る方法 (node_variants.md 2.7、zenoh-ts と remote-api) は、Z1 と並行して小さく
調べる (段階は Z3 の頃に決める)。

## Z1: sim で最小の疎通 (完了 2026-10-06)

結果は report/z1.md。zenoh-pico 1.10.1 / zenohd 1.10.1。受信はアプリの側からのポーリングだけで成立した
(読み取りのタスクは不要)。zenoh-pico の POSIX の TCP 層は gem 側の実装に差し替えている (データの無い poll が
止まらないこと、sim の EINTR、接続の時間制限)。Z2 でも ESP32 の TCP 層に同じ問題がある見込み。

### ゴール

Linux の sim の中で動く mruby のアプリと、PC で動く zenoh のルータ (zenohd) が、Zenoh で値をやり取りできる。

1. アプリから `put` した値を、PC 側で読める。
2. PC 側から `put` した値を、アプリが購読して受け取り、画面に出せる。
3. アプリを閉じて開き直しても、くり返し使える (セッションの後始末が漏れない)。

### 作るもの

- **zenoh-pico の取り込み**: PIN ファイル (`lib/add/ZENOH_PICO_PIN`) に版を固定し、rake で vendor/ (gitignore)
  に取得する。既存の `lib/add/PICORUBY_TI_PIN`、Spinel の `SPINEL_PIN` と同じ流儀。Linux の sim では
  zenoh-pico の Unix のポートを使う。
- **picoruby-zenoh gem** (`lib/add/picoruby-zenoh/`): 最小の API。
  - セッション: 開く (mode: client、接続先: `tcp/<PC>:7447`) と閉じる。
  - 送る: `put(key, payload)` (payload は String。Ruby の値の符号化は後の段階で決める)。
  - 受け取る: `subscribe(key)` が購読を返し、購読から**溜まった値を取り出す** (ポーリング。例: `each_pending`
    か `poll` で `[key, payload]` を返す)。zenoh-pico の受信は `zp_read` 相当をアプリの側から回す
    (読み取りのタスクを立てない形が作れるかを Z1 で確かめる。作れなければ案を返す)。
  - Ruby の名前は `Zenoh` (zenoh-pico の薄い皮、汎用)。Asterism はその上に別の層 (別の gem) として A1 で作る。
- **PC 側**: zenohd を親のリポジトリの docker compose に足し、sim と一緒に上げる (公式のイメージ、版を固定)。sim のコンテナから届くネットワークの設定を
  決める。確かめる道具は Ruby (REST プラグイン経由の HTTP の GET / PUT、または zenoh の CLI)。手順を
  tools/ に残す。
- **試しのアプリ**: `flash/app/test/` に置く (ランチャーには出さない)。送受信した値を画面に出す。

### 範囲の外 (Z1 ではやらない)

- 実機 (Z2)、get / queryable / liveliness (Z3)、Ruby の値の符号化 (msgpack など)、内部の pub/sub との
  ミラー (常駐の仕組み)、Asterism のオブジェクトの層 (A1)、ブラウザ版。

### 受け入れ条件

- ゴールの 1-3 が sim の標準構成と互換構成で通る。
- gem を Family mruby に依存しない形で書いている (Family mruby の API を呼ぶのは試しのアプリだけ)。
- 実機のファームのビルド (TAB5 / NARYAv4 / S3) に影響しない (Z1 では Linux だけでリンクする。ESP32 の
  ビルドに入れるのは Z2)。
- PC 側の手順 (zenohd の起動、確かめ方) が文書と tools/ にある。

## 決定事項 (Z1 の前、2026-10-06 ユーザ決定)

1. zenoh-pico は PIN ファイル + rake で取得する (submodule にしない)。
2. 下の層の gem は `Zenoh` (picoruby-zenoh、汎用)。Asterism は上に別の層として作る。
3. zenohd は親のリポジトリの docker compose に足し、sim と一緒に上げる。

## 進め方

- これまでどおり、段階ごとに指示書 (instruction_z1.md など) を書き、サブエージェントが実装して report を書き、
  親が検収する。仕様 (使う人から見た動き、API の名前) が変わる選択は、サブに選ばせずユーザが決める。
