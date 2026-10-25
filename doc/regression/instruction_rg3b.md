# RG3b 指示書: RAM とフットプリントの表をコマンド 1 つで

> 状態: 指示 | 更新: 2026-10-25 | plan.md の RG3b。app_refactor の各段でサブが手でやっていた「target ごとに clean_all → build:esp32 → idf.py size を前後で比べる」を道具にする。端末の一覧は RG1 の `tools/regress/targets.rb` に寄せ、端末を足すと 1 行で対象に入る形にする。**実機は使わない**

言葉: 文書は常体、「確かめ」ではなく「確認」。コードのコメントは英語。

## 1. 作るもの

1. **ビルドの時点の表** (`rake regress:size`、名前は決めてよい): 端末の一覧 (targets.rb に、ファームを作る target 名 (TAB5・NARYAv3・NARYAv4・ATOMS3R・STICKS3 など) を持たせる) を順に作り、`idf.py size` の DIRAM・IRAM・flash (アプリの大きさ・区画の空き) を集める。`idf.py size-components` で部品ごとの増減も (前後で差のある上位だけ)。基準 (`test/regress/size_baseline.*`、git に入れる) と比べ、**内蔵 RAM (DIRAM・IRAM) が増えたら落とす**、flash は増分を表に出す (落とさない)。出力は端末ごとに 1 行の表 + 差のある部品の数行。
   - 作り直しの順番 (ターゲットを切り替えるときの `rake clean_all`、FMRB_HW_TARGET を環境変数で渡す、.env を変えない) は道具が受け持つ。終わったら build/ を元の種類 (今の Linux の TAB5 など、始める前の状態) に戻すか、戻さないなら最後に何が入っているかを出す。
   - 時間がかかる (端末ごとに数分〜十数分) ので、`ONLY=STICKS3` のように絞れること、前回の結果を書き出しておき、ビルドを省いて表だけ出す口もあるとよい。
   - Spinel のプログラムごとの静的な内蔵 RAM (R6 で FmrbText・HitAreas を入れると増える見込みの分) が分かる数字があれば、それも表に (ELF の .dram0.bss/.data のうち Spinel の生成物の分、など。取れる範囲で)。
2. **動かした時点の表** (`rake regress:run` など): sim (Modern と Retro) で決まった台本 (デスクトップの起動 → いくつかのアプリを起動して閉じる) を流し、起動ログの M1| の段の値、周期ダンプの IRAM free、アプリごとの VM プールの使用量 (fmrb_app: の行) を集めて基準と比べる。揺れがあるので、比べ方 (幅を持たせる) を決めて report に。sim を起こすので重い方の入口 (`rake regress:full` に入れる)。
3. **入口**: `rake regress` (安いもの) には入れない。`rake regress:full` に 1・2 を入れる。usage.md に追記。
4. **基準は今の develop で取る** (R6 の前)。

## 2. 確認

- 全端末で表が出て、基準と比べて差 0 (同じコミット)。
- わざと内蔵 RAM を増やす小さな変更 (例: 試しに static な配列を 1 つ足す) で落ちることを確かめ、必ず戻す。
- 流した時間 (全端末、1 端末)、出力の長さを report に。
- sim の台本が 2 回で同じ範囲に収まる (揺れの幅)。

## 3. 範囲・決まり

- **触ってよいもの**: fmruby-core (develop) の tools/regress/、test/regress/、rakelib/、doc/regression/usage.md。アプリ・gem・ファームのコードは変えない (わざと壊す確認は戻す)。
- **触らないもの**: 実機、sdkconfig (読むだけ)、`.env` の既定 (環境変数で渡す)、VPS、`~/asterism_keys/`。
- push・タグはしない (親がする)。`git commit -a` は使わない。件名 `tools:` `test:` など、本文英語、末尾 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`。doc/regression の指示書・report はコミットしない (親がする)。usage.md はコミットしてよい。
- 終わったら sim を片付ける。
- **止まる条件**: idf.py の出力の形が端末で違い集められない、sim の数字の揺れが大きすぎて比べられない (幅の決め方を選択肢で返す)。
- **report**: `doc/regression/report/rg3b.md` (常体): 作り、端末ごとの表 (今の値)、流した時間、揺れの扱い、わざと壊した確認、R6 の確認でどう使うか、残件、コミット。
