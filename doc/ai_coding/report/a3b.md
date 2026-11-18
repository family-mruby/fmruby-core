# A3b 報告: 仮決めの見直しで決まった直し (タブの行の色、実行したアプリの名前、質問に文書を付ける既定)

> 状態: 完了 | 更新: 2026-11-18 | エディタのタブの行を自分の色の帯にし、エディタが文書ごとに最後に F5 で実行したアプリの名前を覚えて `ai_doc` の `rn` で返すようにし (名前はカーネルが `run_result` に付ける)、AI ヘルパーの「質問」は既定で文書を付けるようにした。sim の Modern の標準構成と全部 mruby の構成で確認した。指示書は instruction_a3b.md

ESP32 では動かしていない。ESP32 のビルドもしていない。本物の LLM は使っていない (fake だけ)。

## 1. 作り

### 1.1 タブの行の地の色

- 色はエディタの定数 `TAB_BG` (`main/ruby/default_app/editor/const.rb`)。**テーマのメニューの色 (`THEME_MENU_BG`)
  そのもの**にした。エディタのメニューの行はもともと「テーマのメニューの色から 1 段ずらした色」
  (`FmrbColors.shade(THEME_MENU_BG, 地の色)`) なので、タブの行はメニューの行とも、編集の領域 (`THEME_WINDOW_BG`)
  とも違う色になる。`/home/colors.toml` の `[editor]` で `menu_bg` か `bg` をちょうどその色にしたときだけは、
  メニューの行から 1 段ずらした色 (`shade(MENU_BG, BG_COLOR)`) にする。
- 今の文書は前のとおりの反転 (地 `MENU_TEXT`、字 `MENU_BG`)。ほかのタブは字 `MENU_TEXT` を地 `TAB_BG` の上に描く。
  区切りの線は足していない。
- ほかのエディタの色と同じく `[editor]` の `tab_bg` で変えられ、色の一覧 (View > Colors) にも 1 行足した
  (`EDITOR_COLOR_KEYS` に `tab_bg`)。エディタのすべての色を `[editor]` で変えられるという今の決まり
  (const.rb の頭の説明) に合わせたもの。
- 3 つのテーマでの実際の色 (sim の写しの画素から):

| テーマ | メニューの行 | タブの行 | 編集の領域 | 今のタブ |
|---|---|---|---|---|
| light (端末の既定) | 0x80 (暗い赤) | 0xC5 (赤みの桃色) | 0xFF (白) | 白の地に暗い赤の字 |
| dark | 0x00 (黒) | 0x49 (灰色がかった紺) | 0x24 (暗い緑) | 白の地に黒の字 |
| cyberpunk (ブラウザ版の既定) | 0x6B (薄紫) | 0x22 (青) | 0x00 (黒) | 白の地に薄紫の字 |

  dark ではタブの行と Window の題の帯が同じ色 (どちらも `THEME_MENU_BG`) だが、間に黒のメニューの行があるので
  つながって見えない。カラーキーの 0x01 はどのテーマでも出ない (`shade` が 0x01 を 0x00 にする。テーマの値
  そのものは 0x01 ではない)。

### 1.2 最後に F5 で実行したアプリの名前

再現: 直す前の版で、`#---fmrb` に `app_screen_name = "Boom Demo"` と書いた落ちるアプリ `/home/crashname.app.rb`
を F5 で実行した。エラーの窓は「Error: Boom Demo」(直前のエラーの名前は `Boom Demo`)。AI ヘルパーの「直して」は
「エディタで実行 (F5) したアプリのエラーが見つかりません。…」で、**エラーが付かなかった** (a2.md 8 章の見立てのとおり)。

作り:

- **カーネル** (`main/ruby/kernel/fmrb_kernel/app_lifecycle.rb` の `reply_run_result`): エディタへの `run_result` に、
  起こしたアプリの名前 `"name"` を足した (`_get_app_info(pid)[:name]`、直前のエラーに付く名前と同じ `ctx->app_name`)。
  - **カーネルを直した理由**: エディタが `run_result` を受けてから `FmrbApp.ps` で名前を引く形も考えたが、すぐ落ちる
    アプリは、エディタが `run_result` を読む前に回収されて一覧から消えうる。sim の実測で、`run_result` をエディタが
    読んだのは起動の 20 ms 後、`drw_text` のアプリが回収されたのは 57 ms 後で、間は 37 ms しかない (文法の誤りなら
    もっと早く落ちる)。カーネルが返事を作る時点ではアプリはまだ回収されていない (回収はカーネル自身が後でする) ので、
    ここで名前を読めば確かに取れる。C の変更は無い。
  - 176 B の上限: パスと名前の合計が 140 バイトを超えるときは `"name"` を付けない (その実行ではエラーが付かない)。
    鍵・`run_result`・pid・文字列の頭の 34 B を引いた残りから少し余裕を取った値。
- **エディタ**: `@run_name` (文字列、まだ実行していない文書は `""`) を足し、`editor/docs.rb` の文書ごとの状態に
  `@doc_runs` (文字列の配列) を足した。切り替え・閉じる・新規で、ほかの文書の値は混ざらない。
  - `run_result` を受けたら (`editor/remote.rb` の `remote_run_name`)、**そのパスの文書**に名前を入れる
    (ふつうは今の文書。返事が来るまでの間に切り替えていても、実行した文書に入る)。起動できなかった
    (`pid` が nil) ときは `""` にする。
  - `ai_doc` に **`"rn"`** (run name) を足した。
- **長い名前の扱い**: `rn` は 24 バイトまでに、文字の境で切る (`AI_RUN_NAME_MAX`)。`f` が上限の 60 バイトで、整数が
  全部 5 バイトになったときの `ai_doc` が 175 B で、176 B に収まる。カーネルの名前はもともと 31 バイトまで
  (`FMRB_MAX_APP_NAME`) なので、切るのは長い名前だけ。AI ヘルパーは直前のエラーの名前を同じ決まりで切ってから
  `rn` と比べる (`run_error` / `cut_name`)。sim で `app_screen_name = "ながいなまえのアプリですよ"` (39 B) のアプリを
  F5 → `rn=ながいなまえのア` (24 B) で、エラーが付いた。
- **AI ヘルパー**: 直前のエラーの名前と `rn` が同じときだけ、エディタの実行のエラーとみなす。ファイル名から名前を
  作る比べ方と、`rp` (pid) を見る条件は外した。`rn` が空 (まだ実行していない) なら付けない。
- 生成 C (`main/ruby/spinel/gen/linux/editor_combined.c`) で型を見た: `EditorApp` の `iv_run_name` は `sp_RbVal`
  (`iv_current_file` と同じ扱い)、`iv_doc_runs` は `sp_StrArray *`。`ai_doc` には `iv_run_name` がそのまま入る。
  カーネル側の `"name"` は `sp_poly_to_s` で文字列になる。

### 1.3 質問に文書を付ける既定

- `@attach` の既定を `true` にした (下の段のボタンは前のとおり「文書も送る」/「文書は送らない」で、押すと切り替わる)。
- 「質問」を送るとき:
  - 「文書は送らない」のとき: 質問だけ。
  - エディタが動いていない: 質問だけを送り、「(エディタが動いていないので、文書は付けずに送りました)」。
  - エディタが `busy`・`help` で断った: 止めずに質問だけを送り、「(エディタでダイアログやメニューが開いているので、
    文書は付けずに送りました)」/「(エディタでヘルプ (F1) が開いているので、…)」。`io`・`nomem`・返事が無い
    (3 秒) ときも同じく「エディタから文書を読めなかったので」で質問だけを送る。
  - 文書が空 (0 バイト): 質問だけを送り、「(エディタの文書が空なので、文書は付けずに送りました)」。
  - それ以外: 文書を付けて送る。
- 付けたかどうかの見え方: 自分の行が「あなた: (質問 + 文書 plain.app.rb) …」/「あなた: (質問、文書なし) …」、
  受け付けた後の状態の行が「受け付けました。文書 plain.app.rb を付けました。」/「受け付けました。文書は付けていません。」
  (名前の無い文書は「名前のない文書」)。
- 最初の案内の文を「「質問」「ヒント」「作る」は、エディタで開いている文書を使います (質問は、下の切り替えで文書を
  送らないこともできます)。」にした。

### 1.4 文書

- `flash/usr/share/doc/ai_helper(_ja).md`: 質問の既定と、付けないとき・その見え方。「直して」は今の文書を最後に
  F5 で実行したアプリのエラーだけを使うこと。
- `flash/usr/share/doc/editor(_ja).md`: タブの行が自分の色の帯であること。F5 で実行したアプリの名前をファイルごとに
  覚えて AI ヘルパーの「直して」が使うこと。
- `doc/reference/TODO.md`: 「ルータに届かない間、Asterism の Link の connect がアプリを約 4 秒止める」を足した
  (ユーザの決定どおり、直さずに記録だけ)。

## 2. 確認の環境

- sim の Modern は `targets.rb` の `FIRMWARE_ENV` と `FMRB_HW_TARGET=TAB5` で `rake clean:linux` → `rake build:linux`。
  全部 mruby の構成は `screens.rb` の `MRUBY_ENV` を重ねた。Retro は `.env` のまま (NARYAv3、Spinel のカーネルと
  エディタ) で作り、最後もこの構成を残した (始めと同じ)。
- テーマは sim の `flash/etc/system_conf.toml` (git に入っていない生成物) の `[theme]` を、Config の dark・cyberpunk の
  値に書き換えて sim を起こし直した。終わったら元のファイルに戻した (`cmp` で同じ)。
- gem: `bundle exec exe/fmrb-tutor --config <スクラッチ>/tutor.toml`。設定は `[llm] provider = "fake"`、
  `[limits] per_device_per_day = 1000`・`cost_per_day_usd = 1000`、記録の場所はスクラッチの `tutorlog/`。ルータは sim の
  zenohd (7447)。端末の Asterism の設定は置いていない (a2.md と同じ)。止めるときは自分が起こしたプロセスの pid で止めた。
- 試験のアプリは sim の `flash/home/` に置いて、終わったら消した (`crashname.app.rb`: `#---fmrb` に
  `app_screen_name = "Boom Demo"` を書いた `@gfx.drw_text` で落ちるアプリ、`plain.app.rb`: 落ちないアプリ、
  `longname.app.rb`: 名前が 39 B の落ちるアプリ)。

## 3. 受け入れ条件

| # | 結果 | 根拠 |
|---|---|---|
| 1 | 通った | 下の 3.1 |
| 2 | 通った | 下の 3.2 |
| 3 | 通った | 下の 3.3 |
| 4 | 通った | `rake regress`: host 33 suites・regress 233 runs、全部通った。最後の `rake regress:screens ONLY=modern` は下の 3.4 |
| 5 | 通った | `git status` は前からある 3 つだけ。sim の `flash/home/` は前からある `ec_smoke_out.rb`・`mahjong.toml` だけ、`flash/etc/system_conf.toml` は元のとおり。gem・sim は止めた |

### 3.1 タブの行

写しは会話のスクラッチ `shots/` (消える):

- `a3b_tabs_retro_light.png` (Retro 320x240、標準構成): `crashname.app.rb` と新規の 2 つ。
- `a3b_tabs_modern_light.png` (Modern 426x240、標準構成): `crashname.app.rb*` と `plain.app.rb`。
- `a3b_tabs_modern_full_light.png` (Modern の全画面 640x360、標準構成)。
- `a3b_tabs_modern_dark.png`・`a3b_tabs_modern_cyberpunk.png` (Modern、テーマを替えた。色は 1.1 の表)。
- `a3b_tabs_modern_mruby.png` (Modern、全部 mruby の構成)。

どれもタブの行がメニューの行と別の帯に見え、今の文書は反転で分かる。標準構成 (Spinel のエディタ) で、エディタが
開いて打てる (`puts 1` を打った) ことを Retro・Modern の両方で見た。

### 3.2 実行したアプリの名前

標準構成 (Modern、Spinel のカーネルとエディタ):

- `crashname.app.rb` を F5 → エディタのログ `run_result: document 1 ran as 'Boom Demo'`、AI ヘルパーのログ
  `ai_doc ... f=/home/crashname.app.rb rp=8 rn=Boom Demo`。「直して」で「このエラーについて頼めます:
  undefined method 'drw_text' for FmrbGfx …」が出た。「ヒントをもらう」を選ぶと、gem の記録 (07:33:41) の `ask` が
  `"mode":"hint"`、`"file":"crashname.app.rb"`、`"error":"undefined method 'drw_text' for FmrbGfx (NoMethodError)\n/home/crashname.app.rb:6:in on_create\n..."`。
- 実行していない文書: シェルの `edit` で `plain.app.rb` を 2 つ目のタブに開いて「直して」→ `rn=` (空) で
  「エディタで実行 (F5) したアプリのエラーが見つかりません。…」。
- 切り替え: タブで `crashname.app.rb` に戻して「直して」→ `rn=Boom Demo` でまたエラーが付いた (文書ごとに覚えている)。
- `plain.app.rb` を F5 (落ちない) → `run_result: document 2 ran as 'plain'`。その文書で「直して」→ 直前のエラーは
  `Boom Demo` のままで `rn=plain` と合わないので付かない。

全部 mruby の構成 (Modern): 同じ `crashname.app.rb` で F5 → `rn=Boom Demo` → エラーが付き、gem の記録 (07:40:23) の
`hint` に同じエラーの文。Ctrl+N の新しい文書で「直して」→ `rn=` で付かない。タブで戻すと `rn=Boom Demo` で付く。
長い名前 (1.2) もこの構成で確かめた。

### 3.3 質問

標準構成 (Modern)。gem の記録の `ask` の行 (時刻、使い方、文、`file`、`doc_sent` の大きさ):

| 時刻 | 場面 | 記録 |
|---|---|---|
| 07:34:27 | 何も切り替えずに質問 (エディタに `plain.app.rb`) | `ask` `q1 attach default` `plain.app.rb` `doc_sent` 106 B (文書の全文) |
| 07:34:49 | 「文書は送らない」に切り替えて質問 | `ask` `q2 off` `file` 空、`doc_sent` 0 B |
| 07:35:11 | エディタの今の文書が空 (Ctrl+N の新規) | `ask` `q4 empty` `file` 空、`doc_sent` 0 B。「(エディタの文書が空なので、…)」 |
| 07:35:18 | エディタで File のメニューを開いたまま | `ask` `q5 busy` `doc_sent` 0 B。エディタは `ai_doc refused busy`、質問は止まらずに通った |
| 07:35:30 | エディタを止めた後 | `ask` `q6 noeditor` `doc_sent` 0 B。「(エディタが動いていないので、…)」 |

(07:34:57 の `q3` は、Ctrl+N が前のアプリの Window に当たって新規ができず、`plain.app.rb` が付いた回。場面の作り直し。)
画面の写し `a3b_ask_noeditor.png` (エディタが動いていないとき)。

### 3.4 regress:screens

`rake regress:screens ONLY=modern`: `screens: 6 passed / 0 failed (modern 6/6; 2.4 min)`。終わった後の `build-linux/` は
始めの Retro の構成に戻った (`build-linux/: linux sim (retro)`)。

## 4. 見立てと違った点・自分で決めた細部

- 見立てどおり、別の `app_screen_name` のアプリでは直す前はエラーが付かなかった (1.2 の再現)。
- 名前をエディタの側で `FmrbApp.ps` から引くと、すぐ落ちるアプリで取りこぼす見込みがあるので、カーネルの
  `run_result` に名前を足した (1.2)。カーネルの直しはこの 1 か所 (Ruby だけ。C は無し)。
- `tab_bg` を `[editor]` の色と View > Colors の一覧に足した (1.1)。利用者から見ると色の一覧に 1 行増える。
- `ai_doc` の項目名は `rn`。長い名前は 24 B で切って、AI ヘルパーが同じ決まりで比べる (1.2)。
- 質問のときの `io`・`nomem`・返事が無いときも、`busy`・`help` と同じく止めずに質問だけを送る形にした (1.3)。
- `run_result` で起動できなかった (pid が nil) ときは、その文書の名前を空にする (最後の F5 はアプリにならなかったため)。

## 5. 残件・気づいたこと

- 直前のエラーはカーネルに 1 つだけあり、消えない。落ちたアプリを直して F5 で実行し、今度は落ちなかったとき、
  名前が同じなので「直して」は前のエラーを付けて出す (直す前と同じ動き)。エラーに時刻や実行の番号を付けるには、
  カーネルの直前のエラーの形を変える必要がある。
- `run_result` のパスと名前が合わせて 140 B を超えると名前が付かない。もとから、パスが 150 B ほどを超えると
  `run_result` も `open_file` も 176 B を超える (m3.md 5 章の残件と同じ)。
- 偽物の LLM のヒントは 1 行目 (`#---fmrb` の上) に入る。`#---fmrb` の囲みは、その前がコメントの行なら読まれる
  (`fmrb_app_meta.c` の決まり) ので、名前は変わらない見込み (AI の行を入れた後の F5 は確かめていない)。
- 32 ビットの ESP32 での VM プールは測っていない。足したのはエディタの文字列 1 つと配列 1 つ、AI ヘルパーの数行で、
  sim の `ps` の AI ヘルパーの mem_used は 654,680〜659,520 B (a2.md の 667,288 B と同じ程度)。

## 6. コミット

fmruby-core (ブランチ `editor-multi-file`、push・マージはしていない):

- `e8939fe2` feat(editor): the tab row on a band of its own colour
- `ab3d31e1` feat(editor,kernel): ai_doc tells which app the document last ran as with F5
- `0f324936` feat(ai_coding): Fix uses the editor's run name; a question takes the document along
- `c4586b61` docs(ai_coding,editor): guides for the question default, Fix and the tab row; Link stall in TODO
- (この報告) docs(ai_coding): report on the fixes the user chose on review

## 7. 後片付け

- sim は止めた。gem は自分の pid で止めた。ルータは sim の zenohd だけを使った。P4-Nano・シリアル・ESP32 の `build/`・
  port 7460 には触っていない。
- `build-linux/` は `.env` の Retro の標準構成 (NARYAv3、Spinel のカーネルとエディタ) に戻した。
- sim の `flash/home/` の試験のアプリを消し、`flash/etc/system_conf.toml` を元に戻した。鍵のファイル・
  `~/asterism_keys/`・端末の `/home/asterism` には触っていない。
