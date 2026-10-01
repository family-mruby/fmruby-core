# 作業指示書 F1: フラッシュの書き込みと青いちらつき

対象: 実装担当のサブエージェント。前提: plan.md、doc/naryav4/report/p6.md (前回の青ちらつきの調べ方)、
doc/reference/dpi_frame_buffer_alignment.md。report は `report/f1.md` へ。

## T1: 仕組みを確かめる

- 保存 1 回で何回・何 ms キャッシュが止まるか (LittleFS の消去・書き込み、`esp_flash_*` の区間) を測る。
- その間に DSI の送り出しで何が起きるか。アンダーランの割り込みを数える (ログでなく、カウンタで数えて後で出す)。
  ブリッジ (LT8912B) の同期が外れているかも見られるなら見る。
- 書き込み元は、LittleFS (/home、/etc) と、遠隔のファイル転送 (/fs/put) と、エディタの保存。どれも同じ形か。

## T2: 自動中断を試す (試験用のビルド)

- `CONFIG_SPI_FLASH_AUTO_SUSPEND=y` を、**リポジトリの sdkconfig.defaults を変えずに**試す (scratchpad の追加の
  defaults ファイルを `SDKCONFIG_DEFAULTS` に足す、別の build ディレクトリ、のどちらか。doc/iram_reduction/report/r3.md
  の T1 のやり方)。依存して変わる設定 (Kconfig) と、IRAM の増減を書く。
- 前後で: 保存 1 回のキャッシュの停止時間、アンダーランの数、保存の時間、描画 (render)・入力 (hid_lat、カーソル) の
  速さ。同じコミットのビルド同士で比べる。
- 効かなければ、T1 の結果から別の案を比べて返す (実装はしない)。

## 検証

- 実機は P4-Nano (NARYAv4)。flash / serial / tab5_* は使ってよい。保存の試験は数 KB のファイルを数十回まで
  (flash の寿命の決まり: 繰り返しの書き込みは最小限。試験で置いたファイルは消す)。
- 目視 (ちらつきが消えたか) は親がユーザに依頼する。焼いた版と、ユーザに試してほしい操作を report の最後に書く。

## 作業の決まり

- 作業ブランチ `feature/flash-write-flicker` (親が develop から切る)。本体の checkout で作業する。計測のコードは
  一時的なもの。コミットは自分が変えたファイルだけをパスで指定。push とマージはしない。
- `.env` は書き換えずコミットしない。最後に `git diff .env` が scratchpad の env_before_flicker.diff と一致する。
- **sdkconfig と sdkconfig.defaults* は変えない** (試験は追加の defaults ファイルで)。graphics-audio と submodule も変えない。
- 親から「実機の操作を止めて」と伝えられたら、書き込み・再起動・アプリの起動と停止・遠隔の入力をすぐ止める。
  音を出したくないときは、tab5_audio (MCP) か devctl の `/audio/mute?on=1` でミュートにしてから作業してよい
  (ミュートの機能が入った版で)。
- report は日本語の常体。コードのコメントと commit メッセージは英語 (件名 `<領域>: <要約>`、
  Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>)。

## 止まる条件

- 自動中断で別の不具合 (クラッシュ、フラッシュの内容の破損、無線・USB の不調) が出たとき。
- 根本の対策に、IDF や部品の変更が要ると分かったとき。

## report に書くこと

仕組み (何が止まり、なぜ青くなるか) と根拠、自動中断の前後の表、IRAM の増減、採用するなら変える
sdkconfig.defaults の差分 (案)、見立てと違った点、残件。
