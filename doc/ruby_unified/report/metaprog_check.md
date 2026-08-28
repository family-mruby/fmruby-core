# アプリ VM のメタプログラミング可否検証 (Ruby Unified 事前確認)

実施: 2026-08-27。対象: Linux sim (FMRB_HW_TARGET=TAB5、426x240)。
結果: **全 13 項目 pass**。

## 目的

Ruby Unified の端末側クライアントは「ユーザ向け層は mruby (アプリ VM)、
下回りは C」の方針とした (高頻度のメッセージ交換はしない前提。遅延の下限は
_spin のポーリング周期 ~50ms で決まり、C を速くしてもそこは縮まないが、
テレメトリ・コマンド・イベント用途には十分)。

この方針が成立するには、dRuby 型の動的代理オブジェクトに必要な
メタプログラミング機能がアプリ VM (picoruby コンパイラ + mruby VM) で
動く必要がある。picoruby には「mruby だから動くはず」が通用しない穴が
実在する (defined? 無し、bare 定数解決、Regexp 無し等) ため、実測で確認した。

## 前提の確認

mruby-metaprog は 3 ターゲットすべてのビルド構成に入っている:

- lib/add/family_mruby_linux.rb
- lib/add/family_mruby_esp32.rb
- lib/add/family_mruby_esp32p4.rb

いずれも `conf.gem gemdir: "#{dir}/mruby-metaprog"`。

## 方法

テストアプリ 2 本を flash/app/test/ に置き、debugd の spawn で起動して
ログ (プレフィックス MMCHK / MMKW) で判定した。

- **mm_check.app.rb**: 基本 11 項目。保守的な構文のみ。
- **mm_kw.app.rb**: コンパイルが落ちる可能性のある構文 (`**kw` 明示
  シグネチャ、キーワード引数つき def) を分離。片方の構文エラーが
  他方の結果を隠さないため。

```
tools/dev_run_check.sh --keep
python3 tool/debug/fmrb_dbg_client.py --json localhost spawn path=/app/test/mm_check.app.rb
python3 tool/debug/fmrb_dbg_client.py --json localhost spawn path=/app/test/mm_kw.app.rb
docker logs fmruby_core 2>&1 | grep "MMCHK\|MMKW"
```

## 結果

| 項目 | 内容 | 結果 |
|---|---|---|
| plain | `p.battery` が method_missing に届く | OK |
| args | 位置引数がそのまま届く | OK |
| kwhash | `p.move_to(x: 1, y: 2)` は `*args` 受けだと末尾 Hash (Symbol キー) で届く | OK |
| block | ブロックを保存し、後からローカル変数を捕捉したまま呼べる | OK |
| rtm true/false | `respond_to?` が respond_to_missing? を見る (両方向) | OK |
| send | `p.send(:battery)` も method_missing に届く | OK |
| define_method | クラスに実行時にメソッドを生やせる。**public で呼べる** (`send` 迂回不要) | OK |
| define_method args | 引数つきブロックの define_method | OK |
| methods | `p.methods` が Array を返し自前メソッドを含む | OK |
| ivar_get | `instance_variable_get` が使える | OK |
| mm **kw | `def method_missing(name, *args, **kw, &blk)` がコンパイル・実行とも通る | OK |
| def kwargs | `def move_to(x:, y: 5)` (必須 + 既定値つきキーワード引数) | OK |

エラーログ (`^E (`) は 0 行。

## 設計への含意

- **dRuby 型の動的代理はアプリ VM 上でそのまま作れる**。method_missing 方式と
  $meta からの define_method スタブ生成方式のどちらも選べる。
- `robot.on(:collision) { ... }` のような「保存して後から呼ぶブロック +
  ローカル捕捉」も問題ない (Spinel では壊れる形。この API は mruby
  エンジン専用と明記すること)。
- キーワード引数は代理の透過転送 (`**kw`) が書けるので、契約 (呼び出し規約)
  に素直に含められる。
- instance_variable_get 等は使える状態にあるが、遠隔公開はしない方針
  (discussion summary 11 節) に変わりなし。

## 残る論点 (この検証の範囲外)

- CRuby と picoruby の型の合わせ目: Float の精度、Symbol/String の往復、
  Binary 表現など。値プロトコル (msgpack) の契約設計で決める。
- 実機 (S3/P4) での再確認。gembox は同一なので低リスクだが、
  zenoh-pico 導入 (doc/ruby_unified/zenoh_idea.md 段階 2) の際に併せて見る。

## 成果物

- flash/app/test/mm_check.app.rb / mm_check.app.toml
- flash/app/test/mm_kw.app.rb / mm_kw.app.toml

いずれも launcher_visible = false。再実行は上記の spawn 手順で。
