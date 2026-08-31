# T0 報告: 前提の実測 — FS は 1 つ、消えていたのは順番のせい

> 状態: 完了 | 更新: 2026-08-31 | 測定 A/B とも決着。T2 は設計どおり進めてよい

指示書は instruction_storage.md、設計は storage_persistence.md。
道具は `?fsprobe=1` (wasm/web/main.js、通常の起動経路には触らない枝) と
headless Chrome の `--dump-dom`。

## 測ったもの

実ブラウザ (headless Chrome) で、`?autostart=1&holdload=25000&fsprobe=1&w=852&h=480`
を開いて出た 7 行がすべてである。

```
FSPROBE preRun: conf readable before unpack = no
FSPROBE preRun: marker write failed: errno=44
FSPROBE preRun: /flash/home made and written
FSPROBE result: display_width=852 asked=852x480 marker=gone
FSPROBE result: /flash/home visible to the page = yes
FSPROBE result: page file in /home after boot = "page-preRun"
FSPROBE result: /home holds [".","..","fsprobe.txt"]
```

## 結論 A: ファイルシステムは 1 つで、ページと機械で共有されている

- `display_width=852` は**機械が書いた値**である。ページは argv
  (`--fmrb-res=852x480`) を渡しただけで、`/flash/etc/system_conf.toml` を
  852 に書き換えたのは機械側の page_settings_wasm.c。それをページが
  `Module.FS.readFile` で読めている。
- `/flash/home` は**機械しか作らない** (T1 でファイル HAL に入れた mkdir。
  梱包にこのディレクトリは 1 つも入っていない)。それがページから見える。
- 逆向きも通る: ページが起動前に置いた `/flash/home/fsprobe.txt` を、
  起動後にページが読み返せる (中身も一致)。

**report/p5.md の付記「ページの MEMFS と機械の MEMFS は別物」は誤り**。
生成 JS のファイル系システムコールが全部 `proxyToMainThread` である、という
読みが正しかった。

## 結論 B: 消えたのは順番のせい

- ページの preRun が走る時点で `/flash/etc/system_conf.toml` は読めず
  (`readable before unpack = no`)、書き込みは **errno 44 (ENOENT)** で失敗する。
  親ディレクトリすら無い。
- 理由は梱包側の展開の作り: file packager は自分の `runWithFS` を
  `Module["preRun"].push(...)` で**後ろに足す**。しかも runWithFS は
  ディレクトリを作ってから `addRunDependency` してデータの到着を待ち、
  実ファイルの書き込み (`processPackageData`) は**全部の preRun が終わった
  後**に走る。だからページが preRun で書いたものは、
  「まだディレクトリが無くて失敗する」か「後から上書きされる」かのどちらかになる。
- 目印が `gone` なのはその帰結。

つまり P5 で設定が効かなかった症状の正体は**順番**であって、FS が別物だから
ではない。

## 結論 C: T2 の継ぎ目はここでよい (先に確かめた)

上の B があるので「ページが preRun で FS を触る」ことは一般には危ういが、
**`/home` は梱包に 1 つも入っていない** (T1 の成果) ため衝突しない:

- ページが preRun で `FS.mkdirTree('/flash/home')` してファイルを置ける
  (`/flash` ごと作れる。後から来る `FS_createPath("/","flash",...)` は
  既存でも平気)。
- それが展開後も、機械の起動時 mkdir (EEXIST で素通り) の後も残っている。

よって T2 は「ページが preRun で `/flash/home` を作り、IDBFS を割り当て、
`FS.syncfs(true)` を `addRunDependency` と組にして機械の起動を待たせる」で
進めてよい。**梱包データの到着待ちとページの読み込み待ちは、どちらも
run dependency なので自然に合流する** (両方が外れて初めて main() が動く)。

## 設定の運び方は変えない

FS が共有だと分かったので、設定をページから直接書くことも「展開の後で
やる限りは」できる。それでも **argv + page_settings_wasm.c のままにする**:
機械が自分の起動の中で、自分が読む直前に焼き込む方が順番の事故が起きない。
T0 の結果は「ページから FS を触ってよい」を保証しただけで、
「ページから触るべき」ではない。

## 残したもの

- `wasm/web/main.js` の `?fsprobe=1` の枝 (通常起動では 1 行も動かない)。
  T2 以降で「ページと機械のどちらから見えているか」を確かめ直したくなるので
  常設する。
- `wasm/CMakeLists.txt` の `EXPORTED_RUNTIME_METHODS` に `FS` を追加
  (T2 で必要。node の `core` は NODERAWFS なので無関係)。

## 手順のメモ (再現用)

```
rake wasm:web                      # ← T1 の移動を git に stage してから
rake wasm:serve PORT=8021
chrome --headless=new --disable-gpu --dump-dom \
  'http://localhost:8021/wasm/web/index.html?autostart=1&holdload=25000&fsprobe=1&w=852&h=480'
```

`--dump-dom` を使うのは、headless Chrome の console 出力より確実に拾えるため
(probe の各行はページ内の `<pre id="fsprobe-out">` にも出す)。

**この測定のときだけ、staging は git の index ではなく作業ツリーから作った**
(T1 の移動が未 stage で `rake wasm:webflash` が古いパスを探して失敗するため)。
梱包された 280 ファイルの中身は移動後の姿と一致している。
