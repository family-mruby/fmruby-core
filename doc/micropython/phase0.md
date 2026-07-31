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

## 未確定事項 (実装時に確定して追記)

- embed port の生成に自作 C モジュール (phase3 で書く gfx/app モジュール) の
  qstr を含める正確な手順。USER_C_MODULES 相当の仕組みが embed port にあるか、
  なければソースを生成対象リストに足す形か。phase3 で必要になるので、
  ここで仕組みだけ見極めておく。
- 選定した MicroPython のタグ。

## 完了条件

- `rake micropython:gen` がクリーンな checkout から再現可能に走る。
- mp_embed/ がコミットされ、`git status` がクリーンな状態で
  `rake micropython:smoke` (ホスト単体) が hello を出力する。
- fmruby-core のビルド (rake build:linux) がこの時点では従来と変わらず通る
  (コンポーネント未登録なので影響ゼロのはず)。
