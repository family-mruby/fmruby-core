# 多重 VM アプリ構想: 巨大 Ruby アプリをマイコンで動かす

作成: 2026-08-09。状態: 検討段階 (実装未着手)。

## 1. 背景と目的

RPG デモ級のアプリはコードが肥大しつつあり、アプリ起動時の一括コンパイルが
時間 (起動待ち) とメモリ (コンパイル中のプールピーク) の両方を圧迫し始めて
いる。本格的な RPG を作るとコード量は現在の数倍になる見込み。

そこで Family mruby の多重 VM 構造 (アプリごとに独立した mrb_state +
FreeRTOS タスク + 専用メモリプール) を最大限使い、以下の 2 形態で
「コード総量の上限をシーン/ワーカー 1 枚あたりの制約に変換する」:

- **形態 B: オフロード型** — UI とメインループは常駐アプリが持ち、
  重い処理 (マップ生成、経路探索、裏の戦闘計算、次シーンの事前コンパイル等)
  を headless の使い捨てワーカーアプリに渡す。
- **形態 A: シーン移管型** — シーンごとに使い捨てアプリを spawn し、
  全画面ごと処理を移管する。シーン終了 = アプリ終了 = プール一括回収。

**着手順は B が先**。A に必要な画面移譲の仕組み (窓モードの後から変更 /
全画面の受け渡し API) が未整備なのに対し、B は既存部品だけでほぼ成立する。

## 2. 調査で確定した事実 (2026-08-09 時点)

### 2.1 アプリのコンパイルと require

- ユーザアプリの .app.rb は起動時に全量をその場でコンパイルする
  (main/app/fmrb_app.c:682 の mrc_load_string_cxt)。コンパイル結果の
  キャッシュは無く、毎回フルコンパイル。
- 1 ファイル上限 64KB (fmrb_app.c の MAX_SCRIPT_FILE_SIZE)。
- コンパイラ (prism) はアプリ自身のプールから確保する (専用プールは撤廃済み)。
- require / load は動作する (picoruby-require + Sandbox 経由の実行時
  コンパイル)。$LOAD_PATH は /lib 固定だが絶対パス指定可。
  <name>.mrb を <name>.rb より優先して探す。
- **require したコードは VM 内では実質解放できない (構造的)**:
  require 1 回ごとに Sandbox のタスクが 1 本 DORMANT キューに残り、
  GC のマークは全キューを走査するため永久に回収されない。このタスクが
  top-level irep 経由でメソッド irep 群を pin するため、remove_const +
  GC でも実際には解放されない。唯一の全回収はアプリ終了時のメモリプール
  一括破棄 (destroy_vm は mrb_close せずプール破棄に頼る設計)。
  → **VM 単位でシーン/ワーカーを使い捨てる本構想は、この唯一の回収手段を
  正面から使う設計である。**
- 実機での .mrb 生成: RITE writer (mrc_dump_irep 系) はソースにあり
  libmruby.a にもコンパイル済みだが、呼ぶ側がいないため ELF に未リンク。
  Ruby から見える dump API も無い。Sandbox に「コンパイル結果をバッファに
  dump する」ネイティブメソッドを 1 つ足せば、初回起動時にコンパイル →
  .mrb 保存 → 以後は .mrb 直読み、というオンデバイスキャッシュが組める。
  読む側 (mrb_read_irep) は実装済みで動く。
  注意: バイトコード版数はファームのコンパイラと一致が必要。ファーム更新時の
  キャッシュ無効化が要る。

### 2.2 多重 VM の現状部品

- **spawn**: FmrbApp#request_run(path) が pid の返る唯一の API
  (editor の F5 実行が実例)。カーネル宛 {"cmd"=>"spawn"} でも起動可
  (送信元の制限なし)。spawn 引数はパスのみで、実行属性は全てサイドカー
  .toml から読む。
- **headless**: .toml の default_window_mode = "background" で canvas 無し
  実行を正式サポート (C/Ruby 両側で window list / hit test から除外)。
  ただしリポジトリ内に実例ゼロの未踏経路。
- **HID**: spawn 後の after_spawn が無条件に _set_hid_target(new_pid) を
  実行するため、headless ワーカーを spawn してもキーボードが子へ移る
  (app_lifecycle.rb)。
- **通信**: FmrbApp#send_message(dest_pid, type, data) は任意 pid へ直送可
  (自動 MessagePack 化)。**ペイロード上限 176 バイト** (fmrb_msg.h)。
  publish/subscribe はカーネル中継のトピック配送
  (fmrb_kernel.rb)。実運用例: stackchan_remote → stackchan。
- **終了**: アプリ終了はカーネルだけが知る。親/spawner への通知は無い。
  終了コードや結果値を返す仕組みも無い。Ruby からの強制 kill API は無い
  ({"cmd"=>"stop"} 送信での正常終了要求はある)。カーネルの "kill"
  ハンドラは TODO スタブのまま。
- **同時実行数**: ユーザアプリは同時 3 本 (FMRB_MAX_APPS=7 のうちスロット
  4-6)。プールは 500KB x3 + LARGE 1MB (排他 1 本、g_large_pool_in_use)。
  S3/P4 で同一構成。
- **P4 の余地**: PSRAM は 20MB 以上未使用。ただしアプリ 1 本増設ごとに
  内蔵 RAM のタスク C スタック (16KB〜) を消費するため、制約は内蔵 RAM 側。
  増設は fmrb_mem_config.h + fmrb_mempool.c + fmrb_task_config.h の
  3 ファイル同期 (Static_assert が不整合を検出する)。
- **画面**: Ctrl+Tab の park/unpark と {"cmd"=>"focus_app"} は実装済み。
  全画面を別アプリへ直接渡す API は無い (全画面化は spawn 時の .toml
  フラグ経由のみ)。
- **FS**: Ruby 側の picoruby-vfs / littlefs / FAT gem はビルドに入って
  いない。File/IO は fmrb_hal_file → ESP-IDF VFS (esp_littlefs) 経由。
  fmrb_hal_file_esp32.c にパス別名テーブル s_path_aliases があり、
  /flash /sd をここで振り分けている。

### 2.3 ギャップ一覧 (重要度順)

1. ユーザアプリ同時 3 スロット (メイン + ワーカー 1-2 で満杯)
2. 親への終了・結果通知が無い
3. spawn 時に HID がワーカーへ移る
4. メッセージ 176 バイト上限 (大きなデータの受け渡し路が無い)
5. 窓モードの後から変更 / 全画面移譲 API が無い (形態 A の先読みに必要)
   → **解消 (2026-08-10)**: エディタ P2 で fmrb_app_set_fullscreen
   (実行時の窓 ⇔ 全画面切替、mruby/Spinel 両バインディング) と
   カーネルの全画面スタック管理が入った (doc/editor_serious_mode/report/p2.md)
6. spawn 実行属性が .toml サイドカー必須 (ヘルパー量産の足かせ)
7. Request/Response の仕組みが無い (応答の対応付けと待機)

## 3. 設計方針

### 3.1 /tmp RAM FS

目的: (a) 176 バイト上限を迂回する大容量の受け渡し路
(b) 揮発データを flash (littlefs) に書かないことによる摩耗回避と高速化
(c) 「再起動で消える」揮発性がシーン状態・中間結果の意味論として正しい。

役割分担: /home = セーブデータ等の永続、/tmp = VM 間受け渡しと一時ファイル。

実装案:
- esp32: PSRAM を後ろ盾にした小さな esp_vfs ドライバ (数百行) を登録し、
  fmrb_hal_file_esp32.c の s_path_aliases に /tmp を 1 行追加する。
  Ruby 側は既存 File API がそのまま使え、新 API は不要。
- posix (Linux sim): fmrb_hal_file_posix がホストの実ファイルに落ちるため
  ほぼ無改修 (ホスト側の一時ディレクトリに割り当てる)。
- サイズはターゲット別: P4 は数 MB、S3 は 256-512KB 程度から。

副産物: /tmp に生成した .rb / .mrb を置いて require できる
(オンデバイス .mrb キャッシュや実行時コード生成の置き場)。

検討事項:
- 書きかけファイルを読まれない作法 (テンポラリ名に書いて rename、または
  「書き終えてから通知メッセージを送る」規約で済ませるか)
- 容量超過時の挙動 (ENOSPC を返すだけで良いか)
- /var/run 等の追加マウントは実益が出るまで作らない (/tmp 一つで始める)

### 3.2 コメント埋め込み toml (サイドカー省略)

一ファイル完結のヘルパーを量産するため、.rb 先頭のコメントに実行属性を
埋め込めるようにする (Python の PEP 723 と同発想)。

書式案: 新文法を作らず、フェンス内の行頭 "# " を剥いで既存の fmrb_toml
解析器へそのまま渡す。

```ruby
#---fmrb
# default_window_mode = "background"
# task_stack_kb = 32
#---
```

規則:
- .toml サイドカーが存在する場合はコメントを見ない (予測可能性優先)。
- 読み取りは spawn 時に先頭 512 バイトのみ。
- **ランチャー表示系メタデータ (名前・アイコン・表示可否) は .toml 限定**。
  起動時スキャンが全 .rb を開く事態を避け、起動時間 (7.24s) を守る。
  ヘルパーはランチャーに出さない前提なのでこの線引きで困らない。

実装点: fmrb_app_spawner.c のサイドカー解析に「.toml 不在時は .rb 先頭の
フェンスを読む」後段を足す。

### 3.3 アプリ間通信の拡張: Request/Response

pub/sub と直送だけでは「仕事を渡して結果を受け取る」往復が書きにくい。
応答の対応付け (どの要求への返事か) と待機 (返事が来るまで/タイムアウト)
をライブラリ層で提供する。

方針: **カーネル改修なし**。FmrbApp (picoruby-fmrb-app の mrblib) に
既存の send_message の上へ薄い規約を載せる。

要求側 API 案:

```ruby
res = request(dest_pid, "gen_map", {seed: 42}, timeout_ms: 5000)
# => ワーカーの応答 (Hash)。タイムアウトで nil か例外
```

- req_id を採番し {"cmd"=>"req", "name"=>.., "req_id"=>N,
  "reply_to"=>self_pid, "data"=>..} を送る。
- 応答待ちは _spin と協調するループ (Task.pass) で行い、ブロッキング中も
  他のメッセージ処理を止めない。非同期版 (request_async + 後で受領) も
  同じ req_id 機構で提供できる。

応答側 API 案:

```ruby
def on_request(name, data)
  case name
  when "gen_map" then {path: "/tmp/map_42.bin"}   # 戻り値がそのまま応答
  end
end
```

- フレームワークが {"cmd"=>"res", "req_id"=>N, "data"=>戻り値} を
  reply_to へ返送する。
- 176 バイトを超えるデータは /tmp にファイルで置き、応答にはパスを入れる
  (3.1 と対で機能する)。

検討事項:
- 相手が応答前に死んだ場合の検知 (タイムアウトに任せるか、3.4 の終了通知と
  連動して早期にエラー化するか)
- 同時複数 in-flight の上限と req_id の回り込み
- 要求の取り消し (v1 では作らない)

### 3.4 終了・結果通知

- 当面の規約: ワーカー/シーンは終了直前に自分で publish (または親へ直送)
  してから exit する。親は保険として FmrbApp.ps_gen のポーリングで
  消滅を検知できる。
- 本命: カーネルの cleanup_terminated_app から @run_parent (request_run の
  要求元) へ {"cmd"=>"app_exited", "pid"=>N} を 1 通送る小改修。
  request_run 経路は親子関係を既に記録しているので追加情報は不要。

### 3.5 HID 奪取の除外

after_spawn の _set_hid_target を headless (background) アプリでは
スキップする。それまでの回避策は spawn 直後に親が {"cmd"=>"focus_app",
"pid"=>自分} で取り返すこと。

### 3.6 シーン移管 (形態 A、後段)

最小形は今日の部品で成立する:
- シーンアプリの .toml (またはコメント toml) に fullscreen を書く
  → spawn 時に全画面を取り、HID も移る (移管では望ましい挙動)
- シーン終了 → カーネル回収で全画面解除、HID は @run_parent へ戻る
- 状態は /tmp のファイルで受け渡す
- 前例 (2026-08-10 追記): エディタ全画面化で **kernel の spawn 要求に
  open_path が追加された** (全画面 spawn では要求元が suspend されるため、
  kernel が tick_process で宛先キュー登録を待って再送する方式)。
  シーン移管の「起動時に状態ファイルパスを渡す」はこの機構をそのまま
  一般化すれば良い (2.3 のギャップ 6 への部分的な答え)。

未整備なのは「次シーンを裏でコンパイルだけ済ませておき、遷移時に全画面を
渡す」先読み。窓モードの後から変更 (または全画面移譲) API が 1 つ要る。
.mrb キャッシュ (2.1) が入ればコンパイル自体が軽くなるため、先読みなし +
遷移時ロード画面でも成立する。優先度は低くて良い。

### 3.7 ワーカーの CPU 協調

ユーザアプリは全て priority 2 で同格。ワーカーがビジーループすると
メインループと CPU を取り合う。ワーカー側は計算の合間に Task.pass を挟む
規約とする。P4 は 2 コアなので、うまく散れば実並列になる。

## 4. 段階計画

| 段階 | 内容 | 依存 |
|---|---|---|
| 1 | オフロード最小デモ (Linux sim): メイン常駐 + headless ワーカー 1 本の往復。未踏経路 (background モード、HID の行方、終了検知) の穴出し | なし |
| 2 | /tmp RAM FS (esp_vfs ドライバ + 別名 1 行 + posix 側) | なし |
| 3 | コメント埋め込み toml (spawner の後段追加) | なし |
| 4 | Request/Response ライブラリ (FmrbApp mrblib) + 終了通知の小改修 | 2 (大データはパス渡し) |
| 5 | RPG のシーン分割試作 (形態 A の最小デモ) | 2, 4 |
| 6 | .mrb オンデバイスキャッシュ (Sandbox に dump 口) / スロット増設 (P4) | 必要になってから |

段階 1 は改修ゼロで始められる (HID は focus_app で取り返す回避策、結果は
littlefs 経由で仮置き)。段階 2-4 が本命の基盤整備。

## 5. キャンバスと音声の所有モデルと受け渡し (2026-08-09 調査)

「ハンドルをどう渡すか」への答えは**「渡さない」**。調査で判明した所有モデル
がそれを許す構造になっている。

### 5.1 所有モデルの実態

pid に紐付くのは core 側 ctx に登録されたものだけ: canvas_id / bg_canvas_id /
extra_canvas_ids[2] (fmrb_app.h)、メッセージキュー、メモリプール、タスク
ハンドル経由の HW 資源 (I2C/RMT/GPIO)。これらは正常終了・例外死・強制 kill
の 3 経路すべてで冪等に回収される (fmrb_app_canvas_release_all)。

それ以外は全てグローバル:

- canvas ID は graphics-audio 側の単調カウンタ採番 (graphics_handler.cpp、
  クライアントの指定 ID は無視)。全体 16 枚上限。GFX プロトコルにも
  host_task のバッチにも pid は乗らない。参照時の所有検証も無く、
  **今日でも他アプリの canvas ID へ描ける** (慣習だけで成立)。
- sprite image/instance/mask もグローバル採番。canvas_id タグは
  DELETE_CANVAS 時の一括解放のためだけで、参照時の照合は無い。
- APU は物理 4ch x 2 インスタンス (MAIN=NSF+FMSQ slot0 / SUB=note_on+FMSQ
  instance1) を誰でも直接叩ける。FMSQ スロット 16 本もアプリが自分で番号を
  決めるグローバルテーブル (衝突は後勝ち)。
- **NSF/FMSQ プレイヤーは graphics-audio 側の 60Hz タスクが所有**しており、
  core 側アプリの生死と無関係に走り続ける。

### 5.2 キャンバス: 渡さず、作り直すか、ファイルで運ぶ

- **形態 A (シーン移管)**: canvas は引き継がない。シーンアプリは spawn 時に
  自分の全画面 canvas を貰い、終了時に確実に回収される。移譲 API は不要。
- **形態 B (worker 描画)**: worker がメインの canvas ID へ直接描くことは
  技術的に可能だが**推奨しない**。理由 3 点:
  1. host_task のバッチは到着順で複数アプリのコマンドが混ざり、メインの
     present と worker の描画が交錯する (フレーム境界の保証が無い)
  2. sprite の所有は canvas タグに寄っており、回収が直感に反する
  3. canvas から canvas への合成 (dest_canvas_id) は graphics-audio 側が
     未実装
- **推奨: worker は /tmp に BMP を書き、メインが transfer_file +
  create_image で取り込んで描く。** 資源が全てメインの canvas 所有になり
  worker の生死と独立、176B 制限も回避、既存の実績経路
  (sync_file / create_image_from_file) をそのまま使う。3.1 の RAM FS と対。
- 中間案 (軽量だが脆い): worker が main の canvas_id で SpriteImage だけを
  作り image_id をメインへ返す。image は main canvas 所有なので worker 終了後
  も残る。ID の pid 跨ぎ手渡しという慣習依存が増えるため第一候補にしない。
- 本気で canvas 共有/移譲を作る場合の最小形は「所有テーブル + submit 時の
  pid 照合 + ctx 間移譲関数」だが、A/B どちらにも必須でないため作らない。

### 5.3 音声: BGM 継続は既に成立している。要るのは規約と stop

- FMSQ/NSF は VM の外 (graphics-audio) で走るため、**シーンアプリが終了時に
  止めなければ BGM はそのまま鳴り続ける** (destroy は音声を自動停止しない)。
  シーン跨ぎの BGM 継続に改修は不要。
- 整備すべきは 2 点:
  1. **FMSQ スロット割り当て規約**: 16 本をシーン間で衝突させない取り決め
     (例: BGM = slot 0-3、SE = slot 8-15)。現状は各アプリが自由に番号を
     決めるため、同番号ロードで再生中の曲が差し替わる。
  2. **FMSQ の stop/fade コマンド追加**: 現状 FmrbAudio#stop は NSF しか
     止めず、FMSQ を止める手段が「別スロットを流す」しかない。
     FMRB_AUDIO_CMD_STOP_SLOT(instance) 相当が要る。
- 罠: note_on 直叩きを使ったアプリが終了すると、カーネルの後始末が SUB 側
  4ch **全部**に note_off を送り、他アプリの発音も巻き添えで消える
  (audio_handler.rb silence_notes_for)。多重 VM 構成では音は FMSQ に寄せ、
  note_on 直叩きは避ける規約とする。
- SmfPlayer / MML はアプリ VM 内で tick する設計のため、これらで鳴らした
  音楽はアプリ終了で止まる。シーン跨ぎ BGM には使わない。
- やってはいけない改修: 終了時に「そのアプリの音を全部止める」を
  cleanup_terminated_app へ足すこと。継続性が壊れる。

### 5.4 sprite の共有 (検討)

共有したくなる場面は実在する。主はシーン移管での資産再利用 (主人公・
タイルセット・UI アイコンは全シーン共通なのに、canvas 削除のたびに image が
一括解放され、次シーンが BMP 転送からやり直す)。次いで worker 生成資産の
メイン利用。**共有するのは image (ピクセルデータ) だけ**とし、instance
(位置・表示状態) はシーン状態なので /tmp の状態ファイルで渡して作り直す。

実現方式は ID 手渡しではなく **graphics-audio 側のパスをキーにした image
キャッシュ**を本命とする:

- create_image(パス) が同一パスなら既存 image_id を返す (参照カウント付き)
- キャッシュ分は canvas 削除で解放されない持続タグを持つ (mask の
  「canvas_id = 0 は unbound」に前例あり)。明示 evict か、参照 0 +
  プール逼迫で追い出す
- VM 間で ID を手渡す慣習が不要になり、「VM 間の接点は名前に限る」という
  本計画の規律と一致する。worker 生成資産も /tmp のパスを渡すだけで乗る

ID 手渡し方式を採らない理由: image の寿命がタグ付き canvas の寿命に縛られて
おり、シーン移管ではその canvas 自体が消える。headless アプリは canvas を
持てないため常駐コーディネータに持たせる逃げも打てない。

留意点: sprite pool はグローバル 64 images / 128 instances で、常駐キャッシュ
はこの枠を恒久的に消費する。追い出し方針とセットで設計する。着手時期は
段階 5 (シーン分割試作) で遷移時間を実測してから判断する (現状の資産は
数 KB で再読み込みコストはまだ小さい)。

### 5.5 終了時に回収されない資源 (worker 使い捨て設計で効く)

調査の副産物。mruby アプリは mrb_close を呼ばない (プール破棄で代替) ため、
finalizer 頼みの後始末は走らない:

- **開きっぱなしの fd**: hw_proxy_file は owner を記録せず、
  hw_proxy_release_resources も file を解放しない。閉じ忘れた fd は
  アプリ終了後も VFS 側に残る可能性が高い。ソケットも同様。
- **MIDI sched の pid 別クリーンアップ**: fmrb_midi_sched_clear_pid は
  gem finalizer からしか呼ばれず、mruby アプリの終了経路では実行されて
  いないと思われる (要実機確認)。
- **FmrbApp#set_timer は未実装のスタブ** (未定義変数を返す)。
- worker を高頻度で使い捨てる前に、fd と MIDI sched の後始末をカーネルの
  cleanup_terminated_app 系へ足すか、「worker は必ず自分で close してから
  exit する」規約で運ぶかを決める。

## 6. 「どこまで巨大にできるか」の見積もり

この方式ではコード総量の上限が「シーン/ワーカー 1 枚あたりの制約」に
変換される:

- 1 ファイル 64KB (分割前提なら実質制約にならない)
- シーン 1 枚のプール 500KB (LARGE なら 1MB。P4 ならプール増量の余地大)
- 遷移時のコンパイル時間 (シーンを小さく保つ。.mrb キャッシュで初回のみ化)

総量はシーン数に比例して littlefs 容量まで伸ばせる。VM 間にポインタ共有が
無いため、シーン間の接点は /tmp の状態ファイルに強制される。これは制約で
あると同時に、巨大アプリを壊れにくく分割する規律として働く。
