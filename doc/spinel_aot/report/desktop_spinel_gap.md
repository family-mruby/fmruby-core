# FMRB_APP_ENGINE_DESKTOP=spinel が通らない理由と修正 (2026-08-21)

U2 の report が「唯一の残りブロッカー」とした `load_shortcuts` の型推論は、
**最初に当たる壁であって、壁はそれ 1 枚ではない**。前半は調査 (コードを
変えずに壁を数えた)、後半はその修正と、**生成が通った後に出た 3 つの穴**。

## 調べ方

`gen_app_combined.rb system_desktop` で結合ソースを作り、`vendor/spinel/bin/spinel`
に直接かけ、止まった箇所に**その場しのぎの stub** を足してもう一度かける、を
通るまで繰り返した。stub は結合ソースにだけ入れた。1 回の生成は 30-60 秒。

この手順は、ビルドを直すときの検証にもそのまま使える (`rake` を回すより速い)。

## 結果: 壁は 9 枚、種類は 2 つ

通過した順。右端は、その呼び出しをデスクトップに入れたコミット。

| # | 止まった箇所 | 種類 | 入れたコミット |
|---|---|---|---|
| 1 | `load_shortcuts` の `e = entries[i]; e["key"]` | 型推論 (書き方で回避可) | 57e61dc (旧) |
| 2 | `FmrbGfx#draw_wallclock` | Spinel 基底に無い | a8c1ba3 2026-08-06 |
| 3 | `FmrbGfx#draw_free_iram` | 同 | a6931fb 2026-08-06 |
| 4 | `FmrbApp.ble_state` | 同 | a6931fb 2026-08-06 |
| 5 | `FmrbApp.wifi_connected?` | 同 | a862aba 2026-08-06 |
| 6 | `GC.step` | 同 (Spinel に GC.step が無い) | a862aba 2026-08-06 |
| 7 | `FmrbGfx#_draw_text_hybrid` (launcher が直接呼ぶ) | 同 | 60d61e8 2026-08-07 |
| 8 | `FmrbApp.ps_gen` | 同 | a862aba 2026-08-06 |
| 9 | `FmrbConst.bt_mac` | 同 (FmrbConst はモジュール定数のみ生成) | ddb6b0d 2026-08-16 |

9 枚目を stub したところで **Spinel の生成が通った**。つまり型推論の壁は
#1 の 1 件だけで、残り 8 件は全部「mruby 側に C で足した API を Spinel 基底に
足していない」という同じ穴である。U2 T1 で埋めた `_send_audio_note` /
`idle_gc=` / `closable` と同じ種類。

時期もそろっている。Spinel デスクトップが最後に通ったのは Phase 4 完了
(2026-07-24) で、8 件のうち 7 件は 2026-08-06〜07 の 2 日間 (GC のこぎり波の
修正、メニューバーの IRAM/BLE 表示、時計の C 化、ランチャーの差分描画) に
入っている。**その週から Spinel デスクトップはビルドされていない**。

## #1 (型推論) は書き手側で回避できる

report/u2.md は「dual-safe な回避はまだ無い」としたが、ある。

```ruby
# 止まる (while + 添字)
while i < entries.size
  e = entries[i]
  key = e["key"]        # unsupported call `[]` recv=LocalVariableReadNode
  ...
end

# 通る (each + ブロック引数)
entries.each do |e|
  key = e["key"]
  ...
end
```

同じ `FmrbApp.config` の戻りを `each` で回す `launcher_exclude_dirs`
(launcher.rb) は、probe の中で一度も止まらなかった。違いは「要素を添字
`[]` で取り出すか、ブロック引数で受けるか」だけで、**添字で取り出した値には
要素型が伝わらず、ブロック引数には伝わる**。`load_shortcuts` は起動時に
1 回走るだけなので、ブロック呼び出しのコストは問題にならない (hot path で
`while` を使う規則の例外として妥当)。

ruby_writing_constraints.md の B 表のこの行は、「dual-safe 回避: 起動時
1 回の経路なら `each` で受ける」に直せる。fork 側の課題 (添字アクセスにも
要素型を伝える) はそのまま残してよい。

## 直すなら

順番は「Spinel 基底の 8 件を足す → #1 を `each` にする → ビルド → sim」。
基底の 8 件は U2 T1 と同じ作り (FFI が要るものは `fmrb_app_ffi.rb` と
`main/app/fmrb_spx_app.c` に対で足す):

- `draw_wallclock` / `draw_free_iram` / `_draw_text_hybrid`: gfx の C shim。
  `_draw_text_hybrid` は `draw_text_mixed` の別名で足りる (launcher が
  private 名を直接呼んでいるので、launcher 側を `draw_text_mixed` に直す
  ほうが筋がよい)。
- `ble_state` / `wifi_connected?` / `ps_gen`: 既存の `ble_start` /
  `wifi_info` / `ps` の隣に、同じ C 関数を読む shim。
- `GC.step`: Spinel の GC は incremental でないので no-op でよい (`idle_gc`
  と同じ判断)。
- `FmrbConst.bt_mac`: `gen_const_rb.rb` はモジュール定数しか出さない。
  実行時に変わる値なので定数にはできず、`FmrbSpxApp` 経由の shim を
  `FmrbConst` にメソッドとして足す形になる。

## 再発させないために

この穴は、mruby 側に C の API を足したときに Spinel 基底へ同じものを
足し忘れることで開く。ビルドが片側ずつなので気づけない。最小の歯止めは、
**CI の test-host ジョブで `FMRB_APP_ENGINE_DESKTOP=spinel` の生成だけ
(C コンパイルまでは不要) を回す**こと。`spinel` の生成は 1 分で済み、
この 9 件は全部そこで止まる。

## 修正 (同日)

上の順番どおりに直した。Spinel 基底に 8 件 (`draw_wallclock` /
`draw_free_iram` は gfx の C shim、`ble_state` / `wifi_connected?` / `ps_gen` /
`FmrbConst.bt_mac` は app の C shim、`GC.step` は no-op、`_draw_text_hybrid`
は launcher 側を `draw_text_mixed` に)、`load_shortcuts` は `each` に。
これで **生成は通った**。しかしそれで終わりではなかった。

### 生成が通った後に出た 3 つの穴

生成器は通すが C コンパイルか実行で落ちるものが 3 種類あった。
「生成だけを CI で回す」案は、このうち 1 つしか捕まえられない。

| # | 症状 | 原因 | 直し |
|---|---|---|---|
| A | C コンパイルエラー (`void value not ignored` / `sp_FmrbGfx *` と `sp_RbVal` の代入不一致) | `fmgr_activate` と `handle_file_manager_key` の分岐の末尾が FmrbGfx (present の戻り) と void leaf で揃わない。B 表の既知パターン (8/13-16 に入ったキー操作) | メソッド末尾に `nil` |
| B | 最初の打鍵で `NameError: uninitialized constant FmrbConst::KEY_F10`、デスクトップ終了 | 生成する `FmrbConst` が `KEY_UP` / `KEY_DOWN` の 2 つだけ。未定義定数は**生成時に警告が出ず**、参照した瞬間に raise する | `gen_const_rb.rb` が const.c の `KEY_*` 87 件を全部出す。さらに `gen_app_combined.rb` / `gen_kernel_combined.rb` が、結合ソースの `FmrbConst::X` を生成モジュールと突き合わせ、**無ければ abort** する |
| C | File Manager を開くと **segfault** (`sp_PolyArray_sort_bang` → `sp_sprintf("comparison of %s ...")` の strlen) | `scan_file_manager_dir` のループ変数 `e` が、同じメソッドの `rescue => e` と同名。Spinel はローカルの型を名前ごとにメソッド全体で 1 つ持つので `e` は Exception、`names` は例外の poly 配列になり、`sort` が比較不能。比較不能の報告文字列を作る途中でゴミを deref して落ちる。gdb を同居コンテナで attach して特定 | ループ変数を `ent` に改名 (file_manager / file_selector。launcher と editor は rescue 変数名が違うので無事) |

B と C は **mruby では正しく動くコード**がそのまま Spinel で落ちる例で、
どちらも ruby_writing_constraints.md の B 表に足した。

### 確認したこと

- `FMRB_KERNEL_ENGINE=spinel FMRB_APP_ENGINE_DESKTOP=spinel
  FMRB_APP_ENGINE_EDITOR=spinel rake build:linux` が通る (x86-64 確認済)。
- sim (426x240): メニューバーの `---KB` / BLE / wifi / 時計が出る、ランチャーの
  ラベル (mixed 描画) が出る、`s` ショートカットで shell が立つ
  (`load_shortcuts` 経路)、About に `BT-MAC -` が出る、File Manager を
  開いて矢印と Enter で /bin に入れる、Esc で閉じる。例外・segfault なし。
- 標準構成 (全 mruby) で同じ操作を回して退行なし。`rake test` 通過。
- ランチャーのアイコンが出ないのは既知 (Spinel の画像描画は未実装)。

### 歯止め

- CI に `spinel-gen` ジョブを足した (kernel / desktop / editor の生成)。
  上の 9 枚と穴 B (生成器の abort) はここで止まる。**穴 A (C コンパイル) と
  穴 C (実行時) は止まらない**。A まで捕まえるなら build-linux を Spinel
  構成でもう 1 回回す必要があるが、それは docker が要る重い側なので
  今回は入れていない。C は書き方の規則 (B 表) で防ぐしかない。
