# 実装指示書 P3: gem と VM を上流の ext 機構に移す

対象: 実装担当のサブエージェント。前提: plan.md、report/p1.md (ext 版の試作と
ホストの書き方)、report/p2a.md (多重インスタンスの棚卸し)、report/p2c.md
(内蔵 RAM の処置)、report/p2b2.md (実機の基準値と `--no-inline-hot`)。
report は `report/p3.md` へ。

## 目的と決定事項 (ユーザ、2026-09-26)

- **ext 移行はやる**。狙いはコードを綺麗にすること:
  - gem 型 (fft / raycast / spinel_hello): FFI の迂回 (getter/setter の
    `ffi_func`、`:binstr` と `sp_net_bin_len` の流用、`--persistent-statics` と
    その規約) をやめ、型付きのエントリ関数を C から直接呼ぶ。
  - VM 型 (kernel / system_desktop / editor): `--no-main --entry` を
    `--ext-init` に置き換え、フォーク独自のライブラリ化 (`9aa7cdd`
    `--no-main`/`--entry`/`--inject`、`cafe659` `--persistent-statics`) を
    使わなくする。メッセージの poll の FFI (Ruby から C を呼ぶ向き) は変えない。
- **上流への追従はしない (様子見)**。固定点は `fmrb-next` (`0b350247`) のまま、
  その上で行う。
- **内蔵 RAM を増やさない** (速度より RAM を優先。多少の速度低下は許容)。
- 生成はすべて `--no-inline-hot` (強制インラインは実機のスタックを溢れさせた)。

## 段階

### P3a: フォーク側の土台 (clone `/home/kishima/fmrb/wt/spinel-rebase`)

`fmrb-next` から新しいブランチ `fmrb-ext` を切ってコミットする (push はしない)。

1. **ext の変換結果を 1 つのファームに複数リンクできるようにする**。今は
   40 シンボル (ランタイムのフック `sp_sym_to_s`、`sp_class_to_s`、`sp_exc_*`、
   `sp_proc_call` 等) が重複する (P0 / P1)。`SP_MULTI_CTX` の構成では、
   フォークの `e0fd181` が `--no-main` の出力について同じ問題を「TU の関数を
   インスタンス経由で呼ぶ」形で解いている。**ext の出力にも同じ経路を通す**
   (シンボルの改名方式ではなく、既存の多重インスタンスの仕組みに乗せる)。
2. **ext の init でインスタンスの状態を戻す**。`c7de66c` (`sp_reset_tu_statics`) と
   `SP_TU_NIL_SLOT` を ext の init でも効かせる。同じプログラムのインスタンスを
   作り直したとき、モジュールの状態が前回の値を指さないこと。
3. **ext ヘッダの include guard を TU ごとの名前にする** (F-7)。
4. ゲート: `make test` / `make bench` / `make test-multi-ctx` が `fmrb-next` と比べて
   退行なし。加えて、**ext の TU を 3 本以上 1 つの実行ファイルにリンクし、
   インスタンスを並行に動かす試験**を `test/multi_ctx/` に足す (P1 の
   raycast / fft の ext 版を題材にしてよい)。
5. フォーク側で `--no-main` / `--entry` / `--persistent-statics` を消すのは、
   P3c が終わって fmrb から使われなくなってから (P3 の最後に判断を書く。
   消すのは親とユーザの判断)。

### P3b: gem 3 本を ext に移す (fmruby-core 作業ブランチ)

- fmruby-core の作業ブランチ: develop から `feature/spinel-ext` を切る
  (P2b-2 のマージ後の develop。親が指示するまで着手しない)。
- P1 の `tool/spinel_ext_poc/` の形を本番に移す: 各 gem の `spinel/` に
  `*_kernel.rb` (型付きエントリ + 型推論用の `if __FILE__ == $0` ブロック)、
  native の C は `Init_<name>()` とエントリを呼び、**raise しうる呼び出しは
  全部 `_try` で包む**、String 引数は `sp_str_from_bytes` で作る。
- 消すもの: 各 gem の `*_ffi.rb`、`sp_net_bin_len` / `sp_ctx_ffi_bin_len` の流用、
  `--persistent-statics`。
- mruby 側から見た gem の API (`Raycast` / `FmrbFft` / `SpinelHello` の Ruby の
  メソッド) は変えない。`:ruby` バックエンドとの答え合わせ (raycast の画素、
  fft の値) が今と同じであること。
- `spinel-doctor` が ext の形 (`if __FILE__ == $0`) を behavior の ERR にする件
  (U-14) は、`rake spinel:doctor` の許可表で扱う。

### P3c: VM 型を `--ext-init` に移す

- kernel / system_desktop / editor の生成を `--ext-init` にする。エントリ関数の
  呼び方 (タスク起動時に instance を作り、1 回呼ぶ) を init に置き換える。
- `fmrb_spinel_host.c` / `fmrb_spx_*.c` の入口の整理。FFI (`fmrb_spx_*`) は変えない。
- 標準構成 (kernel=spinel、editor=spinel、desktop=mruby) と、CI が回す
  全部 Spinel の構成 (desktop も Spinel) の両方。

## 検証 (各段の終わりに)

- sim: P2b-2 と同じ操作 (起動、エディタの起動・打鍵・閉じる・再起動・打鍵、
  raycaster / spinel_hello / fft_bench、設定ダイアログ)。エラー 0。互換構成
  (全部 mruby) も起動と打鍵。全部 Spinel の構成も起動と打鍵。
- 実機 (P3b と P3c の終わり。つながっている P2 系の板。今は P4-Nano (NARYAv4)。
  `rake attach` は自分で実行してよい。区画は変えないので `flash:app` でよいが、
  storage を変えたら全体書き込みと /home の退避・復元):
  - Guru / abort 0 件
  - **待機時の内蔵 RAM 空きが P2b-2 の新 (150,020、P4-Nano) 以上**
  - **各タスクのスタックの最悪 Free が P2b-2 の新以上** (kernel 6,488 など)。
    生成関数の C フレームを逆アセンブルで P2b-2 と比べ、膨らんだものは原因を書く
  - 速度 (edit_lat、raycaster の cast、fft) を P2b-2 の新と比べる。
    大きく悪化したら記録 (直すかは親と相談)
- ESP32 のビルド: P4 (TAB5 / NARYAv4) と S3 (NARYAv3) の両方が通り、アプリが
  区画に入る。静的な D/IRAM を P2c の値 (S3 147,387 / P4 183,448) と比べ、増えていないこと。

## 作業の決まり

- `.env` はユーザの設定。書き換えず、コミットに含めない。対象はコマンドラインの
  環境変数で渡す。最後に `git diff .env` が作業前と同じことを確かめる。
- コミットは自分が変えたファイルだけをパスで指定する。develop へのマージと push はしない。
- worktree を作ったら消す。
- sdkconfig / sdkconfig.defaults は編集禁止。

## 止まる条件

- P3a の 1 で、`e0fd181` の経路に ext の出力を乗せるのに codegen の設計変更が要ると
  分かったとき (案を書いて止まる)
- 内蔵 RAM かスタックの最悪 Free が P2b-2 より悪化し、原因が ext 移行そのものに
  あるとき
- Guru / abort が出たとき (ログとバックトレースを残す)

## report/p3.md に書くこと

- 段ごとのコミット (フォーク / fmruby-core)
- 消えたコードと足したコードの行数 (gem ごと、VM ごと)。狙いの「綺麗になった」を数字で
- 検証の表 (sim、実機、ビルド、内蔵 RAM、スタック、速度)
- フォークから `--no-main` / `--entry` / `--persistent-statics` を消せるかの判断材料
- 見立てと違った点・撤回した仮説
- **PR 候補**節 (台帳は編集しない。F-2 のシンボル分離、F-7 の include guard など、
  上流にも意味のあるもの)

## 受け入れ条件

- gem 3 本と VM 3 本が ext 機構で生成され、FFI 迂回と `--no-main` / `--entry` /
  `--persistent-statics` を fmrb が使っていない
- sim の 3 構成と実機で上の検証が通る
- 内蔵 RAM (静的・待機時) とスタックの最悪 Free が P2b-2 / P2c より悪化していない
