# C3p 報告: スタンプの見本のパックと、App Store の一覧の道具

> 状態: 完了 (push は親) | 更新: 2026-11-21 | family-mruby-apps (ブランチ stamp-packs、手元の main `1f0b1de` の上) の一覧の道具に「スタンプのパック」(`stamp_pack = true`、スクリプトなし) を足し、見本のパック `stamps_basic` (32x32 の絵 15 枚) を作った。一覧の見本のアプリ 3 つも今のファームの書き方に直した (1.1.0)。ブラウザ版・sim の Modern・Retro で、App Store から入れる・起動する・チャットでスタンプを送るまでを確かめた。fmruby-core のコードは変えていない

## 1. 一覧の道具 (family-mruby-apps の tools/)

| ファイル | 変えたこと |
|---|---|
| `tools/manifest.rb` | `Manifest.stamp_files(m)`: パックの `*.png` から `<id>.png` (店の絵) と `<id>.thumb.png` を除いて名前順。チャットのアプリ (`chat/stamps.rb` の `pictures_in`) と同じ選び方 |
| `tools/validate.rb` | `stamp_pack = true` ならスクリプトを求めない (あれば誤り: ランチャーに載ってしまう)。パックの検査 (下)。入る全部のファイルに `/app/usr/<id>/<名前>` が 63 バイトまで |
| `tools/registry.rb` | パックのファイルは `.app.toml` + 絵 (名前順)。行の形 (`A`・`F`) は変えていない |
| `tools/selftest.rb` | パックの見本 13 個 (32 個の見本が全部引っかかる) |
| `README.md` | 「Stamp packs」の節 (形・鍵・絵の決まり・透明・絵のライセンス)。分類に `stamp` |

パックの検査: 分類は新しい `stamp` (パックだけが使える)、`app_env` に `retro` を入れない、店の絵は `<id>.png`、絵が 1 枚以上、
1 枚ごとに 48x48 まで・8192 バイトまで・端末の PNG の読み手 (`png332.c`) が読める形 (非インターレース。グレー・パレットは
1/2/4/8 ビット、RGB・RGBA・グレー+α は 8 ビット)・名前は `[a-z0-9_]{1,40}.png`。

パスの長さ: esp_littlefs の `CONFIG_LITTLEFS_OBJ_NAME_LEN=64` は名前 1 つ分の上限 (NUL 込み) で、パス全体ではない
(`esp_littlefs.c` を読んだ)。道具は安全側に、パス全体を 63 バイトに収める形にした。

## 2. 見本のパック `apps/stamps_basic/`

`stamps_basic.app.toml` (名前 Basic / きほん、1.0.0、family-mruby、MIT、`app_env = ["modern", "web"]`、分類 `stamp`) と、
店の絵 `stamps_basic.png` (96x72、6 枚を並べた。一覧の小さい絵 32x24 はその 3 分の 1)、スタンプ 15 枚:

| ファイル | バイト | ファイル | バイト | ファイル | バイト |
|---|---|---|---|---|---|
| 01_smile | 292 | 06_sweat | 324 | 11_question | 265 |
| 02_laugh | 315 | 07_heart | 269 | 12_exclaim | 201 |
| 03_cry | 303 | 08_star | 265 | 13_thanks (花) | 278 |
| 04_angry | 288 | 09_ok (緑の丸) | 298 | 14_good_night (月と Zz) | 282 |
| 05_surprised | 290 | 10_no (赤のばつ) | 254 | 15_thumbs_up | 239 |

- どれも 32x32 のパレットの PNG。色は RGB332 の値を広げたもの、パレットの 0 番が透明 (`0x01` = RGB(0,0,85)、`tRNS` で α 0)。
  端末はどちらの読み方でも `0x01` (透明) になる。形は図形から描き、暗い縁と白い縁を外に足した (明るい地と暗い地の両方で読める)。
- 絵はこの作業で新しく描いたもので、family-mruby-apps の MIT に従う (README と toml に書いた)。文字は Zz だけ。
- 生成のスクリプトはリポジトリの外 (スクラッチの `stamps/gen_stamps_basic.rb`)。拡大した一覧は
  `C:\Users\Public\asterism_deploy\shots\stamps_basic_sheet.png`。

一覧 (`registry.tsv`) の差分は、パックの `A` の行 1 つと `F` の行 16 (toml + 15 枚) の追加だけ (1 つ目のコミット)。

## 3. 見本のアプリ 3 つを今の書き方に (2 つ目のコミット)

hello_store・paint_pad・wide_only は 2026-09 の書き方 (末尾の `begin; app = X.new; app.start; rescue`、`@user_area_x0 +` の座標、
`draw_window_frame`、`super(ev)`) で、**今のファームでは App Store から入れて起動しても Window が出なかった** (ブラウザ版で確認。
パスで起こしても pid は返るが何も出ない。原因の細部は追っていない: 少なくとも `@user_area_x0` はもう無い)。
アプリの側の直しで直るので、fmruby-core は変えていない。

- 3 つとも: 末尾の起動を外した (クラスを定義するだけ)、座標を区画相対に、`draw_window_frame`・`super(ev)` を外した。版は 1.1.0。
- paint_pad: インクを RGB332 の数値に (クラス本体の表)。1 つ目のインクは `theme_fg` (明るい地・暗い地の両方で見える)。白と黄は外した。
  選んだインクの下に棒。
- wide_only: `@user_area_width` で幅を出す、目盛りに数字、`on_resize(w, h)` で描き直す、Esc で `stop` (全画面で閉じるボタンが無い)。
- README: アプリの `.rb` の形の例を足した。「Why a sidecar」の理由を今のものに (ランチャーは今はコメントの囲みも読むが、
  入った `.app.toml` が App Store の導入の記録なので、配布には要る)。

## 4. 確かめたこと

手元の HTTP の口 (CORS つき) で family-mruby-apps-stamps を出し、`/home/appstore_base.txt` をそこへ向けた。

| 端末 | 結果 |
|---|---|
| ブラウザ版 (`rake wasm:web`、develop と同じ `16c95198`) | 一覧に 4 つ。パックに店の絵が出る。入れると `/app/usr/stamps_basic/` に 16 ファイル (「size checked only」はブラウザ版の前からの作り)。ランチャーに Basic は出ない、Hello Store は出る。チャット (ローカルの zenohd の ws + asterism-chat 0.2.0) の一覧に「きほん」のタブと 15 枚、透けて出た。thumbs_up と smile を送り、会話に出た (ホストの記録は `refused no_stamp` → `stamp` (225 B・279 B、32x32) → `post`)。古い hello_store 1.0.0 が入った状態で 1.1.0 の一覧を読むと `^` の印と「v1.1.0 (have 1.0.0)」、入れ直せた。1.1.0 の 3 つは動いた (Hello Store はクリックで数が増える、Paint Pad は描けて右ボタンで消える、Wide Only は 426 幅の全画面で 400 px の目盛り、Esc で閉じる) |
| sim の Modern (build-linux の TAB5 の版。作ったのは今日 14:11、HEAD `f6ad8bd` で develop より少し古い) | コンテナから PC の口へは `host.docker.internal` (IPv4) で届いた。4 つとも入り、sha256 が照らされて通った (パックも)。3 つのアプリが動いた。注入の `move` を間を空けずに送ると Paint Pad の線が最初の 1 区間だけになる (60 ms ずつ空けると全部描ける。注入の道具の性質で、アプリの問題ではないと見た) |
| sim の Retro (`FMRB_HW_TARGET=NARYAv3 rake build:linux` を両方で作り直した) | Basic と Wide Only は赤字で、入れようとすると「not checked on retro」。Hello Store と Paint Pad は入って動いた |

画面の写し: `C:\Users\Public\asterism_deploy\shots\` の `store_*.png` (ブラウザ版・sim の Modern・Retro) と `stamps_basic_sheet.png`。

後片付け: ブラウザ版はパックと 3 つを App Store の Remove で消し、`appstore_base.txt` を消した (`/home/stamps` は空、`/home/chat` は c2 からのもの)。
sim は `flash/app/usr/` の入れた物・`flash/home/appstore_base.txt`・`flash/cache/app/appstore/` を消した (`git status` は前からの 2 つだけ)。
チャットホスト・zenohd・HTTP の口・ブラウザ・sim は止めた。**`build-linux/` は両方のリポジトリで Retro (NARYAv3) の版のまま** (`.env` と同じ)。

## 5. 親 (ユーザ) に決めてほしいこと

各問いの先頭が推奨。

- **Q1 絵の生成スクリプトの置き場**: (a) 今のままリポジトリの外 (絵は PNG を直接差し替えればよく、生成の道具は CI に要らない。推奨) /
  (b) family-mruby-apps の `tools/` か `art/` に入れる (描き直しを公開の場で回せる。CI の対象外と書く要がある) /
  (c) private-notes に残す。
- **Q2 分類 `stamp`**: (a) 新しい分類にした今の形 (推奨。店の詳しい行に「stamp」と出る) / (b) 既存の `media` にする。
- **Q3 公開**: family-mruby-apps の stamp-packs を main に入れて push する時期 (2 つのコミット + 土台の未 push の 2 つ)。

## 6. 確認していないこと

- 実機 (P4-Nano・Tab5) の App Store でのパックの導入とチャット。sim の Modern は少し古いビルド。
- 公開の DEFAULT_BASE (GitHub の raw) からの導入 (push 前なので)。
- 古いアプリが今のファームで Window を出さなかった理由の細部 (エラーの窓も出なかった。ログはブラウザ版で取れていない)。
- 送ったスタンプをほかの端末で受けて出すところ (今回は 1 つの端末。c3 で確認済みの経路)。
