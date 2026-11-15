# W1b-2 指示書: W4b-3 の境界に合わせ、sim で wasm のアプリを確かめる

> 親: plan.md の段 W1b、report/w1b.md (8 章の残り)、doc/app_boundary/report/w4b_3.md (7 章「W1b への注意」)。
> 時間の目安は doc/regression/usage.md 0 章。W1b と W4b-3 は develop で合流した (c4e67ae2)。

## やること

1. **フックとイベントを W4b-3 の形に**: `components/fmrb_wamr/fmrb_wasm.c` の仮のフック (`on_event` の 8 つの i32 など) を、
   生成した C のヘッダの終わりにあるアプリが公開する関数の形 (`fmrb_app_event_t`、`on_update` の返り値など) に合わせる。
   殻の `kind` (HID のイベントの番号) から `fmrb_app_event_t.type` (選択肢の番号) への読み替えは Lua の `e_call_event` と
   同じ並び。殻の上でだけ動く関数 (`ctx->shell`) の扱いも確かめる。
2. **境界の表の大きさ**: 境界の関数が 122 個になった。WAMR に登録する表 (flash の const → 共通のプールへ写す) の大きさと
   共通のプールの使用を測って report に。
3. **SDK を合わせる**: family-mruby-sdk の TinyGo のつなぎ (`tinygo/fmrb/`) を新しいヘッダの形 (フック・イベント・FmrbAudio の
   受け手の番号・FmrbFile など、デモに要る範囲) に合わせ、`tools/check_bindings.rb` を通す。bounce と c_check を作り直して
   fmruby-core の `flash/app/tinygo/`・`flash/app/test/` に置く。SDK はコメントも含め ASCII のみ (README.ja.md を除く)。
   SDK はローカルのコミットまで (push しない)。
4. **sim で確かめる** (Linux のビルドには WAMR が入っている): Retro と Modern の sim で bounce と c_check を起動し、描画・
   クリックとキー・終了・再起動を画面で確かめる。強制の kill (`/app/kill` や sim_app の kill) の後に次の wasm のアプリが
   起動できるか (W1b の残り: 共通のプールの漏れと錠) を確かめ、直せるものは直す (仕様は変えない)。
5. **重い回帰を 1 回**: `rake regress:screens`・`rake regress:size` (S3 は内蔵 RAM の増分 0、P4 は WAMR の +28 B だけ)・
   `rake regress:run` (W4b-3 の +1.9 KB と、この段の差を表に。基準は親が更新する)。wasm のアプリの場面を regress に
   足すかは report に (今は足さない、ユーザ: Ruby 以外の画面は今は回帰に加えない)。
6. **P4 のビルド**: 最後に `FMRB_HW_TARGET=NARYAv4` の esp32 のビルドを作り、焼くのは親 (.env を変えたら戻す)。

## 確かめること

- 上の 4・5 と `rake regress`・`rake regress:spinel`・`ruby tools/wamr_poc/run.rb boundary`。
- 速さの確認は Ruby のエンジンだけでよい (ユーザ)。小さな差は理由を書けば止まらない (ユーザ)。

## 触ってよい範囲

- fmruby-core (develop) と family-mruby-sdk (ローカルのコミットまで)。graphics-audio は変えない。既存の未コミット
  (components/ のサブモジュール 2 つ、wasm/vendor/) には触らない。シリアルと flash は使わない (親が使う)。
- コミットは件名 `<種類>(<場所>): <要約>`、英語、Co-Authored-By、pathspec を明示。push しない。基準の更新はしない。

## 止まる条件

- 仕様が変わる選択 (アプリの書き方・名前・既定値)。境界の規則・sig を変える必要。内蔵 RAM の増分 (S3 は 0、P4 は +28 B まで)。

## report (report/w1b_2.md)

フックとイベントの形、表の大きさ、SDK の変化、sim の画面と結果、kill の後の動き、重い回帰の数字、P4 のビルドの置き場。
開始と終了の時刻は `date` で書く。
