# Phase B3 ブリングアップ進捗

対象作品ごとの症状と対応を記録する。**作品コードは書かない** (行番号と
症状の記述にとどめる)。ユーザ供給のターゲット作品リストが届くまでは、
`components/basic/test/samples/sample_1x` の自作 L3 サンプル 5 本を
代替コーパスとして使う (samples/README.md 参照)。

## サンプル (代替コーパス)

| 作品 | 状態 | 備考 |
|---|---|---|
| `sample_10_dodge` | 動く | DEF MOVE の再定義 + POSITION で操作する定型、CRASH、ERA、STICK、スコア表示まで確認 |
| `sample_11_shoot` | 動く | MOVE 3 本同時、STRIG 発射、CRASH ペア、CAN、起動時 PLAY を確認 |
| `sample_12_maze` | 動く | SCR$ による当たり判定、COLOR、カナ表示、ランダム壁生成を確認 |
| `sample_13_music` | 動く | PLAY 3 チャンネル (T/M/Y/V/O/R) の FMSQ 変換・転送・再生と画面アニメの並行動作を確認。**音の官能確認はユーザ待ち** |
| `sample_14_hit` | 動く | RND、PAUSE、INKEY$、カナ、CHR$(48+N) の一致判定を確認 |

「動く」= linux sim で BASIC エラーなしに起動し、画面が期待どおり描画され、
入力注入に反応するところまで。ゲームとしての手触り (難易度・当たり判定の
気持ちよさ) と音はユーザ確認。

## ブリングアップで見つけて直したもの

| # | 症状 | 原因 | 対応 |
|---|---|---|---|
| 1 | サンプルのカナが画面上ですべて `?` になる | 文字コード表 (表 B) にひらがなも長音符も無く、変換で置換文字になっていた | ロード時にひらがな -> カタカナへ畳み、長音符は `-` に置換 (B3 report sec 8 #22) |
| 2 | 2 本目以降のサンプルが `Failed to create BASIC state` で起動しない | アプリを kill するとタスクのメモリプールが解放されない (BASIC 固有ではなくアプリ基盤側の挙動) | ブリングアップ手順としてサンプルごとにスタックを起動し直す。基盤側の修正は B3 の対象外として report sec 9 に記録 |
| 3 | 前のサンプルの画面が次のサンプルに残る | kill されたアプリの canvas が graphics 側に残る (同上、基盤側) | 同上 |

## 手順 (再現用)

```
# family-mruby ルートで
docker compose down -v
docker compose -f docker-compose.yml -f docker-compose.headless.yml up -d
# 起動を待ってから、1 本ずつ:
cp fmruby-core/components/basic/test/samples/sample_11_shoot.bas \
   fmruby-core/flash/app/demo/_try.app.bas
printf 'app_handle_name = "_try"\napp_screen_name = "try"\n' \
   > fmruby-core/flash/app/demo/_try.app.toml
python3 - <<'EOF'
import sys; sys.path.insert(0, "fmruby-core/tool/debug")
from fmrb_dbg_client import FmrbDebugClient, TcpTransport
c = FmrbDebugClient(TcpTransport("127.0.0.1", 5555)); c.connect()
print(c.spawn("/app/demo/_try.app.bas")); c.close()
EOF
python3 tools/fmrb_input.py click 150 100 sleep 400 key left key z
python3 tools/fmrb_screenshot.py /tmp/shot.png
docker logs fmruby_core | grep -E "BASIC error|PRINT:"
```

キー入力を効かせるには**一度テキスト面をクリックしてフォーカスを渡す**
必要がある (debugd の spawn で起動したアプリはデスクトップのフォーカス
対象にならないため)。

## ユーザ供給の作品リストが届いたら

1. `reports/corpus_inventory.md` の棚卸し表を埋める (使用命令・POKE
   アドレス・想定互換レベル)。
2. 1 本ずつ上の手順で動かし、この表に「動く / 制限付き / 非対応 + 理由」を
   追記する。
3. 非互換の原因が spec の暫定値なら report の疑義リストへ、実装漏れなら
   直してゴールデンケースを足す。
