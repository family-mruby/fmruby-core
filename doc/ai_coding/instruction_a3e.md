# A3e 指示書: ブラウザ版の「つながるまで 20 秒」と、AI ヘルパーの入力の行の日本語入力の不具合

> 2026-11-20 | 親セッションからサブ (Opus) への指示。先に読むもの: doc/ai_coding/report/a3d.md (入力の行の IME 対応。
> ブラウザ版の確認のしかた `tools/fmrb_web.rb ime` もここ)、doc/kanji_input/plan.md と report の K4b・K5 (ブラウザの IME、
> 自前のかな入力と辞書)、`lib/gems/picoruby-fmrb-asterism/mrblib/link.rb` の `wifi_ready?`、
> `main/kernel/fmrb_platform_sys.c` の `default_net_info`、`flash/app/modern/ai_helper.app.rb`・`ai_helper/screen.rb`
> (入力の行、`utf8_feed`、Backspace)、`wasm/web/main.js` の IME の部分、アプリの書き方の skill
> (直下の `.claude/skills/fmrb-app-new/SKILL.md`)。

ユーザが公開のブラウザ版 (develop e3ff7ec4 のバンドル、Windows の Chrome) で AI ヘルパーを使って、3 つ報告した。

## 1. ブラウザ版が Asterism につなぐ前に 20 秒待つ (直し方は決定済み)

- 起きていること: AI ヘルパーが約 20 秒「Wi-Fi につながるのを待っています…」と出てからつながる。右上の Wi-Fi の印も赤い。
- 親が読んだ原因: `FmrbAsterism::Link#wifi_ready?` は、`FmrbApp.wifi_info` が nil でなく未接続のとき `wifi_wait_ms` (20 秒)
  待つ。ブラウザ版は `CONFIG_IDF_TARGET_LINUX` の `default_net_info` (getifaddrs でループバック以外の IPv4 を探す) を使い、
  wasm ではアドレスが 1 つも返らないので「機能あり・未接続」になる、という見立て (**動かして値は見ていない。まず確かめる**)。
- **直し方 (ユーザ決定、案 A)**: ブラウザ版は、ネットワークの状態を「つながっている」と報告する (ページを開けた時点で
  ネットワークはある)。`net_info` と `net_connected` の両方。ip・ssid・hostname に何を入れるかは、今の Linux 版の流儀
  (ssid は空、ダイアログは "-") に合わせ、嘘の値を作らない。
- 影響の確認: `net_connected`・`wifi_info` を見ている所を grep し、ブラウザ版で「つながっている」になって動きが変わる所を
  全部 report に並べる (デスクトップの印、Config の WiFi、時刻合わせ、遠隔操作、Asterism のアプリなど)。
  困る変わり方があれば、直さずに止まって書く。
- sim (Linux の 3 コンテナ)・ネイティブ版・ESP32 の動きは変えない。

## 2. AI ヘルパーの入力の行で漢字変換ができない (ブラウザ版)

- ユーザの言葉: 「AI ヘルパーで漢字変換ができない」。PC の IME (Windows の日本語入力) で打ったのか、自前のかな入力
  (Ctrl+Space) で打ったのかは、まだ聞いていない (親が聞く。答えが来たら伝える)。**両方の道を調べる**。
  - ブラウザの IME の道: 入力の行に焦点があるとき、ページは隠しの入力欄に焦点を移す (a3d 3.3 では道具の `ime` で
    確定まで通った)。本物の IME では、変換キー (Space)・Enter・矢印・Backspace が、変換中に IME でなくアプリへ
    行っていないか、`compositionstart`/`update`/`end` と `keydown` (`isComposing`・keyCode 229) の扱いを読む。
    エディタとシェルでは同じページの作りで変換できるのか (違いがあるなら、どこが違うか)。
  - 自前のかな入力の道: ブラウザ版のバンドルに辞書があるか (無ければ、かなまでで漢字にならないのが今の仕様か。
    K5 の決定を読んで事実を書く)。
- 手元に Windows の本物の IME は無い。道具 (`tools/fmrb_web.rb ime`、web_input) で作れる範囲で、本物の IME が出す
  出来事の並び (Chrome の MS-IME・Google 日本語入力: keydown 229 → compositionstart → compositionupdate… →
  compositionend、確定の Enter の keydown/keyup が後から来る、など) をできるだけ再現して調べる。再現できなければ、
  読んだ経路から原因の候補を絞り、**ユーザに見てもらう手順** (何を打って、何が出たら、どの候補か) を report に書く。

## 3. 日本語を打って Backspace で消していると、途中から効かなくなり、打った文字もうまく反映されなくなる (ブラウザ版)

- ユーザの言葉そのまま。再現の手順はこれだけしか分かっていない。
- 調べる順: (a) sim・ブラウザ版の自前のかな入力で、入力の行に日本語を打って Backspace を繰り返す (全部消す・消した後に
  また打つ・英字と混ぜる・行の折り返しや長い文)。AI ヘルパーの `utf8_feed` の途中のバイト、Backspace が 1 文字 (複数バイト)
  を消す所、カーソルの x の計算。(b) ブラウザの IME の道: 隠しの入力欄の中身とアプリの入力の行がずれないか
  (入力欄が空のときの Backspace、変換中の Backspace、確定の後の入力欄の掃除)。(c) エディタ・シェルでは起きるか。
- 原因が分かれば直す。直した後、同じ手順で起きないことを確認する。

## 4. 受け入れ条件

1. ブラウザ版 (今の木から作ったバンドル、`web_up`): AI ヘルパーを開いて、Asterism につなぎに行くまでの待ちが無い
   (状態の行が「Wi-Fi につながるのを待っています…」で止まらず、すぐ「AI ホストを探しています…」か、つながらない旨へ進む。
   時間を測って report に書く)。右上の印が「つながっている」の形。
2. 2 章・3 章: 原因を特定して直し、道具で再現した手順で確認する。**特定できないものは、直したことにしない**。
   分かったこと・候補・ユーザに見てほしい手順を書いて返す。
3. sim の Modern: AI ヘルパーの入力の行で、自前のかな入力と Backspace が正しく働く (3 章の (a) の手順)。
4. `rake regress` が通る。最後に 1 回 `rake regress:screens`。AI ヘルパーのコードを変えたら、VM プールの使用量を
   直す前と比べて report に書く。
5. `git status` に取りこぼしが無い (前からある 3 つを除く)。起こしたものは止めてある。

## 5. 範囲と止まる条件

- 触ってよい所 (fmruby-core): `main/kernel/fmrb_platform_sys.c` (1 章の分だけ)、`flash/app/modern/ai_helper.app.rb`・
  `ai_helper/`、`wasm/web/main.js` の IME の部分 (2・3 章の原因がページの側にあると分かったとき。変える前に、
  エディタ・シェルへの影響を読む)、`doc/ai_coding/report/a3e.md`、`rake docs:index` の結果、要るなら
  `doc/reference/TODO.md` の 20 秒の項目 (済みにする)。
- **触らない**: エディタ・シェル・ほかのアプリ、host task の IME の部分、native/、回帰の道具と基準、sdkconfig、
  submodule の中、graphics-audio、直下のリポジトリ、asterism-tutor・console。**GPD・P4-Nano・VPS に触らない**
  (ブラウザ版の VPS への配置は親が行う)。本物の LLM と鍵を使わない (`--llm fake`)。
  範囲の外の直しが要ると分かったら、止まって案を report に書く。
- develop からブランチ `web-net-ime` を切る (この指示書は develop にコミット済み)。コミットの書き方は fmruby-core/CLAUDE.md。
  1 章の直しと、2・3 章の直しは別のコミットにする。**push しない。develop へマージしない。** `git add -A` を使わない。
- 止まる条件: ユーザの作業が要る。範囲の外の変更が要る。**利用者から見た動きを、この指示書に無い形で変える必要が
  出たとき** (選ばずに止まる)。
- PicoRuby の罠 (Regexp 無し、`Array#pack` 無し、ブロックは重い、`String#length` は文字数・バイトは `bytesize`/`getbyte`)。
- 呼び名: asterism-tutor が動いている所は「AI ホスト」(「AI の係」と書かない)。Window (窓と書かない)。

## 6. report (doc/ai_coding/report/a3e.md)

- 1 章: 確かめた値 (直す前の `wifi_info`)、直した形、動きが変わる所の一覧、待ちの時間 (前・後)。
- 2・3 章: 読んだ経路、再現できたか、原因 (確かめたものと推定を分けて)、直した形、確認の結果。
- 確認していないこと、ユーザに見てほしいこと、ユーザに決めてほしいこと、コミットの一覧。
