# Asterism B1 指示書: ブラウザ版の Family mruby を網に入れる (手元の http の版)

> 状態: 指示 | 更新: 2026-10-11 | zenoh-pico を Emscripten でビルドし、WebSocket でルータにつなぐ。wasm 版に picoruby-asterism-zenoh と asterism を入れ、ブラウザで `asterism_demo` と `rover_cam` を動かして、asterism-console の図に出し、台車を操縦する

前提: plan.md の B の節、design.md 9 章、node_variants.md 2.7、fmruby-core の doc/wasm/ (wasm 版の作りと、guide_*.md)、親の CLAUDE.md の「ブラウザ版 (wasm)」(web_* ツール)、picoruby-asterism-zenoh の README と ZENOH_PICO_PIN、report/z1.md・z2.md (zenoh-pico を Linux と P4 に入れたときの記録)、report/s3.md (`rover_cam`)。

## 1. 作るもの (この順)

1. **最小の試し**:
   - zenoh-pico (ZENOH_PICO_PIN の版) を Emscripten でビルドする。システム層は vendor の Emscripten 用のもの、リンクは WebSocket。
   - zenohd に `ws/` の待ち受けを足す compose の重ね合わせを親リポジトリに足す (例: `docker-compose.zenoh-ws.yml`。既存のファイルの意味は変えない。ポートは手元向けに 127.0.0.1 だけでよい)。
   - ブラウザで put と subscribe が通る最小のページ (C だけ、または最小の mruby) を作り、CRuby や `tools/fmrb_zenoh.rb` と行き来することを確かめる。
   - ブラウザの WebSocket は非同期で、zenoh-pico の読み書きは同期の形をしている。Emscripten の層がどう合わせているか (ASYNCIFY、ワーカー、自前の待ち など) を読んで report に書く。
2. **wasm 版に入れる**:
   - wasm 版のビルドに picoruby-asterism-zenoh と asterism を入れる (今は P4 のビルドだけに入っている。入れ方は P4 のビルドの仕組みに合わせる)。
   - ルータの場所はブラウザ版の設定 (system_conf_wasm.toml など、今の設定の流儀) か、ページの URL の引数から渡す。決め方を docs に書く。
   - ブラウザ版の受け取りの見回りが、画面 (描画・入力) を止めないことを確かめる。
3. **ブラウザで動かす**:
   - `flash/app/test/asterism_demo.app.rb` を web_* ツールで動かし、asterism-console の図に ▣ のノード (アプリ・オブジェクトの入れ子つき) として出ることを確かめる。console から呼び出しができる。
   - `flash/app/test/rover_cam.app.rb` を動かし (ROS 2 の台車は S1 の compose)、台車を操縦できること、`/odom` が出ること、カメラの画像がブラウザ版の画面に出るか (wasm 版の表示側が JPEG を読めるか。読めなければ sim と同じく大きさと頻度の表示) を確かめる。
4. **docs**: fmruby-core の doc/wasm/ か doc/ruby_asterism/ に、ブラウザ版を網に入れる手順を書く (zenohd の起動、ブラウザ版のビルドと起動、ルータの場所の渡し方)。

## 2. 受け入れ条件

1. 最小の試しで、ブラウザと CRuby の間で put / subscribe が通る。
2. wasm 版のビルドが通り、Asterism を入れても既存の wasm 版の動き (起動、エディタ、保存) が壊れない。wasm 版の大きさ (wasm と data) の増分を書く。
3. ブラウザの `asterism_demo` が console の図に出て、console から呼べる。
4. ブラウザの `rover_cam` で台車を操縦でき、`/odom` の表示が動く。カメラの扱いを書く。
5. 受け取りの見回りが画面を止めない (打鍵や描画の遅れを、Asterism を入れる前と比べる)。
6. 手順が docs にあり、上から打てば再現できる。本当の IP・機体の名前・MAC はコミットしない。

## 3. 範囲・決まり

- **触ってよいもの**:
  - fmruby-core: wasm 版のビルドの仕組み、`lib/add` 配下 (submodule は直接編集しない)、`flash/app/test/` の試しのアプリ (ブラウザで動かすための小さな直しまで)、doc。ブランチは develop から `feature/asterism-b1` を切る。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` にユーザの手元の変更がある)。
  - 親リポジトリ: compose の重ね合わせを足す (既存のファイルの意味は変えない)。コミットのみ。
  - picoruby-asterism-zenoh: Emscripten のビルドに要る直しがあれば、コミットせずに差分と理由を report に書いて返す (この gem は公開済みのため)。asterism の gem も同じ。
- **触らないもの**: `.env`、sdkconfig、ESP32 のファームの C の振る舞い (wasm 用の分岐を足すのはよい)、fmruby-graphics-audio、公開のブラウザ版 (family-mruby.github.io)。
- **公開**: gem の公開はしない。
- **機体**: 要らない。
- **終わったら**: 自分で起動したものを止める。`build/` は Linux の標準構成 (x86-64) に戻す (`rake clean_all` が wasm の libmruby を消す罠に注意、doc/wasm/ を読む)。
- **止まる条件**:
  - zenoh-pico の Emscripten の層が今の版で使えず、直すのに上流の大きな作業が要る。そのときは zenoh-ts を JS 連携で呼ぶ案と比べて返す。
  - 公開済みの gem を直す必要がある (差分を書いて返す)。
  - 仕様の選択が要る (例: ルータの場所を使う人がどう設定するか、の画面)。
- **report**: Japanese plain style (常体)。書くこと: Emscripten の層の調べ (非同期の合わせ方)、ビルドの組み立て、受け入れ条件の結果と実測、見立てと違った点、B2 への申し送り。
