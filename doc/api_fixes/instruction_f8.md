# F8 指示書: AtomS3R と StickS3 のビルドからエディタと型まわりを外す

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (決定 23)。この 2 つの端末の作りは doc/headless_s3/
> (plan.md と report/)、ビルドの既定は Rakefile の `MINI_BUILD_DEFAULTS`・`BOARD_BUILD_DEFAULTS`。

## 背景

`rake regress:size ALL=1` (2026-11-25) で、ファームの区画 (5,767,168 バイト) の空きが STICKS3 5,344 バイト、ATOMS3R
20,128 バイトになった。大きいのはエディタの型支援のデータベース (sig 142 クラス。`lib/upstream/family_mruby.gembox`
の `picoruby-ti`。全端末に入る)。この 2 つは単一のアプリを動かす Asterism のノードで、キーボードが無く、エディタの
F1 のヘルプと見本は既に storage から外してある (`config/storage_exclude_atoms3r.txt`・`_sticks3.txt`)。
ユーザ決定: **この 2 つからエディタと型まわりを外す。区画は変えない。**

## やること

1. **今の作りを調べて報告に書く**: この 2 つのビルドで、エディタ (`main/ruby/default_app/editor/`。今は
   `FMRB_APP_ENGINE_EDITOR=mruby`)・エディタの C の部品 (`picoruby-fmrb-editor-core`)・型エンジン (`picoruby-ti`) と
   そのデータベース・構文の色分け・デバッガ (`FMRB::Debug`) が、どこでビルドに入り、誰から呼ばれているか。
   ランチャーやファイルマネージャ、関連付け (`FmrbAssoc` の「編集」)、シェルの `edit` などエディタを開く道の一覧。
   この 2 つの端末でそれらの道が実際に通れるか (デスクトップがあるか、単一アプリの構成で何が起動するか)。
2. **外す**: ビルドの選択 (新しい `FMRB_EDITOR=0` のようなスイッチ。名前は今ある `FMRB_*` の流儀に合わせる) を足し、
   `MINI_BUILD_DEFAULTS` で 0 にする。ほかの端末 (NARYAv3・NARYAv4・TAB5)・sim・ブラウザ版・ネイティブ版は今のまま
   (既定は入れる)。外す物: エディタのアプリ、エディタの C の部品、型エンジンとデータベース、エディタだけが使う物
   (色分け・エディタ用のデバッガの橋など。**エディタ以外も使っている物は外さない**。1 の調べに基づく)。
   - rake (mruby の gembox・build_config) と CMake (`components/picoruby-esp32/CMakeLists.txt` の `PICORUBY_SRCS`・
     `main/CMakeLists.txt`) の両方で同じ選択になること (ずれると GC で落ちる。`MRB_*` の定義をそろえる決まり)。
   - エディタを開く道は、エディタの無いビルドで「開けない」ことが分かる形にする (起動を頼むと、今ある起動失敗の
     エラー (`fmrb_err.h`、`Kernel#spawn_error_reason`) で断る。新しい文が要るなら日本語と英語で足す)。落ちない・
     黙って何も起きない、のどちらでもなく、断ったと分かること。
   - storage の除外の一覧に、エディタだけが使うファイルが残っていれば足す。
   - checkpoint の表 (`ckpt-table-*`。クラスの名前の基準) がこの選択でずれないか確かめる (端末の間でアプリを動かす
     とき、送り手と受け手で表が合う必要がある。doc/checkpoint)。ずれるなら、どう扱うかを報告に書いて止まる。
3. **測る**: `rake regress:size ONLY=sticks3` と `ONLY=atoms3r` (ビルドを含む。約 3 分ずつ) で、外す前と後の
   ファームの大きさ・区画の空き・内蔵 RAM (DIRAM・IRAM) を表にする。ほかの端末が変わっていないことを
   `rake regress:size` (S3 と P4) で確かめる。**基準 (`:update`) は更新しない** (親が行う)。
4. **確かめる**: `rake regress`、`rake clean` → `rake build:linux` → sim でエディタを起動して 1 打鍵 (sim は今のまま
   エディタが入っていること)。この 2 つの端末の実機は無いので、**実機で親・ユーザが確かめる手順** (起動ログで見る行、
   アプリを送って動かす、エディタを開こうとすると何が出るか) を報告に書く。

## 報告 (doc/api_fixes/report/f8.md)

1. 今の作り (1 の調べ)。外した物と残した物の一覧と理由。
2. スイッチの名前と、rake・CMake のどこで見ているか。
3. 大きさの表 (外す前 / 後、2 端末。ほかの端末は差なし)。
4. エディタを開く道の、外した後の動き。
5. 実機で確かめる手順。確かめていないこと。途中で見つけた別の不具合。
6. コミット。

## 範囲と止まる条件

- 触ってよい: Rakefile・`rakelib/`・`lib/upstream/` の gembox と build_config・CMake の該当・起動の道の該当・
  `config/storage_exclude_*.txt`・試験・`doc/api_fixes/report/f8.md`・`doc/README.md`・Rakefile の説明のコメント・
  `doc/headless_s3/plan.md` (状態行と 1〜2 行の追記)。
- 変えない: sdkconfig・区画表・ほかの端末の既定・`sig/`・エディタそのもののコード (条件で外すための最小の変更を除く)。
- **ソースを消さない** (ビルドの選択で外す)。
- 利用者から見た動きが「この 2 つの端末でエディタと型支援が無くなる」以外に変わる直し方が要るときは止まって報告する。
- push しない・マージしない (ブランチ `mini-no-editor` にコミットまで)。ユーザに質問しない。終わったら sim を止める。
- シリアルのポートと実機には触らない。`build/` は `regress:size` が使ってよい (終わったら元のターゲットに戻る作り)。
  コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
