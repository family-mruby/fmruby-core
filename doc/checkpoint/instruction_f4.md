# 実装指示書 F4: ビルドの違う端末の間で運ぶ

`doc/checkpoint/plan.md` (決まったこと、F3 の検収まで)、`report/f1.md`〜`report/f3.md` (特に f3 §12「F4 に要るもの」)、
`instruction_f2.md`・`instruction_f3.md`、mruby-checkpoint の `doc/plan.md` (D1〜D16) と
`doc/survey_family_mruby.md` (§2 VM とビルドの違い、§8 の F4) を先に読む。

## 目的

ビルドの違う 2 つの Family mruby の間で、申告済みのアプリ (Window のないもの・Window のあるもの) を移す。
組み合わせは 2 つ:

- **F4a**: sim (ESP-IDF の Linux ビルド、64 bit、tick は別のスレッドが進める) ⇔ ネイティブ版 (64 bit、tick は VM が
  自分で進める)。gem の組み合わせ・時分割・画面の機種 (Retro 320x240 / Modern 426x240) が違う。
- **F4b**: ブラウザ版 (wasm、32 bit、tick は VM が自分で進める) ⇔ ネイティブ版。ポインタの幅が違う。

## 決まっていること

- D1〜D16、F1〜F3 の決まり (描いた中身は描き直す (b)、移動中の印、`on_destroy` は呼ばない、B が断ったら画面に
  出さない、など)。
- 名前の引き当て: 書き出し側の permanent の名前が着いた側に無ければ、理由つきで断る (P2 の SabiRuby と同じ)。
  黙って違うものに結ばない。
- 内蔵 RAM +0。ESP32 に入れるのは F6 (実機)。F4 では ESP32 の構成は変えない。
- ブラウザ版の検証で、人が開いているブラウザ (`tools/fmrb_web.rb` が起こしたもの、ポート 8016) を閉じたり
  操作したりしない。自分のポートで、ヘッドレスのブラウザで確かめる。

## やること

1. **名前の差を測る**: sim・ネイティブ版・ブラウザ版のアプリの VM で、permanent の名前の一覧を取り、差を表に
   する (どの gem の差か)。試験のアプリと標準の小さなアプリが使う範囲で、差が移すのを妨げるかを確かめる。
   妨げるなら、根本から直す方法を比べる (片方にしか無い gem の名前を使っていないなら移せる形になっているか、など)。
2. **sim に入れる (F4a)**: sim は F0 で gem を持っている (`FMRB_HAVE_CHECKPOINT`)。sim の時分割 (tick を進める
   スレッド、`vm_deliver_switch`) で、止める・続けるが正しく動くこと (F1〜F3 の場面を sim の上でも)。sim の
   `/tmp` とイメージの出し入れの道 (F2 のデバッグサーバの命令がそのまま使えるか)。
3. **画面の違い**: Retro と Modern で画面の大きさが違う。`.toml` の Window の位置・大きさが着いた側に収まらない
   とき、基底クラスで比べて `on_resize` を呼ぶ (f3 §12)。フォントの有無の違い (`@current_font` を着いた側で
   選び直した値に揃える)。
4. **ブラウザ版 (F4b)**: ブラウザ版に gem を入れるかどうか (今は入っていない。入れると wasm の大きさと起動の
   時間が増える。測って report に書く)。入れるなら、ブラウザ版の時分割 (VM が自分で tick を進める) で止める・
   続ける、イメージの出し入れの道 (ブラウザ版のファイルの出し入れ: `web_fs` の道具など)、32 bit ⇔ 64 bit。
   ブラウザ版の公開に影響する変更 (wasm の大きさ・起動の時間) は、数字を出してユーザに判断を仰ぐ (止まる条件)。
5. **試験**: `rake regress:checkpoint` に、sim ⇔ ネイティブ版の場面 (Window のないアプリ・Window のあるアプリ、Retro と
   Modern) と、ブラウザ版 ⇔ ネイティブ版の場面 (入れた場合) を足す。カーネルが Spinel の構成と全部 mruby の構成の
   両方で。sim は 3 つのコンテナを使うので、`regress:run` などと同時に動かさない形にする。
6. **文書**: `report/f4.md` (名前の差の表・画面の違いの扱い・ブラウザ版の判断材料・計測・サブの仮決め・F5 に
   要るもの)、`plan.md` の F4 の行を「実装済・検収待ち」に、`doc/regression/usage.md`。基準の費用が動いたら
   数字を report に書く。

## 確かめること

- `rake regress:checkpoint` (両方の構成、新しい場面を含む)、`rake regress`、`rake regress:run`、
  `rake regress:screens ONLY=retro,modern,native,native-keys` (人が開いているブラウザがあるので web は流さない)、
  共有のコードを変えたら `rake regress:size` (内蔵 RAM +0)。
- mruby-checkpoint に手を入れたら、そちらの `rake test`・`test:asan`・`test:stress`。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core は本体の checkout の `feature/linux-native` にコミット (push しない)。mruby-checkpoint は
  worktree のブランチにコミット (push しない。開発中は `MRUBY_CHECKPOINT_DIR`、固定点は親が合わせる)。
  family-mruby のリポジトリ直下 (`tools/`) も触ってよい (コミットまで)。
- 止まる条件: 利用者から見た動きが変わる直し方、ブラウザ版の大きさ・起動の時間が目に見えて増える変更を
  入れる判断、D1〜D16 と食い違う事実、時分割やスケジューラの作りを変える必要。止まったら report に書いて返す。
- 文書は常体、定着したカタカナ語を使う。コミットする文書にローカルの絶対パスを書かない
  (コミットの前に、差分をローカルのパスで検索して確かめる)。
