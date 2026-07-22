# PicoRuby VM リモートデバッグ 残課題整理 (Phase 3c 完了時点)

作成: 2026-07-22。`feature/vm-remote-debug` ブランチの区切りにあたり、
未実施の課題を一つに集約したもの。正となる設計/計画は従来どおり:

- 設計: `doc/vm_remote_debug_design.md`
- 実装計画: `doc/vm_remote_debug_impl_plan.md` / `doc/vm_remote_debug_impl_plan2.md`
- プロトコル: `doc/vm_remote_debug_protocol.md`
- 経緯: `doc/vm_remote_debug_progress.md`

## 到達点 (2026-07-22)

Phase 0-3c 完了。S3 実機 (Retro) で BLE E2E を確認済み:

- VSCode から launch.json なしで F5 -> BLE スキャン -> 実行中アプリの
  QuickPick -> attach (拡張 v0.0.7、ルートリポジトリ 8cccace)
- BP 停止・該当行ハイライト (ワークスペースの同一ドキュメント)、
  コールスタック・変数表示、step/continue、切断 -> VM 走行再開、再 attach
- 停止中も他アプリ・デスクトップは動作継続 (VM 単位パーク)
- 応答の体感速度は問題なし (ユーザ確認)

実機デバッガとしての基本機能はここで完成。以下はすべて拡充・計測・保留分。

## 1. Phase 3d: P4 (esp-hosted vHCI) 対応

コードは S3 と共通のはずで、作業は疎通と実測 (impl_plan2 sec 6):

1. P4 (NARYAv4 / Tab5) + C6 で attach/BP/step の疎通確認。
   P4 実機のフラッシュ確認自体がまだ未報告。
2. スループット実測 (S3 native 比)。遅い場合は ble_send_notify の
   チャンク間 delay (現行 5ms) と stack_trace max_frames 既定値を調整。
3. 切断再接続の安定性 (esp-hosted RPC タイミング、boot.c の
   radio init 順序コメント参照)。
4. 注意: .env が FMRB_HW_TARGET を無条件上書きするため、P4 ビルド時は
   .env の切り替えが必要。

## 2. Phase 4: 仕上げ (impl_plan2 sec 7、優先度順)

1. evaluate: PARK_EVAL 追加、mrc で式コンパイル + 停止フレームで実行
   (mruby-binding / mruby-proc-binding)。GC・例外後始末に mrb_protect_error。
2. 変数の詳細展開: frame_vars に expand を追加し Array/Hash/ivar を
   1 階層ずつ。DAP variablesReference 対応。
3. log_stream push: log_stream コマンド + output イベント
   (ble_fs_poll_logs と同じ差分読み出し、レベルフィルタ必須)。
4. DEBUGGING プロセス状態: カーネル UI の kill/suspend とパークの競合が
   実際に問題化するか観察してから導入判断。
5. パッケージング: VSIX 配布整理、アダプタの pyinstaller exe 化、
   セットアップドキュメント (Windows / WSL 両構成)。
6. ドキュメント: protocol.md へ BLE フレーミング追記、design.md 最終更新、
   進捗ログのクローズ。

## 3. 計測・確認の積み残し

- MRB_USE_DEBUG_HOOK の実機ループベンチ (あり/なし比較)。オーバーヘッド 5% 超なら
  debug ビルド変種化を検討 (impl_plan2 sec 3.3)。なしビルドは cmake + rake の
  両方から define を落として rake clean_all が必要 (ABI 一致)。
- TCP デバッグの回帰確認: 拡張の ui 化により TCP でもアダプタは Windows 側で動く。
  Linux シミュレーションへの TCP attach (localhost:5555 フォワード) を
  新構成で 1 回 E2E 確認する。
- combined ファイル (/project/ マッピング + *_combined.map.json) の
  フレーム表示確認。実機 E2E は standalone アプリ (kamon.app.rb) のみで実施。
- BLE MTU の実測: WinRT で mtu_size が取れない場合の 20 バイトフォールバックが
  実際に発生するか、および転送量への影響。

## 4. 保留した改善候補 (現状問題なしと判断)

- attach 時の 2 接続構成 (ps 用に BLE 接続 -> 切断 -> アダプタで再接続)。
  体感速度に不満が出たら「1 接続で ps -> attach」へ最適化する。
- cobs_encode の容量ガードが COBS 最悪ケースを保証しない件 (fs 由来の既存事項。
  debug 側はバッファ設計 BLE_DBG_MAX_ENC=4120 で安全)。
- 拡張の診断ログ (Output -> "fmrb debug") は残置。トラブルシュートに有用。

## 5. 再開時の環境上の注意 (ハマりどころ)

- VSCode 拡張は extensionKind ["ui"] で Windows 側で動く。拡張ホスト内の
  node fs は \\wsl.localhost UNC を UNC ホスト許可リストで黙って弾く
  (existsSync が false になる)。ファイル存在確認は vscode.workspace.fs +
  vscode-remote URI を使うこと (v0.0.6 で対応済み。node fs に戻さない)。
- アダプタが返す source.path は URI 文字列 (vscode-remote://...) で渡す
  (v0.0.7)。素の posix パスは UI ホストの VSCode に Windows ローカルパスとして
  解決され、存在しないファイルのタブが開く。
- 拡張の変更は再パッケージ (npx @vscode/vsce package) + VSIX 再インストールが
  必須。ウィンドウリロードだけでは反映されない。
- Windows 側に py ランチャ + bleak / msgpack が必要。
