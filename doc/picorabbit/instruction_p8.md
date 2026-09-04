# 実装指示書 P8: スライドに動画を載せる

対象: 実装担当セッション。作業リポジトリ: fmruby-core (parser / renderer /
アプリ / サンプル。GFX 側の変更は不要)。先に読むもの: plan.md の P8、
instruction_p7.md (画像の流れ。動画はこれの写し)、
doc/archive/video/plan.md と report/p1_p4.md (再生器の契約と実測値)、
doc/wasm/report/mjpeg.md (ブラウザ版の再生器)。報告は report/p8.md。

## 確かめてある事実

- 再生器は実装済で、Ruby からは `@gfx.video_open(path, x:, y:, fps:, loop:)`
  → `FmrbVideo#play / pause / stop / rewind / status`
  (`lib/add/picoruby-fmrb-app/mrblib/fmrb-gfx.rb:471`)。**Modern (Tab5) と
  ブラウザ (wasm) で動き、それ以外では通信せずに nil を返す**
  (`ports/esp32/gfx.c` の門。Linux sim もここで nil になるので 10 秒の
  同期待ちにはならない)。
- 描画側 (`display_p4_task.cpp` の VIDEO_OPEN) は `fmrb_hal_file_resolve_path`
  でパスを解くので、`/home/...` `/usr/share/...` `/mnt/sd/...` がそのまま
  渡せる。`sync_file` は要らない (Tab5 は同じ VFS、wasm は同じプロセス)。
- 再生器は**拡縮しない**。コマの大きさのまま canvas の (x, y) に毎コマ
  書き、canvas の端で切る。**同時に 1 本**。開いたときは一時停止で、
  play で動き出す。stop すると最後のコマが canvas に残る。
- 再生中の約束は 1 つ: **その矩形の中に自分で描かない**。うさぎ・亀は
  composite で合成される sprite なので canvas を汚さない。
- 上限: P4 は 448x256 / 1 コマ 512KB、wasm は 640x480。幅・高さは 16 の
  倍数が望ましい (P4 の復号器の格子)。実測は 416x240 @15 で安定、@30 は
  表示側が数コマ落とす。wasm は 320x176 @15 で drop 0。
- 素材は `ffmpeg -i src.mp4 -vf "scale=288:160,fps=15" -q:v 4 -f mjpeg out.mjpg`
  の一行。試験用は `tools/gen_test_mjpg.py`。

## 仕様

### 書き方

画像と同じ `![alt](path)` で、**拡張子 `.mjpg` なら動画**。

```
![](movies/demo.mjpg)               # 15 コマ/秒、繰り返し
![fps=10](movies/demo.mjpg)         # コマ数/秒を指定 (1〜30)
![once](movies/demo.mjpg)           # 1 回で止まる (既定は繰り返し)
{:.center}                          # 寄せは従来どおり
```

- alt は空白区切りの語の並びとして読む。`fps=NN` / `once` / `loop` が動画の
  指定。画像の `w=NNN` / `NN%` もこの読み方に揃える (画像の既存の書き方は
  そのまま通る)。動画に `w=` / `%` を書いても**無視する** (拡縮できない)。
- 相対パスは .md のあるディレクトリ基準 (P7 と同じ `image_source`)。
- 1 枚のスライドに動画は **1 つ**。2 つ目は代替表示。

### 置き方と寿命

- コマの大きさのまま置く。**本文幅と残り高さに入らなければ代替表示**
  (`[video: path WxH does not fit]`)。入るときは `calc_align_x_px` で
  横位置を決める。下に `@line_h / 2` の余白。
- **開くのは描画の途中、動かすのは present の後** (`render_slide` の末尾で
  `play`)。`render_slide` / `render_index` の先頭で **必ず stop** する
  (canvas を clear する前)。wait の段階送りも同じ経路なので、段階を進める
  たびに動画は先頭からやり直しになる。これは仕様として受け入れる。
- アプリ側は、canvas を自分で塗る場面 (黒/白画面、メニューへ戻る、
  Ctrl+Tab の退避、終了) で `@renderer.stop_video` を呼ぶ。復帰は
  `draw_current` が開き直す。
- Export は描いた時点の合成結果を書くので、動画の矩形には最初のコマが
  間に合っていれば写る (間に合わなければ空)。追加作業はしない。
- `video_open` が nil (Retro / sim) なら `[video: path]` の 1 行。例外で
  落とさない。
- 1 本ごとに「open の所要と大きさ・コマ数/秒・位置」をログに出す。

## サンプル

- `flash/usr/share/samples/slides/movies/demo.mjpg` を `gen_test_mjpg.py`
  で作る (288x160、30 コマ、100KB 前後)。
- `demo.md` と `demo_ja.md` に動画の枚を 1 枚ずつ足す (本文 1 行 + 動画、
  中央寄せ)。Modern の本文 12px で title 20 + 4 + 15 + 160 + 8 = 207 < 220
  (max_y) に収まる寸法。

## 検収

### wasm (web_up)

- demo.md の動画の枚で絵が動く (web_screenshot を 2 回撮って差がある)。
  `status` の shown が増え、dropped が 0 か小さい。
- 段階送り・前後・索引・黒画面・メニューへ戻る、のそれぞれで動画が
  止まり、戻ると動き直す。
- 放置中の present が動画のコマ分だけ (デスクトップの時計 1/s +
  動画 15/s。GFX STATS があれば)。

### Tab5

- 同じ枚の実機画面 2 枚 (差があること)、status の shown / dropped。
- ブートに `Guru|abort` 無し。gfx.c の門を wasm 対応で触った後の
  **P4 の回帰確認**を兼ねる (doc/wasm/report/mjpeg.md の「まだ確かめて
  いないこと」4)。

### sim (Retro / Modern 向け)

- 同じ枚で `[video: movies/demo.mjpg]` の代替表示になり、落ちない。
  10 秒待ちにならない (ログの時刻)。

## 受け入れ条件

- 上の検収が report/p8.md に揃う。
- コミット 2 本: (1) parser / renderer / アプリ (本書と plan の P8 を含む)、
  (2) サンプル (.mjpg + .md 2 本)。英語、ユーザ確認のうえ。
- `.env` が作業前の値 (TAB5) に戻っている。

## やらないこと

- 音声 (再生器に無い)。拡縮 (同上)。全画面の動画。1 枚に 2 本以上。
- 途中からの再生、段階送りで動画を続きから動かすこと。
