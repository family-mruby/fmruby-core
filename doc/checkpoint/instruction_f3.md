# 実装指示書 F3: 窓のあるアプリを運ぶ

`doc/checkpoint/plan.md` (決まったこと、F2 の検収まで)、`report/f1.md`・`report/f1b.md`・`report/f2.md`、
`instruction_f2.md`、mruby-checkpoint の `doc/plan.md` (D1〜D16) と `doc/survey_family_mruby.md`
(特に §3.3 VM の外でアプリのために持っているもの、§5 gem と C のデータ、§5.3 作り直し方の選択肢) を先に読む。

## 目的

窓を持つ申告済みの mruby のアプリ (`FmrbGfx` で描き、イベントを受ける普通のアプリ) を、F1 (同じプロセスで
起動し直す) と F2 (ネイティブ版の 2 つのプロセスの間で移す) の両方で、止めた所から続けられるようにする。
画面つきのデモの形になる。今は `FmrbGfx` の C のデータが登録されていないので、セーフポイントで断られる
(`ckpt_window` の場面)。

## 決まっていること

- D6: 運べるのは申告したアプリだけ (`.app.toml` の `movable = true`)。hook は `on_checkpoint` /
  `on_restore` / `on_checkpoint_refused`。移動元を閉じるときに `on_destroy` は呼ばない (F2 の検収)。
- D7: 各 gem の C のデータの書き出し・戻しは、それぞれの gem に置き、登録は「checkpoint を開いた VM だけ」が
  する形 (gem の初期化で登録すると、どの VM も費用を払う。report/f0.md §5)。
- D8: VM の外の資源は、開き直すための情報 + 基底クラス (`FmrbApp`) の肩代わり + 最後にアプリの
  `on_restore`。
- D16: Spinel で動くアプリは対象外。
- 内蔵 RAM +0。ESP32 に入れるのは F6。

## やること

1. **VM の外でアプリのために持っているものの洗い出し**: 調査 §3.3 を、今のコードで確かめ直す (窓の属性
   (位置・大きさ・タイトル・z・全画面かどうか)、キャンバス (`canvas_id`) と描いた中身、スプライト (整数の id)、
   Pub/Sub の購読 (カーネルの `@subscriptions`)、HID の転送先・フォーカス、タイマ、開いたファイル、その他)。
   表にして report に書く。
2. **`FmrbGfx` ほか窓まわりの C のデータの書き出し・戻し (D7)**: 戻した側で同じ属性の窓とキャンバスを作り直し、
   アプリの Ruby が持つ `@gfx` などがそれを指すようにする。
3. **描いた中身**: 着いた側で絵をどう戻すか。候補を比べて選ぶ (サブの仮決めでよい):
   (a) キャンバスの画素をイメージに入れて戻す、(b) 基底クラスが戻した後に描き直しを促す (`on_restore` の
   既定の動きとして全体の再描画)、(c) 両方。画面の大きさ・色の形式 (Retro と Modern) と、イメージの大きさで
   判断し、理由を report に書く。
4. **基底クラスの肩代わり (D8)**: 購読・フォーカス・タイマなど、アプリが書かなくても戻るべきものは `FmrbApp` の
   側で戻す。アプリが自分で持つ外の資源は `on_restore` で直す、という線を決めて `sig/fmrb_app.rbs` と文書に書く。
5. **試験のアプリ**: 窓を持ち、描き、イベント (キー・クリック) を受け、Task も持つ申告済みのアプリ。
   `rake regress:checkpoint` に場面を足す: F1 の形 (起動し直し) と F2 の形 (2 つのプロセスの間で移す) で、
   移った後の画面が移す前と同じ (または描き直した後に同じ) こと、移った後にキーやクリックが届くこと。画面の
   比べ方は今の回帰の道具の画像の比べ方に合わせる。カーネルが Spinel の構成と全部 mruby の構成の両方で。
6. **既存のアプリで試す**: 標準で入っている窓のあるアプリから 1〜2 本 (小さいもの) に `movable = true` を付けて、
   移せるか、何が足りないかを確かめる。足りないものは report の表に (直すのは、決まっている範囲で根本から)。
7. **文書**: `report/f3.md` (洗い出しの表・選んだ戻し方・計測 (イメージの大きさ・時間)・サブの仮決め・F4 に
   要るもの)、`plan.md` の F3 の行を「実装済・検収待ち」に、`doc/regression/usage.md`。基準の費用が動いたら
   数字を report に書く。

## 確かめること

- `rake regress:checkpoint` (両方の構成)、`rake regress`、`rake regress:run`、
  `rake regress:screens ONLY=retro,modern,native,native-keys` (ブラウザ版は流さない)、
  共有のコードを変えたら `rake regress:size` (内蔵 RAM +0)。
- mruby-checkpoint に手を入れたら、そちらの `rake test`・`test:asan`・`test:stress`。

## 触ってよい範囲・止まる条件・守ること

- fmruby-core は本体の checkout の `feature/linux-native` にコミット (push しない)。mruby-checkpoint は
  worktree のブランチにコミット (push しない。開発中は `MRUBY_CHECKPOINT_DIR`、固定点は親が合わせる)。
  family-mruby のリポジトリ直下 (`tools/`) も触ってよい (コミットまで)。
- 止まる条件: 利用者から見た動きが変わる直し方 (既存のアプリの見た目・操作、`FmrbApp` の API の意味を
  変えるなど)、D1〜D16 と食い違う事実、描画の経路 (graphics-audio との境) の作りを変える必要。止まったら
  report に書いて返す。
- 文書は常体、定着したカタカナ語を使う。コミットする文書にローカルの絶対パスを書かない
  (コミットの前に、差分をローカルのパスで検索して確かめる)。
