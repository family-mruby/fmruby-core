# 実装指示: wasm のストレージ永続化 (T0-T4)

> 状態: 完了 (T5 を除く) | 更新: 2026-08-31 | T0-T4 実施済。報告は report/storage_t0.md, storage_t2.md, storage_t3_t4.md

設計と判断の根拠は storage_persistence.md。この文書は**作業の指示**で、
各フェーズの成果は report/storage_tN.md に書く。

前提 (T1 で済んでいること):

- `/home` は空で出荷される。配布物は `/usr/share/` 配下 (samples / services /
  doc)。`/home` は起動時にファイル HAL が作る。
- ブラウザの system service 一覧は `config/services_wasm.toml`。

共通の決まり:

- **実機とサーバに影響を出さない**。触るのは `wasm/` 配下と、必要なら
  `rakelib/wasm.rake` だけ。core の C/Ruby には手を入れない (入れたく
  なったら、その時点で理由を report に書いてから)。
- 変更のたびに `rake wasm:web` → `rake wasm:scan` が通ること。scan は
  「梱包した物 = staging」と資格情報の不在を機械判定する。
- ヘッドレス確認は `rake wasm:serve DIST=1` + `?autostart=1&holdload=N` と
  headless Chrome (P5 で作った probe-hold)。

---

## T0: 前提の実測 (方式が変わり得るので最初)

report/p5.md の付記は「ページの MEMFS と機械の MEMFS は別物」と結論して
いるが、生成 JS を読む限りファイル系のシステムコールは全部
`proxyToMainThread` で、FS の実体は main スレッドに 1 つに見える。
**どちらが本当かで T2 の作りが変わる**ので先に測る。

### 作業

1. `wasm/CMakeLists.txt` の `core_web` の
   `EXPORTED_RUNTIME_METHODS` に `FS` を足す (node の `core` は
   NODERAWFS なので不要)。
2. `wasm/web/main.js` に `?fsprobe=1` のときだけ動く枝を足す。ページを
   汚さないよう、通常の起動経路には触らない。
   - **測定 A (共有か)**: 起動完了後にページから
     `Module.FS.readFile("/flash/etc/system_conf.toml")` を読み、
     `--fmrb-res=852x480` で起動したときに `display_width = 852` に
     なっているかを見る。機械が page_settings_wasm.c で書き換えた結果が
     ページから見えるなら、FS は 1 つで共有されている。
   - **測定 B (順番か)**: preRun で同じファイルに目印を書き、起動後に
     その目印が残っているかを見る。消えていれば、梱包の展開
     (`Module["preRun"].push(runWithFS)`) が後で上書きしている
     = p5 の症状の正体は順番。
   - 結果は `console.log` に 1 行ずつ出し、headless Chrome のログで拾う。

### 受け入れ

- A と B の結果が両方とれて、report/storage_t0.md に「共有かどうか」
  「上書きの順番かどうか」が断定で書けること。
- 通常起動 (クエリ無し) の見た目と動きが変わらないこと。

### この結果で決まること

- A が「共有」なら T2 は設計どおり (ページが IDBFS を割り当てる)。
- A が「別物」なら T2 は作り直し: ページから FS を触れないので、
  機械側から永続化を駆動する経路 (proxy 越しの書き戻し) を設計し直す。
  この場合は T2 に入る前に storage_persistence.md の「方式」を書き換える。

---

## T2: /home の IDBFS 化 (永続化の本体)

### 作業

1. ページ側 (`wasm/web/main.js`) で、**機械を起動する前に**:
   - `FS.mkdir("/flash/home")` (無ければ作る)。機械側の mkdir は既存なら
     EEXIST で素通りするので、先に作っておけば衝突しない。
   - `FS.mount(IDBFS, { autoPersist: true }, "/flash/home")`。
   - `FS.syncfs(true, cb)` で IndexedDB から読み込む。**`addRunDependency`
     と組にして、完了するまで機械を起動させない**。
2. リンクに `-lidbfs.js` を足す (IDBFS は既定では入らない)。
3. 取りこぼしの回収: `visibilitychange` (hidden) と `pagehide` で
   `FS.syncfs(false, ...)`。`beforeunload` は当てにしない。
4. 失敗時の振る舞い: IndexedDB が使えない (プライベート窓など) ときは
   **黙って揮発で動く**。起動を止めない。ページに状態は出す (T3)。

### 受け入れ

- エディタで `/home/x.rb` を書いて保存 → リロード → 中身が残っている。
- 保存 → タブを閉じる → 開き直す → 残っている。
- IndexedDB を消す → 出荷状態 (空の /home) で起動する。
- プライベート窓で起動でき、書けて、消えることが説明できる。
- `rake wasm:scan` が通り、node ビルド (`rake wasm:run`) が従来どおり動く
  (IDBFS はブラウザ経路だけの話で、node には影響しない)。

---

## T3: ページの記憶域まわり

### 作業

- `navigator.storage.persist()` を要求し、結果 (恒久 / 通常) を出す。
- `navigator.storage.estimate()` で使用量と空きを出す。
- 多タブ: Web Locks で 1 枚目だけを書き手にする。2 枚目は永続化を切って
  警告を出す (壊すよりは読み取り専用の方がまし)。

### 受け入れ

- 2 つのタブで開いても、後から閉じた方が先の保存を消さない。
- 恒久化の可否と使用量がページに出る。

---

## T4: 書き出し / 取り込み / 初期化

### 作業

- 書き出し: `/home` を tar に固めてダウンロード。
- 取り込み: tar を選んで `/home` に展開。**パスの正規化は必須**
  (`..` と絶対パスを弾く)。取り込み後に syncfs。
- 初期化: IndexedDB を捨てて出荷状態に戻す (確認を挟む)。
- 実装はページ側の JS で完結させる (機械側の microtar は使わない。
  ページから触れるものをページで扱う方が単純)。

### 受け入れ

- 書き出した tar を別のブラウザで取り込むと、同じ `/home` になる。
- 細工した tar (`../` を含む) が `/home` の外に書けない。

---

## T5 (任意、後回し可)

- `/app/usr` の永続化 (同名アプリの優先順位を決めてから)。
- `/var/cache/launcher_index` の持ち越し (梱包の版が変わったら捨てる鍵)。
