# DS2 指示書: デスクトップのアイコンの先頭にランチャーを置く

> 状態: 指示 | 更新: 2026-10-24 | 既定のデスクトップのアイコンの並び (`flash/usr/share/desktop/desktop.toml`) の先頭に、ランチャーを開くアイコンを足す。ランチャーはアプリではないので、`desktop/file_manager` と同じ特別な項目 (例 `desktop/launcher`) として扱う (2026-10-24 ユーザ決定)。DS1 の続き (plan.md、report/ds1.md)

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 決まっていること

- 既定の並びの**先頭**にランチャー。ほかの並びは変えない。
- `/home/desktop.toml` (利用者が保存した並び) があるときは、**自動で差し込まない** (利用者の並びを尊重する)。ランチャーの「デスクトップに置く / 外す」の操作で、ランチャーのアイコンも置ける・外せるなら、その形にする (できなければ report に)。
- ダブルクリック (タップ) で、メニューバーやショートカット `l` と同じランチャーが開く。

## 2. 作るもの

1. デスクトップ (system_desktop、DS1 で作ったアイコンの並び) に、特別な項目 `desktop/launcher` (名前は今の `desktop/file_manager` の流儀に合わせて決めてよい) を足す。表示名 (英語・日本語) とアイコンの絵。
   - アイコンの絵は、今ある資源 (メニュー・ランチャーで使っている絵、`usr/share/icon` など) を使えるなら使う。新しく描くなら今のアイコンと同じ形式・大きさ・作り方 (生成の道具があればそれ) で。
2. `flash/usr/share/desktop/desktop.toml` の先頭に足し、コメントを直す。
3. DS1 の文書 (plan.md の決まっていることの節) に 1 行反映 (親が最後に直してもよい)。

## 3. 確認

- Linux sim の Modern (426x240) と Retro (320x240、`FMRB_HW_TARGET=NARYAv3` などの Retro の構成。.env は環境変数で上書きし、最後に戻す) で、アイコンの先頭にランチャーが出て、ダブルクリックでランチャーが開く (sim_input の dclick)。Retro で縦に収まる (はみ出し・重なりが無い) ことを画面で。
- ブラウザ版 (web_up) でも同じ。
- `/home/desktop.toml` がある状態では差し込まれないこと。
- 内蔵 RAM・描画の重さが目に見えて変わらない (Retro の定常の空きを、sim のログ等で前後比較できる範囲で)。
- 標準構成の sim 検証の決まり (エディタを 1 回起動して 1 打鍵) と `rake test`。

## 4. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の system_desktop まわり、`flash/usr/share/desktop/desktop.toml`、アイコンの資源、文書。
- **触らないもの**: sdkconfig、`.env` の既定 (環境変数で)、Tab5・StickS3・シリアル (実機は使わない)、VPS・公開中のブラウザ版、ほかのアプリの振る舞い。
- push・タグはしない。`git commit -a` は使わない。件名 `desktop:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc (この指示書・report) はコミットしない (親がする)。
- 終わったら sim・web を片付ける。build/ は残ってよい。
- **止まる条件**: 内蔵 RAM が増える (Retro)、ランチャーをデスクトップから開くのに範囲外 (カーネルの大きな変更) が要る、Retro で収まらず並びを変える必要がある (選択肢を書いて返す)。
- **report**: `doc/desktop_icons/report/ds2.md` (常体): 作り、アイコンの絵の出どころ、各画面の確認と画像、RAM、残件、コミット。
