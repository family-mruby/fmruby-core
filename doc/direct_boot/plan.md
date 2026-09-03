# まっすぐ起動する (ロゴ・BGM を省く / 全画面アプリへ直行)

> 状態: 構想 | 更新: 2026-09-03 | 設定 1〜2 個で「1 つのことをする機械」に
> なる。実装は未着手、コードの当たりと risk は調査済み。

## 目的

Family mruby を**特定のアプリを動かすための機械**として置くときに、起動が
そのアプリまでまっすぐ届くようにする。今は 2 つ余計なものが挟まる。

1. **ロゴと起動音** — 楽しいが、毎回見たいものではない
2. **壁紙とタスクバーが数秒見えてから**、指定した全画面アプリに切り替わる

どちらも設定で消せるようにする。**既定は今のまま** (この機械の第一印象は
ロゴと音であって、それを黙って捨てない)。

## 今どうなっているか (調査済み)

```
on_create
  … 配置計算 …
  start_boot_animation      白背景 + 黒前景、背景にロゴ、FmrbAudio を作る
on_update  (@boot_anim_state != :done のあいだ)
  tick_boot_animation       虹彩を開く 28 フレーム × 60ms = 1.68 秒
                            開ききったら :wait_to_finish で BOOT_HOLD_MS = 1.0 秒
  finish_boot_animation
    ロゴ画像を破棄、音を止める
    draw_background         ← 壁紙
    draw_foreground         ← メニューバー + タスクバー
    FmrbApp.enable_cursor
    FmrbKernel.boot_complete!    LED を心拍に切り替え
    @composite_regions_enabled = true / update_composite_regions
    spawn_startup_app       ← ここでやっと全画面アプリが起動する
```

- **ロゴと音で 2.68 秒** (1.68 + 1.0)。起動全体が 7.24 秒なので**3 分の 1 強**
  (reference/boot_performance.md)。
- **壁紙が見える時間はもっと長い**。`spawn_startup_app` が呼ばれてから
  アプリが最初の絵を出すまで、Tab5 の App Store で **約 5 秒**
  (09:32:40.756 `Loading Ruby script` -> 09:32:45.970 最初の出力)。
  つまり「一瞬」ではなく数秒である。

### 全画面アプリが画面を取ると、デスクトップは止まる

`enter_fullscreen` (kernel/fmrb_kernel/app_lifecycle.rb) が
**デスクトップを suspend する** ("Notify desktop to stop drawing")。
抜けるときは `pop_fullscreen_frames` が resume し、デスクトップの
`on_resume` が `draw_foreground` + `draw_background` を**両方やり直す**。

**この後始末が既にあることが、案 2 を安くしている** — 起動時に一度も描かなく
ても、全画面アプリを閉じた時点で正しく描かれる。

## 案 1: ロゴと起動音を飛ばす

`system_conf.toml` の最上位キー 1 つ。読む道具は既にある
(`read_conf_string`。`startup_app` を読むのに使っている)。

```toml
boot_splash = false   # 既定 true
```

`start_boot_animation` の冒頭で false なら、白/黒で覆うのもロゴの読み込みも
`FmrbAudio` の生成もせず、**`finish_boot_animation` へ直行する**。

- 後半 (`enable_cursor` / `boot_complete!` / 領域合成の有効化 /
  `spawn_startup_app`) は**どの道通す**。ここを飛ばすと LED が心拍に
  ならず、カーソルも出ず、起動アプリも上がらない。
- 音だけ・絵だけを消したい需要は今のところ想像でしかないので、**1 キーに
  する**。分けたくなってから分ける。

**難しさ: 低い。** 状態機械の入口を 1 つ増やすだけで、出口は共通のまま。

## 案 2: 全画面の起動アプリへ直行する (壁紙を見せない)

`finish_boot_animation` が **描いてから spawn する**のが原因。順番を入れ替え
ても直らない (spawn は非同期で、デスクトップはそのまま描きに戻る)。
**描かないようにするしかない。**

```
finish_boot_animation
  起動アプリが全画面で上がると分かっているなら
    draw_background / draw_foreground を「やらない」
    画面はそのまま (ロゴ、または案 1 と併用なら黒) でアプリの初回描画を待つ
  それ以外は今までどおり描く
```

### 全画面かどうかを、spawn する前にどう知るか

kernel は `win[:fullscreen]` で判断するが、それは**spawn した後**にしか無い。
デスクトップ側で先に知る必要がある。

- **採る案: 起動アプリの `.app.toml` を自分で読む。**
  `default_window_mode = "fullscreen"` を見る。kernel が spawn 時に読むのと
  同じ 1 次情報なので食い違わない。デスクトップには toml を行で歩く道具が
  既に 2 つある (`read_conf_string`、ランチャーの `parse_app_toml`)。
- 採らない案: `startup_app_fullscreen = true` のような設定キーを別に置く。
  同じ事実が 2 か所になり、ずれる。

### 描かないまま何も起きなかったときの逃げ道

**画面が黒いまま何も起きない**のが最悪なので、必ず戻り道を用意する。

- アプリの起動が失敗すると kernel から `show_error` が来る
  (`on_control` の `when "show_error"`)。**ここでデスクトップを描く。**
- それでも来ない場合の backstop として時限。`STARTING_TIMEOUT_MS = 25000`
  (起動指示器の取り下げ) が同じ役目の前例なので、**同じ考え方で短めの値**を
  置く。全画面アプリが上がれば `enter_fullscreen` が suspend するので、
  時限が満了する頃には普通は用済みになっている。

**難しさ: 中。** 描画を止めること自体は簡単だが、**戻り道 2 本を必ず通す**
のが本体。ここを手抜きすると「たまに黒いまま起動する機械」になる。

## 受け入れ条件

- 既定の設定で、今と同じようにロゴが出て音が鳴り、デスクトップが出る
- `boot_splash = false` で 2.68 秒縮み、crash マーカー 0
- `startup_app` が全画面アプリのとき、**壁紙もタスクバーも一度も見えない**
- その全画面アプリを閉じると、壁紙もメニューバーも正しく出る
  (`on_resume` の既存経路)
- **`startup_app` の起動に失敗したとき、デスクトップが出る** (黒画面で
  終わらない)。故意に壊した `.app.toml` で確かめる
- `startup_app` が窓アプリのときは今までどおり (デスクトップが先に出る)

## 未確定事項

- **キー名**。`boot_splash` / `boot_animation` / `splash` のどれか。
  system_conf の既存の語彙に合わせて決める。
- **案 2 の時限の値**。25 秒は起動指示器の値で、ここでは長すぎる可能性がある。
  実機で「全画面アプリが最初の絵を出すまで」を数本測ってから決める
  (App Store で約 5 秒という 1 点しか無い)。
- **案 1 と案 2 を併用したときに何を映しておくか。** ロゴを出さず全画面
  アプリを待つ数秒、画面は黒 (`@gfx.clear(0x00)` のまま) になる。それが
  望みなのか、単色や「起動中」の 1 行が要るのかは、使う人に訊く。
  **長い黒画面で不具合を隠さない**という既存の方針とも突き合わせること。
- Retro (S3) でも同じ絵になるか。全画面の扱いは共通だが未確認。

## 触る場所

| | |
|---|---|
| `main/prebuild_scripts/kernel/system_desktop.app.rb` | `start_boot_animation` / `finish_boot_animation` / `spawn_startup_app` / `on_control` の `show_error` |
| `config/system_conf_*.toml` | キーの追加とコメント (`flash/etc/` は生成物なので触らない) |
| kernel 側 | **変更不要の見込み**。suspend/resume は既にある |
