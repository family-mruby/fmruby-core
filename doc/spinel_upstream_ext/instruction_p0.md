# 実装指示書 P0: 最新上流で前提を取り直す

対象: 実装担当のサブエージェント。前提: plan.md (特に「フォークのパッチと
上流の対応表」「上流の ext 機構」「検証の記録」)。
report は `report/p0.md` へ。**この段階はコードを変えない調査**。

## 背景

plan.md の対応表と実測は上流 `1a507623` (2026-09-05) 時点のもの。上流は
2026-09-26 までに 1,270 コミット進み `01521b1e` になった。P1 以降の指示書は
P0 の結果で書くので、古い前提を最新の上流で確かめ直す。

## 作業場所と触ってよい範囲

- 上流の作業ツリー `~/dev/spinel` (origin = matz/spinel)。`git fetch` して
  **調査開始時の origin/master を 1 つ決めて固定**し、report に commit を書く。
  master は 1,270 コミット遅れで、ローカル変更は無い。detached で checkout してよい。
- フォークは `fmruby-core/vendor/spinel` (branch `fmrb-dev`、`622750c`) を
  **読むだけ**。書き込み・branch 作成・push をしない。試し rebase は
  scratchpad などに clone した使い捨てのコピーで行う。
- fmruby-core / graphics-audio の作業ツリー、build/、sim・web・実機は**触らない**。
- `tmp/spinel` (旧開発場所) は触らない。
- git の push、上流への PR・Issue は**しない**。

## 調べること

1. **対応表の取り直し**: plan.md の表の全行について、最新上流での状況を
   判定し直す (置換可 / 不要になる / 維持 / 落とせる / 要確認)。とくに次を実測する。
   - 分類 3 の `#define` 群 (`SP_EXC_STACK_MAX` `SP_CATCH_STACK_MAX`
     `SP_GC_MARK_STACK_MAX`) が `#ifndef` になったか。スタックバッファの数
   - 分類 4: `-m32` で `lib/` 全ファイルがコンパイルできるか (`sp_time.c` の
     `__int128`、bigint の境界)
   - 分類 5: 無条件 include のヘッダ一覧 (`sys/mman.h` `sys/ioctl.h`
     `sys/wait.h` `sys/resource.h` `malloc.h` `fnmatch.h` `sys/file.h` ほか)。
     調査時から増えた/減ったものを明記
   - 分類 6: フォークが足したテスト 6 本を最新上流バイナリで再度通す
   - 上流に「プロセス内の複数ランタイム」「アロケータや I/O の差し替え口」に
     あたる変更が入っていないか (`SP_THREADS` まわりの状態の置き場所の変化も)
2. **ext 機構の取り直し**: plan.md「検証の記録」の手順を最新上流で再実行する
   (ext 出力・ホストからの呼び出し・raise の越境・NUL を含む String・
   init 2 回・2 本同時リンクの multiple definition 数・`-m32` の k.c)。
   オプション名や生成ヘッダの形が変わっていたら全部書く。
3. **fmrb の gem / カーネルが最新上流でどこまで通るか** (生成だけ):
   `lib/add/picoruby-fmrb-raycast` 等の gem 型 Ruby と、kernel / desktop /
   editor の Spinel ソースを最新上流の `bin/spinel` に `-c` で通して、C 生成が
   通るか、`spinel-doctor` 相当の警告が出るかを見る。生成物は scratchpad に置く。
   フォーク専用オプション (`--no-main` `--entry` `--persistent-statics`) は
   上流に無いので `--ext-init` に読み替えて試す。
4. **試し rebase の衝突量**: 使い捨て clone で `fmrb-dev` の 48 コミットを
   最新上流へ rebase してみて、コミットごとに「そのまま載る / 衝突 (ファイルと
   規模) / 上流で不要」を表にする。衝突の解消はしなくてよい (見積もりが目的)。
   分類 6 のコミットは落とす前提で数える。
5. **plan.md の未確定事項**のうち、ホストだけで確かめられるものを確かめる
   (`53941f6` `ca0709c` `622750c` が上流で別の形で直っているか、など)。
   ESP-IDF newlib でのコンパイルは docker が要るので、この段階では
   「どのヘッダがどの判定で落ちるか」の机上整理まででよい。

## 止まる条件

次のときは作業を止め、そこまでの結果を report に書いて返す。

- 上流のビルド (`make deps && make`) がホストの依存不足で通らず、sudo や
  パッケージの導入が要る
- 上流が ext 機構を撤去・大改造していて、plan.md の B 案の前提が崩れている
  (代わりの機構があれば、それを調べたところで止める)
- 触ってよい範囲の外を変える必要があると分かったとき

## report/p0.md に書くこと

- 固定した上流 commit と日時、使ったツールチェーン (`cc --version`)
- 取り直した対応表 (plan.md と同じ列 + 「調査時からの変化」列)
- ext 機構の実測結果 (変化点を先に)
- fmrb の Spinel ソースを通した結果 (ファイルごとに 通過 / 失敗と原因の要約)
- 試し rebase の表と、衝突の総量の見積もり
- 見立てと違った点・撤回した仮説
- P1 (上流バイナリでの gem 試作) と P2 (rebase) の指示書に入れるべきこと

## 受け入れ条件

- 対応表の全行に最新上流での判定が付いている (「要確認」が残る行は理由つき)
- 試し rebase の表が 48 コミット全部を覆っている
- 手順が report だけで再現できる (コマンドを残す)
