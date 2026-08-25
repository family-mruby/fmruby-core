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

### 4.8 GUI 端末としての位置づけ (2026-08-26 追記)

本プロジェクトの特徴は「マイコンなのに窓システム・日本語表示・マウスと
かな入力を持つ」ことにある。Zenoh 網に参加するノードの大半は画面を
持たない (センサ、ロボット、PC 上のデーモン)。そこで fmrb は
**キー空間の中身を人に見せ、人の操作を網に入れる端末** — 操作卓・
可視化盤・掲示板 — を受け持つ。ダッシュボードをブラウザ (zenoh-ts) に
作る代わりに、**機体そのものがダッシュボードになる**。

この視点でのユースケース:

- **家庭内伝言板**: 各部屋の機体が peer で繋がり、`fmrb/+/notice` に
  書くと相手の画面 + TTS で出る。一斉呼び出し (「ごはんだよ」) も
  publish 1 発。liveliness で「どの機体が起きているか」を画面に出す。
- **センサ可視化盤**: zenoh-pico を積んだ別マイコンのセンサノードや
  MQTT 網の値を購読して、常設の小さな表示盤にする (玄関チャイム、
  温度グラフ、洗濯終わり通知)。PC いらずで「家の掲示板」が置ける。
- **ロボットの操縦席** (4.1 の言い換え): ゲームパッド + 画面つきの
  操縦席として teleop の頭側になる。ヘッドレスなロボット側と対になる。
- **同期合奏**: Modern が譜面 (MML/音符列) を publish し、複数 Retro の
  APU が同時演奏。音と画面で Zenoh の発見・同報がそのまま見える。
  peer モードならルータ不要で、段階 1-2 の疎通確認が見せ場を兼ねる。
- **対戦・協力ゲーム** (4.2 の具体化): liveliness がロビー (相手検出)
  になる。入力/状態を publish しておけば zenoh-ts でブラウザ観戦、
  ストレージの record & replay で「昨日のプレイ再生」。
- **教室・ワークショップ**: 先生機が課題を全機体へ一斉配布 (4.6 の
  フリート配布)、生徒機の実行結果や画面を回収して先生機に一覧する。
  「結果を集めて見せる」画面側がこの機械の受け持ち。
- **発表リモコン**: PicoRabbit のスライド送りを別機体やブラウザから。
  発表ノートは手元の機体の画面に出す。
- **PC 側への外出し**: 天気・予定・AI (doc/ai/ideas.md) など重い処理と
  API 鍵を PC 側 worker に置き、機体は `ai/req` を publish して結果を
  画面と音で出すだけにする、という分業もこの位置づけの延長にある。

最初のデモ候補は**同期合奏か伝言板 + TTS** (機体 2 台 + peer モードで
完結し、ルータ不要で価値が音と画面に出る)。

### 4.9 親機 + ヘッドレス子機群 (2026-08-26 追記)

4.8 を推し進めた基本構成として、**Desktop (Modern) を親機、画面を持たない
マイコン群 (M5Stack 系の AtomS3 / Stamp / C3 など) を子機**とする形を
第一級の想定にする。

```
[親機: fmrb Desktop]  <- 発見(liveliness)・一覧・可視化・操作・配布
        |  zenoh (peer モード。ルータ不要、LAN 内)
   +----+--------+--------+
 [子機A]      [子機B]   [子機C]     ... ヘッドレス。センサ/アクチュエータ
 温度+LED     玄関     ロボット台車
```

- **役割分担**: 子機は測る・動かす・置かれるのが本分で、画面も入力も
  持たない。親機が家の司令卓として発見・一覧・操作・記録を受け持つ。
- **子機も Ruby で書ける見込み**: 段階 1 の picoruby-zenoh gem は素の
  PicoRuby が動く ESP32 ボードに載せられる構造にする。すると
  **親機のエディタで子機のロジックを書き、フリート配布 (4.6) で送り、
  結果を親機の画面で見る**という一巡が全部この企画の道具で閉じる。
  "Family" mruby の名のとおり、親機と子機の家族構成になる。
  (zenoh-pico を直接使う C/Arduino の子機も同じキー空間に混ざれる。)
- **子機の自己記述 → 親機の自動 UI**: 子機は自分の口 (キー・型・単位、
  例: 温度 float / LED bool / ボタン event) を msgpack の自己記述として
  queryable で答える規約にする。親機は未知の子機でもパネル (グラフ・
  トグル・ボタン) を自動生成でき、子機を増やすたびに親機を改修しなくて
  済む。HID auto-detect と同じ発想の zenoh 版。
- **最小の一歩**: 子機 1 種 (温度 + LED) + 親機のノード一覧窓。
  peer モードで完結する。MQTT ブリッジ・ROS・PC worker は後から同じ
  キー空間にぶら下げればよく、親機側の作りは変わらない。

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

## 8. 調査更新 (2026-08-25)

3 章 (2026-08-10 時点) の続報。段階 2 以降の前提に効く変化のみ記す。

### 版と互換性

- zenoh 本体と zenoh-pico は **1.10.0 (2026-08-14) を同日・同番号**で
  リリースする体制になっている (1.8.0=2026-03、1.9.0=2026-04)。
- **1.x 系列内はワイヤプロトコルの後方互換を公式に保証** (1.0.0 の
  発表文)。「pico とルータの版を揃える運用が必要」(3 章) は
  「揃えなくても方針上は動くが、組合せの公式互換表は無いので
  固定して記録する」に緩める。
- 採用する版は **zenoh-pico 1.10.0 以上**とする。ESP-IDF の condvar
  clock 不整合によるシングルコアでのビジーループ (issue #1270、
  RISC-V C3 で報告) の修正がここに入っているため。

### ESP-IDF / P4

- zenoh-pico の CI は PlatformIO 経由で **ESP-IDF v5.5.3 に固定**して
  espidf 例 6 本 (pub/sub/pull/get/queryable/scout、serial 有効) を
  ビルドしている。**当方の v5.5.4 とほぼ一致**で、最も安全な組合せ。
- **ESP Component Registry には無い** (2026-08-25 検索 0 件)。導入は
  components/ 以下への手動コンポーネント化になる (既存の流儀どおり)。
- **P4 の実績は依然ゼロ** (リポジトリの issue/PR 検索 0 件)。ただし
  プラットフォーム層は FreeRTOS タスク + ESP-IDF pthread + lwIP BSD
  ソケット + driver/uart の汎用 C で、Xtensa/RISC-V の分岐は無い。
  RISC-V (C3) の稼働報告はあり、傍証は増えた。「段階 2 の最初に
  P4 で確定」は変更なし。

### 機能の現状

- **TLS は ESP32 ではまだ使えない** (Unix 向けは 1.6 系で入ったが、
  esp32 対応 PR #1152 は 2026-08 時点で未マージ)。7 章の
  「v1 は信頼できる LAN 内前提」を確定とし、必要になったら
  ルータ側 ACL (NIC 名 / ユーザ・パスワード判別) で締める。
- serial transport は ESP-IDF (UART) で CI 込みの現役。USB CDC は
  unstable API 扱いの実験機能。S3 の serial CRC 不一致 issue (#1223)
  は 2026-05 に解決済み。
- **シングルスレッドモードは健在** (Z_FEATURE_MULTI_THREAD=0 +
  zp_read 系ポーリング)。5 章の「受信はポーリング」方針はそのまま
  成立する。1.10.0 で zp_spin_once が「処理が残っているか」を返す
  ようになり、ポーリングループが書きやすくなった。
- フットプリントの現行公表値は**基本構成で flash 約 80KB /
  RAM 約 12KB** (RPi Pico の公式実測、2025-01)。3 章の「15〜50KB」は
  0.x 時代の古い数字なので差し替える。受信バッファの既定は
  BATCH_UNICAST_SIZE=2048 / FRAG_MAX_SIZE=4096 で、既定のままなら
  RAM 増は小さい。S3/P4 とも許容範囲 (S3 は 7 章の factory 拡張前提)。

### ROS 2 側の地殻変動: rmw_zenoh

- ROS 2 は **Kilted Kaiju (2025-05) から Zenoh を Tier 1 middleware に
  採用**した。rmw_zenoh は Humble/Jazzy/Kilted/Rolling 全部にバイナリが
  出ている (既定 RMW は依然 Fast DDS。次期 Lyrical も Fast DDS 継続)。
- これにより ROS 2 連携の道が 2 本になった:
  1. **ros2dds ブリッジ経由** (従来案): DDS ベースの ROS 2 と接続。
     ブリッジは 1.10.0 が本体と同期リリースされており現役。
     ファーム側は msgpack のままでよく、実装が最小。デモはこちらから。
  2. **rmw_zenoh 直結** (新): PC 側 ROS 2 を rmw_zenoh で立てれば、
     zenoh-pico から**ブリッジなしで** ROS 2 topic に参加できる。
     ただし key 表現 (`<domain>/<topic>/<型名>/<型hash>`、hash は
     REP-2016)、CDR ペイロード、attachment (seq 番号+timestamp+GID)、
     liveliness token を rmw_zenoh の設計に合わせて自前で作る必要が
     ある。実装例は PX4 v1.17 (zenoh-pico 内蔵、Experimental) と
     esol-community の rmw_zenoh_pico。探索にはホスト側の
     rmw_zenohd ルータが要る。
- 2 章の「Ruby 製 CDR エンコーダ」構想は直結案でそのまま活きるが、
  型 hash と liveliness token の生成が上乗せになる。**v1 はブリッジ
  経由、直結は CDR シムの次の段**とする。

### 周辺 (変更なしの確認)

- MQTT ブリッジ / REST プラグイン / InfluxDB ストレージ / zenoh-ts は
  全て 1.10.0 で同期リリース継続。廃止・改名は無し。zenoh-ts には
  standalone のブリッジ実行形が追加された (1.5.0)。ストレージには
  ReductStore バックエンドが新顔 (2026-05)。

## 9. ROS 以外の採用事例 (2026-08-26 調査)

「ROS の代替経路」にとどまらない実績の確認。証拠の堅さ順に記す。

### 自動車 / SDV (最も堅い非 ROS 実績)

- **Eclipse uProtocol**: 車内通信標準の主要 transport が Zenoh
  (Rust/Python/C++ の公式実装が活発、2024〜)。
- **General Motors**: uProtocol の発起企業として車内通信に採用
  (2023-2024 の公式講演。量産車搭載時期は未確認)。
- **TTTech Auto x ZettaScale**: ASIL-D 対応の商用製品 "Zetta Auto"
  (2023-07 発表)。Eclipse SDV の blueprint や 2025 hackathon 優勝作も
  uProtocol + Zenoh 構成。

### シミュレーション / ツール

- **Gazebo**: gz-transport 15 (Gazebo Jetty、2025-09) で Zenoh transport
  が本体入り。環境変数で切替。roadmap に production 化の続報あり。
  gz-transport は ROS 非依存の独立ライブラリ。
- CARLA を Zenoh に直接載せるブリッジ (Autoware 公式ブログで紹介) も
  実働している。

### ドローン / 車両テレメトリ

- **PX4**: v1.15 から zenoh-pico を内蔵し uORB topic を直接 pub/sub
  (experimental、公式ドキュメントあり)。micro XRCE-DDS の代替 =
  非 DDS 経路として明確。主用途は rmw_zenoh の ROS 2 と繋ぐことだが、
  ROS なしの zenoh 単体購読も成立する。
- **Indy Autonomous Challenge**: レースカーと基地局のテレメトリ・
  緊急停止を Zenoh で (2021〜継続、公式事例)。

### 産業 / データ基盤

- **ReductStore**: 時系列オブジェクトストア製品が v1.19 (2026) で
  native Zenoh API を搭載 — 製品が Zenoh を一級 API にした例。
- GStreamer の zenoh 要素 (映像伝送、コミュニティだが本格実装)、
  Rust 製 AI データフロー基盤 dora-rs のノード間通信など、
  映像・エッジ系の周辺も育っている。

### 空白地帯 = 本プロジェクトの立ち位置

- **ホームオートメーション/ホビー圏に目立った採用例は無い** (Home
  Assistant / ESPHome の統合は見つからず。MQTT/Matter が支配的)。
  zenoh-pico 自体は RPi Pico / ESP32 / Arduino を公式サポートしており
  (2025-01 に RPi Pico 対応)、デバイス側の土台はあるのに、
  **「家庭の中で画面を持つ zenoh 端末」はまだ誰もやっていない**。
  4.8-4.9 の親機 + 子機構成は、この空白にちょうどはまる。
