# R2 指示書: P4-Nano の既定を 640x360 にする、戻り道の Shift を押しっぱなしでも効かせる

> 2026-11-18 | 親セッションからサブ (Opus) への指示。計画の正は plan.md、R1a・R1b の結果は
> report/r1a.md・r1b.md (先に全部読む)。ユーザは P4-Nano の HDMI で 640x360 を見て「問題ない」と確認済み。

## 1. この段でやること

### 1.1 P4-Nano の既定を 640x360 にする
- `config/system_conf_naryav4.toml` の `display_width`・`display_height` を 640x360 にする。
  **Tab5 (`system_conf_tab5.toml`) と、sim の Modern (`system_conf_linux_p4.toml`)・ブラウザ版・
  ネイティブ版・Retro は変えない** (sim と回帰の基準は 426x240 のまま。ユーザの見立てどおり)。
- NARYAv4 の既定に連れて変わる所 (ビルド・storage の像・回帰の道具 `tools/regress/` の NARYAv4 の期待値・
  `regress:boot` の基準・`regress:size` など) を洗い、要る分だけ合わせる。基準の更新が要るなら、
  変化を report に書いたうえで別のコミットにする (親が検収する)。
- 今 426x240 で動いている P4-Nano は、端末の `/etc/system_conf.toml` が残るので、焼いても変わらない
  (Config で選ぶ)。この事実を案内の文書に書く。**端末の設定を勝手に 640x360 にしない**。

### 1.2 戻り道: Shift を押しっぱなしでも効くようにする (ユーザの確認で分かった不具合)
- 今: Shift を**押したまま**電源を入れても効かず、起動中に Shift を**連打**すると効いた (ユーザの実機確認)。
  見立て: 押しっぱなしだとキーの事象 (変化) が来ないので、R1a の「キーの事象ごとに bool に書く」では
  拾えない。
- 直し: キーボードが認識された時点で**今押されているキー**を読む (HID の最初の報告を見る・GET_REPORT で
  今の状態を取る、など。今の USB HID の受け側の作りを読んで決める)。連打でも引き続き効くこと。
  **起動の待ちを足さない**。
- 待ちを足さずには作れないと分かったら、作らずに、案内の文書を「起動中に Shift を連打」に直し、
  理由を report に書く (ユーザは「作れないと分かったら案内を連打に直す」で了承済み)。
- 実物のキーが要る確認はユーザが行う。コードの経路と、取れるならログ (起動時に読んだ修飾キーの状態を
  1 行出す) まで。

### 1.3 回帰の道具と文書 (R1b の残件)
- 回帰の道具 (`tools/regress/` の targets.rb・run.rb など) が `.app.toml` の全画面の属性を読んでいる所を、
  `fullscreen_any_size` に対応させる (R1b の report の残件)。
- アプリの書き方の skill (直下の `.claude/skills/fmrb-app-new/SKILL.md`) に、`fullscreen_hires`・
  `fullscreen_any_size` と、640x360 の画面で全画面のアプリがどう出るかを足す (直下のリポジトリの
  ブランチ `modern-resolution` にコミット)。
- 端末の案内 (`flash/usr/share/doc/display*.md`)、`doc/reference/fullscreen_hires.md`、
  直下の CLAUDE.md の画面の大きさの説明 (「仕組みと注意」の「画面」の項。P4-Nano の既定が 640x360 に
  なったこと、sim の Modern は 426x240 のままで選べること) を今の動きに合わせる。

### 1.4 この段でやらないこと (ユーザの決定)
- **デスクトップのアイコンの並びは今のまま** (左に縦 1 列)。
- **Window のアプリの最初の大きさと位置は今のまま** (現状維持。解像度で大きさを変える話は、ブラウザ版・
  ネイティブ版も併せて別に検討する)。ランチャーの位置も今のまま。
- 組み込みのアプリの描き方の直し。見つけた崩れは一覧にするだけ。
- `doc/reference/TODO.md` に課題を 1 つ足す: 「解像度に合わせた Window の最初の大きさと位置
  (640x360 でエディタが 240x200 で左上に出てアイコンに重なる、ランチャーが上寄り)。ブラウザ版・
  ネイティブ版も併せて検討する (2026-11-18 ユーザ)」。

## 2. 受け入れ条件

1. NARYAv4 のビルドの storage の `/etc/system_conf.toml` が 640x360 になっている (像の中身で確認)。
   Tab5・sim・ブラウザ版・ネイティブ版・Retro の設定は変わっていない。
2. P4-Nano での確認: 端末の `/etc/system_conf.toml` を退避してから消すか既定に置き換える形で、
   「新しく焼いた状態」を作り、640x360 で起動することを確認する。**確認の後、端末の設定を元
   (退避したもの = 今は 426x240。ただしユーザが Config で 640x360 にしているかもしれないので、
   退避した中身のとおりに戻す)** に戻す。`/home` に触らない。
3. 戻り道: 作れたなら、起動時に読んだ状態がログに出ること、連打の道が残っていることをコードで確認。
4. `rake regress` が通る。最後に 1 回 `rake regress:screens`・`rake regress:size` (`ALL=1` は不要。
   NARYAv4 が入る構成で)。内蔵 RAM が増えていない。Tab5 のビルドが通る。
5. `git status` に取りこぼしが無い (各リポジトリ。前からあるものは除く)。sim・ネイティブ版・シリアルの
   capture は止めてある。`build-linux/` は両リポジトリとも Retro の標準構成。`build/` は NARYAv4。

## 3. 範囲と止まる条件

- 触ってよい所: fmruby-core の `config/system_conf_naryav4.toml`、USB HID の受け側と R1a で足した
  戻り道の部分、`tools/regress/`、案内の文書、`doc/reference/TODO.md`、
  `doc/modern_resolution/report/r2.md`、`rake docs:index` の結果。直下 (ブランチ `modern-resolution`):
  skill と CLAUDE.md の該当の所だけ。
- **触らない**: Tab5・sim・ブラウザ版・ネイティブ版の解像度の既定、デスクトップ・ランチャー・アプリの
  並びと大きさ、合成の作り、sdkconfig、submodule の中、graphics-audio、asterism-tutor。plan.md は親が直す。
- fmruby-core はブランチ `modern-resolution` に続けてコミットする (この指示書はコミット済み)。
  **どのリポジトリも push しない。develop へマージしない。** `git add -A` を使わない。
- P4-Nano: 焼くのは `app_sync` だけ (`FMRB_HW_TARGET=NARYAv4 FLASH_BAUD=115200 HOST=192.0.2.20 rake flash:app_sync`)。
  MCP の flash・tab5_* は使わず CLI を使う。シリアルの detach・attach の繰り返しは 2 回まで。
  **ユーザが P4-Nano を使っているかもしれない**: 始める前に `/app/list` で動いているアプリを見て、
  デスクトップとサービス以外が動いていたら、焼く前に止まって返す (親がユーザに聞く)。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る。P4-Nano が起動しなくなった (起動する所までは
  自分で戻す)。**利用者から見た動きを、plan・この指示書に無い形で変える必要が出たとき**。
- 内蔵 RAM を増やさない。起動の速さを落とさない。

## 4. report (doc/modern_resolution/report/r2.md)

- 既定の変更で連れて変わった所、基準の更新の有無と中身。
- 戻り道の直し (作り、作れなかったならその理由)。
- 受け入れ条件 1〜5 の結果と根拠。
- 640x360 での組み込みのアプリの見え方の一覧 (小さい・重なる・崩れる。直していないもの)。
- 確認していないこと、ユーザに見てほしいこと、ユーザに決めてほしいこと、コミットの一覧 (リポジトリごと)。
