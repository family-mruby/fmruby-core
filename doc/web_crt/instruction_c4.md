# C4 指示書: ネイティブ版のコンポジタの CRT の下回り

> 状態: 実装中 | 更新: 2026-11-07 | plan.md の「ネイティブ版のコンポジタ (C4〜C6)」の C4。シェーダーの共通化と、コンポジタで画面全体に CRT をかける下回り

正は plan.md の「ネイティブ版のコンポジタ」の節 (決定事項・方針・受け入れ条件)。
ここには C4 の範囲と進め方だけを書く。

## 範囲

1. **シェーダーの共通化**: `wasm/web/crt.js` の頂点・断片シェーダーと、曲がりの式
   (`warpToSource` とその逆) を 1 か所に出す。ブラウザ版はビルドで JS に、
   ネイティブ版は C の文字列に埋める。ブラウザ版の見た目と `rake webcrt:test` は
   変わらない (`rake regress:screens` の web も)。C 側の曲がりの式に試験を付け、
   JS と同じ値になることを確かめる。
2. **コンポジタの描き方** (`native/wlr/`): 入のとき、wlroots のシーンを画面外の
   バッファに描かせ、それを GLES のテクスチャとして CRT のシェーダーで出力に描く。
   GPU の文脈は wlroots の GLES2 の描画器のものを借りる。**最初に、wlroots 0.19 の
   公開の口だけで組めるかを調べ、組み方を report に書いてから実装する**。
   公開の口で組めない (wlroots の内部に手を入れる必要がある) と分かったら止まって返す。
3. damage のときだけ描く。CRT の 1 コマの描画の時間をログに出す (周期の統計で)。
4. 効果: 走査線・格子・にじみ・曲がり・四隅の暗さ。強さ 0 の効果は計算しない。
   画素の単位は `display.scale` (Family mruby の 1 画素)。倍率が小さいときに
   走査線を弱める式はブラウザ版と同じ。
5. 入力: 曲がりがあるとき、ポインタ・タッチの座標を同じ式の逆で戻して Window に
   渡す。絵の外の押下は捨てる。カーソルは曲げない (ハードウェアのカーソルのまま)。
6. 設定: 機種の定義の `[display] look = "sharp" | "crt"` と効果ごとの強さ
   (`crt_scanline`・`crt_mask`・`crt_glow`・`crt_curve`・`crt_vignette`、0〜100、
   既定はブラウザ版と同じ 55・30・30・15・40)。`--set` で渡せる。既定は `sharp`
   (今と 1 画素も変わらない)。動いている間の切り替えの口 (C5 で UI から呼ぶ) を
   用意しておく (今は C5 の範囲なので、内部の関数で足りる)。
7. pixman (GPU なし) のときは CRT を出さず、理由をログに 1 行出して続ける。
8. 画面写し (`native_screenshot`、回帰の道具) は CRT をかける前の絵を読む。

## 確認

- `rake regress` (ホストの試験と描画の記録)、`rake regress:wl` (コンポジタ、既定の
  sharp で今と変わらないこと)、`rake regress:screens` の web (ブラウザ版の見た目が
  変わらないこと)、`rake webcrt:test`、C 側の曲がりの式の試験。
- WSL2 では GLES が使えない (pixman になる) ので、CRT の見た目は GPD MicroPC2 の
  実機で確かめる:
  - `rake native:deb ARCH=amd64` で作り、`scp` で `<GPD>:/tmp/fm.deb`
    に送る (鍵で入れる)。**`sudo apt install` はユーザが打つ** (sudo にパスワードが
    要る)。ここで止まって親に返し、入れてもらう。
  - GPD の `/etc/default/family-mruby-session` は今 `ROTATE=270` と
    `FMRB_ARGS="--set display.scale=3"`。CRT を入れる設定もここに足す必要があるが、
    sudo が要るので、足す行を親に返す。
  - ログは `~/.local/share/sddm/wayland-session.log` と
    `~/.local/state/family-mruby/session.log` (SSH で読める)。見た目の判断は
    ユーザ。読めるか、押した所に届くか、カクつかないかを、ユーザに確かめてほしい
    項目として report に並べる。
- 内蔵 RAM・ESP32 には関係しない (ネイティブ版だけ)。ESP32 のビルドを壊さない
  (`rake regress:size` は要らないが、共通化で触ったファイルが ESP32 のビルドに
  入らないことを確かめる)。

## 止まる条件

- wlroots の公開の口で組めない。
- 仕様 (plan.md の決定事項) と違う動きが要る。
- ユーザの作業 (GPD での sudo、目視) が要る地点。

## report

`doc/web_crt/report/c4.md`: 組み方 (調べた wlroots の口、捨てた案)、計測 (1 コマの
CRT の時間、GPD)、見立てと違った点、ユーザに確かめてほしい項目、残件。
