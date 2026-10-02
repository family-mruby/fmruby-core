# 作業指示書 S1: Shell の打鍵の遅れの計測と修正

対象: 実装担当のサブエージェント。前提: plan.md、doc/archive/p4_cursor_lag/report/l1.md (同じ形の計測と修正の
先例)。report は `report/s1.md` へ。

## T1: 計測を入れる

- Shell (main/prebuild_scripts/default_app/shell.app.rb と shell/*.rb) と、打鍵が app に届く経路
  (host → kernel → app のキュー → mruby の app の event 処理) を読み、打鍵 1 回について次の区間を測る:
  1. host がキーを受けてから、Shell の `on_event` が呼ばれるまで
  2. `on_event` から、`getch` がその文字を拾うまで
  3. 拾ってから、描き直しの `present` を送るまで (描き直し自体の時間も)
  4. 必要なら表示側 (display_p4 の `cursor:` 行のような既存の統計) との突き合わせ
- 時刻は `Machine.board_millis` か、µs が要るなら既存の C の手段。5 秒ごとか N 打鍵ごとにまとめてログに出す。
  一時的な計装でよい (常設の価値があると判断したら、内蔵 RAM を増やさない形で残し理由を書く)。

## T2: 直す前を測る

- 実機は P4-Nano (NARYAv4)。/dev に無ければ `rake attach`。シリアルは MCP の serial_* (capture は動いているはず。
  開き直さない)、書き込みは flash (app_only)、アプリは tab5_app、打鍵は tab5_input (遠隔のキー入力が USB の
  キーボードと同じ host の経路を通るかをまず確かめる。通らなければ方法を report に書き、ユーザの打鍵が要る
  地点で止まる)。Shell は built-in (default/shell)。起動の仕方はデスクトップのメニューか devctl の経路を調べる。
- ビルドは `.env` を書き換えず `FMRB_HW_TARGET=NARYAv4 rake build:esp32` (ターゲットを替えるときは
  `rake clean_all`。sdkconfig が前のターゲットのまま残る罠がある: doc/archive/p4_cursor_lag/report/l1.md)。
- 条件: (a) Shell だけ、(b) Shell + Monitor・MML (/app/demo/mml.app.rb)・Breakout.py
  (/app/game/breakout/breakout.app.py)・FM-Editor。それぞれ一定の間隔 (例: 100 ms ごと) で数十〜百打鍵。
- スタック: 同じ状態で Shell の stack の残り (周期ダンプの `fmrb_task:` 行) と、コマンドを実行する経路
  (ls、help、irb など重そうなもの) を通したときの最悪値を記録する。

## T3: 直す

- T2 の数値で待っている区間を特定し、plan.md の候補から選ぶ。選んだ理由を report に書いてから直す。
- Shell は Retro と sim でも動く。Spinel 化や kernel・FmrbApp 基底の変更など範囲の大きいものは、止まって
  案を返す (FmrbApp 基底を触るなら、doc/archive の app_model の規則と、Spinel の ivar レイアウトの罠に注意)。
- スタックが足りないと分かったら手当てし、内蔵 RAM の増え方を書く。

## T4: 直した後を測る

- T2 と同じ条件で測り直し、区間ごとの前後を表にする。
- 回帰: sim の標準構成と互換構成で Shell を起動して打鍵・コマンド (ls / help) が通る (sim_up / sim_input /
  sim_screenshot)。標準構成の検証にはエディタの起動と 1 打鍵も含める (CLAUDE.md)。Retro の解像度の sim も
  ビルドできるなら 1 回。ブラウザ版 (wasm) のビルドと起動。`rake test`。
- 一時的な計装を外したなら、外した版を焼いて起動を確かめる。

## 作業の決まり

- 作業ブランチ `feature/shell-input-lag` (親が develop から切る)。本体の checkout で作業する。自分が変えた
  ファイルだけをパスで指定してコミット。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_shell_lag.diff と一致する。
- sdkconfig、graphics-audio、submodule は変えない。内蔵 RAM (静的な D/IRAM、NARYAv4 の develop は 125,524 B)
  を増やさない (スタックの手当ては実行時の確保なので別に書く)。
- 実機の書き込みは問題ないが、ファイルを flash に繰り返し書く試験はしない。試験で端末に置いたファイルは消す。
- 目視の確認は親がユーザに依頼する。ユーザとのやり取りはしない。
- 親から「実機の操作を止めて」と伝えられたら (通話中など。アプリの起動や再起動で音が出る)、書き込み・再起動・
  アプリの起動と停止・遠隔の入力をすぐ止め、ビルドとコードの作業だけを続ける。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。
- 終わったら build/ は NARYAv4 のビルドのまま残してよい。

## 止まる条件

- 遠隔の入力で打鍵できず、ユーザの打鍵が要るとき。
- 測った値が見立てと違い、直し方の候補に無い手当てが要るとき。
- 範囲外の変更 (Spinel 化、kernel・基底クラスの大きな変更、graphics-audio) が要るとき。

## report に書くこと

計測の方法、区間ごとの前後の表、スタックの記録、直した内容と理由、見立てと違った点、撤回した仮説、残件。
