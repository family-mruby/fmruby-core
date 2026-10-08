# 作業指示書 S2b: estalloc の統計を残して、解放のたびの全走査を止める

対象: 実装担当のサブエージェント。前提: plan.md、report/s1.md、**report/s2.md** (原因の特定と測り方)。
report は `report/s2b.md` へ。

## 決定 (ユーザ、2026-10-01)

- 要るのは**使用量の統計** (周期ダンプの VM Pools、`FmrbApp.pool_usage` など) で、解放漏れ・二重解放のデバッグでは
  ない。統計は残し、`est_free` が解放のたびにプールを先頭からたどる検査は止める。
- gc.c の回収条件 (S2 で試した 1/4 の条件) は入れない。

## T1: 分ける

- estalloc (submodule の components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/estalloc/) の
  `ESTALLOC_DEBUG` の中身を洗い出す: 統計 (`est_take_statistics` など)、解放時の検査 (全走査)、確保・解放時の
  埋め (0xaa / 0xff)、ほか。それぞれの費用と、誰が使っているかを表にする。
- 統計だけを残す形にする。案: 重い検査と埋めを別のマクロ (例 `ESTALLOC_DEBUG_CHECKS`、既定は無効) に分け、
  `ESTALLOC_DEBUG` は統計だけにする。埋めを残すかは費用を測って決め、理由を書く (ユーザの方針: 統計だけでよい)。
- 当て方は lib/patch (submodule は直接編集しない)。既存の lib/patch/picoruby-mruby の当て方 (rakelib/setup.rake) に
  ならう。差分は最小にし、上流 (estalloc) に出せる形 (統計と重い検査を分けられる) を意識する。
- **estalloc を使うのは mruby の VM だけとは限らない**。Spinel の実行時 (est pool)、MicroPython などが同じ
  estalloc を使っているか、どの定義でビルドされているかを確かめ、全部に同じく効くようにする。
  `ESTALLOC_DEBUG` を入れている所は lib/patch/picoruby-mruby/mrbgem.rake と lib/add/family_mruby_*.rb
  (esp32 / esp32p4 / linux / wasm) にある。rake の libmruby と CMake の両経路の定義がそろっていること
  (memory の dual build の罠)。

## T2: 測る

- 実機 P4-Nano (NARYAv4): report/s2.md の (a) Shell だけ、(b) Shell + MML + ブロック崩し系 + Monitor、の
  打鍵から表示までと、全 VM の GC の時間を、同じコミットの前後で。S2 の計測の方法を一時的に戻してよい。
- 統計が従来どおり出ること (周期ダンプの VM Pools の Used / Free / Frag、`FmrbApp.pool_usage`)。
- Spinel のカーネル・エディタにも効くなら、その GC の時間の前後も。
- sim (標準・互換) とブラウザ版でも、GC の時間か打鍵の遅れの前後を軽く。

## 検証

- ビルド: TAB5 / NARYAv4 / S3 / Linux (標準・互換) / wasm。静的な D/IRAM が develop を超えない。`rake test`。
- sim の標準構成の検証にはエディタの起動と 1 打鍵を含める。

## 作業の決まり

- 作業ブランチ `feature/shell-gc` (develop 9c6f83f1 を取り込み済み)。本体の checkout で作業する。自分が変えた
  ファイルだけをパスで指定してコミット。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_shell_gc2.diff と一致する。
- sdkconfig、graphics-audio は変えない。submodule は直接編集しない。lib/ を変えたら `rake clean`。
- 実機は**ミュートのまま**作業する (`curl -X POST http://192.0.2.15/audio/mute?on=1`、または MCP の tab5_audio)。
  ファイルを flash に繰り返し書く試験はしない。
- 親から「実機の操作を止めて」と伝えられたら、すぐ止める。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 統計と検査が分けられない作りで、estalloc を大きく書き換える必要があるとき。
- 検査を止めたら壊れる (二重解放などが実在して表に出る) とき。その場合は直さずに場所を記録して返す。

## report に書くこと

`ESTALLOC_DEBUG` の中身の表、分け方と理由、estalloc の使い手、前後の表 (Shell、全 VM、Spinel)、統計が
出ることの確認、上流への説明の下書き、見立てと違った点、残件。
