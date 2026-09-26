# 実装指示書 P1: 上流の spinel で gem 型を試作する

対象: 実装担当のサブエージェント。前提: plan.md、report/p0.md (特に
「P1 / P2 の指示書に入れるべきこと」)。report は `report/p1.md` へ。

## 前提の確認結果と決定事項

- **上流の固定点は `01521b1e`** (`~/dev/spinel`、detached、ビルド済み)。
  別の commit に動かさない。
- **fmrb の sim には載せない** (親の決定、2026-09-26)。fmrb の Spinel
  ランタイム (`components/fmrb_spinel_rt`) はフォーク版で、kernel と
  gem が全部それにリンクしている。上流のランタイムは同じ実行ファイルに同居
  できない (シンボルが全部重なる)。sim への組み込みは rebase 後 (P2/P3) の
  仕事とし、P1 は **fmrb のビルドの外に置く試験用ホスト** で確かめる。
  plan.md の段階 1 の受け入れ条件 (「Linux sim でピクセル一致」) は、
  この形に読み替える。
- 目的は「gem 型を上流の ext 機構 (`--ext-init` / `--ext-entry`) で書くと
  どうなるか」を実物で確かめ、P3 で gem を移すときの形を決めること。

## 作業場所と触ってよい範囲

- 新規ディレクトリ `fmruby-core/tool/spinel_ext_poc/` を作り、試作はすべて
  そこに置く (Ruby の entry、ホストの C、実行スクリプト、README)。
  **ビルドに配線しない** (Rakefile・CMake・components・main・lib は触らない)。
- `lib/add/picoruby-fmrb-raycast/mrblib/raycast_core.rb` は**読むだけ**。
  試作からは相対パスで読む (コピーすると 2 つの版がずれる)。
- 生成物・バイナリは scratchpad (`/tmp/claude-1000/-home-kishima-fmrb-family-mruby/a0ea00a0-6754-4693-ae2d-f05782a9784d/scratchpad/spinel_p1/`) に出す。
  tool/ に生成物を置かない。
- `~/dev/spinel` は読むだけ (checkout を動かさない。PR 準備の別作業が別の
  clone で並行している)。
- fmruby の build/、sim、実機、`.env` は触らない。git commit・push はしない。
- 周辺ツールは Ruby で書く (実行スクリプトも Ruby。CLAUDE.md の方針)。
  C のコメントは英語。

## T1: raycast を型付きエントリで書く

- plan.md「gem 型: 上流 ext に乗せ替える」の形で `raycast_kernel.rb` を書く。
  エントリは少なくとも `load_map(map, w, h)` と `cast(px, py, pa)`。
  状態 (作ったコア) は大域変数ではなく、エントリ間で自然に残る形で持つ。
  `if __FILE__ == $0` の型推論用ブロックを置く。
- `spinel --ext-init Init_raycast --ext-entry ...` で C とヘッダを出す。
- **文字列リテラルの凍結**: raycast_core.rb にリテラルを書き換える箇所が
  無いか先に確かめる。あれば report に書く (core は直さない。直す必要が
  あるなら止まる)。

## T2: 試験用ホストで答え合わせ

- C のホストが `Init_raycast()` → `load_map` → `cast` を呼び、出力バイト列を
  書き出す。
- 同じ map と姿勢の組で、CRuby が raycast_core.rb を直接動かした出力と
  **バイト単位で一致**することを確かめる。姿勢は、固定の数十通り
  (壁に正対・斜め・角・マップ端・全方位 360 度) + 乱数で 1,000 通り以上。
  map は fmrb のゲームが実際に使うもの (`flash/app/game/` の raycaster が
  読む地図) と、小さな手書きの地図の 2 種。
- map を途中で差し替える (`load_map` を 2 回呼ぶ) ケースを含める。
- raise の越境 (`Init_raycast_try` 等で、不正な map のときのクラス名と
  メッセージを受ける) を 1 ケース入れる。
- 64bit と 32bit (`-m32`) の両方で回す。32bit ランタイムの作り方は
  report/p0.md の手順 (bin/spinel は 64bit のまま `--cc='cc -m32'`、
  ランタイムは scratch clone で `make CC='cc -m32'` の lib だけ)。

## T3: 今の作りとの比較を記録する

report に次を表で残す。

- 境界の比較: 今 (FFI 10 本 + `:binstr` + `sp_net_bin_len` 流用 +
  `--persistent-statics`) と ext 版で、Ruby 側・C 側のそれぞれの行数と、
  消えるもの・新しく要るもの。
- 生成 C の大きさ (`size -A` の .text / .data / .bss、x86-64 と -m32)。
  **`.data` は凍結リテラルの影響を見るため必ず**。今のフォーク版で生成した
  raycast_entry.c との比較も付ける (フォークの bin は `vendor/spinel/bin/spinel`、
  読むだけ)。
- 速度の目安: 同じ姿勢 1,000 回の `cast` の時間 (ホストでの相対比較。
  実機の値ではないと明記)。

## T4 (余力があれば): fft でも同じことをする

`lib/add/picoruby-fmrb-fft/spinel/fft_spinel.rb` を同じ形にして、CRuby 版と
数値一致 (Float は許容誤差を決めて書く) を確かめる。Float が境界を越える
ときの型 (ヘッダ上の C 型) を記録する。時間が足りなければ T4 は省いて
report に「未着手」と書く。

## 止まる条件

- raycast_core.rb を変えないと ext 版が作れない (凍結リテラル、型推論の
  失敗など) と分かったとき
- 上流の ext 機構で、今の gem の機能 (map の差し替え・状態の保持・バイト列
  の往復) のどれかが表現できないと分かったとき
- sudo やパッケージの導入が要るとき
- 触ってよい範囲の外を変える必要があると分かったとき

## report/p1.md に書くこと

- 使った上流 commit、ツールチェーン
- T1-T3 (と T4) の結果。一致しなかったケースがあれば、姿勢・map・差分
- P3 で gem を移すときの形の提案 (ファイル構成、ホスト側 C の書き方、
  rake の生成手順をどう変えるか)
- 見立てと違った点・撤回した仮説
- **PR 候補**: 上流の不具合や移植の穴を見つけたら、内容・最小再現
  (fmrb 非依存、scratchpad に置いてパス)・確認した上流 commit を書く。
  台帳 `pr_candidates.md` は編集しない (親が移す)

## 受け入れ条件

- raycast の ext 版が、64bit と 32bit の両方で CRuby とバイト単位で一致する
  (1,000 通り以上、map 差し替えを含む)
- 境界の比較表と `.data` を含む大きさの表がある
- `tool/spinel_ext_poc/` の README の手順だけで、別の人が再実行できる
