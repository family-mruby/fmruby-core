# T2 指示書: プロトコルヘッダの一本化 (と launcher の画面の基準)

> 親: plan.md の T2 と「決めたこと」。材料は report/t0.md の T2 の節。

## やること

1. 共有する線の形のヘッダ 5 本 (report/t0.md の T2 の表) の正を fmruby-core に置き、内容を揃える (差は t0 の表のとおり。
   `MAX_TEXT_LEN` は 128)。
2. fmruby-graphics-audio は `../fmruby-core` を読み取り専用で参照し、CMake で直接 include する。graphics-audio の docker
   (compose・ビルドのコンテナ) に `../fmruby-core` を読み取り専用で mount する。graphics-audio 側の写しは消す。
3. sdl2-display (`fmruby-graphics-audio/sdl2-display/`) は docker の `additional_contexts` で core を足し、写しを消す。
4. `components/fmrb_msg/fmrb_hid_event.h` (同じ名前の別物) の名前を変える。
5. 足し忘れ・ずれの検査: rake (両リポジトリのどちらか、または両方) で、共有のヘッダを読んでいるか・古い写しが残っていないかを
   確かめる。fmruby-core の CLAUDE.md・直下の CLAUDE.md の「入力は 3 ホップ … ヘッダ 2 つとこの switch の 3 箇所」の記述を、
   ヘッダ 1 つ + switch に直す。graphics-audio の CLAUDE.md も合わせる。
6. launcher の画面の基準 (report/t1.md の判断の件): `regress:screens` が launcher の場面の前に、ランチャーのキャッシュ
   (`flash/var/cache/launcher_index` など) を決まった中身に作り直すようにする (手元のキャッシュの古さに左右されない)。
   基準の更新は親がするので、更新が要る場面を report に書いて止める (Avatar が出るのは正しい変化)。

## 確かめること

- 両リポジトリの `rake build:linux`、`docker compose build sdl2-display` (古いイメージを sim_up が止めるので)。
- `rake regress`、`rake regress:screens`、`rake regress:run`、`rake regress:live` (入力・音が通ること)、sim を起こしてキーと
  マウスが届くこと。
- esp32 (S3・P4): `rake regress:size` (内蔵 RAM の静的な増分 0。`MAX_TEXT_LEN` を 128 に揃えた graphics-audio 側の増減も)、
  graphics-audio の esp32 のビルドが通ること (WROVER・P4 の構成)。

## 触ってよい範囲

- fmruby-core (develop)、fmruby-graphics-audio (今の作業の枝。branch を確かめ、main/develop のどちらかを report に書く)。
- 作業ツリーの既存の未コミット (core の components/ サブモジュール 2 つ、wasm/vendor/、doc/wasm_apps/) には触らない。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。
- 終わったら両リポジトリを Linux のビルドに戻し、sim は片付ける。

## 止まる条件

- 仕様が変わる選択、内蔵 RAM の増分、graphics-audio の esp32 のビルドに sdkconfig の変更が要るとき、ユーザの作業が要るとき。

## report (report/t2.md)

共有にしたヘッダと差の揃え方、参照の仕組み (CMake・docker)、検査の仕組み、確かめた結果、launcher の基準の扱い、残件。
