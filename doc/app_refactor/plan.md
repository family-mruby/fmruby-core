# Asterism を使うアプリの共通部分を切り出す

> 状態: 計画済 | 更新: 2026-10-24 | step_sensor・walk_scroller・オセロの卓・お絵かきボード・麻雀・asterism_demo が、それぞれ同じこと (WiFi を待つ、繋ぐ、繋ぎ直す、卓に入る・出る、/home の小さな設定を読み書きする、幅に合わせて文字を詰める) を自前で書いている。共通の部分を部品に移し、アプリを短くする。R1 から順に進める

## 1. 目的

- アプリの本筋 (センサの検知、ゲームの画面と操作) だけがアプリに残るようにする。step_sensor は 723 行あり、歩数計として大きすぎる (2026-10-24 ユーザと確認)。
- 直しを 1 か所で済ませる。たとえば「再起動の直後に前の自分が網に残っていて断られる」の扱いは、今は各アプリに別々に書いてある。

## 2. 今の重なり (2026-10-24 に数えた)

| アプリ | 行数 | WiFi 待ち・接続・繋ぎ直し | 卓に入る・出る | /home・/etc の設定を手で読み書き | 文字の幅・詰め・ボタン |
|---|---|---|---|---|---|
| step_sensor (`flash/app/test/`) | 723 | 自前 (約 60 行) | - | 自前 (約 70 行) | 7 セグメントの数字 |
| walk_scroller (`flash/app/demo/`) | 1,513 | 自前 | - | 自前 (台本) | 自前 |
| asterism_demo (`flash/app/test/`) | 436 | 自前 | - | 自前 | - |
| オセロの卓 (`flash/app/game/othello/`) | 1,018 | 自前 (network_ready?・connect・reconnecting?) | join・leave_table・卓の一覧 | conf_int | text_w・fit・fit_dots・wrap2・button |
| お絵かきボード (`flash/app/game/board/`) | 1,399 | 同上 | 同上 | - | text_w・fit・button |
| 麻雀 (`flash/app/game/mahjong/`、9 ファイル 6,876 行) | 2,078 (本体) | 同上 | 同上 | 補助の設定 | button ほか |

- オセロ・ボード・麻雀の 3 つは、接続と卓の扱いがほぼ同じ形 (同じ名前のメソッド、同じ定数 RESCAN_MS・LIST_EVERY_MS、on_join で卓を探し直す) で、写して育てた跡がある。

## 3. 方針

段階ごとに、部品を作る → まず小さいアプリ 1 本を載せ替える → 振る舞いが変わらないことを確かめる → 残りを載せ替える。

| 段 | 内容 | 載せ替えるアプリ |
|---|---|---|
| R1 (完了 2026-10-25、report/r1.md。FmrbAsterism::Link、StickS3・ブラウザ版でユーザ確認済) | **繋がり続ける口**: WiFi を待つ、画面の準備を待てる、失敗・切断で決まった間隔で繋ぎ直す、「already on the network」は待って繋ぎ直す、状態 (待ち・接続中・接続・繋ぎ直し中・理由) を 1 つの値で返す、繋がったときの処理 (expose・購読・on_join) を一度書けば繋ぎ直しのたびに流れる。置き場は fmrb-asterism (`lib/add/picoruby-fmrb-asterism`)。asterism の gem 本体を変える必要があるかは R1 の最初に読んで決める | step_sensor → walk_scroller → asterism_demo |
| R2 | **TOML の読み書き `FmrbToml`**: 芯は FmrbApp の外の 1 つのモジュール。**読むのはファームにある C の `components/fmrb_toml` (tomlc99) に Ruby の口を付けて** Hash・Array で返す (`[[mark]]` のような表の配列、エスケープも正しく、system_conf と同じ解釈。自前の読み手を増やさない)。書くのは Ruby で、1 つの鍵の行だけを書き換えてほかの行とコメントを残す (`FmrbToml.update`、今の save_tune の一般化)。R2 の最初に、sim・wasm・ESP32 の全部で fmrb_toml がビルドされるか、tomlc99 の確保がどの領域か (内蔵 RAM を使わない)、Spinel のアプリから使えるかを確かめる。今ばらばらにある読み手 (SvcConf.parse、FmrbColors.parse、FmrbAssoc、FmrbAsterism.settings、FmrbI18n・FmrbApp のテーマの system_conf の読み、アプリの conf_int・save_tune) を順に寄せる。FmrbApp には薄い入口だけ (例 `app_settings` で `/home/<app_handle_name>.toml`、`save_app_settings`)。**FmrbApp は mruby 版と Spinel 版 (`main/prebuild_scripts/spinel/fmrb_app_base_spinel.rb`) の 2 か所**、入口はメソッドだけでインスタンス変数を足さない (基底の ivar で Spinel の配置がずれた前例) (2026-10-24 ユーザと相談) | step_sensor・オセロ・walk_scroller・麻雀、サービス・色・関連付け |
| R3 | **step_sensor の整理**: 生の値を 1 つの組に (m_peak と dyn_peak の持ち主をそろえる、`r[5]` のような番号を名前に)、調べるための記録 (直前の標本・処理時間) を小さく、一歩の検知の 3 段の平均を考え直す (合成の波形の試験はそのまま通す)。歩数の表示は矩形で描く 7 セグメント (draw_digit・SEGMENTS) をやめ、既存のフォントの拡大で出す (2026-10-25 ユーザ。今の text size の上限 4 だと 6x8 の字で高さ 32 px、それで足りるか、上限を上げる必要があるかを確かめる) | step_sensor |
| R4 | **卓のクライアントの共通部分**: 卓の一覧、入る・出る、席の札 (token)、様子 (view) の問い合わせの間隔、ホストがいなくなったときの扱い。asterism-games のホスト側の枠組みと対になる、端末側の小さな枠組みにする | オセロ (いちばん小さい) → ボード → 麻雀 |
| R5 | **画面の小物**: 文字の幅 (text_w)、幅に詰める (fit・fit_dots)、2 行に折る (wrap2)、ボタン。既存の FmrbUI に寄せられるものは寄せる | オセロ・ボード・麻雀・walk_scroller |

- 順番は R1 → R2 → R3 (step_sensor が一番短くなる) → R4 → R5。R4 は R1 の上に乗る。

## 4. 決まり・気をつけること

- **振る舞いを変えない**: 載せ替えは見た目・操作・網に出す値を変えない (変えるなら別に決める)。卓のアプリは、手元のゲームホストと sim、ブラウザ版で、入る・打つ・出る・繋ぎ直しを前後で比べる。
- **内蔵 RAM を増やさない**: 部品を mrbgem に入れるとファームの大きさは増えうるが、内蔵 RAM は増やさない。Retro (S3) と小さい端末 (AtomS3R・StickS3) のビルドで前後を比べる。
- **picoruby の罠**: 共通の部品をアプリから require する形、mixin の定数の解決 (self.class::CONST)、Regexp なし、ブロックの重さ (memory の Ruby の罠の節)。
- **Spinel**: 卓のアプリが Spinel で動く構成があるなら、そちらでも確かめる。
- 実装はサブ、親が検収 (fmrb-dev-flow 4 章)。段ごとに instruction_rN.md と report/rN.md。

## 5. 受け入れ条件 (案)

- R1-R3 の後、step_sensor は 400 行前後になり、接続と設定の処理がアプリから消える。合成の波形の試験と、実機 (StickS3) の歩数・調整の画面・保存が前と同じに動く。
- R4 の後、オセロ・ボード・麻雀から接続と卓の扱いの重なりが消え、手元のゲームホストで入る・打つ・出る・繋ぎ直しが前と同じ。
- どの段でも、Retro・Tab5・小さい端末の内蔵 RAM が増えていない。

## 6. 未確定事項

1. R1 の口を fmrb-asterism (端末側だけ) に置くか、asterism の gem 本体 (CRuby の gem と共通) に入れるか。
2. R4 の卓のクライアントを、どこに置くか (fmruby-core の gem か、asterism-games の端末側の部品として配るか)。
3. 麻雀は大きいので、R4 を 1 段で載せ替えるか、さらに分けるか。
