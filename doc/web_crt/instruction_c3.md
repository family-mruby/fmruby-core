# 実装指示書 C3: 画面の額縁

計画は doc/web_crt/plan.md の「額縁 (C3)」の節 (ユーザ決定が正)。C1 の経過は
report/c1.md。

作業ブランチ: fmruby-core の `feature/web-crt` (本体 checkout、直列)。

## 作るもの

1. **額縁の 3 つの型** (HTML・CSS、要れば inline の SVG。画像ファイルは使わない):
   - **液晶モニタ風** (Sharp のとき): 細い黒か濃い灰の枠、下に小さなスタンド、
     電源のランプ。
   - **ブラウン管テレビ風** (CRT のとき、選べる): 厚い枠 (木目か黒い樹脂。CSS の
     グラデーションで)、横か下につまみ 2 つ、スピーカーの網。
   - **CRT モニタ風** (CRT のとき、選べる): ベージュか灰色の分厚い枠、下に電源の
     ボタンとランプ。
   - 名札は「Family mruby」。実在の製品・会社の名前やロゴに似せない。
   - 枠の内側の縁に画面の光が少し映り込むような影 (CSS の inset の影など)。
   - つまみ・ボタンは見た目だけ (押しても何も起きない。カーソルも変えない)。
2. **置き場所**: canvas の外 (`#screen-box` の周り、または包む要素)。`#screen` と
   CRT の重ねる canvas には描かない。`#screen` の大きさと位置の決め方
   (`applyZoom()`)・重ねる canvas の位置合わせ・クリック/タッチの座標の計算は、
   額縁があっても正しく動くこと。
3. **出す・出さない**:
   - 既定で出す。Display の欄に「Frame」の選び口 (なし / 自動 / 型の指定など、
     作りやすい形でよい。CRT のときのテレビかモニタかはここで選ぶ) を足し、
     `writeSetting` で覚える。`?frame=` の URL の指定も `?look=` と同じ扱いで受ける
     (保存しない)。
   - 全画面のときは出さない。狭い画面 (額縁を付けると 1 倍も入らない幅。閾値は
     作って決め、report に書く) では出さない。
4. **Zoom の計算**: Fit と既定の倍率の選び方 (`defaultZoom()`) は額縁の分を
   差し引いて決める。1x/2x/3x を明示したときは、その倍率の画面に額縁を足した
   大きさになる (ページが横に溢れるなら額縁を出さない側に倒すか、report に
   書いて親に返す)。

## 確認

- `rake regress` と `rake regress:screens` (ブラウザ版を含む。`#screen` の写しは
  額縁の有無で変わらないこと)。`rake test` の `webcrt:test` も。
- **ページ全体を撮って見る** (C1 で `#screen` だけを撮る確認が、重ねた canvas の
  不具合を見逃した。今回の変更はすべて `#screen` の外なので、ページ全体の画像で
  しか確かめられない)。手段は、専用の user-data-dir の chrome-headless-shell を
  `--remote-debugging-port` 付きで開き、CDP の `Page.captureScreenshot` で撮るのが
  手早い (WebGL を使う CRT は `--use-angle=swiftshader --enable-unsafe-swiftshader`
  を付ける)。撮るもの (doc/web_crt/report/ に置く、大きすぎないよう PNG を絞る):
  - Sharp + 液晶モニタ風 (2x と Fit)
  - CRT + ブラウン管テレビ風
  - CRT + CRT モニタ風
  - 額縁なし (今の見た目と同じであること)
  - 狭い幅 (例: 幅 400 px) で額縁が消えること
- 額縁があるときに、本物の DOM のマウスイベント (C1 で足した `dom` 命令か CDP の
  `Input.dispatchMouseEvent`) で、画面の四隅に近い物が狙いどおり押せること
  (Sharp と CRT の両方)。
- 全画面に入って出たとき、額縁が消えて戻ること (headless で全画面が試せない
  ときは、そう report に書く)。

## 触ってよい範囲

- `wasm/web/` (index.html・main.js・crt.js、要れば新しい css/js)、
  ルートの `tools/fmrb_web.rb` (命令を足す場合。ルートは `feature/web-crt`
  ブランチにコミットし、作業ツリーの checkout は切り替えない — C1 と同じやり方)、
  doc/web_crt/。

## 止まる条件

- 額縁を足すと `#screen` の写しや回帰の比較が変わると分かったとき。
- 既定の倍率の選び方を変えると、今まで 3 倍で見えていた画面幅で 2 倍に落ちる、
  といった利用者に見える変化が大きいと分かったとき (どの幅で何が変わるかを
  表にして返す)。
- 見立てと違う事実が出たとき。

## 守ること

- `rake clean_all` はしない。sudo しない。本当の HOME・/etc・/usr に書かない。
- tools/fmrb_web.rb が開いているブラウザは閉じない。終わったら、そのページの
  設定を既定 (Look は Sharp、Frame は既定、Zoom は触る前の値) に戻す。
- 公開のブラウザ版への配置はしない。push しない。
- コミットは `feature/web-crt` に (領域名 `wasm`、英文、Co-Authored-By)。文書に
  手元の絶対パスやユーザ名を書かない。
- report は `doc/web_crt/report/c3.md`: 型ごとの作り、狭い画面の閾値、Zoom の
  計算の変化 (幅ごとの表)、見立てと違った点、残件。
