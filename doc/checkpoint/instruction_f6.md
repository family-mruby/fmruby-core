# 実装指示書 F6: 実機 (ESP32-P4、P4-Nano の NARYAv4) に入れる

`doc/checkpoint/plan.md` (F0〜F5・Ff の決まったこと)、`flash_table.md` と `report/ff.md` (基準の表を flash に置く形)、
`report/f0.md` (patch・gem の入れ方、ESP32 は gem を持たない今の形)、`report/f4.md`・`report/f5.md` (移す段取り、
止める前の問い合わせ、Asterism の受け口)、`movable_apps.md`、mruby-checkpoint の `doc/plan.md` (D4・D12、「hook の
ポインタと切り替えの要求の数は実機では `mrb_state` の中に置く」) を先に読む。P4-Nano の構成は `doc/naryav4/`。

## 目的

ESP32-P4 の実機で、申告したアプリを止めてイメージに書き、別の端末 (ネイティブ版・sim・ブラウザ版) との間で
Asterism で移せるようにする。内蔵 RAM は増やさない。

## 決まっていること

- **F6 の最初の端末は P4-Nano (`FMRB_HW_TARGET=NARYAv4`)** (2026-11-05 ユーザ。Tab5 の代わり)。S3 は後で別に扱う
  (この段では作らない。S3 で要る量の見込みだけを report に書く)。
- D4: 実機に gem を入れるのは、内蔵 RAM と flash を測ってから。内蔵 RAM は +0。
- セーフポイントの hook の関数ポインタと切り替えの要求の数は、実機では **`mrb_state` の中**に置く (PSRAM の VM ごと)。
  今の静的な置き場所 (ホストのビルドのもの) との関係は、両方のビルドで同じ形にするか、実機だけ変えるかを判断して
  report に書く (普段の速さの命令数が F0 の基準と 1% 以内であること)。
- 基準の表は Ff の形 (`report/ff.md`。`rake checkpoint:table` がホストで作る端末の表を、IDF のビルドに入れる)。
  1 VM の費用は 32 bit で約 90 KB のまま受け入れる (2026-11-05)。
- 移動元は、着いた側が起動できたと返してから閉じる (D15)。止める前の問い合わせ、`requires`、受け入れる相手の設定
  (`/home/asterism/receive.toml`) は F5 のまま。
- ブラウザ版の公開のビルドに gem を入れるかは未決 (この段では入れない)。

## やること

0. **P4 のビルドが通ることを先に確かめる**: Ff の検収で、`rake regress:size` の TAB5 が IDF の構成の段
   (`Missing required kconfig option after retry`、esp_hosted の manifest の Kconfig の警告の後) で止まった。Ff の前の
   コミットでも同じかを確かめ、原因を突き止めて直す (TAB5 と NARYAv4 の両方)。部品の版の固定で直るなら、mDNS の
   `~1.8.0` と同じ形 (`main/idf_component.yml` に理由のコメントつき)。sdkconfig を変える必要があれば止まる。
1. **ESP32 のビルドに gem と patch の定義を入れる** (P4 の構成で)。`FMRB_HAVE_CHECKPOINT` を P4 で有効にする道、
   hook と数を `mrb_state` へ移す、基準の表を flash に (Ff の道具で P4 の構成の表を作ってリンク)。S3 の構成のビルドは
   今のまま (gem なし) で通ること。
2. **測る**: `idf.py size-components` で内蔵 RAM (+0 であること)・flash の増え、申告したアプリの VM の基準の常駐の
   費用 (PSRAM のプール、32 bit)、イメージの大きさ、止める・書く・起動する時間。
3. **実機で動かす**: P4-Nano を焼いて、
   - 同じ端末の中で止めて続ける (F1 の形、`restart: true`)。
   - P4-Nano ⇔ ネイティブ版 (と、できれば sim) の間で Asterism で移す、両向き。窓のあるアプリ (F3) も。画面の
     大きさの違いは F4 の形 (`on_resize` の後に `on_restore`)。
   - 止める前の問い合わせ (警告つきで移す / `requires` で断る) と、返事が来ないときに移動元で続けること。
4. **試験**: `rake regress:checkpoint` に、実機を使う場面を opt-in で足す (つないだ端末だけ。`BOARD=` など、
   既存の `regress:boot` の形にそろえる)。既定と `regress:full` には入れない。
5. **文書**: `report/f6.md` (入れ方、`mrb_state` に移した形、計測、実機で見た点、S3 の見込み、サブの仮決め、残件)、
   `plan.md` の F6 の行を「P4 実装済・検収待ち」に、`movable_apps.md`・`doc/regression/usage.md` に変わる点があれば。

## 確かめること

- `rake regress:size` (`ALL=1`。内蔵 RAM +0、S3 も変わらない)、`rake regress`、`rake regress:run`、
  `rake regress:checkpoint` (既定と `SIM=1`)、共有のコードを変えたら `rake regress:screens ONLY=retro,modern,native,native-keys`。
- 実機: `rake regress:boot BOARD=naryav4` (crash の印 0 件、IRAM の空きが想定どおり)、上の 3 の各場面のログ。
- 普段の速さ: VM の命令数 (valgrind) が F0 の基準と 1% 以内 (hook と数の置き場所を変えたので必須)。ホストに
  valgrind が無いので、F0 で測った方法 (`report/f0.md`) を確かめ、コンテナの中で測る。どこでも測れなければ止まって報告する
  (ホストに入れるには sudo が要り、ユーザに頼む)。
- mruby-checkpoint に手を入れたら、その試験 (`rake test`・`test:asan`・`test:stress`)。

## 実機の扱い

- P4-Nano は USB のシリアル (CH343) でつなぐ。/dev に見えなければ fmruby-core で `rake attach` を自分で流す
  (usbipd で共有済み)。シリアルは `serial_start` で開きっぱなしにし、`flash` で焼く (MCP の fmrb の道具)。
- 画面は HDMI でテレビに出るので Claude からは見えない。画面の確認が要るところは、遠隔の道具
  (ルートの `tools/fmrb_rd_*.rb`、端末の IP はシリアルのログか Asterism で引く) で画面を取れるならそれで、
  取れなければログで判定し、目視が要る点を report に挙げる (ユーザに頼む点としてまとめる)。
- flash への書き込みの試験は最小限 (イメージの書き出しを繰り返す試験は /tmp や RAM で)。
- 端末の `/home` を消す操作 (`rake flash` が消すことがある) の前に、要るものが無いか確かめる。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core は本体の checkout の `feature/linux-native` にコミット (push しない)。mruby-checkpoint は worktree
  (`wt/mruby-checkpoint-f6` (worktree の決まった置き場所)、ブランチ `wt/f6` を `wt/ff` の上に)
  でコミット (push しない)。family-mruby のリポジトリ直下 (`tools/`) もコミットまで。直下の `tools/fmrb_web.rb` の
  コミットされていない変更 (別の作業のもの) と、直下の `--help` というファイルには触らない。
- `sdkconfig`・`sdkconfig.defaults` は編集しない (要るなら案を report に書いて止まる)。submodule は直接編集しない。
- 止まる条件: 内蔵 RAM が増える、sdkconfig を変える必要、利用者から見た動きが変わる直し方、決まっていることと
  食い違う事実、実機のボタン・初めての機器の bind・耳や目での確認が要る地点。
- 人が開いているブラウザ (ポート 8016) に触らない。
- 文書は常体、定着したカタカナ語を使う。コミットする文書にローカルの絶対パスや実在の IP・端末の ID を書かない
  (コミットの前に、差分を検索して確かめる)。
- 終わったら worktree の build を残さない。
