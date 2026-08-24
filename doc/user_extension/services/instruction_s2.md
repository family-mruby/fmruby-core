# 実装指示書 S2: サービス基盤の仕上げ (自動再 spawn / Monitor / enable・disable)

対象: 実装担当セッション。作業リポジトリ: fmruby-core のみ。
先に読むもの: plan.md、instruction_s1.md (進め方の約束はそのまま)、
report/s1.md (実装の現状)。**S1 追補 (taskbar の窓一覧化 / focusable? /
ps 表記統一) が先に入っている前提** — 未了ならそちらを先に。
報告は report/s2.md。

## 範囲

0. **T0 Modern 限定化** — plan.md「対象機種」のとおり。kernel のブート
   spawn を FMRB_HW_MODERN で囲い、Retro の firmware からホストと
   サンプル (システム/ユーザとも) を外す。検収: Retro 向け sim
   (FMRB_HW_TARGET=NARYAv3) でホストが上がらず、shell の ps / svc が
   従来の「ホスト不在」挙動になる。S3 の bin/storage が増えていないこと。
   以降の T1-T3 の検収は **Modern 向け sim のみ**でよい。
1. **T1 自動再 spawn** — サービスホスト自身 (kernel reaper 連携) と、
   `app =` 項目の `restart = true`
2. **T2 enable / disable** — 実行時の適用 + 再起動をまたぐ永続化
3. **T3 Monitor の Tasks ページ統合** — サービスの表示と stop / start

## T1: 自動再 spawn

### 意味論 (ここが本体)

- **頼まれて止まったものは再 spawn しない**。ユーザの kill / stop・svc stop・
  正常終了は「意図した停止」であり、勝手に生き返ると kill が無意味になる。
  再 spawn するのは**異常死だけ** (例外での死、fault、Reaped に至る想定外
  の経路)。
- kernel の kill 経路に「意図した停止」の印を立て、reaper が読める形に
  する (app context に expected_stop フラグ 1 bit。kill 要求・stop コマンド・
  アプリ自身の stop で立てる)。**この区別が付けられない実装になりそうなら
  一度止めて相談** (ここを曖昧にすると kill の意味が壊れる)。
- **暴走ループの護り**: 再 spawn は 2 秒待ってから。**300 秒の窓で 3 回**
  死んだら諦めてエラーログ 1 行 (「services host crashed 3 times,
  giving up」)。カウンタは窓の外で自然にリセット。

### 誰が誰を

- **ホスト自身**: kernel reaper が異常死を検出したら再 spawn する
  (ホストが死ぬと他の誰にも直せないので kernel の仕事)。kernel Ruby は
  dual-safe で書き、doctor clean を維持。
- **`app =` + `restart = true` のアプリ**: ホストの仕事にする。そのために
  kernel (reaper) が **`app/died` を Pub/Sub に publish** する
  (`{"pid"=>, "name"=>, "expected"=>true/false}`)。これは**システム topic の
  第 1 号** (ideas.md 案 6 の実例) なので、名前と payload を plan.md の
  契約として書き残す。ホストは expected=false かつ restart=true の項目
  だけ再 spawn (同じ 2 秒 / 300 秒 3 回の護り)。

### 検収 (sim)

- ホストを**異常死**させる (検査用に「svc/ctl に crash コマンドがあるとき
  だけ raise する」ような仕掛けは入れない — 代わりに一時サービスで
  ホストの rescue の外に届く壊し方を工夫し、やり方を report に書く。
  どうしても作為が要るなら一時的な検査ビルドで) → 2 秒後に再 spawn、
  サービスが再稼働、ログに 1 行。
- ホストを **kill (意図した停止)** → 再 spawn されない。
- 3 回連続の異常死 → 諦めのログ、以後静か。
- `restart = true` のアプリを kill → 生き返らない。クラッシュ (raise する
  一時アプリ) → 生き返る。
- `app/died` が publish され、一時アプリで購読できる。

## T2: enable / disable (永続)

- **ユーザの toml は書き換えない**。手書きのコメントや並びを壊さないため、
  永続化は**ホスト所有の状態ファイル** `/home/services_state.toml`
  (`名前 = true/false` だけの平文) に書く。読み込みの重ねは
  **システム toml → ユーザ toml → 状態ファイル**の 3 層 (状態ファイルが
  最優先)。plan.md の二層の節をこの 3 層に更新する。
- `svc enable <名前>` / `svc disable <名前>` (shell) → `svc/ctl` の
  `enable` / `disable`。ホストは**実行時に適用** (disable = stop と同じ +
  記録、enable = start と同じ + 記録) **し、状態ファイルへ書く**。
- `svc stop` / `svc start` は従来どおり**実行時だけ** (再起動で元に戻る)。
  stop/start と enable/disable の違いを help と report に明記。
- 検収 (sim): disable → 再起動 (ホスト kill → 再 spawn でも可) しても
  上がってこない / enable で戻る / ユーザ toml のバイト列が不変 (diff) /
  状態ファイルに知らない名前が残っていても無害 (ログ 1 行で無視)。

## T3: Monitor の Tasks ページ

- Tasks ページに、ホストの行の下へサービスの子行を出す: 名前・状態
  (running / stopped / failed。S1 追補で統一した語彙)・err 数。
- 行の操作は **1 ボタン**: running なら [stop]、stopped / failed なら
  [start] (FmrbUI の既存部品で。kill ボタン列 KILL_IDS の作りが参考)。
  enable/disable は Monitor には持ち込まない (shell の仕事)。
- データは `svc/ctl list` (1 サービス 1 通)。ページを開いたときに要求し、
  応答が届くたびに行を足す。ホスト不在なら子行なし (エラー表示もしない)。
- 検収 (sim): 子行の表示 / [stop] で止まり表示が変わる / [start] で戻る /
  ホスト不在で Tasks ページが従来どおり / FmrbUI の契約 (毎フレーム
  描かない・handle の case) を守っていることを host テストか目視で。

## 進め方

- **関門 1**: T1 完了 + sim 検収 (意味論の表す全ケース) で report を見せて
  止まる。expected_stop の設計で迷ったらその前でも止まる。
- **関門 2**: T2 + T3 完了時。Tab5 で「disable が再起動をまたいで効く」
  「Monitor の子行と stop/start」の 2 点だけ実機確認 (書き込み前の
  fmrb_rd_ps 単独確認を忘れない)。
- コミット 3 本 (英語、ユーザ確認のうえ): (1) T0 + kernel (expected_stop +
  reaper 再 spawn + app/died)、(2) ホスト (restart / 3 層 / enable・disable)
  + shell、(3) Monitor + docs (本書と plan.md の更新)。
- 2 構成 (標準 / 全 mruby) + doctor clean + `rake test` (services テストに
  3 層の併合と enable/disable の分を足す)。`.env` 復元。

## やらないこと

- サービス単位の自動再起動 (failed からの自動復帰はしない。手で svc start)
- 状態ファイルの GUI 編集、Monitor からの enable/disable
- `app/died` 以外のシステム topic の整備 (案 6 の本体は別計画)
