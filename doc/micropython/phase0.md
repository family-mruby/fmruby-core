# Phase 0: submodule 追加と embed port 生成の道具立て

## 目的

MicroPython 本体を取得し、組み込み用ソース一式 (micropython_embed ツリー) を
再現可能な手順で生成できるようにする。IDF やアプリ本体にはまだ触れない。

## 前提

- ホストに make と python3 があること (生成時のみ使用。日常ビルドでは不要)。
- git 操作 (submodule 追加、生成物のコミット) はユーザに確認してから行う。

## 作業項目

1. **submodule 追加**: components/micropython/micropython として
   micropython/micropython を追加する。実装時点の最新安定リリースタグに
   固定し、選んだタグをこのファイルに追記する。URL は他の submodule と
   同様 HTTPS (installer の CI が SSH を扱えないため)。

2. **port ディレクトリ作成**: components/micropython/port/ に、embed port を
   駆動する自前のファイルを置く。submodule 本体は編集しない。
   - Makefile: 本体側 ports/embed の提供する micropython_embed.mk を include
     し、生成先を components/micropython/mp_embed/ に向ける。
     書き方は本体の examples/embedding にある Makefile と micropython_embed.mk
     の中身を読んで確定する。
   - mpconfigport.h: MicroPython のビルド設定。初期値は次の方針で書く
     (詳細調整は phase1):
     - ROM レベルは CORE 相当 (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)
     - 浮動小数点は float (MICROPY_FLOAT_IMPL_FLOAT)
     - MICROPY_NLR_SETJMP を有効 (linux/esp32 共通挙動のため)
     - コンパイラ有効 (ソース実行するので必須)、REPL 関連は無効
     - スレッド無効
     - VM 停止用: MICROPY_ENABLE_VM_ABORT と MICROPY_VM_HOOK_LOOP 系を有効
       (実際に使う組み合わせは phase2 で確定)

3. **rake タスク追加**: fmruby-core/Rakefile に namespace :micropython を作り、
   - `rake micropython:gen` — port/Makefile を実行して mp_embed/ を再生成
   - `rake micropython:clean` — 生成中間物の削除
   を追加する。既存の namespace :basic の書き方に合わせる。

4. **生成物のコミット**: 生成された mp_embed/ ツリー (C ソースと genhdr/ の
   生成ヘッダ) をコミット対象にする。これで日常ビルドから make/python3 への
   依存が消える。.gitignore に生成の中間ディレクトリ (build 等) を足す。

5. **ホスト単体スモーク**: 本体の examples/embedding 相当の最小 main.c を
   components/micropython/port/test/ に置き、ホストの cc で mp_embed ツリーと
   一緒にコンパイルして `print('hello')` が動くことを確認する
   (IDF 統合前に生成物の完全性を確かめるのが目的)。確認用のコンパイル手順は
   rake タスク (例: micropython:smoke) にする。

## 完了条件

- `rake micropython:gen` がクリーンな checkout から再現可能に走る。
- mp_embed/ がコミットされ、`git status` がクリーンな状態で
  `rake micropython:smoke` (ホスト単体) が hello を出力する。
- fmruby-core のビルド (rake build:linux) がこの時点では従来と変わらず通る
  (コンポーネント未登録なので影響ゼロのはず)。

## 確定事項 (実装で確定した内容)

### 選定タグ

MicroPython **v1.28.0** (commit e0e9fbb17ed6fd06bb76e266ae554784c9c80804)。
実装時点の最新安定リリース。v1.29.0-preview は開発版なので採らない。
submodule は `components/micropython/micropython`、URL は HTTPS。
MicroPython 自体の submodule (lib/ 以下) は embed port の生成に不要なので
初期化しない。

### 自作 C モジュールの qstr を生成に含める手順

`ports/embed/embed.mk` は `py/py.mk` を include しており、そこに
**USER_C_MODULES の仕組みがそのまま生きている**。`$(USER_C_MODULES)/*/micropython.mk`
を走査して `SRC_USERMOD_C` を集め、それを `SRC_QSTR` に足すので、qstr と
`MP_REGISTER_MODULE` の登録は生成ヘッダ (qstrdefs.generated.h / moduledefs.h) に
入る。生成物パッケージ側にはモジュールの .c はコピーされない (コピー対象は
`$(TOP)/py/*.[ch]` 等に固定) が、モジュールの .c は components/micropython/modules/
に置いたまま CMakeLists.txt の SRCS で普通にコンパイルすればよいので問題ない。

これに合わせて port/Makefile では `USER_C_MODULES = $(abspath ..)` (=
components/micropython) を指定済み。phase3 では
`components/micropython/modules/micropython.mk` を作り、そこで
`SRC_USERMOD_C += $(USERMOD_DIR)/fmrb_mod_*.c` と、fmrb 側ヘッダの include パスを
`CFLAGS_USERMOD` に足す形になる (qstr 抽出は各ソースを cpp に通すので、
include パスが通っていないと生成が失敗する)。phase0 時点ではモジュールが
無いため走査結果は空で、生成に影響しない。

### 生成物の内容

`rake micropython:gen` の出力 (components/micropython/mp_embed/) には
**extmod/ の実装 (.c) が一切含まれない**。extmod にある標準モジュール
(time, json, os, re, random 等) はそのままでは使えない。この制約は README の
「受け入れる制約」にも記載した。

実装中の気づき・実測値・phase1 以降への申し送りは
[report/phase0.md](report/phase0.md) を参照。
