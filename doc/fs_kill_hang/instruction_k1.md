# 作業指示書 K1: 再現・特定・修正

対象: 実装担当のサブエージェント。前提: plan.md、doc/iram_reduction/report/r2.md
(「既存の問題」の節)、doc/archive/app_kill_fix/ (kill の強制経路の経緯)。
report は `report/k1.md` へ。

## T1: 再現と特定

1. **sim で再現を試す** (Linux の file HAL が同じ錠の作りかをまず確かめる)。
   大きなファイルを書き続けるアプリを起動し、書いている途中で sim_app の kill。
   その後、別のアプリの起動・ファイルの読み書き・エディタの保存が通るか。
   止まったら、**再起動する前に** gdb で全スレッドの backtrace を取る
   (ルートの CLAUDE.md の手順)。どの錠で誰が待っているか、錠の持ち主は誰だったか。
   sim の書き込み先はホストのファイルなので、繰り返してよい。
2. sim で再現しなければ (Linux の HAL が違う作りなら)、**実機 (P4-Nano) で最小限**:
   2MB 程度の書き込みの途中で kill を 1-2 回。止まったらシリアルのログを残す。
   実機では gdb が当てられないので、錠の取得と解放、強制終了の経路に一時的な
   ログを入れて、どこで止まっているかを特定する (測定後に外す)。
   **flash への書き込みは全体で 10MB 以内** (ユーザの指示: 過剰な書き込み試験は避ける)。
3. 強制終了の経路 (fmrb_task_delete / force_release_resources) を通ったか、
   協調終了 (should_exit) で止まったのかも分ける。

## T2: 修正

- plan.md の候補を比べ、案と理由を report に書いてから実装する。
  - 大きな設計変更 (ファイル操作を専用タスクに移すなど) になるなら、実装の前に
    止まって案を返す。
- 書きかけのファイルの扱いを決めて report に書く。
- ESP32 と Linux (sim) と wasm の HAL で、同じ種類の穴が無いかも見る。

## 検証

- sim: 書き込みの途中の kill を繰り返しても、その後のファイル操作が通る。
  標準構成と互換構成 (全 mruby)。
- 実機 (P4-Nano): 最小限の回数 (1-2 回) で同じことを確かめる。遠隔のファイル転送、
  エディタの保存も通ること。Guru 0。flash への書き込みは全体で 10MB 以内。
- ビルド: TAB5 / NARYAv4 / S3 / Linux / wasm。静的な D/IRAM が develop を超えない。

## 作業の決まり

- 作業ブランチ `feature/fs-kill-hang` (親が develop から切る)。自分が変えた
  ファイルだけをパスで指定してコミット。push とマージはしない。
- .env は書き換えずコミットしない。最後に `git diff .env` が scratchpad の
  env_before_fskill.diff と一致することを確かめる。
- 実機は /dev に無ければ rake attach を自分で。flash / serial / tab5_* は許可済み。
- sdkconfig、graphics-audio、submodule・外部の部品は変えない (IDF の esp_littlefs の
  錠の扱いに踏み込む必要があれば、案として書く)。
- report は日本語の常体。コードのコメントと commit メッセージは英語。

## 受け入れ条件

- 止まる原因 (どの錠、どの経路) が特定され、根拠 (backtrace かログ) がある
- 修正後、書き込み途中の kill の後もファイル操作が止まらない (sim と P4 実機)
- 内蔵 RAM が増えていない
