# 実装指示書 T1-T5: 段 3 (地の責任の一本化)

対象: 実装担当セッション。作業リポジトリ: fmruby-core。

先に読むもの (この順で):

1. plan_s3.md — 作業内容の本体。タスク定義・コード形・完了条件は全てそちら。
2. issues_s3.md — 背景。なぜこの形なのかの根拠。
3. instruction_u1.md の「進め方の約束」と「書き方の規則」 — **そのまま適用**。
   fmrb-ui.rb だけでなく、T3 で触る両基底 (fmrb-app.rb /
   fmrb_app_base_spinel.rb) の追加コードにも適用する。

この指示書は plan_s3.md に書いていないことだけを書く。

## 進め方

- タスクは T1 -> T2 -> T3 -> T4 -> T5 の順。報告は report/t1.md ... t5.md。
  段 2 の report/s1.md-s4.md とは別物なので上書きしない。
- **ユーザ確認の関門は 2 つ**:
  - T2 完了時: sim 検収 (デスクトップのダイアログ開閉、monitor Tasks) の
    結果を report/t2.md に書いて確認を待つ。境界の検査は目視でなく
    plan_s3.md の検収の項どおりピクセル走査で行い、走査結果を report に貼る。
  - T3 完了時: engine 2 構成 (標準 = Spinel カーネル / 全 mruby) のビルドと
    sim 結果、shell スクロールの費用の数字を report/t3.md に書いて確認を
    待つ。**縮小版 (`_repair_corners`) に落とすかの判断はユーザと相談して
    から**。勝手に判断して先へ進まない。
- コミットはタスク単位。T3 は 2 ファイル (mruby 基底 + Spinel 基底) を
  **必ず 1 コミット**にまとめる (片方だけ入ると engine 切替で挙動が割れる)。
  コミット前にユーザ確認、メッセージは英語。
- 検証環境の罠 (stale build の偽グリーン、sim 3 コンテナ再起動、.env の
  FMRB_HW_TARGET 復元) は instruction_u1.md の記載どおり。

## 書き方の規則への追加

- `paint_bg_rect` は poly 受信で呼ばれる固定名。**名前を変えない**。実装は
  素朴な描画列に限る (1 つでもコンパイル不能な実装があると Spinel ビルド
  全体が落ちる)。実装側の契約 (present しない・確保しない・サイズ/フォントを
  変えたら戻す) は plan_s3.md T2 の項。
- test/fmrb_ui/ 以下はホストの CRuby で走るので、**書き方の規則の対象外**
  (feed ヘルパの map やブロックは自由に使ってよい)。規則が縛るのは
  実機に載るソースだけ。
- 基底に足す `attach_ui` / `_invalidate_attached_uis` も規則の対象
  (反復は while、定常経路で確保しない)。`@attached_uis = []` は
  initialize でのみ確保する。

## タスクごとの補足

### T1

- 既存 112 項目の構成 (geometry / events のセクション分け) に合わせて
  events の後にイベント列のセクションを足す。既存の検査は変更しない。
- 4 番の回帰テストは、修正済みの現挙動 (down で 1 回だけ) を固定する形で
  書く。issues_s3.md の症状記述から逆に「壊れていたときに落ちる」ことを
  コメントで一言書いておく。

### T2

- **desktop の既定 engine は mruby** (Rakefile の FMRB_APP_ENGINE_DESKTOP)。
  system_desktop が Spinel コンパイルされるのは env で明示したときだけなので、
  `rake spinel:doctor` の指摘 0 に加え、`FMRB_KERNEL_ENGINE=spinel
  FMRB_APP_ENGINE_DESKTOP=spinel rake build:linux` で 1 回ビルドして
  ダイアログ開閉 1 場面を sim で確認する (paint_bg_rect の poly ディスパッチ
  は desktop=spinel でしか実行されない。生成が通ることと動くことは別)。
  同じビルドで Spinel コンパイルされるアプリのうち clear_user_area を呼ぶ
  もの (例: /app/debug/spinel_hello.app.rb) も 1 本起動し、枠が保たれる
  ことを見る (T3 が Spinel 基底に足したコードの実行確認)。
  常設の検収構成は 2 点のままでよい。
- デスクトップの paint_bg_rect の塗り色 0x01 (前面 canvas の透明キー) は
  system_desktop.app.rb 内の既存の定数/コメント (`@gfx.clear(0x01)` の箇所)
  と同じ出どころ。マジックナンバーで書かず、既存の定義に寄せる
  (無ければ定数を足す)。
- FakeBgPainter は fake_gfx.rb に置き、呼ばれた引数列を配列で記録するだけの
  素朴なものにする。

### T3

- shell の費用計測は「変更前後で同条件のスクロール連打」を比較する。
  方法は任せるが、**測り方と数字を必ず report/t3.md に書く** (体感の言葉だけ
  で済ませない)。sim の描画時間なら GFX STATS か Machine.board_millis 挟み。
- nsf/smf に残る冗長な手書き後始末は消さない (plan_s3.md「やらないこと」)。

### T4

- 起動ログの sweep 対象は FmrbUI を使うアプリだけでよい (デスクトップ +
  ダイアログ、monitor、shell、logviewer、kamon、picoruby、midi_apu、
  nsf_player、smf_player)。全アプリ起動までは要らない。
- warn の文字列はログ 1 行で部品 id と矩形が読める形に。書式は plan_s3.md の
  例のとおりでよい。

### T5

- fmrb-app-new skill の実体ファイルの場所をまず確認する (リポジトリルート
  family-mruby 側の可能性がある)。fmruby-core の外を編集する場合は、編集は
  してよいがコミットはその置き場所のリポジトリで別途ユーザ確認。
- verify.md は「手順書」なので、T2/T3 の検収で実際に使った手つき
  (走査に使ったコマンド列まで) を書く。使っていない手順を書かない。

## 受け入れ条件 (段 3 全体)

- plan_s3.md「検収 (sim)」の表 6 行が全て満たされ、report に証拠 (画像の
  パス、走査結果、ログ抜粋) がある。
- 標準 / 全 mruby の 2 構成でビルドと sim 起動が通る。
- 契約の収支が文書に反映されている: fmrb-ui.rb 冒頭コメント、
  issues_s3.md 補足の契約表、skill。
- `grep -n "\.each\|\.times\|&blk\|yield\|defined?" lib/add/picoruby-fmrb-app/mrblib/fmrb-ui.rb`
  が空のまま。
- `.env` の FMRB_HW_TARGET が作業前の値に戻っている。
