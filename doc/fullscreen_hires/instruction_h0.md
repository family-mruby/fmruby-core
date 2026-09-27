# 作業指示書 H0: 調査と設計の確定

対象: 実装担当のサブエージェント。前提: plan.md。report は `report/h0.md` へ。
**この段階はコードを変えない** (調査と設計)。

## 調べること

1. **今の全画面の経路**
   - spawner の fullscreen / fullscreen_switchable と、.app.toml の
     `window_mode = "fullscreen"` の読み方 (`main/app/fmrb_app_spawner.c`)。
   - カーネル側の全画面の管理: `@fs_stack`、park、F11 の切り替え、Ctrl+Tab。
   - `fmrb_app_set_fullscreen` と、アプリに届く resize メッセージ
     (`{"cmd":"resize","width","height","fullscreen"}`)。アプリの canvas と
     user area の大きさがどう決まるか。
   - 組み込みのエディタ (spawner の表の `default/editor` 系) の属性。
2. **display_p4 の表示経路** (`main/drivers/display_p4/`)
   - canvas の確保 (P4 は画面の大きさで確保)、PPA Blend での合成、
     SRM で 3 倍して DSI のフレームバッファへ送るところ
     (`display_backend_ppa.cpp`、CPU の代替 `display_backend_cpu.cpp`)。
   - マウスカーソルの重ね描き (3 倍固定の箇所)。
   - 426x240 や SCALE_FACTOR 3 を前提にしている箇所の一覧
     (タッチの仮想解像度、遠隔の画面取得の info 応答 / JPEG / H.264 など)。
   - DSI フレームバッファの 4KB 揃え (dpi_fb_align.hpp) との関係。
3. **ブラウザ版 (wasm) の表示経路**: display_p4 のどこを共有し、どこが別か。
   ブラウザでの拡大の仕方と、画面の大きさの伝わり方。
4. **入力の座標**: マウス・タッチの座標が、どこで 426x240 の座標に直されて
   アプリに届くか。
5. **フォント**: display_p4 と wasm で使えるフォント (名前、大きさ、日本語の
   有無)。エディタが今使うもの (`@gfx.set_font(:ja, 12)`) と、
   `set_font` の選ばれ方 (PicoRabbit で「選ばれたフォントを返す」ようにした件)。
6. **Linux の sim と Retro**: 高解像度を持たない構成で、H2 の属性が黙って
   無視されるための条件 (`FmrbConst` などで分かるか)。

## 決めること (report に案を書く。親とユーザで確定する)

- 高解像度モードの**入り口と出口**: カーネルが全画面に入れるときに決めるか、
  アプリが要求するか。表示タスクへの伝え方 (新しい GFX のコマンドか、
  既存の仕組みの延長か)。全画面を抜けるときに 426x240 の合成へ戻す手順と、
  そのときの再描画 (デスクトップと他の canvas)。
- 全画面の canvas の確保: 高解像度用に 640x360 の canvas を別に取るか、
  今の canvas を取り直すか。PSRAM の量。
- .app.toml のキー名と、spawner の表の属性名。
- カーソル・入力座標・遠隔の画面取得の追従の仕方。
- エディタで選べるフォントの候補と、保存場所 (エディタの既存の設定ファイルが
  あればそこ)。
- H1-H4 の分け方の見直し (今の plan.md の段階でよいか)。
- 想定される落とし穴 (ちらつき、PSRAM の帯域、描画の重さ、F11 や Ctrl+Tab の
  途中で解像度が変わること、異常終了したアプリの後始末)。

## 作業場所と決まり

- fmruby-core の本体 checkout、作業ブランチ `feature/fullscreen-hires`
  (親が切り替え済み)。コードは変えない。report だけ書いてコミットしてよい
  (自分のファイルだけをパスで指定)。push とマージはしない。
- 必要なら sim / ブラウザ版 (web_*) / 実機 (Tab5 が /dev/ttyACM0、tab5_* ツール)
  で今の動作を観察してよい。書き込み (flash) はしない。.env は触らない。
- report は日本語の常体。カタカナの専門用語はなるべく避ける。

## 受け入れ条件

- 上の「調べること」の 1-6 に、該当するファイルと関数の場所つきで答えている
- 「決めること」に、それぞれ推奨の案と理由、代わりの案が書いてある
- 426x240 / 3 倍を前提にしている箇所の一覧がある (H1 の作業範囲になる)
