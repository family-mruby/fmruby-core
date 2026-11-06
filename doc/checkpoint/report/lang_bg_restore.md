# 移したアプリの表示の言語と FmrbUI の地の色 (F3・F4 の直し)

> 状態: 検収済 | 更新: 2026-11-06 | `report/platform_restore.md` の洗い出しで未決にした 2 つを、ユーザの決定 (2026-11-06) どおりに直した。表示の言語 (`FmrbI18n`) は復元のときに控えた言語を忘れて着いた側の言語にし、`FmrbUI` の部品の地の色は `bg:` を省いた部品だけを着いた側のテーマの Window の地の色に入れ直す。直す途中で、アプリが `FmrbI18n.add` で足した語が移すと消えていたことが分かり、語の表を VM を開いた後に作る形にして運ばれるようにした。`rake regress:checkpoint` の 3 つ目のネイティブ版 C (テーマに加えて言語も変えた) に widget ground と language の確認を足し、直す前に落ち、直した後に通ることを確認した

## 1. 症状

- 言語の違う端末へ移したアプリで、`FmrbI18n.t` が引く語が書いた側の言語のまま (または語の名前のまま) になりうる。`FmrbApp.language` は着いた側の値。
- テーマの違う端末へ移したアプリで、`bg:` を省いて作った `FmrbUI` の部品の地 (Label の箱など) が書いた側のテーマの Window の地の色のまま描かれる。文字の色 (`C_TEXT` など) は描くときに定数を読むので着いた側の色になり、テーマが混ざる。

## 2. 原因

- `FmrbI18n.lang` は最初に引いた言語をモジュールのインスタンス変数 `@lang` に控える。モジュールへの変更としてイメージに入る。
- `FmrbUI.new` は `bg:` の既定を `C_BG` (`FmrbConst::THEME_WINDOW_BG`) とし、作ったときの値を `FmrbUI` と各部品の `@bg` に写す。`C_BG` そのものは着いた側の値になる (定数は VM を開いた時点の基準にあり、イメージの変更表に出ない) が、部品に写した値はアプリの状態として運ばれる。既定の色とアプリが選んだ色を区別する印が無かった。
- 直す途中で分かったこと: 語の表は定数 `STRINGS = { "en" => {}, "ja" => {} }` で、VM を開いた時点からある。イメージは「VM を開いた時点からあるもの」への変更を、そのもの自身に名前 (gem の format_v1.md の 8 章、`V:FmrbI18n@STRINGS` など) があるときだけ運ぶ。中の `"en"`・`"ja"` の表には名前が無いので、`FmrbI18n.add` がそこへ足した語は**黙って落ちていた** (着いた側で `t(:key)` が `"key"` を返す)。中の表を新しいものに差し替えると、今度は外の表の鍵 (`"en"` の文字列。これも名前が無い) を書けず、イメージが断られた (`object made by mrb_open has no name: String of class String (key 0 of #0)`)。
- `@lang` は、全部 mruby の構成でも標準の構成でも (移せるのは mruby の VM のアプリ)、`FmrbApp.language` が返す `FmrbConst::LANGUAGE` の文字列そのもの (`to_s` は同じものを返す) を指していた。イメージはこれを名前 (`V:FmrbConst@LANGUAGE`) で運び、着いた側の同じ名前の値に結ぶので、今のビルドでは言語だけはたまたま着いた側の値になっていた (`checkpoint_inspect.rb` で確認: `set FmrbI18n :@lang = FmrbConst::LANGUAGE (#8)`)。語が落ちるので、どちらにしても着いた側の言語の語は出なかった。

## 3. 直し方

- `FmrbI18n.forget` を足し (`@lang` を nil に戻す)、`FmrbApp#_checkpoint_restored` の最初、`FmrbAssoc.forget` の隣で呼ぶ。次に `lang` を引いたときに着いた側の `FmrbApp.language` を読む。今は名前で結び直すことで同じ結果になっているが、`FmrbApp.language` が別の文字列を返すようになったり、書いた側の言語が表に無く `DEFAULT_LANG` を控えていたりしても着いた側の言語になる。
- 語の表を定数から外し、最初に引いたときに作ってモジュールのインスタンス変数 `@strings` に置く (`FmrbI18n.strings`)。VM を開いた後に作ったものなので、中の表も鍵も語もイメージで運ばれる。`add`・`lang`・`t` の振る舞い (足した語を消さない、表に無い言語は "en"、表に無い語は名前) は変えていない。`STRINGS` を参照している所はほかに無かった。Spinel のプログラム (デスクトップなど) も同じソースで、通る。
- `FmrbUI`: `bg:` の既定を -1 (省いた印。Spinel でも Integer のまま型が絞れる) にし、`@bg_theme` (真偽) に「既定の色か」を覚える。部品 (`Widget`) も `@bg_theme` を持ち、`FmrbUI#add` が `take_ground(色, 既定か)` で両方を渡す。`FmrbUI#bg=` と `Widget#bg=` で色を入れたら既定でなくなる (Shell の `color` のように、アプリが選んだ色)。`_checkpoint_ground` が既定の `FmrbUI` と部品だけを `C_BG` (着いた側の値) に入れ直して描き直しの印を付ける。部品に呼ぶメソッドは名前を 1 つにした (`follow_theme_ground`、Spinel の決まり、fmrb-ui.rb の冒頭の 6)。
- 部品の見つけ方: `_checkpoint_restored` が `ObjectSpace.each_object(FmrbUI)` で集めて `_checkpoint_ground` を呼ぶ (`FmrbGfx`・`GfxBlock` の結び直しと同じ形。`detach_ui` で外した組も拾う)。場所は Window の枠を作り直す前、`clear_user_area` (部品に描き直しの印を付ける) の前。描くのはアプリの `on_restore`。
- 型の説明 (`sig/fmrb_ui.rbs` の `FmrbUI.new`、`sig/fmrb_misc.rbs` の `FmrbI18n.lang`) に、移したときの振る舞いを 1 文ずつ足した。
- アプリがインスタンス変数に控えた訳語・色は今までどおりアプリの状態として運ぶ。イメージの形式は変わらない。

## 4. 試験

- `flash/app/test/ckpt_calls.app.rb`: `on_create` で `FmrbI18n.add("en" => { ckpt_word: "word-en" }, "ja" => { ckpt_word: "word-ja" })` と `FmrbI18n.lang` を呼び、ログの行の終わりに `lang=` (`FmrbI18n.lang`)・`word=` (`FmrbI18n.t(:ckpt_word)`)・`language=` (`FmrbApp.language`) を足した。
- `flash/app/test/ckpt_window.app.rb`: `FmrbUI` を 2 つ持ち、`bg:` を省いた Label と `bg: 0x92` の Label を 1 つずつ置く (利用の範囲の (4, 24) と (40, 24)、30x10)。ログの `drawn` と `on_restore` の行に `ui_bg=`・`chosen_bg=`・`window_bg=` (10 進の RGB332) を出す。
- `tools/regress/checkpoint.rb`: 3 つ目のネイティブ版 C の `/etc/system_conf.toml` で `language` も `"ja"` にした (A と B は `"en"`)。C の場面を `third_program_rounds` にまとめ、確認の項目を分けた。
  - window theme (枠、前からある): タイトルバーと縁の画素。
  - widget ground (足した): 見本 (C で新しく起動)・A・移した後で、省いた Label の地の画素がその端末の Window の地の色 (C で 0x00、A で 0xFF)、渡した Label が 0x92。移した後は `on_restore` の行の値も見る (`ui_bg` = `window_bg` = 0x00、`chosen_bg` = 0x92)。その後に、前からある見本との画素の比較。
  - language (足した): A で `ckpt_calls` の行が `lang=en word=word-en language=en` であることを見てから C へ移し、C の `on_restore` の行で `language=ja`、`lang` が `language` と同じ、`word=word-ja`。

直す前に流した結果 (標準・全部 mruby とも同じ):

```
# fmrb-ui.rb・fmrb-i18n.rb・fmrb-app.rb を 95de2fa5 のまま
standard: FAILED widget ground: the labels hold 0xFF (default) and 0x92 (chosen) where the window background there is 0x00 (... ckpt_window: on_restore dots=2 text=ab ui_bg=255 chosen_bg=146 window_bg=0)
# FmrbUI だけ直し、FmrbI18n は直さない
standard: FAILED language: the word there is ckpt_word, not word-ja
```

2 回目の C のログの行は `lang=ja word=ckpt_word language=ja` で、言語は (2 章の最後のとおり) すでに着いた側だった。この試験で直す前に落ちるのは語の方で、`forget` を外しても今のビルドでは落ちない (今の形が崩れたときの守り)。

## 5. 確認

| 確認 | 結果 |
|---|---|
| `rake regress:checkpoint` (標準・全部 mruby) | 標準・全部 mruby とも ok (`widget ground: default 0x00 there, chosen 0x92 carried`、`language: ja there (word-ja), as FmrbApp.language`、ほかの場面も前と同じ) |
| `rake regress` | ok (ホスト 21 本、描画の記録 205 回。`FmrbUI` の部品の描画の記録も変わらない) |
| `rake regress:screens ONLY=native` | native 3/3・native-keys 4/4 ok。部品の見た目は普段と同じ。`screens/drivers.rb` が `FMRB_ROOT` を見ないので、そこだけ一時的に書き換えて流した (コミットしていない) |
| sim・ブラウザ版・実機の場面 | 流していない。sim の道具は worktree から流せない、実機は使わない決まり。`ckpt_calls`・`ckpt_window` のログの行は後ろに値を足しただけで、sim・Asterism・実機の場面が見る前の部分は変わらない。`ckpt_window` の Label は、sim との間の比較 (同じ明るいテーマ) では両側で同じ色に描かれる |

## 6. 洗い出し (同じ形のほかの値)

起動時・初めて使うときに端末の設定や VM の中のものを控え、移った先で古いままになりうるもの。

| 値 | 所 | 今の扱い | 判断 |
|---|---|---|---|
| VM を開いた時点からある名前の無いものへの変更 (定数の Hash の中の Hash・Array、その鍵の文字列など) | gem (mruby-checkpoint) | 変更は運ばれない (黙って落ちる)。名前のあるものから指し直すと、書くときに断られる | 未決 (gem の話)。FmrbI18n は避けた。下の選択肢 |
| `Fft` の `@spinel_refs`、`Raycast` の `@refs`、`SpinelHello` の `@refs` | 各 gem の mrblib (モジュールのインスタンス変数) | 書いた側の端末で開いた Spinel のインスタンスの数が運ばれる。着いた側では開いていないのに 1 以上なので、`open` が開かずに使い、`close` が開いていないものを閉じうる | 未決。運べるアプリが使うことはまだない (試験のアプリも使わない)。下の選択肢 |
| `PicoRabbit::Theme.default` の `bg`・`text`・`quote_text`・`footer_*` | picorabbit の mrblib | 作ったときのテーマの色を Theme に写す。移すとテーマの違う端末で混ざる (`FmrbUI` と同じ形) | 未決。picorabbit は `movable` でない。運べるようにするときに `FmrbUI` と同じ形 (既定の印と復元で入れ直す) にするのがよい |
| Shell の `@bg_col` (`THEME_WINDOW_BG` から) | Shell | 同じ形 | 対象外 (Spinel で動き、運べない) |
| `FmrbUI` の `@win_w`・`@win_h`・`@ox`・`@oy`、部品の位置 | `FmrbUI.new` | 作ったときの利用の範囲。大きさが変わるときは `on_resize` が来て、アプリが部品を作り直す (`detach_ui`) | 対象外 (F4 の決まりのとおり、アプリの仕事) |
| 部品の文字の幅 (`@tw`・`@show`) | 部品を作ったとき | 混ぜ書きの描き方 (Font0 と misaki 8) はどの端末にもあり、幅は同じ | 対象外 |

選択肢 (推奨を先に):

- 名前の無いものへの変更: (A, 推奨) gem で、名前のあるものから届く VM を開いた時点のもの (Hash・Array の中身、その鍵) にも名前を付ける (`V:FmrbI18n@STRINGS/...` の形) か、変更を名前のあるものの中身ごと運ぶ。少なくとも黙って落とさず断る。(B) gem は変えず、movable_apps.md の 3 章に「VM を開いた時点からある表の中を書き換えない」と書く。(C) 今のまま (基底の mrblib で見つけたものを 1 つずつ避ける)。
- Spinel のインスタンスの数: (A, 推奨) 開いている間は書けないようにする (C のデータと同じく理由つきで断る。アプリが閉じてから頼む)。(B) 復元のときに数を 0 に戻し、アプリが次に `open` したときに開き直す。(C) 今のまま (運べるアプリでは使わないと文書に書く)。
