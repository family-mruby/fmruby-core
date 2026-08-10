# Zenoh 対応検討: 外部ネットワーク通信の一級経路

作成: 2026-08-10。状態: 検討段階 (実装未着手)。
関連: doc/multivm_app/plan.md (多重 VM 構想。zenoh-agent はその worker
パターンの実用例になる)。

## 1. 動機と経緯

発端は「ROS 2 と通信する機能を加えられるか」。検討の結果、対象を ROS に
限定せず、**ネットワーク越えとプロトコル変換の汎用経路として Zenoh を
採用する方向**とした。

検討した選択肢:

| 案 | 内容 | 評価 |
|---|---|---|
| rosbridge | WebSocket + JSON で rosbridge_server に接続。ファーム側は既存の Net::WebSocket::Client (純 Ruby、ビルド済み) + JSON だけで済む | C 追加ゼロで最安。ただし ROS 専用で、用途が広がらない |
| micro-ROS | Micro XRCE-DDS クライアントで DDS グラフに参加 | 過去に mruby 対応の実績あり (ユーザ)。**課題はメッセージ定義を事前にビルドへ焼き込む必要がある点**で、動的言語と根本的に相性が悪い。加えてクライアントが数百 KB 級、P4 (RISC-V) の公式対応も不明 |
| **Zenoh (zenoh-pico)** | 制約デバイス向けの C99 クライアント。ペイロード無型 (キー表現 + バイト列) | **採用方向**。理由は下記 |

## 2. Zenoh を選ぶ理由

1. **メッセージ定義の事前ビルドが不要**。Zenoh はペイロードに型を持たず、
   直列化はアプリが選ぶ。fmrb 側には msgpack gem が既にあるので、
   fmrb 同士・自前システムとの通信は msgpack で即完結する。
   micro-ROS で経験した課題への直接の答え。
2. **プロトコル変換をルータに寄せられる**。zenoh ルータのブリッジ/
   プラグインにより、ファームは zenoh 一本を話すだけで複数エコシステムと
   相互接続できる (3 章)。
3. **ネットワーク越え**。NAT 裏のデバイス同士も外部に置いたルータ経由で
   通信できる (scouting / ルーティングが中核機能)。機体間通信
   (Retro <-> Modern) や遠隔アクセスが同じ仕組みに乗る。
4. **内部設計との対称性**。多重 VM 計画の通信モデルと 1:1 で対応する:
   - カーネル pub/sub <-> zenoh の publish/subscribe (キー空間ミラー)
   - 計画中の Request/Response <-> zenoh の get/queryable
5. **組み込み適性**。ESP-IDF が公式サポート対象で、要求は事実上 lwIP
   ソケットだけ。POSIX でもビルドできるため Linux sim 先行の開発ループが
   組める。micro-ROS のような重いビルド統合が無い。

ROS 2 と型付きで話す場合のみ CDR エンコードが必要になるが、これも
「型記述を実行時データとして持つ Ruby 製 CDR エンコーダ」で対応する方針
(Twist 等の単純型なら現実的)。定義は「ビルド成果物」ではなく
「実行時データ」になる。

## 3. 技術調査メモ (2026-08-10 時点)

### zenoh-pico (デバイス側クライアント)

- 本体 1.8 系 (2026-03 リリース) が現行。pico は 1.6 系ドキュメントが最新。
  プロトコルの進化が速いため、**pico とルータの版を揃える**運用が必要。
- **フットプリント: 全機能で 50KB 未満、絞った構成で 15KB 程度**
  (公称値)。S3/P4 どちらでも余裕。RAM はバッチ/フラグメントのバッファ
  設定 (BATCH_UNICAST_SIZE / FRAG_MAX_SIZE 等) で調整できる。
- 対応プラットフォーム: Unix / ESP-IDF / Zephyr / Arduino / FreeRTOS 系
  ほか。ESP32 は実績あり。**P4 (RISC-V) は明示リストに無い**が、
  プラットフォーム層は lwIP ソケット + FreeRTOS API の薄い皮なので
  移植リスクは低いと見込む (段階 2 で確認)。
- transport: TCP / UDP unicast / UDP multicast、**serial (UART/USB)**
  (コンパイルフラグ Z_FEATURE_LINK_SERIAL 系 + unstable API)。
  TLS の esp32 対応は開発中 (2026-02 に PR あり)。
- 動作モード: **client** (ルータ接続) と **peer** (UDP multicast の
  ルータレス P2P)。LAN 内の機体間通信はルータ無しでも成立する。
- 機能: publish/subscribe、**queryable/get** (Request/Response)、
  **liveliness** (生存監視 — アプリ/機体の死活検知に使える)、scouting
  (自動発見)、matching (購読者の有無検知)、interest (write filtering =
  誰も聞いていない topic は送らない)。いずれも既定で有効、フラグで
  外して小さくできる。1.8 で admin space (内省 API) も pico に入った。

### ルータ側のエコシステム (PC / クラウドで動かす)

- **ブリッジ**: ros2dds (ROS 2)、dds 素通し、**MQTT** (双方向ルーティング)。
  いずれも zenohd のプラグインまたは単体実行形 (zenoh-bridge-*) がある。
- **REST プラグイン**: キー空間を HTTP GET/PUT で読み書きできる。
  curl やスクリプトからの最も安い接続口。
- **ストレージ**: メモリ / ファイルシステム / RocksDB / **InfluxDB**。
  キー空間の指定範囲を自動保存し、record & replay ができる。
- **zenoh-ts + remote-api プラグイン**: ブラウザから WebSocket で zenoh
  セッションを張れる TypeScript バインディング。**Web ダッシュボードが
  ファーム側の追加実装ゼロで作れる**。

## 4. 応用カタログ (Family mruby で何ができるか)

キー空間の設計は `fmrb/<機体名>/...` を基本とし、内部 pub/sub の topic を
そのままぶら下げる想定。

### 4.1 ロボット・フィジカル系 (発端の用途)

- **ROS 2 teleop**: fmrb のゲームパッド/キーボード入力を cmd_vel として
  publish → ros2dds ブリッジ経由で実ロボットを操縦。逆にセンサ topic を
  購読して画面に描く (Retro が「ロボットの操縦席」になる)。
- ROS 抜きでも: zenoh を話す他のマイコン (zenoh-pico 同士) と直結して
  ラジコン・センサノード群を組む。

### 4.2 機体間通信 (Retro <-> Modern、複数 fmrb)

- peer モード (UDP multicast) なら**ルータすら不要**で LAN 内の fmrb
  同士が繋がる。
- 対戦/協力ゲーム (RPG のマルチプレイ)、機体間のファイル/クリップボード
  転送、Modern で書いて Retro に配るワークフロー。
- liveliness で相手機体のオンライン検知 (ランチャーに「隣の機体」を出す
  ような UI も可能)。

### 4.3 IoT・ホームオートメーション

- MQTT ブリッジ経由で既存のスマートホーム機器 (Home Assistant 等の
  MQTT 網) と相互接続。fmrb アプリから家電を叩く / センサ値を画面に出す。
- 玄関センサ → Retro の画面と APU でチャイム、のような「家の端末」用途。

### 4.4 テレメトリ・記録・可視化

- IMU (BMI270)・温度・入力イベント等を publish し、ルータの InfluxDB
  ストレージに自動記録 → Grafana 等で可視化。ファーム側は put するだけ。
- record & replay で「ゲームの入力を記録して再生」のような遊びも
  ストレージ機能だけでできる。

### 4.5 Web ダッシュボード・ブラウザ連携

- zenoh-ts (remote-api プラグイン) でブラウザから直接 zenoh に参加できる
  ため、**ファーム無改修で** fmrb の状態表示・操作 UI を Web に作れる。
- REST プラグインなら curl 一発でキーの読み書き。CI やシェルスクリプト
  からの操作口として最安。

### 4.6 開発・運用ツール (dogfooding)

- **ログ配信**: LOG_BUFFER の内容を zenoh に流し、PC 側で購読・保存。
  シリアル接続なしのログ観測 (今の BLE コンソール/シリアルの第三の経路)。
- **リモート操作/デバッグ**: debugd 相当のコマンドを queryable として
  公開すれば、ps / spawn / kill / スクリーンショットがネットワーク越えで
  叩ける (remote desktop の HTTP と違い、NAT 裏でも外部ルータ経由で届く)。
- **アプリ配布**: .app.rb を put で機体へ送り込み、ランチャー再スキャン。
  複数機体への一斉配布 (フリート更新) もキー空間のワイルドカードで書ける。

### 4.7 serial transport の特殊用途 (S3 で効く)

- zenoh-pico は **UART/USB serial を transport にできる**ため、
  **WiFi を使わずに** PC 経由で zenoh 網に参加できる。
- S3 は WiFi と BLE が排他なので、「BLE キーボードを使いながら
  ネットワークにも居る」が serial 経由なら成立する。空き UART の確保が
  課題 (UART0 はコンソール、MIDI が 1 本使用) なので要確認。

## 5. 構成案

```
[fmrb アプリ群] <-(kernel pub/sub / Request-Response)-> [zenoh-agent worker]
                                                          | picoruby-zenoh (C gem)
                                                          | zenoh-pico
                                                          v
                                             [zenoh ルータ (PC/クラウド)]
                                              |- ros2dds ブリッジ -> ROS 2
                                              |- MQTT ブリッジ -> IoT 機器
                                              |- REST / zenoh-ts -> Web
                                              |- ストレージ -> InfluxDB 等
                                              |- 別の fmrb 機体 (または peer 直結)
```

- **zenoh-agent は headless worker アプリ** (多重 VM 計画の形態 B)。
  セッションを 1 本所有し、内部トピックと zenoh キー空間を相互ミラーする。
  他のアプリは ROS も zenoh も知らず、内部 pub/sub を使うだけ。
- **受信はポーリングモード** (zp_read 相当) を _spin から回す。コールバック
  スレッドを立てないことで、mruby VM へのスレッド跨ぎ問題を最初から
  回避する。
- 大きいペイロードは内部メッセージ 176B 制限に載せず、/tmp (RAM FS 計画)
  経由のパス渡し規約に乗せる。
- 直列化: 既定は msgpack。ROS 2 相互運用時のみ CDR シム。

## 6. 実装計画

### 段階 1: sim で最小疎通

- picoruby-zenoh gem (lib/add/) を新設し、zenoh-pico をリンク。
  API は最小から: session open/close、declare_publisher / put、
  declare_subscriber + poll、get / queryable。
- Linux sim から PC 上の zenohd と put/subscribe の往復を確認。

### 段階 2: 実機 (P4 / S3)

- ESP-IDF v5.5.4 でのビルド整合を確認し、Tab5 で WiFi 越し疎通。
  P4 (RISC-V) はプラットフォーム層の移植可否をここで確定する。
- idf.py size-components で flash/RAM 実測。
- S3 は factory パーティション拡張 (7 章) とセットで対応。flash は
  制約にしない (方針決定済み)。

### 段階 3: zenoh-agent worker 化

- 内部 pub/sub とのミラー、Request/Response と get/queryable の接続。
  多重 VM 計画の基盤 (終了通知、/tmp) の進捗と同期する。
- キー空間の命名規則 (`fmrb/<機体名>/...`) をここで確定する。

### 段階 4: 応用デモ

- 候補は 4 章から選ぶ。手数が少なく見栄えがするのは:
  機体間通信 (peer モード、ルータ不要)、Web ダッシュボード (zenoh-ts、
  ファーム無改修)、ROS 2 teleop (ros2dds ブリッジ)。
- Ruby 製 CDR エンコーダの最小実装 (対応型は Twist / std_msgs 程度から)
  は ROS デモの段で。

## 7. リスクと制約

- **S3 の flash**: 残 6% は factory パーティション (4M) に対する残りで、
  16MB flash 全体では約 7MB が未使用 (config/partitions_n16r8.csv の
  コメント参照)。**パーティション見直し (factory 拡張) で対応する方針
  (決定)**。注意点は、パーティション表の変更はアプリ単体更新では配布
  できず、フル書き込み (bootloader + table + app + storage) が要ること。
  installer / リリース手順への影響とセットで実施する。
- **内蔵 RAM**: 追加タスクを立てない (ポーリング) 方針で増分を抑えるが、
  セッションバッファ分 (バッチ/フラグメント設定に依存) は実測で確認する。
- **P4 (RISC-V) の実績が明示されていない**: プラットフォーム層は薄いので
  低リスクと見込むが、段階 2 で最初に確定させる。
- **版の整合**: zenoh はプロトコルの進化が速い。zenoh-pico とルータの
  版を固定してリポジトリに記録する。
- **セキュリティ**: 本機の WiFi はアクセス制御なしの方針。zenoh は
  ルータ側で TLS/認証を張れる (pico の TLS は開発中) が、v1 は
  「信頼できる LAN 内」前提と明記する。外部ルータ経由の運用
  (ネットワーク越え) を始める段階で再検討する。
- BLE との排他 (S3) など、WiFi 前提の既存制約はそのまま適用される
  (serial transport が回避策になり得る。4.7 章)。
