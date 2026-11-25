# F1 指示書: 意図と動きが食い違っている API の不具合 12 個を直す

> 2026-11-25 | 親セッションからサブへの指示。計画は plan.md (2 章の A の一覧)。元の記録は doc/reference/TODO.md
> (「ファイルまわり」「Kernel.send_message」「S4a で見つかったほかの疑い」「音のまわり」「データと機器のまわり」
> 「通信のまわり」) と、doc/archive/sig_coverage/report/ の s2c・s4a・s4b・s5a・s5b (疑いを見つけたときの根拠の行)。

## 進め方 (項目ごと)

1. **再現する**。sim のシェルの irb (MCP の sim_up / sim_app (default/shell) / sim_input / sim_screenshot) か、
   ホストの試験で、直す前の動きを確かめる。記録の「読みだけ」の物は、実装を読み直して本当にそうなるかを先に確かめる。
   **再現しない・読み違いだった物は直さず、報告に事実を書く。**
2. **直す**。意図 (実装のコメント・文書・Ruby の普通の動き) のとおりになる最小の直し。
   - 上流 (PicoRuby) の gem の中の物 (`File`・`JSON` など `components/picoruby-esp32/picoruby/mrbgems/` の下) は、
     `components/` を直接編集しない。`lib/upstream/` の差し込み (patch・replace) で直す (CLAUDE.md の「開発時の注意」。
     今ある差し込みの形を読んで合わせる)。Family mruby の gem (`lib/gems/picoruby-fmrb-*`) は直接直す。
     Asterism の gem (`vendor/asterism`) は別のリポジトリから取り込んだ物なので、取り込みの仕組み (どこが元か) を
     確かめ、元に直しが要るなら**直さずに止まって報告する**。
   - lib/ を直したら、ビルドの前に `rake clean`。
3. **固定する**。ホストの試験 (`rake test` の並び) か回帰の場面で、直した動きを確かめられる物は試験を足す。
4. **sig を合わせる**。`sig/*.rbs` の説明が直す前の動き (「今の動き」として書いた注意) を書いている所を、直した後の
   動きに直す。A3 が直ったら、`sig/kernel.rbs` に 3 つの宣言を戻し (S4a で書いた形は git の履歴にある:
   `git log -S"def self.send_message" -- sig/kernel.rbs`)、`tools/sig_coverage/ignore.yml` の「left out until they
   are fixed」の 3 行を消す。sig の書き方は sig/README.md と doc/reference/sig_writing_and_coverage.md。
5. 項目ごと (近い物はまとめて) にコミットする。

## 項目ごとの注意

- **A2**: `"a"`・`"a+"`・`"r+"`・`"rb+"`・`"w+"`・`"wb"` の表を作り、直す前と後の動き (読める・書ける・書く位置・
  無いファイルを作る) を並べる。今ある同梱のアプリが使っている mode を grep し、動きが変わる物が無いことを確かめる。
- **A3**: 本体の型 (`components/fmrb_msg/fmrb_msg.h`) を使う形にする。直した後、2 つのアプリ (か irb とアプリ) の間で
  `Kernel.send_message` → 受け側の動きを sim で確かめる。**直す前の `receive_message` は動かさない** (スタックを壊す)。
  task_id の検査の上限 (0〜15 と本体の 9) は B なので変えない。
- **A4**: 範囲の外で出す例外のクラスは、冒頭のコメントと `JSON::DiggerError` の説明に合わせる。負の位置・
  Hash に無いキーの今の動きも表にして、変えるのは「範囲の外の位置」だけにする。
- **A5**: 日本語 (`"あ"` → "あ")、サロゲートペア、`Array#pack` の有無を確かめる。直すなら UTF-8 に組み立てる。
- **A7・A8**: 音はホストの試験 (MIDI のバイト列・待ち行列の中身) で確かめられる形があれば使う
  (`rake -T | grep -i midi`、doc/midi/)。耳での確認は要らない形にする。
- **A9**: `read` が -1 のとき `high?`・`low?` がどうあるべきかは、実装のコメントから読めるときだけ直す。
  読めなければ B として報告する。sim に GPIO は無いので、確かめられる範囲 (ホストの試験) を報告に書く。
- **A11**: Asterism の gem の物。上の「止まる」の条件に当たるなら、再現の方法と直し方の案だけ報告する。
- **A12**: sig の文だけ直す (実装は変えない)。

## 確かめる

- 項目ごとに、直した後の動きを sim かホストで。最後に `rake regress`、`rake sig:coverage` (「3. doc gaps」が 0 のまま)、
  `rake clean` → `rake build:linux` → sim でシェルの irb とエディタを 1 回起動して 1 打鍵。
- 内蔵 RAM を増やす直しをしない (A3 で構造体を大きくするときは、スタックに置く大きさと置き場所を報告に書く)。
- ESP32 のビルドはしない。ブラウザ版も作らない。

## 報告 (doc/api_fixes/report/f1.md)

1. 項目ごとの表: 再現 (したか・どう確かめたか) / 直した所 (`パス:行`) / 直した後の確認 / 足した試験 / sig の直し。
2. 直さなかった項目と理由 (再現しない・B に当たる・止まる条件)。
3. 上流に出せる直し (PicoRuby の gem) の一覧と、PR 候補として書ける形か。
4. 直している途中で見つけた別の不具合 (直さずに書く)。確かめていないこと。
5. コミットの一覧。

## 範囲と止まる条件

- 触ってよい: A の項目の実装 (`lib/gems/`・`lib/upstream/`・`flash/app/tool/gpio_viewer.app.rb`)、その試験、
  `sig/` の該当の説明と `sig/kernel.rbs`、`tools/sig_coverage/ignore.yml` (A3 の 3 行) と `baseline.json`、`sdk/`
  (境界の作り直し)、`doc/api_fixes/report/f1.md`、`doc/README.md`。
- 変えない: plan.md の B の項目の動き、`components/` の submodule の中、`vendor/asterism`、sdkconfig、ほかのアプリ。
- **利用者から見た動きが、plan の A に書いた「意図のとおりになる」以外に変わる直し方が要るときは、止まって報告する**
  (できていたことをできなくする・既定値を変える、など)。
- push しない・マージしない (ブランチ `api-fixes` にコミットまで)。ユーザに質問しない。終わったら sim を止める。
  コメントとコミットは英語、報告は日本語 (常体)。実在の値を書かない。
