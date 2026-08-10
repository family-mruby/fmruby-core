# エディタ本気モード計画: 全画面・高解像度・高速化

作成: 2026-08-09。状態: 検討段階 (実装未着手)。
関連: doc/spinel_aot/selective_offload.md (エディタは Spinel 化の決定済み対象、
2026-08-04)、「Modern は作る機械」構想 (全画面 79 桁エディタ)。

## 1. 目的

エディタを Modern (Tab5) の主力作業環境にする:

- 全画面の「本気モード」
- Modern では解像度を引き上げ、80 桁級の編集画面
- 打鍵から表示までの引っかかりを無くす (体感速度)。ハイライトの
  「1KB 超で自動オフ」(HL_AUTO_LIMIT_BYTES) を撤廃できる状態が目標

## 2. 現状の事実 (2026-08-09 調査)

- editor.app.rb は 1890 行 / 55KB の単一クラス。ビルド時バイトコード化
  された組み込みアプリ (FMRB_LOAD_MODE_BYTECODE)。
- **レイアウトは既に可変**: on_resize → recompute_layout が @user_area_*
  から全て導出し、ハードコード座標なし。全画面にならないのは spawner の
  組み込みテーブルが 240x200 固定で fullscreen フラグを持たないだけ
  (fmrb_app_spawner.c:114-133)。
- **描画は全面再描画**: 打鍵ごとに redraw_all がメニュー + 編集領域全行 +
  ステータスを描き直す。ハイライト行は同色区間ごとに draw_text を分割する
  ため、全画面時の 1 回の再描画は 200-400 個の gfx コマンドに達する。
  差分 (dirty-line) 追跡は無い。
- ハイライトのトークナイザ (picoruby-syntax-highlight) は**純 C**。
  1KB 制限の原因はトークナイザではなく、周辺の Ruby
  (@lines.join("\n") による全文字列再構築、行オフセット再計算、
  行内の draw_text 分割)。
- フォントは 3 種のみ: 6x8 (Font0)、8x8 (misaki、日本語可)、12x12。
- 解像度: framebuffer サイズは system_conf の実行時値で、P4 の fb は
  INIT_DISPLAY から動的確保。コンパイル時固定なのはスケール係数
  DISPLAY_P4_SCALE_FACTOR=3 と touch / remote desktop 系の定数群。
- P4 の canvas は RGB565 で spawn 時サイズ確保、以後再確保しない
  (Retro 側は最初から画面サイズで確保する方針で、ここが非対称)。
- P4 の合成は PPA Blend (拡縮不可) + 最終段 SRM 1 回 (拡縮はここだけ)。

## 3. 段階 A: 全画面モード (両機種対応、ほぼ改修なし)

- **Retro / Modern の両方で対応する**。解像度引き上げ (段階 B) は Modern
  専用だが、全画面化はどちらでも成立する。Modern 426x240 で 70 桁 x 27 行、
  Retro 320x240 で 53 桁 x 27 行前後。
- **全画面は速度にも効く**: 全画面モードは他アプリを suspend するため、
  CPU の取り合いと合成対象が減り、編集中の処理が軽くなるはず
  (効果は計測で確認する)。
- spawner の default/editor エントリに .fullscreen = true
  (+ .fullscreen_switchable、.large_memory) を追加するだけで、
  エディタ側のコード変更ゼロで全画面になる。
- LARGE プール (1MB) は排他 1 本の点に注意 (多重 VM 計画
  doc/multivm_app/plan.md と競合し得る)。
- 実行時の「窓 ⇔ 本気モード」切替をやる場合は、P4 の canvas 確保を
  Retro と同じ「画面サイズで確保」方針に合わせる改修が前提
  (spawn 時サイズ固定のままでは拡大できない)。v1 は起動時選択で良い。

## 4. 段階 B: Modern の解像度引き上げ (全体切替 640x360)

- **640x360 が最適点**: 2x 整数スケールで 1280x720 パネルに厳密一致
  (現状 426x240@3x の 1px 帯も消える)。8x8 misaki フォントで
  **80 桁 x 45 行、日本語表示可**。6x8 なら 106x42。
- 変更点: system_conf_p4.toml の display_width/height、
  DISPLAY_P4_SCALE_FACTOR 3→2 (理想は fb サイズから導出)、
  touch の仮想解像度定数、remote desktop の定数 (info 応答 / JPEG /
  H.264。縦 360 は 16 の倍数でないため MCU パディング 368 が必要)、
  壁紙アセット追加。リンクプロトコルの座標は uint16 で問題なし。
- メモリ増は fb +256KB、全画面 canvas 1 枚 +460KB、いずれも PSRAM
  (32MB) で問題なし。未知数は 2.25 倍になったキャッシュ同期と
  RD キャプチャ memcpy のコスト。
- **アプリ単位の解像度は採らない**: PPA Blend が拡縮できないため、
  高解像度 canvas を 426x240 デスクトップに混在させるには canvas ごとの
  SRM パスという新設計が要る。全体解像度の切替 (起動時に TOML で決定)
  が正解。Retro (320x240) は無傷。
- **着手前の必須作業**: render_frame の時間計測を display_p4_task に
  仕込む (現状 GFX STATS は cmds/s・presents/s のみで、フレーム所要時間の
  計測が無い)。計測してから B を判断する。

## 5. Linux sim の解像度を .env のターゲットに連動させる

目的: FMRB_HW_TARGET=TAB5 のとき、sim を Tab5 と同じ解像度で起動し、
Modern の UI 確認を sim で行えるようにする。

- 現状: Rakefile が config/system_conf_linux.toml (320x240 固定) を
  flash/etc/system_conf.toml へコピーするだけで、ターゲットと無関係。
  Rakefile は FMRB_HW_TARGET を既に読んでいる (HW_TARGET, Rakefile:93)。
- 実装案: config/system_conf_linux_p4.toml (426x240、将来は 640x360、
  壁紙等も Modern 相当) を追加し、Rakefile のコピー箇所 (Rakefile:688-691)
  で HW_TARGET が P4 系 (TAB5 / NARYAv4) ならそちらを選ぶ。
- 期待根拠: sim の表示経路も INIT_DISPLAY 駆動で fb は動的確保のため、
  toml の値を変えるだけで SDL 側も追随するはず。**最初にこれを実測で確認
  する** (sdl2-display が任意サイズを受けるか、fmrb_screenshot.py /
  fmrb_input.rb の座標系が追随するか)。
- 注意: 検証ツール群の座標は「フレームバッファ座標」前提なので、解像度が
  変わると既存の操作スクリプトの座標値は読み替えになる (ツール側の改修は
  不要の見込み、SHM ヘッダにサイズが載っているため)。

## 6. Spinel 化 (selective_offload の既定路線に接続)

**エンジン方針 (2026-08-10 決定)**: Spinel カーネルが今後の標準、mruby
カーネルは互換性検証の位置づけ。どちらでも動く状態を維持する。
エディタも段階 6 で FMRB_APP_ENGINE_EDITOR による .env 切り替えにする
(A/B 計測・切り分け・退避のため)。ただし段階 4 の editor-gem は
syntax-highlight と同じ「ただのネイティブ gem」であり、切り替えは
作らない (純 Ruby 版文書モデルの並走維持はしない)。
検証対象の構成は 2 点に絞る: **標準構成** (kernel=spinel, desktop=mruby,
editor=spinel) と**互換構成** (全部 mruby)。混成は「動くはずだが
検証対象外」とする。

成立する。ビルド機構 (gen_app_combined.rb の APPS 表、CMake、spawner の
NATIVE 経路) は desktop 用が拡張前提で整備済み、gfx FFI はエディタの
描画呼び出しを 100% カバー済み。機械的な配線は 5 箇所程度の小改修。

障害 3 つ (工数順):

1. **Task.new + 外部ローカル捕捉ブロック** (入力ループ、editor.app.rb:161)。
   Spinel ランタイムに Task が無く、escaping proc 制約にも当たる。
   desktop と同じ on_event/on_update 型への作り直しが必須。
2. **FMRB::Debug (19 呼び出し)**: FFI 未整備 + symbol キー Hash 多用。
   selective_offload.md の提案どおり**デバッガ UI は mruby 側に残す**。
3. **sp_io の VFS 接続が PENDING** (esp32_host_deps_sweep.md。エディタの
   ファイル操作が該当)。

効果の見積もり (カーネルでの実測 phase6.md T6-3 から): 解釈部分は約 4 倍、
**25ms 超の停止が 16/1000 件 → 0 件**。ただし **Spinel は gfx コマンド量を
減らさない** — 全面再描画の 200-400 コマンドはそのまま。よって差分描画が
先 (7 章)。

## 7. 入力の引っかかり (GC) への対策 — 懸念は正当、対策は段階的

懸念: 入力ループとUIを Ruby (mruby) に残す構成では、エディタ VM の GC で
打鍵処理が詰まらないか。

事実関係:

- カーネル側の経路は既に解決済み: HID は host_task (C) → カーネル
  (Spinel 化済み、.env で有効) → アプリキュー。カーネル mruby 時代の
  「負荷時 max 89ms、25ms 超 16/1000 件」は Spinel 化で 0 件になった実測が
  ある。残る詰まり要因は**エディタ VM 自身の処理と GC だけ**。
- mruby の GC は incremental だが、停止が伸びるのはゴミ発生量が多い時。
  現在のエディタは打鍵のたびに全面再描画 + ハイライト時の全文 join で
  ゴミを大量生産しており、**GC が詰まる条件を自分で作っている**。

対策 (順に効く):

1. **ゴミ発生量を桁で減らす**: dirty-line 差分描画 (変更行だけ再描画)、
   全文 join の撤廃 (行単位の増分トークナイズ)。GC の頻度・停止時間は
   発生ゴミ量に追随して下がる。純 Ruby 改修で即着手可能。
2. **文字列操作を mruby ヒープから追い出す**: editor-gem 分割
   (selective_offload §5.1) で文書モデル (行バッファ・編集操作・検索) を
   Spinel/C 側へ。mruby 側に残るのは UI の薄い層だけになり、ヒープが
   小さく安定して GC 自体が軽くなる。
3. **計測器は既にある**: 入力遅延は spx: hid_lat (1000 イベントごと)、
   GC は doc/gc_monitoring.md の計装。1 の前後で数値を取り、目標
   (例: 打鍵→present p99 < 33ms、25ms 超ゼロ) を決めて判定する。
4. 必要なら mruby GC の調整 (step ratio 等)、最終手段はエディタ全体の
   Spinel 化 (入力経路から mruby が消える)。

## 8. 段階計画

| 段階 | 内容 | 依存 | 状態 |
|---|---|---|---|
| 1 | 全画面フラグ + 計測整備 | なし | **完了** (9 章) |
| 2 | Linux sim の .env 連動解像度 (5 章) | なし | **完了** |
| 3 | 差分描画 + 全文 join 撤廃 + 入力経路の直結化 | 1 の計測 | **完了** |
| 4 | editor-gem 分割 (文書モデルを Spinel へ) | 3 | 未着手。**50KB 級ファイルで VM 死 (9 章) の解決先** |
| 5 | 640x360 全体切替 (段階 B)。sim 側 (2) を 640x360 に更新 | 1 の計測 | 未着手 |
| 6 | エディタ本体の Spinel 化 (sp_io VFS 解消後。Task 再構成は P1 で解消済み)。**FMRB_APP_ENGINE_EDITOR で .env 切り替え可能にする** | 4 | 未着手 |

段階 1-3 は Spinel 無関係に効く。4 以降は selective_offload.md の
決定済み路線の実行にあたる。

## 9. P1 実装結果 (2026-08-10、詳細は report/p1.md)

段階 1-3 を実装済み (コミット 9ab9c80 / 06139e6 / 17f3eef / fe4336c)。
目標 (p99 < 33ms、25ms 超ゼロ) は全条件達成:
全画面/小ファイル 29.1ms → 2.50ms (mean)、10.9KB HL on は
「エディタ停止」→ 2.29ms。**HL_AUTO_LIMIT_BYTES は撤廃済み**
(20.7KB でも mean 1.8ms)。

**計測が事前の想定を 2 つ覆した**:

- 遅延の主因は描画でも GC でもなく**ポーリング 2 段**だった
  (@input_buffer + Task の sleep 33ms と on_update 33ms の合計。
  全面再描画自体は 2.0-2.7ms)。差分描画だけでは届かないため、
  キーを on_event で直接処理する形に入力経路を変更した。
  7 章の GC 懸念は現状のファイルサイズでは顕在化していない。
- 全画面時の窓サイズを display から入れる分岐は SYSTEM_APP 限定で、
  USER_APP は default_user_app_* に落ちる → spawn_builtin_app 側で対応。

付随修正: 全画面アプリの上端 13px にクリックが届かない問題
(kernel が無条件にデスクトップへ回していた) / 検索ダイアログの Enter が
sim で効かない問題 (keycode → scancode)。
shell → editor のファイル引き渡しは全画面では原理的に動かない
(spawn 直後に要求元が suspend される) ため、**kernel の spawn 要求に
open_path を追加**し、kernel 自身が tick_process で再送する形にした。

**既知の未解決**: 50KB 級のファイルを開くとエディタ VM が例外ログ無しで
消える (20.7KB は正常)。ハイライトではなく文書モデルが 512KB プールを
使い切るためで、段階 4 (editor-gem 分割) で解く。

実機の見た目・操作感・実機遅延計測はユーザ確認待ち (report/p1.md)。

**P2 (instruction_p2.md) を発行済み**: GUI からの起動、窓 ⇔ 全画面の
実行時トグル (再 spawn ではなく同一 VM。P4 の canvas を画面サイズ確保に
変更 + カーネルに実行時 enter/exit fullscreen 経路)、および sim で
見つかった**全画面入れ子の復帰バグ** (全画面エディタ → F5 で全画面 RPG →
Ctrl+Q で戻るとタスクバーが出る。@fullscreen_pid 単一スロットで入れ子を
知らないのが見立て) の修正。実行時 fullscreen 経路は多重 VM 計画の
ギャップ 5 (窓モードの後から変更 API) への答えにもなる。
