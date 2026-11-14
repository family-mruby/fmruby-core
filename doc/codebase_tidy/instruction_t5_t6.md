# T5・T6 指示書: FmrbSystem への分割と置き場の名前 (続けて 1 回で)

> 親: plan.md の T5・T6 と「決めたこと」「進め方」。材料は report/t0.md の T5・T6 の節。時間の目安は doc/regression/usage.md 0 章。

## T5: FmrbApp のクラスメソッドの分割

1. system 用の 34 本 (t0 の表。wifi_*・ble_*・linux_*・reboot など) を `FmrbSystem` に移す。読み取り系の境目は `FmrbApp` に残す。
   mruby・Spinel・Python の 3 系統とも。古い名前は残さない。
2. mruby と Spinel のずれ (25 本) を揃え、sig に無いもの (37 本) を sig に書く。sig は `FmrbApp` と `FmrbSystem` に分ける
   (sig/README.md の 112 バイトの決まり。sig を直したら `rake clean`)。
3. 呼んでいる所 (カーネル・組み込みアプリ・ネイティブ版の設定アプリ・desktop・Shell・サービス) を全部直す。
4. **古い呼び出しの検査**: 移した 34 本の名前で `FmrbApp.<名前>` が flash・main・lib・tools・native に 0 であることを確かめる rake の
   検査を `rake test` に足す (mruby は実行してみるまで NoMethodError が出ないため)。
5. T5 の後の確認はこれだけ: `rake regress`、`rake regress:spinel`、4 の検査。コミットは T5 だけで閉じる。

## T6: 置き場の名前 (まとめて 1 回)

1. `tool/` と `tools/` (core) を `tools/` に一本化する (ルートの `tools/` はそのまま)。
2. `main/prebuild_scripts/` を `main/ruby/` に。
3. `lib/add/` を `lib/gems/` (自作 gem) と `lib/upstream/` (上流への差し込み) に分け、`lib/patch/`・`lib/replace/` も `lib/upstream/` の下に入れる
   (Rakefile の setup の手順、`rake clean` の対象)。
4. `config/` の端末ごとのファイル名を `FMRB_HW_TARGET` の小文字にそろえる (n16r8 → naryav3 など、t0 の表)。これに伴う
   `sdkconfig.defaults.*` の中のパス (約 10 行) の書き換えは承認済み (2026-11-14 ユーザ)。ほかの sdkconfig の中身は変えない。
5. 試験用アプリ (`flash/app/test/`) は置き場を変えず、配布物 (リリースのビルド・Web Installer・ブラウザ版の束) からだけ外す。
   開発のビルドと回帰の道具では今どおり置く。
6. 参照を全部直す: Rakefile・rakelib・CMake・CI・docker・MCP (`tools/mcp/`)・直下の tools・skill・CLAUDE.md (3 つ)・
   doc/ の文書 (archive を含む全部。古いパスを grep して 0 に)・memory (`/home/kishima/.claude/projects/-home-kishima-fmrb-family-mruby/memory/`)。
7. T6 の後に `rake regress:full` を 1 回 (35 分ほど)。これを計画全体の締めの確認にする。落ちたらその所だけ直し、影響する道具だけを流し直す。

## 触ってよい範囲

- fmruby-core (develop)、直下の family-mruby (develop)、fmruby-graphics-audio (参照のパスが変わる所だけ)、`.claude` (skill。自分の直しだけ
  コミット)、memory のファイル。作業ツリーの既存の未コミット (core の components/ サブモジュール 2 つ、wasm/vendor/、doc/wasm_apps/、
  doc/README.md の wasm_apps の分、graphics-audio の components/ の 2 つ) には触らない。`git mv` で履歴を残す。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。T5 と T6 は別のコミット (T6 は場所ごとに分けてよい)。
  push しない。基準の更新はしない (要るものは report に)。
- 終わったら Linux のビルド (x86-64)、sim・web・ネイティブ版を片付け、裏の処理を残さない。

## 止まる条件

- 仕様が変わる選択、内蔵 RAM の増分、VM プールの KB 単位の増分で理由が説明できないもの、ユーザの作業が要るとき。

## report

`report/t5.md` と `report/t6.md`: 移した名前の表、検査の仕組み、置き場の新旧の表と直した参照の数、`regress:full` の結果、残件。
