# 作業指示書 L1: カーソルの遅れの計測と修正

対象: 実装担当のサブエージェント。前提: plan.md。report は `report/l1.md` へ。

## T1: 計測を入れる

- `main/drivers/display_p4/display_p4_task.cpp` の受信ループ (`process_message` を呼ぶところ) と、host 側の
  カーソルの送信 (`main/kernel/host/host_task.c` の `host_send_cursor_position`) を読み、次を測る仕組みを入れる。
  - カーソルの位置の命令が host から送られてから、display タスクが処理するまでの時間 (平均・最大・件数)。
    同じチップの中なので時刻はそのまま比べられる (送る側で時刻を記録し、受ける側で差を取るなど。
    やり方は任せる)。
  - その時点で受信バッファに溜まっている量 (メッセージバッファの使用量など)。
  - 1 周あたりに処理した命令の数、描画した枚数と時間 (既存の `render:` 行と並べられる形)。
- 5 秒ごとの既存の統計行 (`display_p4: render:`) にそろえて出す。一時的な計装でよい。常設にする価値が
  あると判断したら、内蔵 RAM を増やさない形で残し、その理由を report に書く。

## T2: 直す前を測る

- 実機は P4-Nano (NARYAv4)。/dev に無ければ `rake attach` を自分で。シリアルは MCP の serial_* (親が開いた
  capture が動いているはず。開き直さない)、書き込みは flash (app_only でよい)、アプリの起動・停止は tab5_app、
  カーソルの移動は tab5_input (遠隔の入力が host のマウスの経路を通るかをまず確かめる。通らなければ、
  計測の方法を report に書いて、ユーザのマウス操作が要る地点で止まる)。
- ビルドは `.env` を書き換えず、`FMRB_HW_TARGET=NARYAv4 rake build:esp32` (ターゲットを替えるときは
  `rake clean_all`)。ビルドログの `HW target:` で確かめる。
- 条件: (a) アプリ 0 個 (デスクトップと Services だけ)、(b) Monitor・MML (/app/demo/mml.app.rb)・
  Breakout.py・FM-Editor を起動した状態。Breakout.py と FM-Editor のパスは ps / ランチャーの定義から調べる。
  それぞれで、カーソルを動かし続けて 30 秒以上測る。

## T3: 直す

- plan.md の方針どおり、1 つのタスクのまま:
  1. 描く前に、届いている命令を時間の上限つきでまとめて処理する (1 周 1 件をやめる)。
  2. 描画が締め切りを超えても、命令を処理する時間を確保する (Retro の graphics-audio `0f0a054` の
     「最低 10 ms は休む」に当たるもの。P4 では休むより「処理してから描く」の形が自然なはず)。
- カーソルの小さな範囲の描き直し (`cursor_overlay_update`) はそのまま使う。
- 描画のまとめ方を変えることで、動画 (`video_service`)、画面の切り替え (`screen_mode_poll`)、
  EXPORT_FRAME、全画面の高解像度モードが壊れないことを確かめる (コードの読みと実機)。
- T2 の結果が見立てと違ったら (カーソルの命令が待っていない、など) 直さずに止まり、測った値と案を返す。

## T4: 直した後を測る

- T2 と同じ条件・同じ時間で測り、前後を表にする (待ち時間の平均・最大、溜まった量、描画の枚数と時間、
  アプリの GFX STATS)。
- 回帰: ブラウザ版 (wasm) のビルドと web_up での起動 (display_p4 は wasm でも使う)、`rake test`。
  Tab5 はつながっていなければビルドだけ。
- 一時的な計装を外したなら、外した版をもう一度焼いて起動を確かめる。

## 作業の決まり

- 作業ブランチ `feature/p4-cursor-lag` (親が develop から切る)。本体の checkout で作業する。自分が変えた
  ファイルだけをパスで指定してコミット。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_cursor_lag.diff と
  一致することを確かめる。
- sdkconfig、graphics-audio、submodule は変えない。内蔵 RAM (静的な D/IRAM) を develop より増やさない。
- 目視の確認は親がユーザに依頼する (HDMI の出力はサブから見えない)。ユーザとのやり取りはしない。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。
- 終わったら build/ は NARYAv4 のビルドのまま残してよい (親が焼き直しに使う)。

## 止まる条件

- 遠隔の入力でカーソルが動かせず、ユーザのマウス操作が要るとき。
- 測った値が見立てと違うとき。
- 範囲外の変更 (graphics-audio、host のプロトコルの大きな変更、タスクの追加) が要ると分かったとき。

## report に書くこと

計測の方法、前後の表、直した内容と理由、見立てと違った点、撤回した仮説、残件。
