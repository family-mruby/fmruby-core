# PicoRuby最新版の統合

fmruby-core/components/picoruby-esp32/picoruby/
この入れ子になったsubmoduleを以下の最新版に更新することが目的
https://github.com/picoruby/picoruby

## 前提

以下の作業のルートは fmruby-core/ である。

### 補足: 現在の構成 (2026-07時点)

- 現在の picoruby submodule の pin は c14aa4400 (2026-03-21) である。
- VM は picoruby-mruby (mruby VM) 系を使用している。mrubyc 系ではない。
  - gembox: lib/add/family_mruby.gembox
  - build config: lib/add/family_mruby_linux.rb / family_mruby_esp32.rb (picoruby/build_config/ にコピーされる)
- submodule は複数段の入れ子になっており、パッチも複数の階層に及ぶ。
  - components/picoruby-esp32/picoruby (picoruby 本体。fmruby-core の submodule)
  - その内部の submodule: mrbgems/mruby-compiler2 (とその中の lib/prism)、mrbgems/picoruby-mruby/lib/mruby、mrbgems/mruby-bin-mrbc2 など
- パッチのコピーは Rakefile の `rake setup` タスクが行う。build:linux / build:esp32 の依存タスクなので、ビルドすれば自動で実行される。
- lib/ 以下は3種類あり、性質が異なる。
  - lib/add/ : 新規 mrbgem や設定ファイルの「追加」。upstream のファイルと直接衝突はしないが、依存している picoruby 側 API (Machine, Sandbox 等) の変更の影響は受ける。
  - lib/replace/ : ディレクトリ丸ごとの「置換」。現状 picoruby-machine のみ。upstream 側の picoruby-machine の変更が一切反映されないため、今回のマージで最も注意が必要な箇所。
  - lib/patch/ : 既存ファイル単位の「上書き」。約35ファイル。マージ作業の主対象。
- lib/patch/ は picoruby 直下だけでなく、入れ子 submodule の中身も上書きしている点に注意。
  - lib/patch/compiler/ は mrbgems/mruby-compiler2/ の中 (submodule 内) を上書きする。
  - lib/patch/picoruby-mruby/lib/mruby/ は picoruby-mruby/lib/mruby の中 (submodule 内) を上書きする (vm.c、mruby-task 等)。
  - これらは picoruby 直下で git diff しても submodule dirty としか表示されないため、各 submodule に cd して個別に git diff する必要がある。
- lib/patch/esp_littlefs は picoruby と無関係 (components/esp_littlefs 向け) なので今回のマージ対象外。

## 課題

以下の点でマージ作業は注意が必要。

- 私たちが実装した大量のパッチが存在している
- gitsubmoudleの複数段の入れ子構造
- 最新版ではmrubyのバージョンが上がり、バイトコードの互換性はなくなっている
- 本流の変化も大きいため、パッチを単に当てるだけでは済まないと推測される

## 進め方

### 差分の理解

現在我々は、submoduleに対するパッチをlib/ 以下に配置して、ビルド時にRakefileの処理でコピーして上書きする構造をとっている。
diff形式ではないため、実際の差分をまず理解する必要がある。
コピーした状態で、fmruby-core/components/picoruby-esp32/picoruby/ 以下のフォルダに移動してgit diffすれば、実際の差分が把握できるはず。
diffを作業フォルダにまとめて、マージ作業のタスクリストを作成して、以後、そのタスクリストを更新しながら作業する。

#### 手順の補足

1. `rake setup` を実行して lib/ の内容をコピーした状態にする。
2. components/picoruby-esp32/picoruby/ に cd する。
3. `git status --short` で全体を確認する。lib/add/ 由来は untracked (??)、lib/patch/ と lib/replace/ 由来は M として見える。
4. `git diff` で既存ファイルへの差分を取得する。さらに入れ子 submodule (mruby-compiler2、picoruby-mruby/lib/mruby) にも個別に cd して同様に diff を取る。
5. 正 (source of truth) はあくまで lib/ 以下と Rakefile の setup タスクであり、コピー先の diff はその結果にすぎない。マージ結果も必ず lib/ 側のファイルに反映する。

タスクリストは1パッチ (または1領域) 1項目とし、diffそのものに加えて「なぜこのパッチが必要か (意図)」を必ず書く。
意図を知らずに構文的にマージすると壊れるパッチ (vm.cのタスクスイッチ修正、estallocのマルチVM対応など) があるため。
意図の多くは doc/ 以下の既存の作業記録から復元できる。

### マージ作業

https://github.com/picoruby/picoruby の master HEAD に更新する。

手作業でdiffを当て直す方式ではなく、gitの3-way mergeを利用する。
lib/patch はdiffではなくファイル丸ごとコピーのため、手作業の2-way比較では「upstream側も同じファイルを変更していた」ことを見落とし、upstreamの修正を古いパッチで潰す事故が起きやすい。
gitにrebaseさせれば、双方が変更したファイルだけがconflictとして浮かび上がり、人間の判断が必要な箇所に集中できる。

手順:

1. submodule内 (components/picoruby-esp32/picoruby/) で、旧pin (c14aa4400) から作業ブランチを作成する。
2. `rake setup` でコピーされたパッチ一式を、領域ごとに分けてコミットする (compiler系 / sandbox / socket など)。conflict解決の単位になるため、まとめず細かく分ける。
3. upstream の master HEAD をfetchし、作業ブランチをその上にrebaseする。
4. conflictが出た箇所だけ、タスクリストの「パッチの意図」を参照しながら解決する。1件解決するごとにソースコードをチェックして、期待した処理が保たれているか確認する。
5. 入れ子submodule (picoruby-mruby/lib/mruby、mruby-compiler2) にもパッチがあるため、picoruby本体の新HEADが指す各入れ子submoduleの新しいpinを確認し、その階層ごとに同じ手順 (ブランチ作成、パッチをコミット、新pinへrebase) を繰り返す。
6. 解決結果を lib/ 以下にファイルの形式で書き戻し、既存のビルドフローが保たれるようにする。submoduleの作業ツリーは最終的にクリーンな新pinの状態に戻す (作業ブランチはローカルに残してよいが、pushしない)。

lib/replace/picoruby-machine は丸ごと置換のためrebaseに乗らない。これは「マージ」ではなく「再導出」として別枠で扱う。
新しいupstreamのpicoruby-machineと現在のreplace版のdiffを取り、我々の変更点を新版の上に作り直す。

単なるマージでは済まない場所は、必要な修正を入れる。
どうして修正が必要だったかは、doc/以下に作業記録として残す。

判断に困ったときは、私に聞いてほしい。

#### 注意事項の補足

- 上記手順に伴う submodule 内でのローカルな git 操作 (branch / commit / rebase / fetch / checkout) は、本作業の一部として許可する。ただし push、および fmruby-core 側の commit (submodule pointer 更新を含む) は勝手に行わず、依頼者に確認する (fmruby-core/CLAUDE.md の方針)。
- picoruby 本体の更新に伴い、その内部の入れ子 submodule (mruby、mruby-compiler2、prism 等) の pin も upstream が指す commit に合わせて更新する必要がある。
- mruby のバージョンアップに伴い、コンパイラ (mruby-compiler2 / mrbc2) と VM が同じバイトコードバージョンで揃っている必要がある。lib/patch/compiler/ の prism アロケータ関連パッチが新しい mruby-compiler2 にそのまま適用できるかは要確認。
- lib/ 以下を編集したら、ビルド前に `rake clean` を実行する (setup のコピーだけでは古いビルド成果物が残る)。Linux と ESP32 でターゲットを切り替えるときは `rake clean_all` を実行する。

### 動作確認

ESP32、Linux双方のビルドが通るところまでは確認してほしい。

- ビルドコマンドは `rake build:linux` / `rake build:esp32` (docker コンテナ内で実行される)。
- プログラムの実行には GUI が必要なため、ビルド確認までとする。

ただし、VM内部に手を入れるパッチは「ビルドが通る」ことが正しさの保証にならない。
高リスクパッチ (picoruby-mruby/lib/mruby/src/vm.c、alloc.c / estalloc、sandbox、picoruby-machine置換) はタスクリスト上で明示しておき、実機確認の重点項目として引き継ぐこと。

実機動作確認は私がやる。

## 確認済み事項

- 「最新」の定義: picoruby/picoruby の master HEAD に合わせる。
- 差分とタスクリストをまとめる「作業フォルダ」: doc/work_picoruby_merge/ を使用し、git 管理する。

