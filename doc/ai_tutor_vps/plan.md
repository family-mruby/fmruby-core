# AI の係 (asterism-tutor) を VPS のコンテナで動かす

> 状態: 進行中 | 更新: 2026-11-19 | asterism-tutor を、Asterism console の隣の Docker コンテナとして VPS で常時動かす (麻雀のゲームホスト asterism-games と同じ流儀)。V0〜V2 済 (asterism-tutor 0.2.0 と console 0.4.2 を公開、VPS の手順と scripts を用意)。次は V3 (VPS に入れる。ユーザの作業)

もとの企画は `doc/ai_coding/` (端末の AI ヘルパーと、親の PC で動く gem)。ここは、その gem を
親の PC の代わりに VPS で動かす段だけを扱う。手本は asterism-games の `deploy/vps/` と `docs/install.md`。

## 1. 目的

- 親の PC で gem を起こしておかなくても、端末 (P4-Nano・Tab5) の AI ヘルパーが使えるようにする。
- 端末は、ふだんの Asterism の接続先 (VPS のルータ) のまま使える。今の試験のように、端末の接続先を
  手元のルータへ書き換えたり、PC のファイアウォールに穴を開けたりしない。

## 2. 決定 (2026-11-19 ユーザ、選択形式)

1. **リポジトリ `ruby-asterism/asterism-tutor` を公開し、イメージは ghcr に置く**
   (`ghcr.io/ruby-asterism/asterism-tutor:<版>`。タグ `v<版>` で CI が作る。asterism-games と同じ)。
2. **使えるのは名指しした端末だけ**。console で AI 用の役割を作り、付けた端末だけが呼べる
   (mTLS のルータの ACL で拘束)。gem の `network.devices` にも同じノード ID を書く。
3. **記録は今と同じく全部残す** (頼み・送った文書・LLM への指示・答え・使用量と費用・検査の結果)。
   VPS の volume に JSONL で置き、gem は消さない。
4. **最初のモデルと上限は gem の既定のまま**: `claude-haiku-5-5`、端末ごとに 1 日 50 回、全体で 1 日 1 ドル。
   `.env` で変えられる。

5. **LLM の鍵は tutor 用に別に作る** (ゲームホストの鍵と分ける。費用を分けて見られ、片方だけ止められる。
   鍵を作るのはユーザ)。
6. **README は英語にし、今の日本語は `README.ja.md` に移す**。AI の決まり (`data/rules_ja.md`) と
   知識の束は日本語のまま。
7. **記録は console の毎日のバックアップに入れない** (tutor の volume にだけ置く。写しを増やさない)。
8. **ブラウザ版の端末も、役割を付ければ使える**ようにする (利用者ごとの証明書に役割 tutor)。
   ブラウザ版の AI ヘルパーでも通しを確認する。

9. **Linux のネイティブ版の端末でも使えるようにしておく** (2026-11-19 ユーザ)。ESP32・ブラウザ版と同じく、
   役割を付けた端末として VPS の係を呼べること。通しを 1 回確認する。

10. **ブラウザ版は、console を直して利用者ごとに役割を選べるようにする** (2026-11-19 ユーザ。V1 で、今の
    console はブラウザ向けの役割を利用者ごとに選べず、共有の ws のページは他のブラウザの名前で呼べると
    分かったため)。console のコードの変更とリリースが要る (段 V1c)。
11. **1 日の区切りは日本時間の 0 時** (コンテナの `TZ=Asia/Tokyo`。ゲームホストと同じ)。
12. **係を止めたせいで失敗になった頼みは、1 日の回数に数えない**。
13. ブラウザ版で AI ヘルパーがつながるまで約 20 秒待つ件は、課題に残す (doc/reference/TODO.md)。

## 3. 方針

### 3.1 構成

```
端末 (AI ヘルパー) --tls--> VPS の zenohd <--tls/7448-- tutor のコンテナ --https--> LLM の API
                            (console の compose project)   (自分の compose project "asterism-tutor")
```

- compose project は console とも asterism-games とも別にする (互いの update.sh が相手に触らない)。
  console の project の network `<project>_router` に、名前で入る。外へ開く port は無い。
- VPS の置き場は `/opt/docker-compose-service/asterism/asterism-tutor` の見込み (ゲームホストの隣。
  起こし方は今ある systemd の包みに合わせる。細部は V2 で VPS の今の形を見て決める)。
- ルータへは、console が出す tutor 用の証明書 (.tar。台帳の行 `tutor`) でつなぐ。gem はもう
  `network.credentials` で .tar を読める。`credentials/` を読み取り専用で mount する (uid 1000、600)。
- LLM の鍵は、VPS の `deploy/vps/.env` の `ANTHROPIC_API_KEY` にだけ置く (ゲームホストの AI の打ち手と同じ
  置き方。イメージ・リポジトリ・記録には入らない)。ゲームホストの鍵とは別の鍵。
- volume を 1 つ持つ: 記録 (JSONL)、取りに来ていない結果、その日の回数と費用。コンテナを作り直しても
  上限の数えが 0 に戻らないこと (今の gem がどこまでファイルに持っているかは V1 で確認する)。

### 3.2 gem の側で足すもの (asterism-tutor)

- `deploy/vps/`: `Dockerfile`・`docker-compose.yml`・`docker-compose.build.yml` (その場で作る用)・
  `env.example`・`update.sh`・`README.md`・`credentials/.gitignore`。
- 設定を環境変数でも渡せるようにする (`.env` だけで動く)。ファイル (`tutor.toml`) とオプションは今のまま。
  対象: ルータ、証明書、ノード ID、端末の一覧、LLM の種類とモデル、上限 2 つ、記録と状態の置き場、
  「作成」「修正してもらう」を止める。
- 動いているかの確認 (healthcheck): ルータにつながり helper を公開できている、を返す口
  (`asterism-tutor status` のような形。作りは V1 で決める)。
- SIGTERM で、進行中の頼みの扱いを決めて終わる (結果を volume に書いてから出る、か、打ち切って端末に
  理由を返す)。メモリの上限 (`mem_limit`) は実測して決める。
- `.github/workflows/image.yml` (タグ `v*` で amd64 のイメージを ghcr へ。作業ブランチには CI を足さない)。
- `LICENSE` (MIT。ほかの Asterism のリポジトリと同じ)、`CHANGELOG.md`、README に VPS での動かし方。
- 知識の束は今のままイメージに入れる (`bundles/3.1.0`)。束の更新 = イメージの版を上げる。

### 3.3 console の側 (画面の操作。コードは変えない見込み)

- 台帳に行 `tutor` を足し、証明書 (.tar) を出す。
- 役割を 2 つ作る:
  - **tutor-host** (係の側): 自分のノードの下の公開 (`asterism/${node}/tutor/**`) と、要る読み。
  - **tutor** (使う端末の側): 係の helper を呼べる。使わせる端末にだけ付ける。
- 鍵式 (ACL の行) は、今の gem と端末が実際に使う鍵を V1 で洗って決める。console のコードの変更が要ると
  分かったら、止めてユーザに返す。

### 3.4 公開の前の確認 (V0)

- 履歴と木に、実在の値 (LAN の IP、端末の ID、ローカルの絶対パス、ドメイン、鍵) が無いこと。
  2026-11-19 に履歴を grep して、当たったのは仮の端末 ID (`fmruby-aaa001` など) だけ。公開の直前にもう一度
  見る。コミットの著者はほかの公開リポジトリと同じ。
- 公開リポジトリのコメントは ASCII だけ、の決まりとの関係: 今の gem はコメントが英語、README と
  `data/rules_ja.md`・束の文書が日本語。README は英語にして `README.ja.md` を足す (2 章の 6)。
- 束に入っている見本・ひな形・書き方の文書は fmruby-core 由来 (公開済みのもの) であることを確認する。
- **公開の切り替え (GitHub の設定) は、ユーザの明示の指示で行う**。

## 4. 段

| 段 | 中身 | 場所 | 誰が |
|---|---|---|---|
| V0 (済 2026-11-19、公開済み) | 公開の準備: LICENSE、README、CHANGELOG、履歴の確認 | asterism-tutor | サブ + 親。公開はユーザの指示で |
| V1 | コンテナで動く形: 環境変数の設定、状態の volume、status、SIGTERM、`deploy/vps/` 一式、image の CI。手元で、console の手元の stack (mTLS のルータ) の隣に起こし、sim の AI ヘルパーから通す (偽物の LLM)。役割の鍵式を決める | asterism-tutor | サブ (Opus)、親が検収 |
| V1 (済 2026-11-19) | 上の V1。report/v1.md。asterism-tutor のブランチ `vps-container` (0.2.0、未マージ) | | |
| V1b (済 2026-11-19、report/v1b.md) | 決定 11・12 を gem に入れる (TZ、止めた失敗を数えない) | asterism-tutor | サブ、親が検収 |
| V1c (済 2026-11-19、report/v1c.md、console 0.4.2) | console: ブラウザの利用者ごとに役割を選べるようにする (決定 10)。指示書は別に書く | asterism-console | サブ、親が検収。リリースはユーザの指示で |
| V2 (済 2026-11-19。手順書と scripts は公開しない置き場に) | VPS の手順と scripts (RUNBOOK。置き場はこれまでの `asterism_deploy` の下)。console の役割と台帳の行、.env、credentials、起こす・更新・戻す・記録の読み方 | 手順書 | 親 |
| V3 | VPS に入れる: タグを打って image を作る、console の画面の操作、scripts の実行 | VPS | ユーザ (sudo の要らない script は、その回の合意があれば親) |
| V4 | 通し: P4-Nano (接続先は VPS のまま) から 質問・ヒント・作成・修正。上限・役割の無い端末が断られること。記録が残ること。ブラウザ版・ネイティブ版からも 1 回ずつ | 端末 + VPS | 親が遠隔で、目視はユーザ |

## 5. 受け入れ条件

1. 手元: `deploy/vps` の compose で起こしたコンテナが、console の手元のルータに証明書でつながり、
   sim の AI ヘルパーから 質問・ヒント・作成・修正 が通る (偽物の LLM)。gem の試験が通る。
2. 役割 tutor の無い端末からは呼べない (ACL で届かない)。`devices` に無い ID は gem が断る。
3. コンテナを作り直しても、記録・取りに来ていない結果・その日の回数と費用が残る。
4. healthcheck が、ルータにつながっていないときに unhealthy になる。
5. タグを打つと ghcr にイメージができ、VPS の `update.sh` でその版に上がる。
6. VPS: P4-Nano から本物の LLM (Haiku) で 4 つの頼みが通り、VPS の記録に 1 件ずつ残る。
   1 日の上限を小さくして、超えたときの理由が端末に出る。
7. 鍵が、イメージ・リポジトリ・記録・ログのどこにも出ていない。

## 6. 未確定事項

- 親が記録を読む道具 (今は ssh で JSONL を読むだけ。console の画面に出すのは別の企画)。
- ブラウザ版・ネイティブ版で AI ヘルパーが今どこまで動くか (アプリが束に入っているか、エディタの口、Asterism の
  経路) は未確認。V1 で調べ、端末の側の直しが要るなら段を分けてユーザに返す。
- 親の PC で動かす形 (今の試験の形) を残すか。残す (gem の機能は変えない) 見立て。
- gem として rubygems に出すか (今は出さない見立て。イメージだけ)。
- 束の版 (`3.1.0`) と端末のファームの版がずれたときの扱い (`doc/ai_coding` の残件)。

## 7. 触らないもの

- 端末の側 (AI ヘルパー・エディタ・カーネル) は変えない見込み。変更が要ると分かったら別の段にする。
- VPS のほかのサービス、console のコード、ルータの設定の作り。VPS のデータは消さない。
- 公開リポジトリの文書に、VPS のドメイン・IP・端末の ID を書かない (`router.example.org` などの仮の名前)。
