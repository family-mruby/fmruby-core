# Asterism W1 指示書: ルータどうしの中継と守り (mTLS・ACL)、画面のグラフに重ねる

> 状態: 指示 | 更新: 2026-10-09 | docker でクラウド役と家役の zenohd を立て、ルータどうしを mTLS でつなぎ、ACL でキーを絞る。家のルータに機体と sim、クラウドのルータに CRuby をつなぎ、Asterism の呼び出しと ROS 2 のトピックが中継を通ることを確かめる。asterism-console のグラフにルータ 2 台とその間のつながりを出す

前提: plan.md の W と V の節、design.md の 8 章、report/v1.md (W1 への申し送り)、report/c7.md (CRuby の config で TLS)。

## 1. 構成

```
[家] P4-Nano, sim ── zenohd-home (LAN 用、平文 tcp/7447) ──mTLS──┐
                                                                   ├── zenohd-cloud (tls/7448 を待ち受け、相手の証明書を確かめる + ACL)
[外] CRuby (examples/node.rb, ros2) ──TLS──────────────────────────┘
     asterism-console の bridge (どちらのルータにつなぐかは確かめて決める)
```

- **ルータどうし**: zenohd-home が zenohd-cloud に `tls/` で接続し、mTLS (自前の CA で署名した証明書) を使う。
  証明書の無いルータ、別の CA のルータはつながれないことを確かめる。
- **ACL** (zenohd-cloud): 相手を証明書の名前で見分け、書けるキーを絞る。
  - 例: 家のルータは `asterism/**` と ROS 2 のキーに書ける。外の CRuby は特定の範囲だけ。
  - 管理用の空間 (`@/**`) は、console の bridge にだけ読ませる。
  - zenohd 1.10.1 の設定の項目名は、使う版の例・文書で確かめる。
- **機体側**: 今までどおり家のルータに平文でつなぐ (zenoh-pico は ESP32 で TLS が無い)。機体のファームは変えない。

## 2. 作るもの

- **compose**: 親リポジトリに重ね合わせのファイル (例: `docker-compose.relay.yml`) を足す。zenohd-cloud を足し、zenohd を家のルータとして cloud につなぐ。
  - 既存のファイルの意味は変えない。重ねないときは今までどおり。
- **証明書**: CA と、各ルータ・クライアント用の証明書を作るスクリプト (Ruby の標準の OpenSSL)。
  - 作った鍵と証明書は gitignore の場所に置く (コミットしない)。
  - 置き場所は、W2 で画面から発行することを見越して決める (asterism-console の `script/` か、親の `tools/`)。
- **asterism-console**:
  - ルータが 2 台のとき、グラフにルータ 2 台と、その間のつながり (router_link) が出ること。
  - 中継の先のルータが管理用の空間に答えるか確かめる。答えないなら、案を実装する。案は「ルータごとに bridge」「ルータごとの要約」など、軽いもの。
  - 点の詳細に、ルータのリンクの種類 (tls / tcp) と、分かれば証明書の名前を出す。
- **CRuby の例**: クラウドのルータに TLS でつなぐ設定の例を README に書く (`config:` で CA と証明書を渡す)。

## 3. 受け入れ条件

1. zenohd-home と zenohd-cloud が mTLS でつながる。証明書の無いルータ、別の CA のルータは断られる (ログで確かめる)。
2. 家のルータにつないだ sim・P4-Nano の asterism_demo と、クラウドのルータに TLS でつないだ CRuby の examples/node.rb が、互いに呼び合える (中継を通る)。
   呼び出しの往復の時間を、直接 (ルータ 1 台) と中継 (2 台) で比べて書く。
3. ROS 2 のトピックが中継を通る (家側の sim の ros2_talker → クラウド側の ROS 2 のコンテナの `ros2 topic echo`、またはその逆)。
4. ACL: 許していないキーへの書き込みが届かない。管理用の空間が、許した相手 (console) 以外から読めない。
5. asterism-console のグラフに、ルータ 2 台、その間のつながり、両側のセッションとノードが出る。headless のブラウザで撮って残す。
6. 鍵・証明書・本当の IP・機体の名前がコミットされていない。asterism-console の試験 (`bin/rails test`) が通る。

## 4. 範囲・決まり

- **触ってよいもの**:
  - 親リポジトリの compose の重ね合わせ (足すだけ)、`tools/`、`.gitignore`。
  - asterism-console (main にコミット。push してよい。ユーザの許可済み)。
  - fmruby-core の `doc/ruby_asterism/report/w1.md`。fmruby-core のブランチは develop から `feature/asterism-w1` を切る。
  - asterism / asterism-zenoh に不具合が見つかった場合: 直して main にコミットしてよいが、push はしない。report に書く。
- **触らないもの**: 機体のファーム、fmruby-core のビルドの仕組み、`.env`、sdkconfig。
- **公開**: gem の公開はしない。
- **機体**: P4-Nano。本当の IP は親が渡す。ミュート中。
  - 親のシリアルの capture が向いている (`serial_start` を呼ばない)。
  - 機体のアプリは終了させてよい。
- **終わったら**: sim・zenohd・ROS 2・Rails・bridge は全部止める。sim は sim_down。`build/` は Linux の標準構成 (x86-64) のまま。
- **止まる条件**:
  - zenohd 1.10.1 で mTLS か ACL が使えない (版を上げる必要がある) とき。
  - 機体側の変更が要るとき。
  - 既存の compose の意味を変える必要があるとき。
- **report**: Japanese plain style (常体)。書くことは次のとおり。
  - 構成、設定の要点 (項目名)、証明書の作り方。
  - 受け入れ条件の結果と往復の時間。
  - 中継の先の管理用の空間の扱い。
  - W2 (管理画面) への申し送り。
