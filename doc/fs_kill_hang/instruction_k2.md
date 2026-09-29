# 作業指示書 K2: Spinel の File と Lua の io を HAL 経由に

対象: 実装担当のサブエージェント。前提: plan.md (「残り」)、report/k1.md
(特に 3 章の修正、8 章の残り)。report は `report/k2.md` へ。

## ゴール

Spinel の File と Lua の io のファイル操作が、すべて file HAL (錠あり) を通る。
これにより、K1 の守り (強制 kill の前にファイルの錠を取る) が、この 2 つにも効く。

## T1: Spinel の File (先に)

- Spinel の実行時ライブラリ (components/fmrb_spinel_rt/spinel_rt/sp_io.c、
  フォーク kishima/spinel fmrb-ext) の File / Dir の操作のうち、入出力の差し替え口
  (sp_io の backend、fmrb_spinel_host.c の hal_open 群) を通らずに VFS を直接呼んでいる
  ものを洗い出す。
- fmrb 側の差し替え口で足りるなら、fmrb 側 (fmrb_spinel_host.c など) だけで直す。
  フォークの変更が要るなら、clone `/home/kishima/fmrb/wt/spinel-rebase` の `fmrb-ext` に
  コミットし (push はしない)、`import_from_fork.rb` でスナップショットを取り直す
  (SPINEL_PIN の更新は親の判断)。フォークの変更が大きくなるなら、止まって案を返す。
- 生成プログラム (kernel / editor / desktop / gem) の File の使い方 (読み・書き・追記・
  一覧・stat・削除) を全部通ることを確かめる。

## T2: Lua の io

- Lua の本体 (submodule) は編集しない。組み込み側 (fmrb_lua、lib/add 以下など、
  Lua を組み込んでいる所) で `io` を差し替え、ファイルの open / read / write / close /
  lines / seek などが file HAL を通るようにする。標準の `io` と同じ使い勝手 (既存の
  Lua のアプリとサンプルが動くこと) を保つ。
- `os.remove` / `os.rename` など、ファイルに触る `os` の関数も同じく HAL 経由にする。
- 標準入出力 (`io.write` の既定の出力など) の扱いは、今の動きを変えない。

## 検証

- sim (標準構成と互換構成): 各言語 (Spinel のアプリ = エディタの保存など、Lua の
  アプリ) で、大きな 1 回の書き込み (猶予 1 秒を超える大きさ) の途中で kill し、
  その後のファイル操作が止まらないこと。直す前に止まることも確かめる (再現できる
  なら)。止まったら gdb で確かめる。書き込み先は sim のファイルなので繰り返してよい。
- 既存の Lua のアプリ・サンプルと、エディタの保存・開き直しが従来どおり動くこと。
- P4-Nano: 1-2 回の確認。ファイルとして書く量は数 MB 以内 (ファームの焼き直しは数えない)。
- ビルド: TAB5 / NARYAv4 / S3 / Linux (標準・互換) / wasm。静的な D/IRAM が develop を超えない。
  `rake test` も通す。

## 作業の決まり

- 作業ブランチ `feature/fs-kill-hang-k2` (親が develop から切る)。自分が変えた
  ファイルだけをパスで指定してコミット。push とマージはしない。
- .env は書き換えずコミットしない。最後に `git diff .env` が scratchpad の
  env_before_k2.diff と一致することを確かめる。
- 実機は /dev に無ければ rake attach を自分で。flash / serial / tab5_* は許可済み。目視は不要。
- sdkconfig、graphics-audio、submodule の直接編集は禁止。
- report は日本語の常体。コードのコメントと commit メッセージは英語。

## 受け入れ条件

- Spinel の File と Lua の io のファイル操作が、すべて file HAL を通る (洗い出しの表つき)
- 大きな 1 回の書き込みの途中の kill の後も、ファイル操作が止まらない (sim と P4)
- 既存のアプリの動きが変わらない
- 内蔵 RAM が増えていない
