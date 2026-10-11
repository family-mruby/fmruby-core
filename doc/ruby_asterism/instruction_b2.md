# Asterism B2 指示書: 公開の版のブラウザ版から網に入る

> 状態: 指示 | 更新: 2026-10-12 | https で配るブラウザ版 (公開の版と同じ作り) から、Asterism の網に入れるようにする。`wss://`、ルータの場所の設定、ブラウザごとのノードの ID。公開の版への反映 (family-mruby.github.io への配置) はしない

前提: plan.md の B の節、report/b1.md (特に 10 章「B2 への申し送り」)、doc/ruby_asterism/browser.md、design.md 9 章 (ブラウザ版の識別: ランダムな ID、localStorage、Web Locks)、fmruby-core の doc/wasm/ (ページの設定 CONF_KEYS、公開の版の作り)、asterism-console の docs/v2_2.md (端末の印の決まり)。

言葉: 文書では、Family mruby の基板・sim・ブラウザ版を「端末」と呼ぶ (「機体」は使わない)。

## 1. 作るもの

1. **wss://**:
   - ページが https なら `wss://`、http なら `ws://` でつなぐ (zenoh-pico のロケータは `ws/` のまま。URL の組み立て `_z_ws_endpoint_init` で選ぶ)。ロケータの設定で明示的に選べるようにするかは、作りの簡単な方でよい (docs に書く)。
   - まず調べる: https のページから `ws://localhost` / `ws://127.0.0.1` へつなぐことを、Chrome と Firefox が許すか (ループバックは混在の例外になっているはず)。許すなら、手元のルータには TLS なしでつなげる。結果を report に書く。
   - 手元以外のルータ用に、TLS を終えてルータの `ws/` に流す中継の例を、親リポジトリに compose の重ね合わせとして足す (例: caddy か nginx。証明書は手元の確かめ用に自己署名でよい。既存のファイルの意味は変えない)。
2. **ルータの場所の設定**:
   - ページの設定 (今の設定の画面と CONF_KEYS の流儀) に `zenoh_router` を足し、保存されて次の起動に渡るようにする。`?zenoh=` は今までどおりその回だけの上書き。
   - 画面の見た目・項目名は今の設定の画面の流儀に合わせる (新しい作りの画面を起こさない)。
3. **ブラウザごとのノードの ID**:
   - design.md 9 章の案: 最初の起動でランダムな ID を作り、ブラウザの保存領域 (localStorage など、今の永続化の流儀) に置く。同じブラウザで 2 つのタブが同じ ID で入らないよう、Web Locks で 1 つだけにする (2 つ目のタブは別の ID か、つながない。分かりやすい方を選び docs に書く)。
   - `/home/asterism_node.txt` があればそれを優先する (B1 と同じ)。
   - ID の形は、asterism-console が端末 (▣) と判じられる形にする。console の決まり (`fmruby-<16 進 6 桁>` / `linux` / `wasm`) に合う形 (例: `wasm-<16 進 6 桁>`) を選び、console の側の決まりを足す必要があれば console も直す (main にコミット・push してよい)。
4. **公開の版の作りで確かめる**:
   - 公開の版と同じ作り方 (doc/wasm/ の公開の手順のビルド) で bundle を作り、手元で https で配って (自己署名でよい) 確かめる。**family-mruby.github.io への配置・push はしない**。
5. **docs**: browser.md に、https の版からつなぐ手順 (手元のルータ、中継越しのルータ)、設定、ID の決まりを足す。

## 2. 受け入れ条件

1. https で配ったブラウザ版から、手元のルータ (ループバックの例外が効くなら `ws://`、効かなければ中継の `wss://`) につながり、asterism-console の図に出る。
2. 中継の `wss://` 越しでもつながる (自己署名の証明書をブラウザに許した状態でよい)。
3. 設定の画面でルータを変えると保存され、次の起動でそこへつながる。`?zenoh=` の上書きも効く。
4. 2 つのブラウザ (別のプロファイル) が別々の ID で同時に入り、console に ▣ で 2 つ出る。同じブラウザの 2 つ目のタブの振る舞いが docs のとおり。
5. S4 の操縦機 (`rover_pilot`) が https の版から動く (台車の操縦とカメラ)。
6. 既存のブラウザ版の動き (起動、エディタ、保存、設定) が壊れない。

## 3. 範囲・決まり

- **触ってよいもの**:
  - fmruby-core: wasm 版のページ・設定・Zenoh のリンク (`wasm/`)、`lib/add` 配下、doc。ブランチは develop から `feature/asterism-b2`。コミットのみ、push しない。**コミットは必ずファイルを指定する。`git commit -a` は使わない** (`.env` にユーザの手元の変更がある)。
  - 親リポジトリ: compose の重ね合わせを足す。コミットのみ。
  - asterism-console: 端末の印の決まりを足すなら main にコミット・push してよい。
  - 公開済みの gem (picoruby-asterism-zenoh、asterism) は直さない (要るなら差分を書いて返す)。
- **触らないもの**: `.env`、sdkconfig、ESP32 のファーム、fmruby-graphics-audio、family-mruby.github.io (公開の版)。
- **端末**: 要らない (台車の操縦の確かめは MuJoCo の台車と S4 の頭脳を sim で動かしてよい。P4-Nano を使うなら IP は親が渡す)。
- **終わったら**: 起動したものを全部止める。`build/` は Linux の標準構成 (x86-64)。
- **止まる条件**:
  - 設定の画面に、今の流儀に無い新しい形が要る (仕様の選択)。
  - 公開済みの gem の直しが要る。
  - 公開の版へ反映しないと確かめられないことが出た。
- **report**: Japanese plain style (常体)。書くこと: ループバックの例外の調べ、wss の作り、設定と ID の決まり、受け入れ条件の結果、残った課題 (公開の版へ出すときに要ること)。
