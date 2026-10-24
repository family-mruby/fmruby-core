# DS2 実施報告: ランチャーのアイコン、既定の組の入れ替え、ランチャーの右クリックの操作

> 状態: 実装完了・キーの選び方はユーザ確認待ち | 更新: 2026-10-24 | 既定のデスクトップのアイコンの先頭に `desktop/launcher` (ランチャーを開く)。既定の組を入れ替え (ヒデ雀・お絵かきを外し、FlappyDemo・BASIC app demo・Breakout.py を足す)。ランチャーのタイトルバーの「デスクトップに置く / 外す」ボタンを右クリックのメニューに移し、再走査は Reload ボタンと F5 に、丸い閉じるボタンを足した。Modern sim (426x240)・Retro sim (320x240、en/ja)・ブラウザ版で確認

計画: ../plan.md / 前段: ds1.md / 画像: ds2/

## 1. 結果の要約

| 確認すること | 結果 |
|---|---|
| 既定の並びの先頭にランチャー、ダブルクリックでメニューバー・`l` と同じランチャーが開く | 確認 (Modern sim・Retro sim・ブラウザ版) |
| Retro (320x240) で収まる | 確認。8 個は 1 列目 5 個・2 列目 3 個 (DS1 の列の折り返し)、画面の左半分に収まる。ja でも崩れない |
| `/home/desktop.toml` があるときは差し込まない | 確認 (Modern・Retro。保存済みの並びのまま、ランチャーは出ない) |
| 新しく足したアプリがダブルクリックで動く | 確認。FlappyDemo (Ruby)、BASIC app demo (BASIC。描いて正常終了)、Breakout.py (MicroPython) を Modern・Retro の両方で |
| 右クリックのメニュー、キー、Reload ボタン、閉じるボタン | 確認 (Modern sim で全部、Retro sim で en/ja のメニューと Esc、ブラウザ版でメニューと置く・保存・再読み込み後) |
| 内蔵 RAM | C (main/) の変更なし。増分 0 の見込み (4 章) |
| エディタを 1 回起動して 1 打鍵 (標準構成) | 確認 (最終ビルドの Modern sim。`Ln 1, Col 2 *`) |
| `rake test` | 通過 |

## 2. 作ったもの (コミット 2 つ)

### 8664c1c1 desktop: a launcher icon heads the default desktop icons

- `desktop_icons.rb`: 特別な項目 `desktop/launcher` (`DICON_LAUNCHER`)。表示名は既存の i18n `:launcher` (Launcher / ランチャー)、絵は `usr/share/icon/launcher.icon`。ダブルクリックで `open_launcher` (メニューバー・ショートカット `l` と同じ関数)。`desktop/file_manager` と同じ扱い。
- `flash/usr/share/desktop/desktop.toml`: 先頭に `desktop/launcher`。既定の組をユーザの指示で入れ替えた (3 章)。注釈も直した。
- アイコンの枠の幅を 64 から 68 にした。`Breakout.py` (11 文字 = 66 px) が「Breakout.p / y」と割れたため。68 でも Retro の 2 列目は x = 76..144 で左半分 (160) に収まる。

### e63311bd desktop: the launcher's desktop toggle moves to a right-click menu

ユーザの追加指示 (2026-10-24、仕様の変更として承認済み) による。

- タイトルバーの「Add to desktop / Remove from desktop」ボタン (`:lnc_desk`) を消した。
- アイコンの上で右クリック: 小さなメニューに「デスクトップに置く」か「デスクトップから外す」(該当する方) とキー `D`。ファイル管理の右クリックのメニューと同じ描き方 (枠・項目の高さ 14・右寄せのキーの表示)。右クリックしたアイコンは選択になる。
- 何も無い所で右クリック: ランチャー自身のアイコン (`desktop/launcher`) の「ランチャーを置く / 外す」(`D`) と「Reload / 再読込」(`F5`) の 2 項目のメニュー。これでランチャーのアイコンも置く・外すができる (DS2 指示書 1 章の希望)。File Manager のアイコンは従来どおり手で `/home/desktop.toml` を編集する。
- タイトルバーの右端に Reload ボタン (「Reload (F5)」/「再読込 (F5)」、幅 72) と、ほかの窓と同じ丸い閉じるボタン (半径 3 の塗りの円、当たり判定は中心から ±5、ファイル管理と同じ位置・大きさ)。再走査中の「Scanning n/m...」は左側だけを塗るようにし、ボタンを消さない。
- メニューはランチャーの canvas に描く。アイコンは canvas の上に重なるスプライトなので、メニューに重なるアイコンのスプライトはメニューを出している間だけ隠し、メニューを閉じる再描画で戻す。メニューはランチャーの矩形 (カーネルが合成する範囲) の中、スクロールバーより左に収める。
- `system_desktop.app.rb`: `handle_launcher_key` に scancode も渡す。右クリックの経路の注釈、ivar の初期化。
- `i18n.rb`: `lnc_self_add` / `lnc_self_remove` / `lnc_reload`。
- `desktop_icons.rb` と `desktop.toml` の注釈、`/home/desktop.toml` に書く注釈の「desktop button」を「icon menu」に。

### キー (ユーザに確認してほしい)

| キー | 働き | 選んだ理由 |
|---|---|---|
| Tab | 選択中のアイコンのメニューを開く (アイコンの下に出す) | ファイル管理と同じ。ショートカットの一覧 (F10 のメニューの Shortcuts) は既に「一覧 (ファイル・ランチャー): Tab = 選択中の操作メニュー、Esc = 閉じる」と書いている |
| D | 選択中のアプリを置く / 外す。選択が無いときはランチャー自身のアイコン | Desktop の頭文字。ランチャーの既存のキー (矢印・Enter) とぶつからない。ランチャーが開いている間は文字のショートカット (`l` など) はもともと効かない |
| F5 | Reload (再走査) | ランチャーでは未使用、デスクトップ全体でも F10 以外の F キーは使っていない。R ではなく F5 にしたのは、文字キーをこれ以上使わないため |
| Esc | メニューが開いていればメニューを閉じる。無ければランチャーを閉じる | 今までランチャーをキーで閉じる手段が無かった (外をクリックするか起動するしか無かった)。ショートカットの一覧の記述にも合う。**今回足した振る舞い** |

- メニューが開いている間の最初のキーはメニューを閉じる。Esc と Tab はそこで使い切り、ほかのキーはそのまま働く (ファイル管理と同じ規則)。
- 右クリックを何も無い所でした場合は「何もしない」ではなく、ランチャー自身のアイコンと Reload のメニューにした (指示の選択肢のうち後者に近い形)。

## 3. 既定の組 (ユーザの指示で入れ替え)

上から: ランチャー、エディタ、Shell、File Manager、FlappyDemo (`/app/game/flappy.app.rb`)、BASIC app demo (`/app/basic/basic.app.bas`)、Breakout.py (`/app/game/breakout/breakout.app.py`)、NSF プレーヤ (`/app/tool/nsf_player.app.rb`、最後のまま)。ヒデ雀とお絵かきはデスクトップから外した (ランチャーには残る)。

- アイコンの絵: FlappyDemo は自前のアイコンが無いので Ruby の既定 (赤い宝石)、BASIC app demo は BASIC の既定 (緑の B)、Breakout.py は Python の既定 (黄色)、NSF プレーヤは自前の音符 (親のコミット dc974e2d の新しい絵)。
- Retro の storage の除外 (`config/storage_exclude_naryav3.txt`) にこの 8 個は含まれないので、Retro の実機でも 8 個出る見込み。
- 8 個は 240 の高さで 1 列に 5 個まで (17 + 5x44 = 237) なので、2 列目に 3 個。Modern (426x240) も同じ。ブラウザ版の 1280x720 のページでは 1 列に収まる。

## 4. アイコンの絵の出どころ

- 既存の絵にランチャー (アプリの一覧) を表すものが無かった (メニューバーのランチャーは文字だけ、`usr/share/icon` は shell・editor・appstore・folder・各言語・blockgame など)。そこで同じ形式で新しく描いた: `flash/usr/share/icon/launcher.icon` (12x12、color=0x1C の緑、2x2 のタイル)。BMP は `rake icons` (tool/gen_icon_bmp.rb) で生成した 24x24 の `launcher.bmp`。ほかのアイコンの BMP は生成し直しても変わらないことを確認した。
- 親の作業の確認 (依頼分): ランチャーを右クリックで再走査したあと、BlockGame は自前のアイコン (水色のブロック) で出る。お絵かき (Board) はランチャーで新しい絵、NSF プレーヤはデスクトップでも音符で出る (画像 modern_launcher_rescanned.png、modern_desktop.png)。手元の sim の `/var/cache/launcher_index` (利用者のローカル物、gitignore) は古く、NSF プレーヤが Ruby の既定の絵、BlockGame が既定の絵のままだった。確認のためランチャーの再走査を実行し、このキャッシュは書き直された (Retro の ja 確認でも ja で書き直され、次の en の起動で作り直される)。

## 5. 内蔵 RAM とメモリ

- C (main/) とグラフィックス側の変更は無い。増えたのは Ruby のバイトコードと objects で、デスクトップ VM のプール (起動時に確保済みの固定の塊) の中に収まる。内蔵 RAM の増分は 0 の見込み。sim は内蔵 RAM を出さないので、実機の `M1|` / `IRAM free:` の比較はしていない。
- デスクトップ VM (`sim_app ps` の system_desktop、起動直後):
  - Modern sim、同じ既定の組 (8 個) で比較: 8664c1c1 のコード 736,656 B (2 回とも同じ) → e63311bd のコード 743,656 B。メニューのコードで +7 KB (プール 1,536 KB の 0.5%)。
  - Retro sim: 8664c1c1 の前段のビルド (入れ替え前の組) で、ランチャー無しの保存済み 6 個 692,848 B / ランチャー付きの既定 7 個 692,616 B (差は揺れの範囲) → 最終 (8 個 + メニュー) 701,344 B (+8.7 KB、組が 1 個多い分とメニューのコード)。
  - アイコンの構築の増分はログ `Desktop icons: 8/8 shown, pool +5KB (46-47%)` (Modern・Retro とも)。ランチャー 1 個の分はおよそ +2 KB (保存済みの 6 個で +3 KB、既定の 7 個で +5 KB、Retro)。
  - 1 回だけ起動直後に `pool +46KB (65%)` が出た (解像度の持ち越しで sim_up が起こし直した直後)。同じビルドの次の起動では +5KB (47%) だったので、ごみの回収の時機の揺れと見る。
- グラフィックス側: ランチャーのアイコンの SpriteImage が 1 枚増える (24x24、ランチャーの一覧に無い絵なので `ensure_desktop_icon_images` が読み込む)。
- 描画: ランチャーのアイコン 1 個分の draw_tile と名前が増えるだけ。メニューの開閉はランチャーの中身の再描画 1 回 (ランチャーの既存のスクロールと同じ経路)。

## 6. 画面の確認 (画像は ds2/)

| 構成 | 確認したこと | 画像 |
|---|---|---|
| Modern sim 標準 (TAB5、Spinel カーネル + Spinel エディタ、デスクトップは mruby) | 既定の 8 個、ランチャーのダブルクリック、新しい 3 個の起動、右クリックのメニュー (アプリ・何も無い所)、置く・外す と `/home/desktop.toml` への保存、ランチャー自身を外す、Tab・D・F5・Esc、Reload ボタン、閉じるボタン、エディタの 1 打鍵 | modern_desktop, modern_launcher, modern_menu_app, modern_menu_self, modern_menu_key, modern_flappy, modern_basic, modern_breakout, modern_launcher_rescanned, modern_editor_key |
| 同上、`/home/desktop.toml` あり (ランチャーの無い 6 個) | 差し込まれない | modern_saved_list (入れ替え前の組で撮影。差し込まない仕組みは組に依らない) |
| Retro sim (NARYAv3、標準のエンジン)、en | 8 個の配置、ランチャーのダブルクリック、新しい 3 個の起動、エディタの 1 打鍵、右クリックのメニュー (両方)、タイトルバー (Reload・閉じる) が 300 幅に収まる、Esc でメニュー→ランチャーの順に閉じる | retro_desktop, retro_menu_app, retro_menu_self, retro_editor_key (エディタは 8664c1c1 のビルド) |
| Retro sim、ja | ラベル (フラッピーゲーム・BASICデモ・ブロック崩し・NSFプレーヤ) が崩れない、タイトルバー「ランチャー」「再読込 (F5)」、メニュー「デスクトップに置く D」「ランチャーを外す D / 再読込 F5」 | retro_desktop_ja, retro_menu_app_ja, retro_menu_self_ja |
| Retro sim、`/home/desktop.toml` あり | 差し込まれない | retro_saved_list |
| ブラウザ版 (wasm、手元の開発サーバ、1280x720 のページ) | 既定の 8 個、ランチャーのダブルクリック、タイトルバー、右クリックのメニュー、置くと `/flash/home/desktop.toml` に書かれ再読み込み後も残る | web_launcher, web_menu_app, web_after_reload |

- Retro は `FMRB_HW_TARGET=NARYAv3` を環境変数で渡して `rake clean_all && rake build:linux`。MCP の sim_up は解像度の期待値を `.env` から読むため、その間だけ `.env` の `FMRB_HW_TARGET` を書き換え、終わってから元の内容に戻した (git の差分なし)。
- ja は生成物の `flash/etc/system_conf.toml` を一時的に `language = "ja"` にし、戻した。
- 確認のために作った `/home/desktop.toml` (sim の flash/home、ブラウザ版の IndexedDB) は消した。
- 最後に build/ を Linux の Modern (TAB5) で `clean_all` から作り直し、`build/fmruby-core.elf` が x86-64 であることを確認した。ブラウザ版の束 (`rake wasm:mrb` → `rake wasm:web`) は手元の wasm/build に作り直した (公開中のものは触っていない)。
- sim 上のエラー行 (`E (`) は各回 0。

## 7. 判断したこと・見立てと違った点

- ランチャーの中の矢印キーは keycode で比べているが、sim でも効いた (今回は触っていない)。新しいキーは scancode で比べる。
- メニューを開くと、そのメニューに重なるアイコンの絵 (右クリックしたアイコン自身を含む) が一時的に消える。スプライトが canvas の上に合成されるため避けられない。ファイル管理の流儀 (ポインタの位置に出す) に合わせた。気になるならアイコンの下に出す形にできる。
- ランチャー自身のアイコンを外したあと置き直すと、並びの末尾に付く (アプリを置くときと同じ)。先頭に戻すには `/home/desktop.toml` を消して既定の組に戻すか、手で編集する。
- 最初の sim で Esc がランチャーを閉じないことに気づいた (既存の振る舞い)。今回の Esc の追加で閉じるようになった。
- デスクトップのショートカットの一覧 (Shortcuts) に D と F5 は足していない。一覧は Retro の高さで既に下が切れる (行を足すと既存の行が落ちる) ため。メニューとボタンにキーを書いたので、発見はそこでできる。
- sim の起動・停止で `fmruby_zenohd` コンテナも一緒に作り直され、最後の sim_down で消えた (作業の前には別の起動から 3 時間動いていた)。必要なら起こし直してほしい。

## 8. 残件

- キーの選び (Tab・D・F5・Esc) のユーザ確認。
- Tab5 の実機での二本指タップ (右クリック) でのメニュー、丸い閉じるボタンのタップのしやすさ (実タッチは注入で確かめられない)。
- 実機の内蔵 RAM の同一コミット比較 (C の変更は無いので 0 の見込み)。
- plan.md の「決まったこと」への反映 (親): ランチャーのアイコン、既定の組の入れ替え、置く・外すが右クリックのメニューになったこと、再走査が Reload ボタン / F5 になったこと。ds1.md と plan.md の「タイトルバー右端のボタン」の記述は古くなった。
- ショートカットの一覧に D・F5 を載せるか (載せるなら一覧の高さの扱いを決める)。
