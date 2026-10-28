# 窓を並べるアプリ (Ruby で書くタイル型の窓管理)

> 状態: 計画済 | 更新: 2026-10-28 | 他のアプリの窓を動かす API を、宣言したアプリ 1 つにだけ開く。利用者が .app.rb 1 本でタイル型の窓管理を書ける。主な対象は Linux 版 (コンポジタ) とブラウザ版

## 目的

- 窓の並べ方を、利用者が Ruby のアプリとして書いて差し替えられるようにする。
- Linux 版 (linux_pc_wl) では、Linux の窓も代役アプリ (linux_window.app.rb) を
  通して同じ API で並ぶ。Firefox やターミナルを Ruby のタイル型の窓管理で
  並べることが、普段使いの環境の改善と、外向けの小ネタ (「デスクトップを
  Ruby で自作できる」) の両方になる。

## 今の作り (出発点)

- 窓の管理はカーネルの Ruby (main/prebuild_scripts/kernel/fmrb_kernel/) にある。
  ドラッグ・大きさの変更・前面化・フォーカスは input_router.rb と
  window_manager.rb。窓を実際に動かすのは C の小さな関数
  (`_get_window_list`、`_update_window_position`、`_update_window_size`、
  `_bring_to_front`) だけ。
- アプリが動かせるのは自分の窓だけ (`set_window_position`)。
- アプリ→カーネルの依頼 (`focus_app`、`kill` など) と、カーネル→デスクトップの
  知らせ (`apps_changed`、`focus_changed`) の通り道はすでにある。
- Linux の窓は代役アプリの中身の位置に、コンポジタが置き直す。代役アプリを
  動かせば Linux の窓も動く。
- 最大化と Super+矢印 (L13) は C (native/backend/native_winops.c) で、
  これも上の関数を呼んでいるだけ。

## 方針

- **窓を並べるアプリ (arranger) は同時に 1 つ**。`.app.toml` で宣言したアプリ
  だけが他の窓を動かす依頼を出せる。2 つ目の宣言アプリは起動を断る。
- 新しい C の関数は足さない見込み。カーネルの Ruby に依頼の受け口と知らせを
  足す (共有コードの変更はカーネルの Ruby と FmrbApp の Ruby 側だけ)。
- arranger が落ちたり止まったりしたら、窓はその場に残り、手でのドラッグに
  戻る (今の動き)。並べ直しはしない。
- arranger が動いている間も、手でのドラッグ・大きさの変更はできる
  (arranger に知らせが行き、並べ直すかどうかは arranger が決める)。
- 全画面のアプリ、デスクトップ、ダイアログ・エラーの窓は並べる対象に入れない
  (一覧には載せ、印で区別する)。

## スコープ

### API (案)

アプリ側 (宣言したアプリだけが使える):

| 呼び出し | 中身 |
|---|---|
| `windows` | 窓の一覧。pid、名前、位置・大きさ、前後、Linux の窓か、並べてよいか、最小の大きさ |
| `place_window(pid, x, y, w, h)` | 位置と大きさをまとめて変える |
| `raise_window(pid)` / `focus_window(pid)` | 前面化・フォーカス |
| `work_area` | 並べてよい領域 (メニューバーを除いた画面) |

アプリへの知らせ (`on_control` に届く):

| cmd | いつ |
|---|---|
| `window_opened` / `window_closed` | 窓が増えた・減った |
| `window_moved` | 手で動かした・大きさを変えた |
| `focus_changed` | フォーカスが移った |
| `screen_changed` | 画面の大きさが変わった (Linux 版の出力の変更、ブラウザの窓の大きさ) |
| `arranger_key` | arranger 用のキーが押された |

### キー

- arranger 用の修飾キーを 1 つ決め、それとの組み合わせはカーネルが arranger に
  回す (他のアプリには届かない)。

### 見本

- `flash/app/tool/tiler.app.rb`: 縦に 2 分割から始まる、よくあるマスタ+スタック型。
  修飾キー+矢印でフォーカスを移し、修飾キー+Enter で主の窓と入れ替える。
  数十行で読めることを見本の条件にする。

### 対象の端末

- 主: Linux 版 (linux_pc_wl)、ブラウザ版。Modern (Tab5) でも動く。
- Retro (320x240) と Cardputer Zero (320x170) は対象外 (動かしても止めないが、
  確認はしない)。

## 段取り (案)

| 段 | 中身 |
|---|---|
| W1 | カーネルに受け口と知らせを足す。宣言の鍵、1 つだけの制限、キーの横取り。sim とブラウザ版で、見本の前段の小さなアプリで確認 |
| W2 | 見本の tiler.app.rb。Linux 版で Firefox とターミナルが並ぶこと |
| W3 | arranger がいる間は L13 の C 側を止める。services.toml の見本の項目。文書 (reference と spec.md 3.1 節の鍵) |

## 受け入れ条件 (案)

- 宣言していないアプリが他の窓を動かそうとすると、断られてログに出る。
- 見本を起動すると、開いている窓が並び直り、窓を開く・閉じるたびに並び直る。
- Linux 版で Linux の窓 (Firefox、ターミナル) も同じに並ぶ。
- 見本を止めると窓はその場に残り、手のドラッグが今までどおり効く。
- `rake regress` が通る。内蔵 RAM は増えない (カーネルの Ruby の追加だけで済む見込み)。

## 決定事項 (2026-10-28)

- **修飾キー**: Linux 版は Super、ブラウザ版は Alt (Super は Windows に取られる)。
  設定で変えられる。
- **L13 の最大化・Super+矢印**: arranger が動いている間は C 側
  (native_winops.c) を止め、全部 arranger に回す。arranger を止めれば
  今の動きに戻る。
- **大きさを変えられない窓**: 並べる対象から外して浮かせる (ダイアログと同じ扱い)。
- **自動起動はサービスの仕組みを使う**: /etc/services.toml の `app =` 項目
  (doc/user_extension/services/plan.md「アプリの自動起動」)。見本の項目を
  `enable = false`、`restart = true` で載せ、使う人が /home/services.toml で
  `enable = true` にする。arranger 用の新しい起動の仕組みは作らない。
  異常に止まったときはホストが起こし直し、その間は窓がその場に残る。

## 未確定事項

1. **宣言の鍵の名前**: `window_arranger = true` か、別の名前か (W1 の指示書で決める)。
2. 見本の項目を Linux 版 (linux_pc_wl) の既定だけ `enable = true` にするかは、
   W2 で使ってみてから決める。
