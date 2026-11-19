# A3d 指示書: AI ヘルパーの入力の行を、PC・ブラウザの IME に対応させる

> 2026-11-19 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/kanji_input/plan.md と report の
> K4・K4b (PC の IME・ブラウザの IME とのつなぎ、`FmrbApp.set_text_cursor`)、
> `main/kernel/host/host_task.c` の IME の部分 (`s_ime_on`・`ime_focused_cursor`・
> `ime_publish_focused_cursor`・`HOST_TEXT_IN`)、エディタ (`main/ruby/default_app/editor/render.rb`) と
> シェル (`main/ruby/default_app/shell.app.rb`・`shell/shell_scroll.rb`) の申告のしかた、
> 今の `flash/app/modern/ai_helper.app.rb`・`ai_helper/screen.rb`、アプリの書き方の skill
> (直下の `.claude/skills/fmrb-app-new/SKILL.md`)。

## 1. 起きていること (ユーザの報告、GPD MicroPC2 のネイティブ版 `linux_pc_wl`)

- エディタでは Ctrl+Space で日本語入力に切り替わるが、AI ヘルパーの入力の行では切り替わらない。
- GPD では fcitx5 (Mozc) が動いていて、PC の IME がアプリに文字を打つ (`s_ime_on`。この間、自前の
  かな入力は止まる)。IME が働くのは、文字のカーソルを申告しているアプリ (`FmrbApp.set_text_cursor`)。
  エディタとシェルは申告している。**AI ヘルパーは申告していない** (親が grep で確認)。
- P4-Nano・sim では、自前のかな入力が全アプリに効くので、AI ヘルパーでも漢字まで入る (確認済み)。

親の見立て: AI ヘルパーが入力の行のカーソルを申告すれば直る。**まず経路を読んで見立てを確かめる**
(申告が無いと IME がどうなるか: 無効にされるのか、文字は来るが捨てているのか、確定の文字が別の形で
届いて AI ヘルパーが受けていないのか)。見立てと違ったら、分かったことを report に書いて、範囲の中で
直せるなら直す。

## 2. 直すもの

- AI ヘルパーの入力の行で、文字のカーソルの位置を申告する (エディタ・シェルと同じ流儀)。
  入力の行が使えるとき (文を打てる状態) だけ申告し、問いのボタンだけの状態・プルダウンを開いている間・
  Window が後ろにいる間など、打てないときは申告を外す (流儀はエディタ・シェルに合わせる)。
  大きさを変えた後・スクロールした後も、申告の位置が入力の行のカーソルと合っていること。
- IME が確定した文字 (と、要るなら変換中の文字の表示) が、入力の行に正しく入ること。今の作りは
  「かな入力は UTF-8 の 1 バイトずつキーのイベントで来る」前提 (`screen.rb` の 508 行あたり)。
  IME の文字がどの形で来るかを確かめ、足りなければ受ける。
- かなの指示器 (あれば) と状態の表示が、IME のいる端末でもおかしくならないこと。
- **P4-Nano・sim (IME のいない端末) の動きを変えない**。自前のかな入力と漢字変換が今までどおり入ること。
- ブラウザ版: ブラウザの IME でも同じ申告を使っている。AI ヘルパーで日本語が入るかを確認する
  (直すのは AI ヘルパーの中だけ。ブラウザ版のページの側の直しが要るなら、直さずに report に書く)。

## 3. 受け入れ条件

1. ネイティブ版で、PC の IME がいる状態 (`s_ime_on`) を作り、AI ヘルパーの入力の行に IME の文字が入る。
   手元 (WSL) に fcitx5 が無ければ、今ある IME の試験の仕組み (doc/kanji_input の K4 の確認のしかた、
   `rake regress:wl` や native の試験) で、申告したカーソルが IME の側へ届くことと、IME からの文字が
   入力の行に入ることを確かめる。**実物の fcitx5 での確認は GPD でユーザと親が行う**ので、
   手元でどこまで確かめたかを report に正確に書く。
2. sim の Modern (標準構成): AI ヘルパーの入力の行で、自前のかな入力 (Ctrl+Space → ローマ字 → かな →
   変換 → 確定) が今までどおり入る。質問を 1 つ送れる (偽物の LLM。a2.md の手順)。
3. ブラウザ版: 今の木から作ったバンドルで AI ヘルパーが開き、入力の行に日本語が入る
   (自前のかな入力。ブラウザの IME は、道具で確かめられる範囲で)。
4. `rake regress` が通る。最後に 1 回 `rake regress:screens`。VM プール: AI ヘルパーの起動後の使用量を
   直す前と比べて report に書く (KB 単位で増えないこと)。
5. `git status` に取りこぼしが無い (前からある 3 つを除く)。起こしたものは止めてある。

## 4. 範囲と止まる条件

- 触ってよい所 (fmruby-core): `flash/app/modern/ai_helper.app.rb`・`ai_helper/`、端末の案内
  (`flash/usr/share/doc/ai_helper(_ja).md`。書くことがあれば)、`doc/ai_coding/report/a3d.md`、
  `rake docs:index` の結果。
- **触らない**: カーネル・C・エディタ・シェル・ほかのアプリ、native/・wasm/ のコード、回帰の道具と基準、
  sdkconfig、submodule の中、graphics-audio、直下のリポジトリ、asterism-tutor・console。
  **GPD・P4-Nano・VPS に触らない** (GPD への反映は親が行う)。本物の LLM と鍵を使わない (`--llm fake`)。
  AI ヘルパーの外の直しが要ると分かったら、止まって案を report に書く。
- develop からブランチ `ai-helper-ime` を切る (この指示書は develop にコミット済み)。コミットの書き方は
  fmruby-core/CLAUDE.md。**push しない。develop へマージしない。** `git add -A` を使わない。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る。**利用者から見た動きを、この指示書に無い形で
  変える必要が出たとき** (選ばずに止まる)。
- PicoRuby の罠 (Regexp 無し、`Array#pack` 無し、ブロックは重い、`String#length` は文字数) と、
  申告を毎フレーム無駄に送らないこと (位置が変わったときだけ、など。エディタの流儀に合わせる)。

## 5. report (doc/ai_coding/report/a3d.md)

- 読んだ経路と、原因 (見立てと合っていたか)。直した形。
- 受け入れ条件 1〜5 の結果と根拠 (手元で確かめた範囲と、GPD で見てほしいこと)。
- ブラウザ版の結果。自分で決めた細部、確認していないこと、ユーザに決めてほしいこと、コミットの一覧。
