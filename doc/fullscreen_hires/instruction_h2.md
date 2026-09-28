# 作業指示書 H2: カーネル・アプリ・入力

対象: 実装担当のサブエージェント。前提: plan.md の「確定した設計」、report/h0.md
(8.1・8.4・8.5・8.7)、report/h1.md (3 章の入口と順番、6 章の保留)。report は `report/h2.md` へ。

## ゴール

`.app.toml` の `fullscreen_hires = true` (組み込みアプリは spawner の表の属性) を
持つアプリが全画面になると、カーネルが自動で 640x360 に切り替え、全画面を抜けると
426x240 に戻る。マウス・タッチ・遠隔入力の座標と、マウスの速さが追従する。

## やること

1. **属性**: `.app.toml` の `fullscreen_hires`、spawner の表・attr・ctx の
   `.fullscreen_hires`。app info と C API は mruby 版と Spinel 版の両方
   (Ctrl+Tab のときの教訓: カーネルの C API の拡張は両方が要る)。
2. **構成の判定**: C の 1 関数 (`FMRB_HW_MODERN || FMRB_PLATFORM_WASM` かつ実行時の
   基本の大きさが 426x240)。カーネル Ruby へは `_fullscreen_hires_size`
   (nil か [640,360])。Retro と Linux の sim では nil (黙って従来の全画面)。
3. **カーネル**: `sync_screen_mode` を 1 か所に置き、全画面の積み (enter /
   pop_fullscreen_frames / park / unpark) が変わるたびに呼ぶ。一番上の park 中で
   ない app が属性を持ち、構成が対応していれば高解像度。
   `fullscreen_size` を pid 別にする (F11 で入るときの大きさ)。送る順番は
   report/h1.md 3 章のとおり (入るときは SET_SCREEN_MODE が先でリサイズが後、
   戻るときはリサイズが先で SET_SCREEN_MODE(0,0) が後)。
4. **切り替えの完了の条件の見直し** (report/h1.md 6.4 の i): アプリの on_update が
   resize を処理する前に present すると、古い配置の絵で「持ち主が新しい大きさで
   present した」とみなされうる。完了の条件を「resize を受け取った後の present」に
   するなど、確実にする。これで 1 枚方式の切り替えの一瞬の乱れが減るかも見る
   (減らなくても H2 の合否には関係しない)。
5. **入力**: 「いまの画面の大きさ」を host_task に 1 つ置き、touch / usb / rd_input が
   読む。モードが変わったら、各デバイスのカーソル位置を比率で直す。**マウスの
   速さは倍率で補正し、画面上の速さを今と同じに保つ**。ブラウザ版はページ側で
   追従する (H0 の見立て) ことを確かめる。
6. **組み込みのエディタに属性を付ける** (editor と editor_fs。P4 系で既定オン)。
   エディタ自体の配置の計算やフォントは H3。H2 では、640x360 の全画面で起動して
   打鍵でき、F11 で窓と往復でき、Ctrl+Tab で切り替えて戻れるところまで。
7. 試験用アプリ (flash/app/test/hires_test) にも `.app.toml` の属性を付け、
   試験用の口 (`_dev_screen_mode`) を使わなくても全画面で高解像度になることを確かめる。

## 検証

- sim (標準構成): 従来どおり 426x240 の全画面になること (高解像度にならない)。
  エディタの起動・打鍵・閉じる・再起動・打鍵 (fmruby-core/CLAUDE.md の標準手順)。
- 実機: NARYAv4 (P4-Nano) と、つながっていれば Tab5。`rake attach` は自分で
  実行してよい。flash / serial_start / serial_stop / tab5_app は許可済み。
  - エディタを全画面で起動 → 640x360、打鍵、F11 で窓 (426x240) と往復、
    Ctrl+Tab で別アプリと往復、エディタを閉じてデスクトップに戻る。これを数回。
  - 試験用アプリを .app.toml だけで高解像度に。
  - Guru 0、アンダーラン 0 (ログ)。tab5_screenshot で 640x360 と 426x240。
  - 最後に止まって、**ユーザに 1 回だけ目視を依頼する**。見てほしい点は具体的に書き、
    時刻の照合は求めない (例: エディタの全画面が画面いっぱいに出るか、F11 と
    Ctrl+Tab の往復で崩れが残らないか、マウスが画面の端まで届き速さが自然か)。
- ブラウザ版 (web_*): エディタの全画面で高解像度になり、戻れる。
- ビルド: TAB5 / NARYAv4 / S3 / Linux / wasm。静的な D/IRAM が H1 の値
  (TAB5 183,008、NARYAv4 178,410、S3 146,995) を超えない。

## 作業の決まり

- 作業ブランチ `feature/fullscreen-hires`。コミットは 2-4 本、自分が変えた
  ファイルだけをパスで指定。push とマージはしない。
- .env は書き換えずコミットしない。最後に `git diff .env` が
  scratchpad の env_before_hires.diff と一致することを確かめる。
- sdkconfig は編集禁止。graphics-audio は変えない。内蔵 RAM を増やさない。
- Guru が出たらログを残して止まる。
- report は日本語の常体。コードのコメントと commit メッセージは英語。

## 受け入れ条件

- 属性を持つアプリが全画面で自動的に 640x360 になり、抜けると 426x240 に戻る
  (NARYAv4 とブラウザ版。Tab5 はつながっていれば)
- 入力の座標が画面全体に届き、マウスの速さが今と同じ感覚
- sim と Retro は従来どおり
- ユーザの目視で、F11・Ctrl+Tab・閉じるの往復に崩れが残らない
- 静的な D/IRAM が H1 の値を超えない
