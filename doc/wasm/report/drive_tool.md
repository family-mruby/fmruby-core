# 報告: ブラウザ版を shell から操作する道具 (fmrb_web.rb)

> 状態: 完了 | 更新: 2026-08-31 | ページが命令を取りに来る形。エディタで書いて保存 → リロードまで自動で通した

Linux sim には `fmrb_input.rb` / `fmrb_screenshot.py` があり、Tab5 には
`fmrb_rd_*.rb` があるのに、**ブラウザ版だけ手で触るしかなかった**。
storage の T2 で「エディタで保存 → リロード」を確かめられなかったのも
これが理由。

## 形: ページが取りに来る

ブラウザは外から叩けない。そこで向きを逆にした。

```
tools/fmrb_web.rb  --POST-->  rake wasm:serve (中継)  <--GET--  ページ (?drive=1)
                   <--結果--                          --結果-->
```

- 開発サーバに待ち行列を置く。道具は命令を POST してその場で待ち、ページは
  1 秒の長めの GET で取りに来て、実行し、結果を POST で返す。道具の 1 往復が
  そのまま 1 コマンドになる。
- **依存は標準ライブラリだけ** (Net::HTTP と JSON)。CDP も WebSocket も
  使わないので、headless でも、人が開いている実ブラウザでも同じに動く。
- 入力は**実マウス・実キーボードと同じ ring** に積む
  (`wasm/backend/input_wasm.c`)。機械の側から見て、操作された run と
  駆動された run の区別は付かない。
- 走査コードは `fmrb_input.rb` の表をそのまま使う (require_relative)。
  つまり**ファームウェアの keymap の逆引き**で、配列 (jp/us) にも追従する。
  実際 jp 配列で `text 'puts "hello"'` の二重引用符が正しく入った。

## できること

```
fmrb_web.rb up [--headless]        ブラウザを起動して機械が上がるまで待つ
fmrb_web.rb down                   閉じる
fmrb_web.rb status                 running / 解像度 / frame / /home の状態
fmrb_web.rb screenshot OUT.png     画面 (canvas の中身 = 426x240 の実フレーム)
fmrb_web.rb move|click|down|up X Y
fmrb_web.rb key NAME | text "..."  | sleep MS
fmrb_web.rb ls|cat|get|put|rm PATH ページの FS 経由でファイルを読み書き
fmrb_web.rb reload                 ページを再読込して機械が戻るまで待つ
```

コマンドは左から順に連結できる (`click 30 8 sleep 700 click 25 32`)。
スクリーンショットは**ページの画像ではなく機械のフレームバッファ**なので、
sim の画像と同じ土俵で比べられる。

`up` は Chrome を探す (Linux にあればそれ、無ければ WSL から Windows の
Chrome)。`down` は WSL 側の kill に加えて、プロファイル名
(`fmrb_drive`) で Windows 側の残りを落とす — **利用者自身の Chrome には
触らない**。

## これで閉じた穴: エディタで保存 → リロード

T2 の報告で「人の手で一度確かめたい」と書いた道筋を、道具で最後まで通した。

1. メニュー → Editor を開く
2. `puts "hello from the driver"` と打つ (jp 配列、引用符も正しく入る)
3. Ctrl+S → 保存ダイアログ → `[home]` → 名前 `driven.rb` → Save
4. `ls /flash/home` → `driven.rb 28`
5. `reload` → 機械が上がり直す → `cat /flash/home/driven.rb` →
   **`puts "hello from the driver"` がそのまま残っている**
6. さらに `put` で PC から `/home/from_pc.rb` を入れ、`down` → `up` で
   **ブラウザごと再起動**しても両方残っていた
7. 機械自身のファイルマネージャで `/home` を開くと `driven.rb 28B` と
   `from_pc.rb 24B` が並んでいる (機械の目でも見えている)

## 気づき

- **保存ダイアログの一覧はクリックだけでは入れない**。選択と決定が分かれて
  いて、`click` の後に `key enter` が要る (二重クリックは注入の間隔では
  作れない)。sim でも同じはずで、覚えておくと早い。
- `reload` は結果を返してから 200ms 後に実行する。先にリロードすると
  道具が返事を待ったまま取り残される。
- 道具の待ちは「返事が無い = 失敗」ではなく soft にした。ページが起動中の
  ときは黙って次を試す (`up` と `reload` はこれで機械の復帰を待つ)。

## MCP に載せた (2026-08-31)

`web_up / web_down / web_screenshot / web_input / web_fs / web_reload` の
6 ツールとして fmrb MCP サーバに載せた (この道具を呼ぶだけの層)。
別セッション・別エージェントからも同じように操作できる。報告は親リポジトリの
doc/mcp_tools/report/p5_web.md。

そのとき 1 つ分かったことがある: **機械の最初の 1 フレームは「ページが
答える」と同じではない**。ブートはブラウザ本体のスレッドを長く握るので、
起動直後・リロード直後の命令は待たされて空振りする。MCP 側は
「続けて 2 回答えるまで待つ」ようにし、この道具には `--timeout` を足した。
