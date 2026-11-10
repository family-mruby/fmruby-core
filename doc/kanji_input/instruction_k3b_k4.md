# K3b・K4 指示書: 残りの直しと、ブラウザ版のブラウザの IME

> 状態: 完了 (report/k3b_k4.md) | 更新: 2026-11-11 | 正は plan.md、report/k2b_k3.md、report/k0.md (K4 の見立て)

## K3b: K3 の残り

1. **sim の Modern の全画面 (640x360) でも下の 1 行を出す**: 親が graphics-audio の変更を認める (Linux の全画面の描画
   `screen_mode_render_frame` に、z が 30000 以上の Canvas を重ねる数行)。fmruby-graphics-audio でコミットする
   (Linux の経路だけ。WROVER の経路は変えない)。
2. **変換のタスクのスタックを内蔵 RAM に置かない**: 辞書があるときに 6 KB + TCB が内蔵 RAM に増える。PSRAM のスタックで
   作れるか調べる (sdkconfig を変えずにできる範囲で。既存のタスクで PSRAM のスタックの例があれば合わせる)。sdkconfig の
   変更が要るなら変えずに report に提案を書く。
3. 変換キー (0x8A) を sim の入力の道具の名前に足し、注入で試す (tools/fmrb_input.rb などの表)。

## K4: ブラウザ版のブラウザの IME

- report/k0.md の見立て: ネイティブ版の道 (`fmrb_host_send_text` → HOST_MSG_HID_TEXT、`FMRB_KANA_MODE_IME`、`set_text_cursor`)
  をブラウザ版にも開け、入力のリングにテキストの種類を足し、見えない textarea で compositionend を受けて確定の文字を流す。
- ブラウザ版では自前の変換 (K2b・K3) は使わない (辞書を入れない)。ブラウザの IME が日本語入力のときは、端末のかなモードを
  使わなくても漢字まじりの文字が入る。英字の入力・ショートカット・今のキーの扱いは壊さない (IME が無効のときは今どおり)。
- エディタ・Shell・チャットのような入力欄で確認する。IME の候補の窓 (ブラウザの) が画面の近くに出るように、textarea の
  位置を `set_text_cursor` に合わせられるなら合わせる。

## 確認

`rake regress`、`rake regress:screens` (sim・ブラウザ版の基準が変わらないこと)、`rake regress:size`。
sim Modern の全画面エディタで変換の 1 行が見える。ブラウザ版で、ヘッドレスの Chromium に IME の確定 (composition の事象を
CDP などで注入できる範囲) を流してエディタ・Shell に漢字が入る。注入できない部分は手順を report に書き、ユーザの目視に回す。

## 触ってよい範囲

fmruby-core (入力・wasm/web・道具・文書)、fmruby-graphics-audio (1 の Linux の全画面の描画だけ)。各リポジトリで
コミット (develop)。push しない (親がする)。

## report

`doc/kanji_input/report/k3b_k4.md`。
