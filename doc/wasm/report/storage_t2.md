# T2 報告: /home が IndexedDB に残るようになった

> 状態: 完了 | 更新: 2026-08-31 | ブラウザを閉じても /home が残ることを実測。編集は wasm/web/main.js と wasm/CMakeLists.txt だけ

指示書は instruction_storage.md、前提の実測は report/storage_t0.md。

## やったこと

**core (C / Ruby) は 1 行も触っていない**。ページ側だけで完結した。

- `wasm/web/main.js`
  - `moduleConfig.preRun` に `mountHome` を積む。中身は
    `FS.mkdirTree('/flash/home')` → `FS.mount(IDBFS, {autoPersist:true})` →
    `addRunDependency('fmrb-home-load')` + `FS.syncfs(true, ...)` →
    完了したら `removeRunDependency`。**梱包データの到着待ちと同じ仕組み**
    なので、両方が外れて初めて main() が動く = 機械は読み込み済みの /home で
    起動する。
  - `visibilitychange` (hidden) と `pagehide` で `FS.syncfs(false)`。
    `beforeunload` は使わない (非同期の書き戻しが間に合わない)。
  - 失敗したら警告を出して**揮発のまま起動を続ける** (プライベート窓や
    記憶域を拒否された環境で、起動そのものを止めない)。
- `wasm/CMakeLists.txt`: `-lidbfs.js` (IDBFS は既定で入らない) と
  `EXPORTED_RUNTIME_METHODS` の `FS` (T0 で追加済)。node の `core` は
  NODERAWFS なので無関係。

## 実測 (headless Chrome、プロファイルを残して 2 回起動)

機械側から /home へ書く経路を確かめるため、**検証用のサービスを 1 本だけ
staging に足して**測った (リポジトリには入れない)。無ければ
`/home/machine_probe.txt` に時刻を書き、あれば読むだけのサービス。

| 実行 | プロファイル | machine_probe | 判定 |
|---|---|---|---|
| 1 回目 | 新規 | `written-at-1788134301821` | 機械が書いた |
| 2 回目 | 1 回目と同じ | `written-at-1788134301821` | **残っていた** (書き直しではない) |
| 3 回目 | 別の新規 | `written-at-1788134377998` | 別の時刻 = 梱包由来ではない |

- `home store = persistent` (populate 成功) が 3 回とも出た。
- 検証用サービスを外した最終ビルドでは `/home holds [".",".."]` で、
  初回起動の /home は空。デスクトップの表示も従来どおり (画面確認済み)。

これで通っている経路は **worker (サービスホスト) の File.open →
ファイル HAL → proxy されたシステムコール → IDBFS の割り当て先 →
autoPersist → IndexedDB** で、T2 が対象にした経路そのもの。

## 効いた設計判断

- **/home を空にしておいたこと (T1) が前提**。梱包が /home に 1 つも
  ファイルを持たないので、ページが先に作って割り当てても、後から走る
  展開と喧嘩しない。もし手本が /home に残っていたら、割り当てで隠すか
  展開に上書きされるかのどちらかで、どちらも壊れる。
- **機械側の /home mkdir (T1) と衝突しない**。割り当て済みのディレクトリに
  対する mkdir は EEXIST で素通りする。実測でも起動は素通りした。
- **run dependency に乗せたこと**。ページ側で「起動を待たせる」独自の仕掛けを
  作らずに済んだ。

## 残り

- ~~エディタで書いて保存 → リロードの実操作は未確認~~ →
  **2026-08-31 に確認済**。ブラウザを駆動する道具を作って通した
  (report/drive_tool.md): エディタで書いて /home に保存 → リロード →
  中身がそのまま。ブラウザごと再起動しても残る。
- 恒久化の要求 (`navigator.storage.persist()`)、使用量表示、多タブ排他は
  T3。書き出し / 取り込みは T4。
- `rake wasm:web` は T1 の移動が git の index に入るまで失敗する。この
  report の測定は作業ツリーから staging を作って行った。
