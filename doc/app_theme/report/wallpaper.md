# 報告: 壁紙を選べるようにした

> 状態: 完了 | 更新: 2026-09-02 | 既定はテーマ追従、`wallpaper` にパスを
> 書けばそちらが勝つ。Config から選べる

ユーザ要望 (2026-09-02):「基本はテーマに壁紙を従わせるが、壁紙だけはパス指定で
任意のものが指定してあればそちらを優先する」。

## 決めた規則

| `wallpaper` の値 | 出る絵 |
|---|---|
| 空 (既定) | **テーマが決める**。cyberpunk ならネオン、それ以外は西部の家。画面の大きさに合う 1 枚を名前で選ぶ |
| パス | **そのファイル**。テーマより優先 |
| `"none"` | 絵なし (単色の机) |

## 直前の作りと、変えたところ

デスクトップは**名前が固定**のファイルを読んでいた
(`/usr/share/backgrounds/bg_426x240.png`、Retro/sim/ブラウザは
`/data/bg_426x240.png`)。**中身を差し替える**ことで絵を変える作りで、
差し替えるのはビルド (ブラウザ版) か起動時の `[[sync_files]]` (sim/Retro) の
仕事だった。つまり**動いている機械には選ぶ余地が無かった**。

- デスクトップが**自分で決める**ようにした (`wallpaper_path` →
  `resolve_wallpaper`)。Modern は選んだファイルを直接読み、Retro・sim・
  ブラウザは `gfx.sync_file` で固定の名前へ送ってから読む。sync_file は
  大きさと CRC32 を見て同じなら送らないので、毎回起動しても安い。
- テーマの判定は `cfg_current_preset`。**プリセット表を 2 か所に持たない**
  ため、Config の `cfg_detect_preset` に `FmrbConst::THEME_*` を渡している。
- 解像度は名前で選ぶ (`bg_cyber_852x480.png` など)。**存在確認は
  `File.exist?`** で、`gfx.file_status` ではない — あれは描画側に聞く関数で、
  ここで見たいのは送り元 (こちら側) のファイル。

### 起動時の転送は消した

Retro と sim は `[[sync_files]]` で起動時に西部の壁紙を `/data` へ送っていた。
デスクトップが自分で送るようになったので**この指定は不要**になり、しかも
**残しておくと害がある**: cyberpunk のときは起動時に西部 (4.1KB)、デスクトップ
起動時にサイバー (19.6KB) と同じ場所へ 2 回送り、互いに上書きするので CRC も
毎回外れる。**毎ブート約 24KB** が UART を流れることになる。3 つの config
(linux / linux_p4 / n16r8) から壁紙のエントリを外した。`[[sync_files]]` の
仕組み自体は残っている (アプリが使える)。

## Config の行

`Wallpaper` 行を足した。選択肢は**開いたときに探す**
(`cfg_scan_wallpapers`): `(テーマ)` `なし` + `/usr/share/backgrounds` と
**`/home/backgrounds` の .png**。ユーザがそこに画像を置けば候補に出る。

**当初は `/home` 直下を走査していた** (2026-09-03 に変更)。`/home` は
ユーザの持ち物を置く場所で、そこの名前を全部読んで一部を壁紙として
差し出すのはこのダイアログの仕事ではない。配布分と同じ名前の
`/home/backgrounds` に置き場所を決めた。**保存されるのはパスそのもの**
なので (`resolve_wallpaper`)、`/home` 直下を指した既存の設定は
そのまま表示され続ける。変わるのは一覧に並ぶかどうかだけ。
`/home/backgrounds` は起動時に作らない (無ければ候補が減るだけ)。
値の表示はファイル名だけ (パスは列に入らない)。

- **`CFG_H` を行数から計算するようにした**。200 の固定値で、行を 1 本足した
  瞬間に最後の行が footer の下に潜った。以後は増やしても崩れない。

## ブラウザ版で追加が要ったもの

`/etc` は訪問のたびにバンドルから作り直されるので、機械の中で保存した設定は
ページが localStorage に控えて `--fmrb-conf=` で戻している。その一覧
(`CONF_KEYS`) に **`wallpaper` が無く**、選んでも再読み込みで消えていた。

- `CONF_KEYS` に追加 (`CONF_THEME_KEYS` の切り出し位置も 4 → 5)。
- C 側の `CONF_TOKEN_MAX` は **32 バイト**で、
  `"/usr/share/backgrounds/bg_cyber_426x240.png"` は引用符込み 45 文字。
  値だけ `CONF_VALUE_MAX = 96` に分けた。**長すぎる値は切り詰めずに捨てる**
  (半分のパスはどのファイルも指さない)。
- 空白を含むパスは今も通らない (取り込みが空白で切る)。置く画像の名前に
  空白を入れないこと。

## Spinel で壁紙が出なかった (2026-09-03 追記)

**全 Spinel 構成 (カーネル + デスクトップ + エディタ) では壁紙が出なかった。**
標準構成そのものなので、気づくのが遅れると出荷される。

原因は `wallpaper_path` の**末尾が代入**だったこと。

```ruby
def wallpaper_path
  return @wallpaper_path if @wallpaper_resolved
  @wallpaper_resolved = true
  @wallpaper_path = resolve_wallpaper    # ← Ruby では代入式の値が返る
end
```

生成 C はこうなっていた。

```c
self->iv_wallpaper_path = sp_SystemDesktopApp_resolve_wallpaper(self);
return sp_box_nil();                     /* 代入の値を捨てている */
```

**Spinel の既知の穴「末尾 nil」** (doc/archive/... の spinel desktop 復旧記録に
ある 3 種のひとつ)。1 回目の呼び出しだけ nil を返し、2 回目からは memo が
効いて正しい値になるので、「ファイルの読み込みに失敗している」ように見えた。
実際は sync_file まで走っていて、ログにも `File sync: data/bg_426x240.png
up-to-date` が出ている。**最後に `@wallpaper_path` を 1 行足して直した。**

戻り値を使う他のメソッドは全部末尾を確認した (定数・ローカル・呼び出しで
終わっており、同じ形は無い)。

## 確かめたこと

sim (NARYAv4 構成、Spinel):

- 既定 (Light + `(テーマ)`) → 西部の家。
- `wallpaper` に `bg_cyber_426x240.png` を選ぶ → **Light のままネオンの壁紙**
  (パスがテーマに勝つ)。保存後の TOML も
  `wallpaper = "/usr/share/backgrounds/bg_cyber_426x240.png"`。
- `(テーマ)` に戻して Theme を Cyberpunk → **壁紙もメニューバーもネオン**
  (テーマが壁紙を連れてくる)。

ブラウザ (新しいプロファイル):

- 既定の見た目は変わらない (ネオン + cyberpunk)。
- Config で西部の家を選ぶ → **ページを再読み込みしても残る**
  (cyberpunk の色 + 西部の壁紙)。
- Config のテーマ欄が **Cyberpunk** と正しく出る (0x 読みの修正の効果)。

## 残っていること

- **ブラウザのページ側に壁紙の選択肢は無い**。壁紙もテーマも Config で選ぶ。
  (ページにも Theme のセレクタがあったが、2026-09-03 に外した。Config の方が
  presets が多く、1 つの設定に入口が 2 つあると優先順位の規則が要る — そして
  その規則が保たなかった。詳細は doc/wasm/report/page_theme_removed.md)
- 画面より小さい / 大きい画像を選んだときの扱いは決めていない (今は左上原点で
  そのまま描く)。同梱の絵は画面ちょうどなので、まずユーザの画像で問題になる。
