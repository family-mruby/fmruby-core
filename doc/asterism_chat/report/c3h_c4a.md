# C3h・C4a 報告: チャットホストのスタンプと、VPS に置くための一式

> 状態: 完了 (mTLS のルータ・console の役割での通しは残り) | 更新: 2026-11-21 | asterism-chat 0.2.0 (手元のコミットだけ。push・タグ・GitHub のリポジトリなし)。スタンプのホスト側 (`post` の `stamp`、`stamp_has`・`stamp_put`・`stamp_get`、置き場と上限) と、`deploy/vps/`・`docs/install.md`・CI のファイル。ローカルのルータにつないだコンテナで、文字とスタンプ・healthy・SIGTERM・作り直しの後の残りを確認

サブの最後の返事を親がまとめた (サブは gem の中だけを触った)。細部の正は gem の `README.md`・`docs/install.md`・`CHANGELOG.md`。

## 1. 足した線の形 (端末の側の C3 が読む)

オブジェクト `<host>/chat/room`。断るときは `{"error", "message"}` (文は端末にそのまま出せる日本語)。

| メソッド | 引数 | 答え |
|---|---|---|
| `post` | `{"room", "stamp" => sha, "cid"}` (`text` と `stamp` のどちらか一方) | `{"room","n","t"}` |
| `stamp_has` | `{"sha"}` | `{"sha","has"}` |
| `stamp_put` | `{"sha","png" => バイト列 (str でも bin でも)}` | `{"sha","bytes","new"}` (`new` が false なら既にあった) |
| `stamp_get` | `{"sha","as" => "str"\|"bin"}` (`as` は省ける。既定 str) | `{"sha","png"}` |

- sha は sha256 の 16 進 64 桁 (小文字にそろえる)。スタンプの発言は `{"n","t","node","name","text" => "","stamp" => sha}`。
  文字の発言には `stamp` の鍵が無い。
- 送る流れ: `post` が `no_stamp` なら `stamp_put` してから同じ `post`。1 日の発言の数は文字と共通。
- `hello` の答えに `"stamp_px" => 48`・`"stamp_bytes" => 8192` を足した。
- 誤りの種類 (足した分): `no_stamp`・`too_big` (8 KB か 48x48 を超える)・`bad_stamp` (端末が読める PNG でない)・
  `bad_sha` (中身と sha が合わない)・`stamp_limit` (1 日に置ける新しい絵)・`stamp_full` (置き場が指されている絵で一杯)。
- PNG の検査 (解かない): 先頭の印、IHDR (長さ・CRC)、幅と高さ 1〜48、インターレースなし、色の形は端末の png332.c が
  読めるもの (グレー・パレットは 1/2/4/8 ビット、RGB・グレー+α・RGBA は 8 ビット)。

## 2. 上限の既定

- 1 枚: 48x48 以下・8 KB 以下 (定数。`hello` で伝える)。ホストが持つ絵: 2,000 枚・16 MB。1 端末が 1 日に置ける新しい絵: 30 枚。
- 捨て方: 上限を超えたら、残っている発言のどれからも指されていない絵を古い順に捨てる。収まらなければ `stamp_full`。

## 3. 役割の行 (console。tutor-host・tutor の行を写したもの。**ルータではまだ試していない**)

- **chat-host** (台帳の行 `chat`、kind client): Read and write `asterism/${node}`・`asterism/${node}/chat/**`・
  `asterism/${node}/@profile`・`asterism/${node}/chat/@profile`・`@status`・`@pubs`。Watch `asterism/**`。ブラウザには付けない。
- **chat** (使う端末): Read and write は端末自身の `asterism/${node}`・`asterism/${node}/chat/**`・`@profile` 類。
  Read `asterism/*/chat/room`・`asterism/*/chat/protocol/*`・`asterism/*/chat/room/meta`・`asterism/*/chat/room/call/${node}`・
  `asterism/*/chat/news` (推測の行)。Ask `asterism/*/chat/room/meta`・`asterism/*/chat/room/call/${node}`。Watch `asterism/**`。
  「Browsers have this role」は off (ブラウザは利用者ごと)。

## 4. 配置の一式と、手元で確かめたこと

- `deploy/vps/`: Dockerfile (`ruby:3.4.13-slim`、uid 1000、199 MB)、compose (project `asterism-chat`、`restart: unless-stopped`、
  healthcheck `asterism-chat status`、volume `state`、`mem_limit` 192m)、`env.example`、`update.sh` (状態の問い合わせは `</dev/null`。
  `--when-idle` は無し: 進行中の仕事が無い)、`credentials/`。`.github/workflows/image.yml` は置いただけ。
- 手元: イメージを作り、自分の network の zenohd につないで `update.sh --build 0.2.0` → up・healthy。送り手 2 つで文字とスタンプ
  (`no_stamp` → put → post、別の送り手が同じ絵を取る)。ルータを止めると status は 1、戻すと約 6 秒で 0。SIGTERM で終了コード 0。
  volume を残して作り直すと履歴・名前・絵が残る。gem の RSS は約 50 MB。
- `bundle exec rake test`: 部品 25 件・通し 6 件、通った (親も流し直した)。

## 5. 残り・決めること

- mTLS のルータと console の役割の行での通し (C3 の通しか C4 で。console の rehearsal を使うか、VPS で初めて確かめるか)。
- `stamp_get` の既定の形 (str のまま。端末の C3 で bin が都合よければ設定か `"as"` で切り替え)。
- 本物の端末 (mruby) から str・bin でスタンプを送る所 (C3)。絵が 2,000 枚あるときの起動の時間とメモリ。
- gem のコミット: `7ced21f` `de982ad` `f149b76` `45b316d` `e67e413`。
