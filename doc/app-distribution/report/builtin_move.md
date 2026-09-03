# 報告: 店を default app へ移した

> 状態: 完了 | 更新: 2026-09-03 | `flash/app/tool/` の Ruby から
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

## 実測 (Tab5、同じボード・同じ日)

| | 旧 (`/app/tool` の Ruby) | 新 (組み込み bytecode) |
|---|---|---|
| spawn → 窓ができるまで | **10.04 秒** | **0.45 秒** |
| うち翻訳 | **8.79 秒** | 無し |
| タスクのスタック | 16384 B | **12288 B** (残 5116) |
| VM プール | 1 MB (`large_memory`) | **1 MB (既定)** |
| app 区画 | — | 6% → 5% (+19.1 KB) |

**22 倍**。読む前に「約 5 秒」という数字を持っていたが
(doc/direct_boot/plan.md)、それは別の測り方だったらしく、**翻訳だけで
8.79 秒**かかっていた。

`large_memory` 無しで動く。ログのプールは 1048576 バイト = 既定値、
`internal RAM ok: free=129436 largest=71680 need=20480` と余裕もある。
**この移動の目的は達した。**

一覧の取得 (ネットワーク) は別勘定で、開いてから約 1.6 秒。

## 確かめたこと

- ランチャーに「App Store」が 3 番目、専用アイコン付きで出る。
- 開いて一覧が取れる (3 apps for modern)、説明とサムネイルも出る。
- **Retro (S3) は未確認**。今 Tab5 が繋がっているため。効き方は Retro の方が
  大きいはず (メモリ不足で動かなかったのはあちら)。
