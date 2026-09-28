# 作業指示書 H1: 表示 (display_p4 と wasm の C 側)

対象: 実装担当のサブエージェント。前提: plan.md の「確定した設計」、
report/h0.md (特に 2 章・3 章・7 章・8.2・8.3・8.5・8.7)。report は `report/h1.md` へ。

## ゴール

表示タスクが `SET_SCREEN_MODE` を受けて、426x240 (3 倍) と 640x360 (2 倍) を
行き来できる。H1 ではカーネルからは送らず、**試験用の口**から送って確かめる
(カーネルの判定は H2)。

## やること

1. **fb と倍率**: 640x360 の fb を 1 枚、最初の使用時に PSRAM に取る
   (426x240 の fb はそのまま残す)。倍率は定数をやめて fb の大きさから導く。
   present / present_patch (Tab5 と NARYAv4)、CPU の代替、wasm の backend を追従。
   余白 (DSI 側で絵の無い部分) の黒塗りを、モードが変わるたびに行う。
2. **SET_SCREEN_MODE{owner_canvas_id, w, h, flags}**: GFX コマンドとして足す
   (graphics-audio 側の定義との整合は、graphics-audio に同じ型の一覧があれば
   番号だけ予約してよい。graphics-audio のコードは変えない)。
   - 高解像度中は重ね合わせを飛ばし、持ち主の canvas 1 枚を fb に写して 2 倍で SRM。
   - 持ち主の canvas が新しい大きさで present するまで前の絵を保つ (500ms 程度で打ち切り)。
   - 高解像度中に持ち主の canvas が消えるか隠れたら、自分で通常へ戻す。
   - 通常へ戻るときは、デスクトップと他の canvas を全部描き直す。
3. **canvas の取り直し**: UPDATE_WINDOW などで確保より大きい要求が来たら取り直す
   (縮めない)。
4. **カーソル**: 重ね描きの倍率をモードに合わせる (画面上 32px になるのは許容)。
5. **静止画の取り込み**: 取り込みバッファは最大の大きさで取り、1 枚ごとに幅と
   高さを記録する。capture_acquire、EXPORT_FRAME、JPEG の初期化
   (`rd_encoder_jpeg_init` が初期化済みだと大きさを変えずに戻る件) を追従。
   tab5_screenshot で 640x360 が取れること (道具側で大きさを読んでいなければ、
   tools/mcp/lib/tab5.rb の最小限の追従は H1 に含めてよい)。
6. **試験用の口**: アプリから SET_SCREEN_MODE を直接出せる最小の口
   (開発用。リリースでは外れる既存の仕組み (dev フラグ等) に揃える)。試験用の
   アプリ (例 `flash/app/test/hires_test.app.rb`) で、全画面にして 640x360 の
   格子と文字を描き、通常へ戻せるもの。

## 検証

- ビルド: TAB5 / NARYAv4 / S3 (NARYAv3) / Linux / wasm が通る。静的な D/IRAM が
  develop (bfe3b906 以降の develop) と同じ (TAB5 183,448、NARYAv4 178,850、S3 は
  develop で測り直して比べる)。
- 実機: 今つながっているのは Tab5 (`/dev/ttyACM0`、`rake attach` は自分で実行して
  よい)。flash / serial_start / serial_stop / tab5_app は許可済み。
  - 試験用アプリで高解像度へ入って戻る、を 10 回。Guru 0、tab5_screenshot で
    640x360 の絵と 426x240 の絵が取れる。
  - **焼いて最初に高解像度に入った時点と、通常へ戻った時点で止まり、ユーザに
    目視を依頼する** (崩れ・ちらつき・余白の残像)。
- ブラウザ版 (web_* ツール): 同じ試験用アプリで入って戻る。見た目の大きさの
  追従は H4 なので、H1 では「崩れずに描ける」まで。
- NARYAv4 (P4-Nano) がつながっていれば、アンダーランの件数が増えないこと
  (つながっていなければ report に「未」と書く)。

## 作業の決まり

- 本体 checkout の作業ブランチ `feature/fullscreen-hires`。コミットは 2-4 本に
  分け、自分が変えたファイルだけをパスで指定する。push とマージはしない。
- .env は書き換えずコミットしない (対象は環境変数で渡す)。最後に `git diff .env` が
  scratchpad の env_before_hires.diff と一致することを確かめる。
- sdkconfig は編集禁止。graphics-audio のコードは変えない。
- 内蔵 RAM を増やさない。
- Guru が出たら、ログを残して止まる。
- report は日本語の常体。コードのコメントと commit メッセージは英語。

## 受け入れ条件

- 試験用アプリで、Tab5 とブラウザ版で高解像度へ入って戻れる。ユーザの目視で
  崩れ・残像が無い
- 5 構成のビルドが通り、静的な D/IRAM が増えていない
- H2 でカーネルから呼ぶための入口 (関数・コマンド) が report に書いてある
