# F7 指示書: アプリごとの「今いる場所」(Dir.chdir を全端末で使えるように)

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md の「F7 の設計」(決定 21)。今の動きの調査は
> report/f4.md の C7 (sim と ESP32 の表、P4-Nano での実測は plan.md の「F4 の結果」)。

## やること

plan.md の「F7 の設計」の 1〜6 をそのとおり実装する。要点:

1. **置き場所**: 今いる場所は、ファイルの HAL の側に、呼んだタスク (アプリ) をキーにして持つ (アプリの文脈
   `fmrb_app_task_context_t` に欄を足すか、HAL の中にアプリの番号で引く表を持つかは、どちらが内蔵 RAM を使わず、
   HAL が上の層 (`main/app`) に寄りかからないかで決める。**HAL は libc と fmrb の下の層だけで閉じる**今の作りを
   壊さない。要るなら「今のタスクの番号を返す」小さな口を下の層に置く)。アプリの終了・kill・異常終了で消えること。
   - 内蔵 RAM に置かない (PSRAM の BSS か、最初の chdir のときに `fmrb_sys_malloc`)。長さの上限は、今の HAL の
     経路の上限 (ESP32 の表は 128 バイト。report/s2c.md) と合わせる。
2. **`fmrb_hal_file_resolve_path`** (アプリのパス → 実際のパス。posix と esp32) で、相対パスに今いる場所を付けて
   から今までの処理に渡す。`.`・`..` をまとめ、ルートより上に出ない。**絶対パスの結果は 1 バイトも変えない**
   (ホストの試験で、今ある経路の解決の期待値が変わらないことを確かめる。`rake -T | grep -i file` や
   `test/` の中の HAL の試験を探す)。読み取り専用の領域・仮想のフォルダ (`fmrb_hal_file_virtual_children`) の
   判定が、まとめた後のパスに対して働くこと。
3. **`fmrb_hal_file_chdir`・`getcwd`**: プロセスの `chdir()`・`getcwd()` を呼ばない。在るフォルダかを HAL の
   stat で確かめて、場所を入れ替える。
4. **Ruby の側**: `Dir.chdir` (ブロックの形を含む。今は RuntimeError になる。report/f4.md C7)・`Dir.getwd`・`Dir.pwd`
   (あれば)・`File.expand_path` (基準を `Dir.getwd` に。2 番目の引数の形があればそのまま)。上流の gem の中の物は
   `lib/upstream/` の差し込みで直す。`File.absolute_path?` など、関係するメソッドの今の動きを表にして、変わる物を
   報告に書く。
5. **sig**: `sig/dir.rbs`・`sig/fmrb_file.rbs` の説明を、直した後の動きに合わせる (今は「ESP32 では今いる場所を
   見ない」「アプリは絶対パスで」の類の注意がある)。

## 確かめる

- ホストの試験 (`test/api_fixes/run.rb` に足す。HAL の posix の実装を組んで: 相対パスの解決、`..`、ルートより上、
   長すぎる、2 つのタスクで別々の場所、アプリの終了で消える、絶対パスが変わらない)。
- sim (シェルの irb): `Dir.getwd` → "/"、`Dir.chdir("/tmp")` → 成功、`Dir.getwd` → "/tmp"、
  `File.open("a.txt","w"){...}` の後 `File.exist?("/tmp/a.txt")` → true、`File.expand_path("a.txt")` → "/tmp/a.txt"、
  `Dir.chdir("/nope")` → 例外、`Dir.chdir("/tmp") { Dir.getwd }` → "/tmp" で、後の `Dir.getwd` は元のまま。
  **chdir の後もほかのアプリ (ファイルマネージャ・エディタ) が絶対パスで普通に動く** (F4 で見つかった sim の不具合が
  消えたこと)。2 つのシェルで別々の場所になること。アプリを閉じて開き直すと "/" に戻ること。
- `rake regress`・`rake sig:coverage` (「3. doc gaps」が 0 のまま)、`rake clean` → `rake build:linux` → エディタの
  起動 + 1 打鍵。`rake regress:screens` の sim の分が通ること (ファイルを開く道が広く変わるので)。
- 内蔵 RAM・PSRAM・VM プールの増分を報告する。ESP32 だけのコードは構文検査まで。直した ESP32 だけのファイルを
  報告に並べる。**親が P4-Nano で流す式** (/tmp だけを使う。1 行 60 文字以内) を報告に書く。

## 報告 (doc/api_fixes/report/f7.md)

置き場所と理由 / 直した関数 / 変わる動きの表 (前 → 後、sim と ESP32) / 同梱のアプリへの影響 (相対パスを使っている所を
grep して一覧に。今は ESP32 でルートからの意味だった物が変わらないこと) / 試験 / メモリ / ESP32 だけのファイル /
親が実機で流す式 / 確かめていないこと / コミット。

## 範囲と止まる条件

- 触ってよい: ファイルの HAL (posix・esp32)、アプリの文脈か下の層の小さな口、Dir・File の Ruby と C
  (`lib/gems/`・`lib/upstream/`)、試験、`sig/dir.rbs`・`sig/fmrb_file.rbs`、`doc/api_fixes/report/f7.md`、`doc/README.md`。
- 変えない: 絶対パスの解決の結果、シェルの `cd`、`components/` の submodule の中、sdkconfig、区画。
- **同梱のアプリ・カーネル・デスクトップ・エディタが相対パスに頼っていて、動きが変わる所があれば、一覧にして止まる。**
- 利用者から見た動きが、plan.md の設計に書いた以外に変わる直し方が要るときは止まって報告する。
- 内蔵 RAM を増やさない。AtomS3R・StickS3 の区画 (空き約 520 KB) に入ること。
- push しない・マージしない (ブランチ `api-fixes-f7`)。ユーザに質問しない。終わったら sim を止め、裏の処理が
  残っていないことを確かめる。シリアルのポートと実機には触らない。ESP32 のビルドはしない。
  コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
