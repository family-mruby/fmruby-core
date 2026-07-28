# Phase B3.5 指示書: 実行ループ基盤とエディタ RUN ボタン

前提: Phase B3 完了。B4 (周辺・体験) の前に、「エディタで編集 -> RUN ->
確認 -> 直して再 RUN」のループを成立させる。2 部構成:
A = アプリ基盤の修正 (B3 report sec 9 の 3 件。BASIC に限らず全アプリに
効く)、B = エディタへの RUN 機能追加。

BASIC フェーズの一部だが、触るのは主に main/app/ と system_desktop /
default_app の mruby Ruby であり、components/basic はほぼ触らない。
mruby 側 Ruby の編集では picoruby の既知の癖 (block 呼び出しコスト、
配列多重代入不可、bare CONST 解決など。過去の feedback 参照) に注意する。
エンジン設定は .env の現状 (mruby kernel / mruby desktop) で検証する。

## A. アプリ基盤修正

### T3.5-1: kill の協調終了化 (プールリークの解消)

現状: `fmrb_app.c` の `fmrb_app_kill()` はタスクを強制 delete し、コード
コメント自身が「mem handle / semaphore 等をリークする緊急経路」と明記して
いる。debugd の spawn/kill やエディタ RUN の再実行はこの経路を通るため、
kill のたびにプールが漏れ、同名アプリの再起動が alloc 失敗する
(B3 report sec 9-1)。

方針: kill を 2 段にする。

1. **協調終了を先に試す**: exit 要求フラグ + notify でアプリ自身に
   通常終了させ、既存の app_task_main クリーンアップ + kernel reaper の
   経路に乗せる (リソース解放はこの経路が既に正しく行う)。
   - BASIC アプリ: コアは 32 ステートメントごとの `on_tick`
     (= `fmrb_app_poll_exit_signal`) を見ている。**INKEY$(0) などの
     ブロッキング待ちループでも exit 要求を見ていることを確認**し、
     見ていなければ直す (B3 で「INKEY$(0) 待ちの kill で必ず踏む」と
     報告されているので、ここが穴の可能性が高い)。
   - mruby アプリ: on_update 駆動なので同様に確認。
2. **タイムアウト時のみ強制 delete にフォールバック** (現行コードの経路)。
   その場合も、コンテキストから辿れる既知リソース (mem handle、
   semaphore、canvas 破棄依頼) を明示的に解放してから FREE に遷移させる。

注意: `fmrb_app_kill` / `fmrb_app_reap` の状態遷移 (STOPPING/FREE、gen、
task ハンドルの null 化) は SMP の自己削除レースを踏んだ末の形なので
(reap のコメント参照)、遷移の意味を変えない。変更は最小に。

### T3.5-2: kill されたアプリの canvas 残留の解消

B3 report sec 9-2。T3.5-1 の協調終了に乗れば通常経路の canvas destroy で
消えるはず。強制フォールバック時も kernel から canvas destroy を送る。

### T3.5-3: spawn されたアプリへのキーフォーカス付与

B3 report sec 9-3。debugd spawn (今後はエディタ RUN も) で起動したアプリが
デスクトップのフォーカス対象にならず、キーを効かせるには一度画面クリックが
必要。system_desktop 側のフォーカス/ウィンドウリスト管理を調査し、
spawn 完了時に新アプリへフォーカスを移す。フルスクリーンアプリも対象。
関連する持病として「canvas 生成でウィンドウリストが更新されない」問題が
過去に記録されているので、対症ではなく更新経路の根本を確認すること。

副次効果の確認: B2 でキー入力ケースは「sim では決定的に再現できない」と
してスキップされた (`basic_screen_check.py` の `.keys` ケース)。フォーカスが
決定的になったら、これを sim でも回せるか試し、結果を report に書く。

## B. エディタ RUN

### T3.5-4: Run メニューと F5

`main/prebuild_scripts/default_app/editor.app.rb` (メニューバーと Debug
ドロップダウンが既にある) に Run を追加する。

- 操作: メニュー「Run」または F5。
- 動作列: 未保存なら保存 (無題なら保存ダイアログ) -> 前回 RUN した
  インスタンスが走っていれば kill (T3.5-1 の協調 kill) -> spawn ->
  フォーカスは新アプリへ (T3.5-3)。
- **アプリ -> kernel の spawn 依頼経路を 1 本追加する**。エディタは
  アプリ VM なので desktop の `spawn_app()` を直接呼べない。既存の
  app -> kernel メッセージ機構に SPAWN 依頼を足し、kernel 側は debugd の
  spawn 処理と同じ実装に合流させる。パスは spawner が既に受け付ける範囲
  (/app、/home 以下) に限定する。
- 対象は .rb / .bas の両方 (spawner の拡張子ディスパッチに任せる)。
- RUN で起動したアプリからエディタへ戻る導線を確認し、フルスクリーン
  (BASIC ゲーム) の場合の戻り方 / 止め方を report に記録する。
- E2 デバッガとの関係: RUN で起動したプロセスに Debug メニューから
  attach できることを確認する (新機能は作らない。確認のみ)。

## T3.5-5: 検証

- **RUN ループ耐久**: headless でエディタを起動し、fmrb_input.py で
  .bas を開いて F5 -> 起動確認 -> エディタへ戻って再 F5、を 10 サイクル。
  プール使用量ログが安定していること (リークなし)、毎回クリックなしで
  キーが届くこと。
- kill 経路: INKEY$(0) 待ちの BASIC アプリを debugd から kill -> 同名
  再 spawn が成功すること。
- 既存回帰: `rake basic:test` / `tools/basic_screen_check.py` /
  ランチャからの通常起動・終了 / E2 デバッガの attach が全部通ること。

## 受け入れ基準

1. kill -> 再 spawn 10 回でプール使用量が安定 (数値を report に)
2. kill 後の canvas 残留なし (次画面が正しく前面に出る)
3. debugd spawn / エディタ RUN 直後にクリックなしでキー入力が届く
4. エディタの Run メニュー / F5 で .bas と .rb が起動し、再 RUN が通る
5. 既存回帰 (golden / screen check / 通常起動 / デバッガ attach) green
6. `reports/phase_b3_5_report.md` 完成 (フォールバック強制 kill が
   発動する条件と、その場合の残リスクを明記)

## 報告

`reports/phase_b3_5_report.md`。基盤修正 (A) はアプリ全体に効くため、
変更した状態遷移を before/after の図か表で残すこと。
