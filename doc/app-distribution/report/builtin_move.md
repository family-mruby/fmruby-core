# 報告: 店を default app へ移した

> 状態: 実装済 (実測待ち) | 更新: 2026-09-03 | `flash/app/tool/` の Ruby から
> `main/prebuild_scripts/default_app/` の bytecode へ。`large_memory` を外した

ユーザ提案 (2026-09-03):「app store app は default の prebuild app に移動する
のがいいかなと思う」。

## なぜ

**855 行を起動のたびに翻訳していた。** `flash/app/**` の Ruby は spawn 時に
その場でコンパイルされる。コード生成の作業領域が VM プールを食い、店だけが
`large_memory = 1` を必要としていた — それでも Retro では足りなかった
(2026-09-03 のユーザ報告)。bytecode なら翻訳そのものが起きない。

位置づけも合う。**アプリを入れる道具が、自分で自分を消せる場所に居た**。
shell / editor / inspector と同じ棚に移った。

## 費用

| | |
|---|---|
| bytecode | **19.3 KB** (元のソースは 26.4 KB) |
| Tab5 の app 区画の空き | 346.8 KB → **5.6%** を使う |

storage から 26.4 KB 減って app 区画が 19.3 KB 増えるので、**正味は区画を
またいだ移動**である。

## やったこと

- `git mv` で `default_app/appstore.app.rb` へ。**CMake は
  `default_app/*.rb` を GLOB しているので登録は自動** (`appstore.c` /
  `appstore_irep` が `NAME_WE` から決まる)。
- `.app.toml` は削除。持っていた設定は spawner の表へ移した
  (280x190、位置 14,26、`resizable`、最小 240x150)。**`large_memory` は落とした**
  — 翻訳が無くなれば要らないはず、というのがこの移動の主目的だから。
- ランチャーは走査で見つけられなくなるので `builtin_apps` に足した。
  名前は i18n の `:app_store` (en "App Store" / ja "アプリの店")。
  **`.app.toml` が消えた今、名前とアイコンはこの 1 か所にしかない。**

## まだ確かめていないこと

**ビルドも実測もしていない** (ユーザの指示でビルドは保留)。見るべきは 3 点。

1. **Retro で `large_memory` 無しに動くか。** これが移動の目的。駄目なら
   spawner の表に戻す口はある。
2. **起動が速くなったか。** 翻訳が消えるぶん。今ある実測は「Tab5 で店が
   最初の絵を出すまで約 5 秒」(doc/direct_boot/plan.md) の 1 点。
3. **区画に収まるか。** Tab5 は 6% 空き。19.3 KB は入る計算だが、実測で確認。

ランチャーの一覧に「アプリの店」が 3 番目に出ること、開いて一覧が取れること、
入れて消せることも通す。
