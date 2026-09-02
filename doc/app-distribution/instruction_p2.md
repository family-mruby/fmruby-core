# 実装指示 P2: ブラウザ版の店

計画は plan.md、形式の正は spec.md、P1 の記録は report/p1a.md と p1b.md。
作業場所は `fmruby-core/wasm/web/` (`index.html` と `main.js`)。

## 範囲

ページの中に店を持たせ、`registry.json` から選んだアプリを機械の
`/flash/app/usr/<id>/` へ置く。**取得はページ (JavaScript) が行う** —
ブラウザ版には `Net::HTTP` が無い (`family_mruby_wasm.rb:71`。ソケットが
無い)。

spec.md 13.2 の「探す」と「入れたもの」は、ページでは 1 つの一覧で足りる
(320x240 の窓ではないので分ける理由が無い)。行ごとに状態を出す。

## `?app=<id>` は P2 に入れない

**受け皿が無いことが分かった (2026-09-02)。**

ページは機械のファイルは書けるが、**アプリを起動する手段を持たない**。
ブラウザには debug server が無く (実機の `/app/launch` にあたるものが無い)、
`startup_app` のような設定も存在しない (`page_settings_wasm.c` が受けるのは
解像度と配色だけ)。

要るのは**ファームウェア側の小さな仕掛け**で、材料は揃っている:

- `FmrbApp.config(key)` で system_conf の鍵を読める (`launcher.rb:343` が
  `launcher_exclude` でやっている)
- `spawn_app(app_name)` で起動できる (`launcher.rb:785`)
- ページから機械へ設定を渡す道は既にある (`--fmrb-conf=` →
  `page_settings_wasm.c` → `/flash/etc/system_conf.toml`)

つまり「起動時に `startup_app` を読んで `spawn_app` する」を desktop に足し、
`page_settings_wasm.c` に文字列の鍵を 1 つ通せばよい。**ただし desktop は
Spinel で生成されるので、足したコードが生成を通るかを別に確かめる必要がある**
(基底クラスに ivar を 1 個足しただけで壊れた例がある)。実機にも効く機能
なので、**独立したフェーズとして切る**。

## 作るもの

### 1. 一覧を取ってくる

既定の取得元:

```
https://raw.githubusercontent.com/family-mruby/family-mruby-apps/main/
```

`?registry=<URL>` で差し替えられるようにする (手元の写しで試すため)。

`registry.json` を 1 回取り、`base` と `files` から各ファイルの URL を組む。
COEP 下でも読めることは実測済み (spec.md 6)。

### 2. 出す

1 行 1 アプリ。**写しの小さな絵を出す** (`screenshot` の PNG をそのまま
`<img>` に。実機用の `thumb` BMP はページでは使わない)。名前・説明・作者・
版・分類を添える。

行の状態は 4 つ:

| 状態 | 出すもの |
|---|---|
| 未導入・動く | 「入れる」 |
| 未導入・**この環境では未確認** | 理由 (`env` に web が無い / 画面が足りない)。入れさせない |
| 導入済み・最新 | 版と「消す」 |
| 導入済み・**新しい版がある** | 「1.0.0 → 1.1.0」と「更新」 |

導入済みかどうかと版は、**入っている `.app.toml` を読んで判定する**
(`app_id` / `app_version` がそこにある。別の台帳を作らない。spec.md 13.3)。

### 3. 入れる

```
files を全部取得
  -> 大きさと sha256 を registry と照合 (crypto.subtle.digest)
  -> M.FS.mkdirTree('/flash/app/usr/<id>')
  -> M.FS.writeFile(...)
  -> flushHome()
```

`sha256` が合わなければ**一覧を取り直して 1 度だけやり直す** (spec.md 13.5)。

`required_heap_kb_linux` があれば、ブラウザの通常プール 1536 KB と比べる。
超えるなら large (3072 KB) が要ることを表示し、`large_memory = 1` を
`.app.toml` に書き足してから置く (spec.md 8.4)。**どちらも超えるなら断る。**

### 4. 入れたあとの案内

**入れただけでは一覧に出ない** — 機械はもう起動時の走査を終えている。
ページは「再読み込みするとランチャーに出ます」と出し、再読み込みの
ボタンを添える。

再読み込み後に出ることは P1-B で確認済み (ブラウザにはキャッシュが無く
毎回全走査になる)。

### 5. 消す

`/flash/app/usr/<id>/` のファイルを消して `flushHome()`。**`FS.rmdir` で
殻も消す** — `web_fs` には無いが `FS` にはある。空の殻が残ると害は無いが
溜まる (report/p1b.md)。

## 受け入れ条件

- [ ] 一覧に 3 本が出て、**3 本とも小さな絵が出る**。
- [ ] `wide_only` が「この環境では未確認」ではなく**入る** (426 幅は 400 を
      満たす)。`app_env` に `web` が無い検体を作ったら断ること。
- [ ] `hello_store` を入れて再読み込みすると、ランチャーに出て起動する。
- [ ] もう一度再読み込みしても残っている。
- [ ] `registry.json` の版を上げた写しを `?registry=` で読ませると、
      「更新」が出る。
- [ ] 消すと一覧から消え、`/flash/app/usr` に殻が残らない。
- [ ] `sha256` を 1 つ壊した写しを読ませると、入れるのが失敗する。

## 気をつけること

- **`rake wasm:web` の前に `source ~/emsdk/emsdk_env.sh`**。無いと 277
  ファイルを staging したところで止まる (report/p1b.md)。
- ページを直しただけなら `wasm:web` は要らない (`wasm/web/` は素の静的
  ファイル)。`web_reload` で足りる。
- 検証中は `?registry=` で手元の写しを読ませる。公開の `main` を試験の
  ために動かさない。

## report に残すこと

`report/p2.md`。特に、取得と検証で踏んだ罠、環境の判定が実際に効いたか、
そして `?app=` を別フェーズに送った判断の結果 (やってみて別の道が
見つかったならそれ)。
