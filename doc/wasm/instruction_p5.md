# 実装指示書 P5: 配信 — Family mruby on WebAssembly を公開する

対象: 実装担当セッション。前提: implementation_plan.md の P5 節 (要件の正)
と report/p4a.md。タスクごとにコミット (push はしない)。report は
doc/wasm/report/p5.md へ。

## 現在地 (P5 のうち先行して済んでいるもの)

- **staging の安全化 — 済み**: rake wasm:web は git 追跡分 + system_conf_wasm
  だけを build/webflash に束ねる。秘密・権利物は構造的に入らない
  (wifi/secrets の検知ゲートつき)。flash レイアウト整理済み
  (/home 最小 seed、サンプルは /usr/share/samples、flash_local overlay)。
- **ページ設定基盤 — 済み**: localStorage + preRun で MEMFS を書き換える
  機構が動いている (第一号 = テーマ切替。cyberpunk 既定 / classic)。
- **起動 UI — 済み**: クリック = 電源 (AudioContext を先に確保して boot、
  ジングルが鳴る)。開発サーバは no-cache。
- 教訓 (踏み直さないこと): テーマ色 0x01 禁止 (前景カラーキー)、
  既定の見た目は JS 依存にしない (staging に焼き込む)、キャッシュに注意。

## T1: 解像度選択と表示倍率

1. **内部解像度**: 設定パネルに解像度セレクトを追加 —
   426x240 (実機一致・既定) / 640x360 / 852x480。URL クエリ (?w=&h=) も
   同じ値へ写す。適用はテーマと同じ preRun で
   /flash/etc/system_conf.toml の display_width/height を書き換え、
   リロードで反映。core 側は conf 駆動なので無改修の見込み
   (display_width/height、canvas と present は実サイズ追従が実装済み)。
   default_user_app_width/height も比例させるか固定かを決めて report へ。
2. **表示倍率 (CSS)**: 1x/2x/3x/フィットのセレクト。canvas の CSS サイズを
   変えるだけ (image-rendering: pixelated は設定済み)。既定は「フィット」。
3. **検証 (headless で可)**: 各解像度で drive.js — デスクトップ、メニュー、
   ランチャー、エディタ起動 + 打鍵、PicoRabbit のデッキメニューまでの
   画面を撮る。レイアウト破綻 (はみ出し・重なり) を目視確認し、
   問題があった解像度は選択肢から外して report に残す (直すのは範囲外)。
   ※ node は実 flash/etc を読むので、検証時は conf 差し替えで行う
   (P4a 以降のプレビューと同じ手順)。

## T2: 同時起動アプリ数 (max_apps)

- FMRB_MAX_APPS=9 (fmrb_task_config.h:11) を「静的容量の天井」とし、
  wasm ビルドだけ 32 に上書き (wasm/CMakeLists.txt +
  family_mruby_wasm.rb — **両側一致必須**、MRB 定義と同じ規律)。
- system_conf に `max_apps` (既定 = 天井) を新設し、spawner
  (fmrb_app_spawner.c) が spawn 時に照合。
- **kernel Ruby 側の FMRB_MAX_APPS 前提** (input_router.rb:55 のコメントの
  ループ) が定数をどう受けているか確認し、必要なら conf 値を渡す。
- pthread pool (現状 40) を容量 + 常設タスク数に合わせて引き上げ。
- 検証: wasm で 10 本以上のアプリを起動して動くこと (drive.js で launcher
  連打)。実機/linux は既定不変 (ビルド通過 + sim ブートで確認)。

## T3: 鍵の持ち込みと TTS (fetch ブリッジ) — **P5 では実施しない**

ユーザ判断 (2026-08-29): 今回は含めない。本当のリリース時に含めるかを
再判断する (そのとき P5b として起こす)。設計は下記のとおり確定済みなので、
実施時はこの節をそのまま使う:

1. 設定パネルに API 鍵欄 (「このブラウザに保存 / セッションのみ」+ 削除 +
   注記)。保存先は localStorage / sessionStorage。クッキー不使用。
2. fetch ブリッジ: HTTP(S) 要求単位の C API を新設し、JS fetch へ proxy。
   応答までは FreeRTOS の待ちでブロック (**協調 port の生存条件を守る** —
   スピン禁止)。
3. wasm 用 tts_http 差し替え (socket 版はそのまま)。鍵は起動時に JS が
   MEMFS の services 設定へ書く (実機と同じ平文設定モデル)。
4. 先に **VOICEVOX (localhost、鍵不要、CORS 設定可)** で経路を通し、
   その後 OpenAI の CORS 可否を実測。
5. 受け入れ: 鍵ありで TTS が鳴る / 鍵なしは明示メッセージ /
   開発者ツールで送信先が対象 API のみであること。

## T4: 配信物一式の完成 — **公開の手前で止める** (ユーザ判断 2026-08-29)

インターネットへの実公開 (配置先の決定・アップロード) はしない。
「静的ファイルを置きさえすれば公開できる」状態まで作り、ローカルで証明する。

1. **配布ディレクトリを組む rake タスク** (例 wasm:dist):
   build/dist/ に index.html + main.js + keymap.js + core_web.{js,wasm,data} +
   coi-serviceworker を自己完結で並べる (今の ../build/ ../../tool/ 参照を
   dist 内相対に直す)。
2. **coi-serviceworker** を同梱し、COOP/COEP を静的配信で成立させる。
3. **キャッシュ対策**: core_web.js/.wasm/.data と main.js の参照に版
   (ビルド時のハッシュか日時) をクエリ付与する。wasm:dist が index.html に
   焼く形でよい。「新しい .data + 古い main.js」の半端を構造的に防ぐ。
4. 説明文 (操作方法、かな入力の切替、音はクリック後、データはリロードで
   消える旨、Safari 未確認・モバイル対象外・ネットワーク機能なし) を
   ページに一段書く。
5. **受け入れ条件 (すべてローカルで判定)**:
   - **ヘッダを一切足さない素の静的サーバ** (例 python3 -m http.server) で
     dist/ を配信し、Chrome / Firefox から起動・操作・音まで動く
     (= coi-serviceworker が仕事をしている証明。GitHub Pages 相当)。
   - バイナリ照合: .data に ssid/password/NSF/秘密が 0 件
     (webflash ゲート + 照合スクリプトを rake タスク化して常設)。
   - main/secrets.h を置いた状態でビルドしても bundle に値が入らないこと。
   - かな入力 (Ctrl+Space)、テーマ切替、解像度切替を含む一通りの操作。
   - 初回ロードが目安 10 秒以内 (ローカル計測 + サイズから回線試算を report へ)。

実公開 (置き先の決定、アップロード、SNS 告知) はユーザの操作として P5 の
外に置く。dist/ を置くだけの状態で引き渡すこと。

## report に書くこと

- 解像度ごとのレイアウト検証結果 (採用した選択肢と外した理由)。
- max_apps の配線 (kernel Ruby への渡し方) と多数起動の実測。
- dist/ の構成と、素の静的サーバでの動作確認結果 (ブラウザと版)。
- 初回ロードのローカル実測とサイズからの回線試算、
  coi-serviceworker で詰まった点。

## やらないこと

- **インターネットへの実公開** (配置先決定・アップロード・告知はユーザ作業)。
- TTS / 鍵持ち込み (T3 節参照。リリース判断時に P5b として)。
- BLE (P6 候補)、IDBFS 永続化 (反応を見てから)、性能改善、
  Spinel エディタのタイトル帯配色 (既知の軽微差)、
  レイアウト破綻の修正 (記録して選択肢から外すまで)。
