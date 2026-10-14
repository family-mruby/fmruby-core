# G2 ステップ 2 報告: 部屋 (署名つきの鍵で入る、期限つきの名前空間)

> 状態: 完了 (検収待ち) | 更新: 2026-10-14 | instruction_g2_step2.md の実装と確認の記録。asterism-console 0.2.0 (部屋の画面・枠・signer の署名・door の部屋の口)、picoruby-asterism-zenoh の名前空間、ブラウザ版の部屋のリンクを作った。手元の構成 (deploy/local、nginx・door・signer・家のルータを含む) で、管理者が作った部屋の中で 2 つのブラウザがオセロを 10 手指し、別の部屋・登録済みの側 (家のルータの奥の端末役・証明書の CRuby・合言葉のブラウザ版) とはどちら向きにも何も届かないことを確認した。今の役割 (match・Fill in のひな形) では advanced pub/sub の履歴が届かないことも確認した。止まる条件には当たっていない

## 1. 作ったもの

| リポジトリ | ブランチ | コミット | 中身 |
|---|---|---|---|
| picoruby-asterism-zenoh | main | 16f9773 | 名前空間 (キーの前置きの付け外し)。版は 0.4.1 のまま |
| asterism-console | main | 003e869、1e21f56 (文書の直し) | 0.2.0: 部屋の画面、枠、部屋の鍵の署名、door の部屋の口、配置一式と文書 |
| fmruby-core | feature/asterism-g2-rooms (develop から) | (この報告と同じコミット) | ブラウザ版の部屋のリンク、browser.md、plan.md、この報告 |
| asterism | - | - | 変えていない (CRuby の部屋の係は `config: {"namespace" => ...}` で今のまま入れる) |

push・タグはしていない。

### 1.1 asterism-console (0.2.0)

- **部屋の鍵** (`lib/relay/room_token.rb`): `base64url(中身).base64url(Ed25519 の署名)`。中身は `v`・`kid` (署名の鍵の印)・`room` (部屋の ID、16 進 16 桁の乱数)・`slot`・`exp`・`max`・`jti` (UUID)。役割は入れない。長さ 275 バイト、リンク全体 (オセロを開くもの) で 384 バイト。
- **signer**: 部屋の鍵の署名の秘密鍵 (Ed25519) を signer の volume (`/ca/ca/room_keys/`、0600) に置く。`POST /room-token` (24 時間より先の期限は断る)、`GET /room-keys` (公開鍵と kid)、`POST /room-keys` (新しい鍵。古い鍵の署名も確かめられるまま)。署名の記録 `room_tokens.log` に鍵そのものは書かない。console は鍵を保存しない。
- **枠**: `bin/asterism setup` が `ASTERISM_ROOM_SLOTS` (8) 個の証明書 `room-01` … `room-08` を作る (台帳の行に「room slot」の印、鍵はサーバで作る。ルータ・bridge の証明書と同じ作り)。置き場は volume `rooms` の `certs/`。部屋の係 (CRuby) 用に `room-NN.tar` (console の「Save all (.tar)」と同じ形、`zenoh.json5` に `namespace`) も置く。
- **ACL** (`Relay::CloudConfig`): 枠の subject に `room/<枠>/**` と `room/<枠>/**/@adv/**` だけを許す。zenohd の `max_sessions` の既定を 96 に。
- **画面** (Admin > Rooms): 部屋を作る (名前・開くアプリ・時間 (既定 3、上限 24)・接続の上限 (上限 16))、リンクは 1 回だけ表示 (`ASTERISM_ROOM_PAGE`、鍵はフラグメント)、別のリンク、接続数 (door から)、閉じる。空いた枠のうち、前の部屋が最も前に終わった枠を割り当てる。変更のたびに door の読むファイル `rooms.json` (公開鍵・開いている部屋と枠・取り消した部屋) を書き直す。
- **door** (`bin/ws_door` に部屋の口 7452、`lib/relay/room_door.rb`): 要求を読み、鍵 (署名・期限・部屋がその枠にいるか・取り消し) と上限を確かめてから初めて zenohd に繋ぐ。枠の証明書で `tls/` に mTLS、101 を自分で返し、ws の 1 通と TLS の 2 バイトの長さの枠を詰め替え、最初の InitAck の batch_size を 2 減らす。上限: 全体 48・部屋ごと 16 (部屋の上限が小さければそれ)・1 通 65535 バイト・要求の頭は 5 秒 8 KB 以内・確かめ中は 64 まで。署名の正しくない鍵は住所ごとに数え、1 分に 30 を超えたら 1 分間 429 (正しい鍵は通す)。鍵の期限・部屋の取り消し・枠が別の部屋に移ったときに接続を切る (枠を次の部屋に渡すときは、新しい部屋の最初の接続の前に前の部屋の接続を切る)。数を状態の口 (7453) で console に返す。鍵は記録しない。
- **配置**: compose (door の部屋の口・thread の stack 128 KB と `MALLOC_ARENA_MAX=2`・volume `rooms`)、nginx の `location = /room` (認証の問い合わせなし、住所ごとの頻度 60/分と接続 32)、Caddy の例、env.example、docs/install.md (部屋の節・0.1.x からの更新)、docs/rooms.md (仕組み、英語)。deploy/local の通し稽古も部屋のリンクの設定を持つ。
- 試験 369 件 (部屋の分: 鍵 3、door 9 (zenohd 役の TLS の相手が数える接続とバイト)、signer 1、CloudConfig 1、Room 3、画面 2)、rubocop 0 件。

### 1.2 picoruby-asterism-zenoh

- `Session.open(..., namespace: "room/01")` (`config: {"namespace" => ...}` も可)。渡さないときは弱いシンボル `picoruby_zenoh_default_namespace()` の値 (既定は無し)。`namespace: ""` は無し。`Session#namespace`。
- 出すキー 7 か所 (put・subscribe・liveliness_watch・queryable・get・liveliness・liveliness_get、キーを渡す reply) に `<前置き>/` を付け、受けるもの 4 か所 (購読と生存の印・get の返事・queryable に渡す問い合わせ・Query#key) から外す。前置きの外は捨てて `dropped` に数える。線の上は zenoh-c の `namespace` と同じ形。
- 名前空間を使わないときの内蔵 RAM の増分は無し (静的な領域を足していない)。`rake test` 130 項目 (名前空間 47)、`rake test_reconnect` 通過。

### 1.3 ブラウザ版 (fmruby-core)

- `#room=<鍵>` をフラグメントから読み (合言葉と同じく、すぐアドレスの欄から消し、保存しない)、鍵の中身から枠と期限を読む (署名は確かめない)。`Module.zenohRoom` で `wss://<host>:<port>/room?t=<鍵>` へ (wss のときだけ)、`Module.zenohNamespace` = `room/<枠>` を `picoruby_zenoh_default_namespace()` で binding に渡す。アプリは変えていない。
- 画面: Network の枠の見出しの下に `Room 01 until <時刻>: keys under room/01/, ...`、状態の行に `running in room 01 (until ...)`。期限の過ぎた鍵・読めない鍵では起動しない。つなぎ直しが 10 回続けて失敗すると「the room may have closed or ended」。
- **ノードの ID (決めたこと)**: 部屋ではブラウザ自身の `wasm-<16 進 6 桁>` (保存してある ID、2 つ目のタブは使い捨て) を使い、リンクの `zenoh_node` は使わない。理由: 部屋にはアカウントが無く、全員が同じ鍵を持つので、console が ID を決めても守れない。ID は部屋の中で重ならなければよく、キーには `room/<枠>` が付くので console の網の `wasm-<ハンドル>` とはぶつからない。console のリンクの決まり (合言葉にはノードの ID が要る) はそのまま。browser.md 5.2・5.4 に書いた。
- `rake wasm:mruby wasm:mrb wasm:web wasm:dist` 通過 (picoruby-asterism-zenoh はこの作業の main を `PICORUBY_ASTERISM_ZENOH_DIR` で使った)。

## 2. 受け入れ条件ごとの結果

確認の構成: asterism-console の deploy/local (nginx 1.27・door・zenohd 1.10.1・console・bridge・signer・家のルータ `home-a`)。image は作業中の console から作った試し用。ブラウザは docker の Playwright 1.56 (Chromium)。ブラウザ版はこのブランチの `rake wasm:dist`。管理者は 172.31.240.50 から TOTP つきでサインイン。

| # | 条件 | 結果 |
|---|---|---|
| 1 | 部屋を作り、リンクを 2 つのブラウザで開いてオセロ。別の部屋・登録済みの端末とは両向きに何も届かない | 合格。管理者が画面で部屋 A (枠 01)・B (枠 02) を作り、A のリンクを 2 つのブラウザ (`wasm-814453`・`wasm-8ba2ac`) で開いてオセロを 10 手 (`d3c3b3b2b1a1c4c1c2d2`、最後の手の答え 54 ms)。A のロビーには A の相手だけ、B のブラウザは相手なし。部屋の中の見張り (枠の .tar と名前空間の CRuby) は A で A の 2 人だけ、B で B の 1 人だけを見た。登録済みの側 (家のルータの奥の CRuby `fmruby-p4`、mTLS の CRuby `alice`、合言葉のブラウザ版 `wasm-kaede`) は互いには見え、部屋のものは何も受けず (`room/**`・`room/01/**` の購読、`room/**` への get と生存の印の問い合わせ、どれも 0 件)、`room/01/...`・`room/02/...` へ書き込もうとした分 (2 秒ごと 150 秒) は部屋の見張りに 1 件も届かなかった |
| 2 | 期限切れ・書き換えた鍵・取り消した部屋は入れない。取り消すと切れ、つなぎ直しも断る。枠の使い回しで前の部屋の接続が残らない | 合格。45 秒の鍵のページはその時刻に door が切り (`its key has ended`)、つなぎ直しは 410。期限の過ぎたリンクはページが起動しない。署名を 1 文字書き換えた鍵は 403。2 人が入った部屋 A を画面で閉じると 1 秒以内に 2 本とも切れ、つなぎ直しは 410。全部の枠を埋めてから A を閉じると、次の部屋 C は枠 01 に入り、door の数は枠 01 = C の 1 本だけ。単体の試験で、枠を移したときに前の部屋の接続が新しい部屋の最初の接続の前に切れることも確かめた |
| 3 | door は鍵を確かめる前に zenohd へ 1 バイトも渡さない。上限 (16・48・1 通の大きさ) が効く | 合格。単体の試験で、鍵なし・壊れた鍵・書き換え・別の署名・期限切れ・上限を超える期限・頭の無い接続・違うパスのどれでも、zenohd 役の接続 0・バイト 0。1 通 65536 バイトで切れる。通し稽古で、部屋 C に 17 本目 → 503 (`room is full (16)`)、3 部屋 × 16 = 48 本のあとの 49 本目 → 503 (`the door is full (48)`) |
| 4 | 今の仕組み (ハンドルのパスのブラウザ版、mTLS の端末) がそのまま動く | 合格。console の「Open the browser build」(ハンドル `kaede`、オセロ) で `wss://router.localtest:7443/?pass=***&page=***` に入り `wasm-kaede` で動いた。家のルータの奥の CRuby と mTLS の CRuby も今どおり |
| 5 | console・asterism の試験、rubocop、picoruby-asterism-zenoh の試験、wasm のビルド、sim の標準構成の起動 | 合格。console 369 件 失敗 0、rubocop 0 件。asterism `rake test` 通過 (適合の試験 CRuby 194 通過)。picoruby-asterism-zenoh `rake test` 130 項目・`test_reconnect`。wasm のビルド通過。sim は標準構成 (TAB5 426x240) で起動し、エディタを開いて 1 打鍵。`build/fmruby-core.elf` は x86-64 |
| 6 | 測る | 3 章 |

console の試験は、作業の最初に 1 回だけ、変更前の版で 1 件失敗した (再実行で通過。中身は確かめていない)。sim のエラー行は 1 件 (エディタを違うパスで起動しようとした 1 回目の分と見ているが、行は確かめていない)。

## 3. 測った値 (手元の WSL2、通し稽古)

- **door を通る遅れ** (CRuby 2 本の往復、300 回): zenohd の `tls/` に枠の証明書で直接 中央値 1.05 ms (90% 1.30 ms)、door 経由 (試し用の中継の container → nginx の TLS → door → zenohd) 中央値 1.54-1.55 ms (90% 1.81 ms)。差 0.5 ms には試し用の中継 1 段と nginx が入っている (ステップ 1 の door だけの差は 0.25 ms)。
- **1 接続のメモリ** (door の container): 接続なし 16 MB、48 本 何もしない 28.6 MB (1 本 0.26 MB)、48 本 通信中 (1 本 1 KB を 0.2 秒ごと) 45 MB (1 本 0.6 MB)。終わると thread は 4 本に戻った (RSS は 51 MB のまま。malloc が返さない)。
- **16 接続の部屋が 3 つのときの VPS 相当**: door 45 MB、zenohd 7-8 MB、nginx 30-34 MB (手元の nginx は CPU 24 個分の worker。VPS は少ない)。console・bridge・signer・backup は部屋で増えない (合わせて約 260 MB)。VPS の空き約 390 MB (ステップ 1) に収まる。door の CPU は通信中 32% (手元の 1 コア換算)。

## 4. `@adv` の確認 (今の役割)

zenohd 1.10.1 を mTLS と `Relay::CloudConfig` の ACL で立て、2 つの CRuby (役割だけを持つ) で、cache つきの advanced publisher に 2 回 put したあとに遅れて来た advanced subscriber (history) が受け取るかを見た (試しの台本 `adv_check.rb`)。

| 役割 | 履歴 | その後の put | 普通の put |
|---|---|---|---|
| match (seed のまま) | **0 件** | 届く | 届く |
| Fill in のひな形 (アプリ demo) | **0 件** | 届く | 届く |
| match + `@adv` の 4 行 | 2 件 | 届く | 届く |
| demo + `@adv` の 4 行 | 2 件 | 届く | 届く |
| match + `@adv`、rw の行を抜く / read を抜く / ask を抜く | 0 件 | 届く | 届く |
| match + `@adv`、watch の行を抜く | 2 件 | 届く | 届く |

穴はある (遅れて来た人に履歴が届かない。普通の put と生存の印は今どおり)。直すには 3 行が要る (`<app>` はアプリ名):

```
Read and write:  asterism/${node}/<app>/**/@adv/**
Read:            asterism/*/<app>/**/@adv/**
Ask:             asterism/*/<app>/**/@adv/**
```

- Fill in のひな形: `role_form_controller.js` の rw・read・ask に上の行を足す (watch は履歴には要らなかった。publisher_detection で後から来た publisher を見つける働きは確かめていない)。
- 既存の役割の直し方: Admin > Relay > Roles で match (と Fill in で作った役割) の 3 つの欄に上の行を足し、Apply。match なら `<app>` は `game`。
- 直すかはユーザ。今の端末のアプリ (オセロなど) は advanced pub/sub を使っていないので、今は困らない。部屋の ACL には最初から入れた。

## 5. ユーザがすること

1. **push とタグ**: picoruby-asterism-zenoh の main (16f9773)、asterism-console の main (003e869・1e21f56) を push。console は `v0.2.0` のタグで image を作る。fmruby-core はこのブランチを develop へ。
2. **ピンを上げる**: picoruby-asterism-zenoh の push の後、fmruby-core の `lib/add/PICORUBY_ASTERISM_ZENOH_PIN` を 16f9773 に上げる。それまでは既定のピンでビルドするとブラウザ版が部屋に入れない (前置きが付かない。ACL が断るので部屋の外へは漏れない)。ビルドは通る (弱いシンボル)。
3. **ブラウザ版の置き場を作り直す** (`rake wasm:dist` / `wasm:site`)。
4. **VPS の更新** (docs/install.md の 15・20 章):
   - `.env` に部屋の行を足す (`ASTERISM_ROOM_PAGE`・`ASTERISM_ROOM_SLOTS`・`ASTERISM_ROOM_DEFAULT_HOURS`・`ASTERISM_ROOM_MAX_HOURS`・`ASTERISM_ROOM_MAX_CONNECTIONS`・`WS_DOOR_ROOM_TOTAL`・`WS_DOOR_ROOM_PER_ROOM`・`ROOM_DOOR_PORT`)、`ASTERISM_RELAY_MAX_SESSIONS=96`。
   - nginx: `asterism-http.conf` に `asterism_room`・`asterism_room_conn` の 2 行、`asterism-upstreams.conf` に `asterism_room_door` (127.0.0.1:7452)、`asterism-router.conf` に `location = /room`。`nginx -t` と reload。
   - `./update.sh 0.2.0` (setup が枠の証明書 8 つと署名の鍵を作る)。そのあと **Apply** (枠が ACL に入り、max_sessions 96 が効く)。
   - パケットフィルタと `asterism-door-allow.conf` は今の住所の制限のまま `/room` にも効く。知り合いが外から入るには `/room` の location に `allow all;` を足し、7443 のパケットフィルタを開ける (残る G2 の項目「443 番へ移す」と合わせて決める)。
5. **部屋の作り方**: Admin > Rooms で名前・開くアプリ (例 `/app/test/othello.app.rb`)・時間・接続の数を入れて Make the room。出たリンクを渡す (1 回だけ表示。要るときは Make another link)。終わったら Close the room。
6. `@adv` の直し (4 章) をするか決める。

## 6. 残った課題

- 部屋の中では全員が同じ鍵なので、他人の ID を名乗る・他人のキーに書くことは防げない (ステップ 1 の 5.4 のとおり、部屋の係の守りに任せる)。
- nginx の住所ごとの接続 32 は「1 つの住所の後ろに 2 部屋」の見積もり。イベントで多い場合は上げる。
- door の RSS は使った後に下がらない (malloc)。長く動かしたときの増え方は見ていない。
- ゲームホスト (枠の .tar と `config: {"namespace" => ...}` で直接入る形) は、部屋の見張りの CRuby で入れることだけ確かめた。係のアプリは次のステップ。
- 1 つの部屋の鍵は作ったときの期限で、部屋の期限の延長は無い (新しい部屋を作る)。
- 公開の入口 (443 番・サブドメイン・パケットフィルタ) とサーバ越しの G-RT は、残る G2 の項目のまま。

## 7. 試したもの

`C:\Users\Public\asterism_deploy\g2_step2\` (コミットしていない。鍵・証明書・トークンは置いていない): 管理者の画面の操作 (`admin.js`)、3 つのページとオセロ (`players.js`)、ページを開いて見張る (`pages.js`)、合言葉のブラウザ版 (`passpage.js`)、登録済みの側の見張り (`registered.rb`)、部屋の中の見張り (`room_observer.rb`)、負荷 (`load.rb`・`wsproxy.rb`)、往復 (`rtt.rb`)、`@adv` (`adv_check.rb`)、画面 (`screenshots/`)。

片付け: 通し稽古は volume ごと消し、`deploy/local/.env`・`certs/`、試し用の image、中継の container、Playwright の container を消した。sim は落とした。VPS・`~/asterism-home`・端末・`.env`・sdkconfig には触れていない。
