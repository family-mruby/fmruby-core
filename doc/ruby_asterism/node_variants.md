# ノードの種類と開けた可能性 (wasm / NARYA v4 / ヘッドレス)

記録: 2026-08-31。状態: 構想 (実装未着手)。
関連: zenoh_idea.md (4.8-4.9 の親機+子機構成を本書が更新する)、
pet_design.md (居住地の実装が本書で変わる)、usecases.md (2, 3, 6, 9)、
doc/wasm/ (ブラウザ版)、doc/naryav4/ (次期 Modern 基板)、
doc/remote_desktop/design.md (ヘッドレス機の唯一の画面になる)。

## 1. 前提の変化

構想文書 (naming.md / usecases.md / pet_design.md、2026-08-27〜28) の
あと 3 日で、前提が 2 つ変わった。

- **ブラウザの中で動くのが Family mruby そのものになった** (doc/wasm/、
  P1-P5 + ストレージ永続化完了)。同じ .app.rb、同じ gem、同じカーネルが
  静的サーバから配れる。/home が残り、fetch の橋があり、web_* ツールで
  自律検証できる。構想文書は「ブラウザ = zenoh-ts のページ、V2 は
  ruby.wasm」を前提にしていたが、その前提が消えた。
- **親機に実体ができた** (doc/naryav4/、P0-P4 完了)。HDMI 1280x720、
  USB キーボード/マウス、無線は別チップ (WiFi と BLE が同時に使える =
  S3 の排他が無い)、tab5_* の遠隔経路が開通済み。zenoh_idea.md 4.9 の
  「Desktop を親機、画面のない子機群を従える」の親機側が机の上に置ける。

これに加えて、**ヘッドレス Family mruby** (3 章) の構想が加わった。

## 2. wasm と NARYA v4 で開けたこと

順位は費用対効果の見立て順。

### 2.1 居住地が全部同じコードになる (ペット V2 が安くなる)

pet_design.md は居住地の実装を 4 種 (Retro/Tab5 の habitat アプリ、
PC の worker、ブラウザの zenoh-ts ページ、V2 のブラウザは ruby.wasm) と
していた。今はブラウザも habitat.app.rb そのもので済み、V2 の
「振る舞いを sandbox で eval する」仕組みも実機とブラウザで同一になる。
ruby.wasm の移植工数が消え、処理系は **CRuby (PC) と Family mruby
(実機 + ブラウザ) の 2 つ**に落ちる。契約を詰めるには十分で、説明は
むしろ易しい。

「Ruby のオブジェクトはどこに存在するのか」の答えは、同じ OS の画面
4 枚 (HDMI・TV・ブラウザ・PC 端末) をペットが渡り歩く絵になる。

### 2.2 星座を机の上に組める = 検証の場所が変わる

多ノードの試験はこれまで実機 2 台か sim + CRuby だった。今は
**ブラウザのタブ 1 枚 = 機体 1 台** (アプリ 32 本、キャンバス 34 枚) で、
sim の 3 コンテナと CRuby を足せば PC 1 台で 5-6 ノードの網が組め、
web_* / sim_* で画面まで自動確認できる。契約の要 (呼び出し・Timeout・
世代番号・引っ越しの ACK) を flash なしで回せる。

開発順序 (zenoh_idea.md 6 章、討議まとめ 14 節) に 1 段挟む:
**CRuby 参照実装 → sim → wasm → NARYA v4 / Tab5 → S3**。

同一生成元のタブ同士は /home (IDBFS) を共有するので、機体の識別は
ページの引数 (ノード名) で与える規約が要る。

### 2.3 「From the Web」が文字どおりになる

討議まとめ 17 節は Web 側代表を Rails にしていた。Rails は残してよいが、
つかみは「**リンクを開いただけで、あなたのブラウザが Asterism の
ノードになる**」に変えられる。会場オーケストラ (usecases.md 9) を
Family mruby のページで受ければ、聴衆のデスクトップが壇上の HDMI に
ノード一覧として並び、ペットが客席へ遊びに行く、まで行ける。

ただし 6MB + coi-serviceworker のリロードを会場 WiFi・携帯ブラウザで
数百台は未検証。**携帯向けは zenoh-ts を保険**に残す。

### 2.4 親機の仕事が NARYA v4 で具体化する

- 子機の自己記述 ($meta) から自動生成する操作盤・グラフが 1280x720 の
  常設画面に置ける。「家の掲示板」がモニタ 1 枚で成立する。
- 「親機のエディタで子機のロジックを書いて配る」の親機側は、79 桁の
  全画面エディタ + picoruby-ti という Modern 固有資産そのもの。
  usecases.md 2 (網の中の実物から補完する) の置き場所はここ。
- WiFi と BLE が同時に使えるので、親機は zenoh 網に居ながら BLE 機器も
  扱える。Retro (S3) では成立しなかった組み合わせ。
- USB ホストがあるので、親機に挿したゲームパッドを `/devices/<親機>/pad`
  として網に出す (teleop の頭側) のが自然。

### 2.5 ブラウザが Retro の代弁者になる (wasm P6 候補との合流)

doc/wasm/implementation_plan.md の P6 候補 (Web Bluetooth で実機へ beam)
と重ねると、S3 が WiFi を切って BLE キーボードを使っているときでも、
**ブラウザ側のページが Retro の代理 Resource を網に立てる**ことができる。
`retro.apu.play` (usecases.md 3) を、実機側は debugd の BLE 口だけ、
代理はブラウザ、という構成で実現できる。zenoh_idea.md 4.7 (serial で
網に居る) のブラウザ版。

### 2.6 一箱の中の分散 = LocalBackend の試験台

討議まとめ 13 節の LocalBackend (カーネル pub/sub の上の Asterism) は、
タブ 1 枚に 32 アプリが同居する wasm で一番厚く叩ける。機体内・機体間で
同じ `Asterism["/..."]` が動くことを、まず一箱の中で示せる。

### 2.7 未確認の論点: ブラウザからどう網に入るか

zenoh-pico は BSD ソケット前提で、ブラウザには生 TCP も UDP
マルチキャストも無い。候補:

- (a) zenoh-pico の WebSocket リンクを emscripten の websocket に載せる
  (リンクの存在と対応範囲は版ごとに要確認)
- (b) websockify 型の中継 (zenoh-pico 無改修、中継が 1 つ増える)
- (c) remote-api プラグインの JSON/WebSocket を Ruby で話す
  (ワイヤは別物になるが、ブラウザ側の WebSocket は素直)

いずれも **PC 側に WebSocket を聞く zenohd が要る** = peer モードだけ
では済まず、実家ノードに router の役が固定される。GitHub Pages (https)
から ws:// は localhost 宛以外は弾かれる (wss 必須) ことも設計に効く。

wasm 側で zenoh-pico をシングルスレッド + `_spin` ポーリングで動かす
形は実機と同じなので協調 port と相性はよいはずだが、実測が要る。

## 3. ヘッドレス Family mruby (第 3 の層)

### 3.1 何か

実物 (M5Stamp-P4 + Stamp-AddOn C6) の仕様と殻の設計は doc/stamp_p4/。

切符サイズの P4 + C6 積層モジュール (PSRAM は Tab5 と同じ) に、
**Modern の体で顔 (パネル) だけ無い**構成を載せる。wasm が「linux の
顔で Modern の体」だったのと対。画面は遠隔画面 (remote desktop) だけ。

必要な部品はこの 1 か月でほぼ揃っている:

- **描画**: display_backend_t の背後に PPA と CPU の 2 実装があり、
  切替は `FMRB_DISPLAY_BACKEND` の define だけ。**PPA はチップの周辺機能で
  パネルに依存しない**ので、ヘッドレスでも PSRAM 上のフレームバッファへ
  PPA 合成 (実機 17ms/frame) が使える。走査出力を誰も見ないだけ。
- **見る手段**: 遠隔画面は P4 の esp_http_server がビューアごと配信
  (MJPEG 15fps / H.264 + WebSocket) し、入力も戻る。capture 経路は
  バイト一致の決定的なもの。ヘッドレスではこれが唯一の画面に昇格する
  だけで、新規実装は要らない。
- **開発ループ**: put → launch の再 flash なしループと tab5_* の MCP
  ツール群は WiFi しか使っていない。ヘッドレス機は初日から自律検証
  できる (Retro より検証しやすい)。
- **ターゲット追加の型**: doc/naryav4/plan.md の P1 (sdkconfig 分岐 +
  ボード分岐) が雛形。DSI も LT8912B も codec もタッチも無い分、v4 より
  薄い分岐になる。
- **無いものの塞ぎ方**: IDF seam (S1-S4) と wasm の stub 4 枚が
  「カーネルが最低限何を要求するか」の地図。音は audio_backend_t の
  4 点を空にすれば済む形が wasm で確認済み。

つまり新しい移植ではなく、**wasm + naryav4 + seam の 3 つの成果を
組み合わせた 4 つ目の構成**として作れる。

### 3.2 Asterism の中での位置 (zenoh_idea.md 4.9 の更新)

親機と子機の 2 層から 3 層になる。

| 層 | 機体 | 持つもの |
|---|---|---|
| 親機 | NARYA v4 / Tab5 | 画面・エディタ・操作卓 |
| 頭脳つき子機 | ヘッドレス P4 | 多重 VM・サービスホスト (自動再 spawn)・/tmp・pub/sub・エディタ (遠隔画面越し)・Tab5 と同じ PSRAM = 同じアプリがそのまま動く |
| 末端子機 | R2P2 (Pico W / C3 系) | 測る・動かす・置かれる |

R2P2 子機を捨てる話ではない。gem を fmrb 非依存に作る条件 (4.9) は
そのまま活きて、ヘッドレス機では同じ gem を zenoh-agent worker から
使う (zenoh_idea.md 5 章の構成)。Family mruby との統合分だけ得をする。

この層で変わるユースケース:

- **実家ノードの候補**: PC を常時つけなくても、切符サイズの箱が家に
  1 つあれば、ペットの checkpoint・timesync・ai サービスの鍵置き場・
  夜間の宿主が務まる。ただし **zenoh-pico は router になれない**ので、
  ブラウザ参加 (WebSocket) が要る場面では router 役の PC が依然必要
  (2.7)。実家 = 常時稼働ノード、router = PC、と分けて考える。
- **ロボットの頭**: 討議まとめ 18-20 節の「Ruby は上位制御を受け持つ」
  を載せる頭として、車台に入る大きさで遠隔画面越しに開発・実行できる。
  teleop の「ヘッドレスなロボット側」の実体。
- **フリート配布・教室** (zenoh_idea.md 4.6 / 4.8): 安ければ配れる。
  生徒 1 人に 1 個、開発はブラウザから、先生機の NARYA v4 が結果を集める。
- **ペット V3 の「画面のないノードに居る」**: LED の気配に加えて、
  遠隔画面を覗くと本当にそこに居る、という二重の見せ方ができる。

### 3.3 開発の入口が 4 通りになる

1. **ブラウザの遠隔画面 → 機体内の FM-EDITOR**: PC は画面と鍵盤を貸す
   だけ。全部が機体の中で完結。既存機能そのもの。
2. **ブラウザ版 Family mruby (wasm) で書いて → HTTP put → 起動**: fetch の
   橋があるので、wasm 側のエディタから dev_remote_ctl の `/fs/put`
   `/app/launch` を叩ける。ブラウザ版が「フリートの開発機」になる。
   ただし GitHub Pages (https) から LAN の http へは混在コンテンツと
   Private Network Access で弾かれるので、ページを機体から配る
   (同一生成元) か、機体側で https を張るかが要る。機体から配る場合、
   **SharedArrayBuffer は安全な文脈 (https か localhost) が必須**なので
   自己署名でも https が要る。
3. **NARYA v4 の全画面エディタで書いて → 配布**: 4.9 の一巡。親機の
   存在理由。
4. **PC のツール (MCP / VSCode 拡張 / rake) → 機体**: 今日の Tab5 と同一。

### 3.4 確かめたい点

- **モジュールの実物**: flash 容量、C6 との SDIO 配線、C6 の電源制御の
  有無、LED、USB。Tab5 では C6 の電源投入が PI4IO 経由で display 初期化に
  依存していて、NARYA v4 で「display 待ちを外す分岐」を作ったばかり。
  ヘッドレスではその分岐が本線になる。
- **起動同期**: ブート画面と INIT_DISPLAY の非同期同期がパネル前提で
  書かれていないか。wasm では通っているが、「その構成しか通らない経路」は
  動かして確かめる。
- **遠隔画面の常用性**: MJPEG 15fps / H.264 が「開発の主画面」として
  打鍵の追従に耐えるか。Tab5 では補助だったが、ヘッドレスでは主になる。
  遅延の実測が要る。
- **HW_FAMILY の扱い**: FmrbConst::HW_FAMILY は MODERN のままでよいか、
  ヘッドレスを区別するか (エディタや PicoRabbit が画面サイズで分岐して
  いる箇所に効く)。

### 3.5 使い方の候補 (2026-08-31 の案出し)

「画面が無い」ではなく「画面が縛られていない」機体として、固有の持ち物
(多重 VM・仮想デスクトップ・遠隔画面・サービスホスト・WiFi と BLE の同時
使用・P4 の周辺機能) から出した案。費用の見立てを併記する。

画面が別のものになる:

1. **窓の中の窓**: 当初は仮想デスクトップ丸ごと
   (`/machines/<機体>/display`) を親機の窓に出す案だったが、**アプリの
   窓 1 枚を転送する形 (remote_window.md) に置き換える**。合成が自然で、
   Retro も受け側になれる。usecases.md 10。**安い**。
2. **画面は何にでもなる**: 426x240 を LED マトリクス (RMT proxy は既存)・
   小型 OLED・電子ペーパーに落として貼る。デスクトップが動いていることが
   ぼんやり分かる置物。ペットが来たときの気配表示にもなる。
3. **スマホが画面のマイコン**: C6 を SoftAP にしてスマホのブラウザで
   遠隔画面を開く。PC 無しで Stamp とスマホだけで動く。実現性は 3.6。

体を持つ:

4. **ロボットの頭の中を覗く**: 車台に載せ、振る舞い 1 つ = アプリ 1 本に
   すると、仮想デスクトップがロボットの「頭の中」の可視化になる。窓を
   閉じると振る舞いが止まり、pub/sub の流れが画面に出る。操縦席は親機の
   ゲームパッド。**この機体でしか成立しない**。
5. **目玉ノード (カメラ)**: P4 の MIPI-CSI + doc/camera (esp_video、凍結中)。
   映像をキャンバスに描いて Ruby で簡単な処理、「誰か来た」を網に
   publish、覗くと H.264 で見える。Stamp 側の CSI 端子の有無が前提。
6. **声の Family mruby**: マイク + スピーカ + TTS + ai サービス。
   doc/ai/ideas.md の音声アシスタントの体は画面の無い箱がふさわしい。
   遠隔画面は「何を聞き取ったか」の点検用。マイク経路の有無が前提。

常時居る:

7. **Retro のネットワークカード**: S3 は WiFi と BLE が排他だが Stamp は
   同時に使える。Retro の隣に置いて UART (zenoh-pico の serial リンク) か
   BLE で繋ぐと、Retro は BLE キーボードを使ったまま網に居られる。代弁者を
   Ruby で書ける点がブラウザ案 (2.5) より強い。**この機体でしか成立しない**。
8. **家の黒箱 (時間軸の担い手)**: SD があれば網のキー空間を記録し続け、
   `robot.at(5.minutes.ago)` に答える queryable を務める。usecases.md 5 を
   zenohd 無しで箱 1 つで成立させる。実家ノードの仕事と同居できる。
9. **トランプの箱サイズの Ruby クラスタ**: Stamp を 5 個並べ、各機の VM
   3 本 + /tmp を計算資源として `each` で仕事を撒く。速くはないが絵に
   なり、フリート配布・LocalBackend・分散 GC の試験台を兼ねる。

開発の道具:

10. **実機 CI ノード**: tab5_* の経路は WiFi しか使わないので、画面の無い
    本物の P4 を常時つないでおけば put → launch → screenshot の回帰を実機で
    夜通し回せる。Tab5 を検証に占有しなくて済む。**安く、本プロジェクトに
    一番効く**。
11. **1 人 1 台のサーバ実習**: 教室で生徒 1 人に 1 個。開発はブラウザの
    遠隔画面から、先生機が全員の画面を窓の入れ子 (1) で一覧する。

推し: 1 と 10 が安くて確実、4 と 7 がこの機体固有の面白さ。5・6・8 は
モジュールの端子 (CSI・マイク・SD) 次第。

### 3.6 スマホが画面のマイコン: 実現性の検討 (2026-08-31)

**実装計画は doc/softap_remote/plan.md に独立させた** (Modern 全機種に効く
機能で、Asterism の進捗に依存しないため)。以下は検討の記録。

対象は iPhone (Safari)。PC も家の WiFi も無い場所で、Stamp + iPhone だけで
Family mruby を見て・触って・書けるか。

**今日の実装から出発点を確認する** (doc/remote_desktop/design.md):

- WiFi は STA 専用 (wifi_task.c は WIFI_MODE_STA のみ、資格情報は
  /etc/wifi.toml)。SoftAP は未実装。無線は esp_hosted 経由の C6 で、
  スレーブ側は AP / AP+STA を持つが、esp_wifi_remote 経由で AP を張る
  実績は本プロジェクトに無い (要確認 1)。
- 映像は 2 経路。H.264 + WebSocket + WebCodecs は安全な文脈 (https) 限定で、
  平文 http の LAN では Chrome でも MJPEG に落ちる (実機で判明済み)。
  SoftAP の `http://192.168.4.1` も平文なので **iPhone では最初から MJPEG
  一本**。`<img src="/stream">` の multipart で 15fps・1-2.4Mbps。
  Safari の multipart 対応は実測が要る (要確認 2)。
- 入力は /ws にバイナリで mouse_move / mouse_button (仮想 426x240 の絶対
  座標) / key (HID scancode + 修飾)。**ビューアは mouse と keydown/keyup
  だけで、touch / pointer の処理は無い**。キーは KeyboardEvent.code →
  HID の静的表 (keymap.js)。
- ホスト名は mDNS (`_http._tcp`)。iPhone は Bonjour を持つので
  `http://fmruby.local/` が引ける見込み (AP の netif でも広告するか要確認 3)。
- 全画面は Fullscreen API + Keyboard Lock。**iPhone の Safari は要素の
  全画面を持たない**ので、この経路は使えない。

**成立の見立て**: 表示は既存の MJPEG 経路のまま、機体側の追加は SoftAP
だけで済む。**作業の本体はビューア側 (触る操作とソフトキーボード)**。

機体側の作業:

- wifi_task に AP モード。**切替は設定だけで決める** (タイムアウトや
  ボタンでの自動切替はしない。ユーザ指定 2026-08-31)。モード (秘密で
  ない) は system_conf.toml の `[network] wifi_mode = "sta" | "ap" |
  "apsta"`、鍵 (秘密) は /etc/wifi.toml に `[ap]` (ssid / password /
  channel / max_connection) を足す。**mode の言うとおりに起動し、必要な
  鍵が無ければ起動せずログに出す** (既定の鍵で勝手に開けない)。
  apsta は家用 (STA + 自分の AP)、出先は ap (知らない WiFi に加わる経路が
  構造的に無い)。切替はファイルを書いて再起動 (遠隔画面のネットワーク窓か
  tab5_fs put。ヘッドレスの初期設定は USB シリアル)。
- DHCP サーバは esp_netif の AP 既定で足りる。DNS は張らない
  (iPhone は「インターネット未接続」と表示するが、同一サブネットへの
  通信は WiFi 側に流れる。captive portal の検出応答を細工して自動で
  ビューアを開かせる案もあるが、その画面は機能制限つきの簡易ブラウザ
  なので、Safari で `fmruby.local` を開かせる方が確実)。
- mDNS を AP の netif でも広告する。

ビューア側の作業 (tool/web/remote/、EMBED_FILES で機体に埋め込み):

- **触る操作**: touchstart/move/end を mouse_button/mouse_move に写す。
  座標はビューアが 426x240 の絶対座標に変換して送るので、**Tab5 本体の
  タッチ (相対移動) と違い、遠隔画面では絶対位置でよい**。タップ = 左
  クリック、長押し = 右クリック、動かす = ドラッグ。二本指のピンチと
  ダブルタップ拡大は `touch-action: none` と viewport の
  `user-scalable=no` で殺す。hover は無いのでカーソル追従は捨てる。
- **ソフトキーボード**: iOS はフォーカスした入力要素が無いと出ない。
  隠し textarea を置き、`beforeinput` / `input` で文字を取る。iOS の
  ソフトキーボードは KeyboardEvent.code が入らないことが多いので、
  **文字 → HID の逆引き** (fmrb_input.rb が fmrb_keymap.c から作るのと
  同じ表) をビューアに持つ。Ctrl / Esc / 矢印 / F5 / Tab はソフト
  キーボードに無いので、**画面上のキー帯** (押しっぱなしにできる Ctrl、
  Esc、矢印 4 つ、F5、Tab、Enter) を足す。これがビューア作業の本体。
- 日本語: iOS の日本語キーボードは変換済みの文字を返すので、機体の
  かなモード (ローマ字合成) には載らない。**書くときは英語キーボード**、
  日本語は当面対象外と割り切る。
- **本気で書くときは Bluetooth キーボードを iPhone に繋ぐ**。Safari は
  物理キーボードなら code を返すので、今の keymap.js がそのまま効く。
  触る操作 + ソフトキーボードは「見る・少し触る」、BT キーボードは
  「書く」の 2 段構え。
- 見え方: iPhone 横向きで幅 800-900 CSS px、426x240 の約 2 倍。
  ブラウザの枠を消すには「ホーム画面に追加」(standalone) を使う
  (manifest を 1 枚足す)。要素の全画面は無いのでこれが代替。
- iOS Safari は WebSocket を平文 http ページから張れる (混在コンテンツ
  にならない)。

キーボード入力の整理 (機体はどの経路でも USB キーボードと同じ HID
scancode を受けるのでアプリは無改修):

- **BT 物理キーボード**: Safari が code を返すので keymap.js のまま。
  修飾・Esc・矢印・F5 が全部効く「書く」ときの本線。機体の
  keyboard_layout を物理配列に合わせる。**Ctrl+Space は iOS が入力ソース
  切替に横取りする**ので、かなモードは指示器クリックか半角/全角キーで。
- **ソフトキーボード**: 文字だけ。隠し textarea の beforeinput で取り、
  文字→HID+Shift の逆引き表で送る。autocorrect / autocapitalize / 予測を
  属性で切る。Ctrl は画面キー帯の 1 回押し粘着式 (Ctrl → s で Ctrl+S)。
  長押し・リピートは不可。
- **日本語**: iOS の IME は使わず、機体のかなモード (指示器タップ) +
  英語ソフトキーボードのローマ字で合成する。
- 打鍵は WebSocket で即時、見た目は MJPEG 15fps なので 70-150ms 程度。

セキュリティ (出先で使う前提。2026-08-31 の方針):

- 現状は遠隔画面も dev_remote_ctl (/fs/put, /app/launch = 任意コード実行と
  全ファイル読み書き) も**無認証**で「信頼できる LAN 内」前提。AP を
  出先で張るならこの前提が崩れるので層を足す。
1. **AP の鍵が第一の門**: WPA2-PSK 以上 (C6 が AP で WPA3-SAE を張れるなら
   WPA3。WPA2-PSK は鍵を知る者が握手を録ると復号できる)。
   `max_connection = 1` で自分の端末以外を入れない。SSID 非表示は数えない。
2. **HTTP 側に共通鍵**: wifi.toml `[remote] token` を遠隔画面 (ビューアが
   最初に聞き、WS と /stream に添える) と dev_remote_ctl の全経路で必須に
   する。**mode = ap で token 未設定なら遠隔系を起動しない**。失敗 n 回で
   数十秒の締め出し。家の LAN でも有効にしてよく、無効化は設定で明示。
3. **STA を切る**: 出先は mode = ap。知らない LAN に HTTP 口を晒す経路を
   無くす。
4. 通信路の暗号化は WPA で担保し、https (自己署名) は後回し。やれば
   WebCodecs/H.264 も解禁されるが、P4 の TLS の RAM と iPhone の証明書
   警告を抱えるので必要になってから。
5. 機体内の API 鍵 (TTS/AI) は AP 単独ではインターネットに出ないので、
   漏れる経路は /fs/get だけ = 2 で塞がる。

**要確認 (順に潰す)**:

1. esp_wifi_remote (esp_hosted) で AP を張れるか。C6 のスレーブ FW 側は
   対応している。P4 側の API 経路と、AP+STA 同時のチャネル制約。
2. iPhone Safari で `<img>` の multipart MJPEG が 15fps で流れ続けるか、
   止まらないか (IP カメラでの実績はあるが、本機の送出形式で実測)。
3. AP の netif で mDNS が iPhone から引けるか。引けなければ
   `http://192.168.4.1/` を案内する。
4. 遠隔画面の入力遅延が「主画面」として耐えるか (Tab5 では補助だった)。
5. 電源: モバイルバッテリ給電で C6 の消費を含めた稼働時間。

**今日できる実験 (機体無改修)**: Tab5 か NARYA v4 を家の WiFi に置き、
iPhone の Safari で `http://<IP>/` を開く。MJPEG が出るか (要確認 2)、
タップが何も起こさないこと (touch 未対応の確認)、BT キーボードで打鍵が
届くかが、SoftAP と切り離して分かる。

### 3.7 ヘッドレス Family mruby の価値 (素の mruby / R2P2 との差、2026-08-31)

センサを読んで LED を光らせるだけなら R2P2 が正しく、4.9 の「末端子機は
R2P2」は変わらない。Family mruby である価値は**言語ではなく OS の部分**
にあり、3 つに絞れる。

1. **落ちない・止まらない箱**: 多重 VM でアプリが分離され、1 本が死んでも
   機体は生きる。サービスは自動で再 spawn し、kill/launch/一覧を遠隔で
   叩ける。R2P2 は 1 プログラムなので固まれば USB を抜くまで死んだまま。
   無人で何か月も置く箱の差はここに出る。
2. **同じアプリが画面の有無を問わず動く**: .app.rb がブラウザ (wasm)・
   sim・Tab5・ヘッドレスで同じ。ブラウザで書いて sim で試しヘッドレスに
   put する、が無改修で回る。
3. **画面はあとから着く**: ヘッドレス機の中でもアプリは窓を持って描いて
   いる。それを覗く・別の画面に出す (remote_window.md) ができる。
   **R2P2 には転送する窓が無い**ので、これが一番「Family mruby でなければ
   ならない」理由になる。

裏返すと、ヘッドレス機は「マイコン」ではなく**「デスクトップで管理する
小さなサーバ」**。GUI で面倒を見られることが、素の mruby との違いを
ユーザに見せる一番分かりやすい形。

窓の転送 (remote_window.md) は、この 3 の実体化として**ヘッドレス機の
最初の看板機能**に据える。実装は Tab5 → NARYA v4 の間で先に試せる。

### 3.8 Zenoh との関係 (1 案、2026-08-31)

どちらか無しでも成立するが、組むと箱の性格が変わる。**箱と窓の転送は
HTTP で先に動かし、zenoh 計画の段階 3 (agent worker) が来たときに箱を
最初の住処にする**、という順序の 1 案。

今の箱の外への口は HTTP だけ (dev_remote_ctl と遠隔画面) で、こちらから
IP か mDNS 名を知って取りに行く形、LAN の中に限られる。Zenoh を足すと:

1. **発見**: liveliness で、箱が親機の「隣の機体」一覧に IP 無しで現れる。
2. **押し出し**: 箱側で起きたこと (玄関が開いた、閾値を超えた) を聞いて
   いる相手に届けられる。HTTP では箱は答えるだけで自分から言えない。
3. **網越え**: 家の箱に出先のブラウザや別拠点から外の router 経由で届く。
4. **同じ言葉**: R2P2 の末端子機・PC の CRuby・ブラウザと同じキー空間に
   載り、箱だけ HTTP という別扱いが消える。

zenoh 計画 (zenoh_idea.md 5 章) の側から見ると、zenoh-agent は headless
worker アプリで、**ヘッドレス機では中身がほぼ全部ワーカーなので、
zenoh-agent はサービスホスト配下の普通のサービス** (自動再 spawn、
enable/disable) になる。一番自然な住処。

役割として務まるもの・務まらないもの:

- 務まらない: **router** (zenoh-pico は client/peer のみ)。ブラウザ参加・
  WAN・ブリッジ・ストレージが要る場面の router は PC のまま (3.2)。
- 務まる: **常時居るノード** = 実家ノード (liveliness の見張り、checkpoint
  の queryable、timesync、鍵置き場)、SD があれば記録係。キャリアに
  Ethernet PHY を載せれば無線の都合から独立して常時網に居られる。
- 務まる: **P4 での zenoh-pico の実証台**。P4 の実績はゼロ (段階 2 の最初の
  関門) で、ヘッドレス機は MCP で無人の長時間試験を回しやすいので、Tab5
  の次に置く実証台としてよい。

窓の転送 (remote_window.md) との関係: 最初は HTTP/WS だが、Zenoh に
載せ替えるとフレームを `/machines/x/apps/y/window/frame` に put し入力を
queryable で受ける形になり、**受け側は箱の場所を知らなくてよく、
ブラウザ (zenoh-ts) も受け側になれ、interest/matching で「誰も見ていない
窓は送らない」が自動で成立する**。箱は静かに動き、覗かれたときだけ絵を
出す。同じ理屈で dev_remote_ctl (ps/spawn/kill/put) を queryable にすると
`fmrb/*/app/launch` のワイルドカードで箱が何個あっても一斉に配れる
(zenoh_idea.md 4.6 のフリート配布)。

ユーザから見た絵: **ヘッドレス機 = 家の自動化エンジン、Zenoh = その神経**。
末端子機 (R2P2) は測って publish するだけ。箱は「22 時以降に玄関が開いたら
知らせる」「湿度が下がったら加湿器を入れる」のような規則を Ruby のアプリ
1 本ずつとして 24 時間走らせる (落ちても機体は生き、規則は個別に止め・
直せる)。親機と各画面には規則アプリの窓が転送で出る。書くのは親機の
エディタ、置くのは箱。Zenoh 無しの箱は「LAN の中の小さなサーバ」、
Zenoh ありの箱は「網の中の資源」。

## 4. 変わらないもの

契約 v1 の範囲 (Proc 転送なし・分散 GC なし)、ユーザ層は mruby アプリ
VM で下回りだけ C、遅延の床は `_spin` の周期、末端子機は R2P2 +
fmrb 非依存 gem。ここは今回の 3 件で揺れない。むしろ「同じ gem が
ブラウザでもヘッドレスでも動く」ことが fmrb 非依存の設計条件を強める。

## 5. 最初の一歩 (提案)

**ブラウザのタブ 2 枚 + PC の zenohd で、ペットがタブからタブへ
引っ越す**。実機ゼロで契約の要 (世代番号・ACK・Timeout) が全部通り、
そのまま NARYA v4・Retro・ヘッドレス機を足せば「机の上の星座」になる。
画面のある 2 台とブラウザに加えて、画面のない箱にペットが入り、
覗くと居る — 「オブジェクトはどこに存在するのか」の問いに、一番小さい
機体が答える形。

前段として 2.7 (ブラウザからの網への入り方) の小さな確認が要る。
ヘッドレス機はモジュールの仕様 (品名・flash・C6 の接続) が分かれば、
doc/naryav4/plan.md の P1 に相当する分岐の見積もりが出せる。
