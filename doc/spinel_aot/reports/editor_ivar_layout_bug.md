# Spinel エディタが起動直後に TypeError で死ぬ件 (ivar レイアウトの食い違い)

標準構成 (Spinel カーネル + Spinel エディタ) で FM-Editor を起動すると、
`on_create` の中で `TypeError: Object can't be coerced into Integer` を出して
即死する。mruby 構成のエディタは無事。2026-08-25 に原因特定・修正・検証まで
完了した。

## 犯人

`c0b5e43 "app base: wiping the user area puts the frame and the widgets back"`。
`FmrbApp` に `@attached_uis` を 1 個足しただけの 28 行の変更で、Ruby 側に
論理的な誤りは無い。**壊したのは Spinel の型推論**であり、この変更は引き金に
すぎない。

## 機序

Spinel はクラスごとに ivar の C 構造体を起こし、**継承したメソッドは
`sp_FmrbApp_initialize((sp_FmrbApp *)self)` のように基底型へキャストして呼ぶ**。
これが成り立つのは、基底の構造体が派生の構造体の**先頭一致 (共通前置列)**
である間だけ。Spinel 自身もこの不変条件を意識していて、`analyze.c` の
`inherit_members` は「[親の ivar..., 自分の ivar...] の cast-compatible な
並びを保つ」と書いてある。

c0b5e43 の後、生成 C はこうなっていた。

```
struct sp_FmrbApp_s  { ... sp_PolyArray * iv_attached_uis; ... }   /* poly_array */
struct sp_EditorApp_s{ ... sp_RbVal       iv_attached_uis; ... }   /* poly       */
```

**同じ ivar が親と子で別の型**になり、`sp_PolyArray *` (ポインタ) と
`sp_RbVal` (タグ付き共用体) は大きさが違うので、**この 1 個より後ろの
フィールドが全部ずれる**。結果、基底の `initialize` が `if @canvas` の枝で
書いた `@window_height` / `@user_area_width` / `@user_area_height` は、
`EditorApp` から見ると全く別のフィールドの位置に着地する。エディタの
`recompute_layout` がその未初期化スロットを整数演算に入れて TypeError。

gdb で裏を取った実測値 (breakpoint は `sp_raise_cls`):

```
#3 sp_EditorApp_recompute_layout
#4 sp_EditorApp_on_create
  iv_user_area_height = {tag = 188, ...}   ゴミ
  iv_user_area_width  = {tag = 238, ...}   ゴミ
  iv_window_height    = {tag = 200, ...}   ゴミ
  iv_canvas           = {tag = 3 (BOOL)}   ゴミ
```

「基底が書いた値が子から読めない」ので、`@canvas` すらまともに読めていない。

### なぜエディタだけか

型がずれるのは、子側の `@attached_uis` が `poly_array` からさらに `poly` まで
広がった場合だけ。デスクトップ (`SystemDesktopApp`) では子も `poly_array` の
ままで、親子の構造体は完全一致していた。**同じ変更でも、そのクラスの他の
コードが型推論をどこまで広げたかで、壊れるクラスと壊れないクラスに分かれる**。
一時的に作った小さな Spinel アプリで `clear_user_area` を呼んでも再現しない
のはこのため。

## コンパイラ側の原因

`src/analyze.c` の解析の終盤はこの順に走る。

1. ivar 型の**上方伝播** (子 → 親。`ty_unify` で親を広げる。16 回の不動点)
2. 局所変数・ブロック引数を poly へ倒す後始末
3. `inherit_members` で ivar の名前を子へ再登録
4. 最終ループ: `infer_ivar_types` + `infer_inherited_ivars` +
   `reconcile_locals_reading_ivars` を 8 回

問題は 4 の中身で、**`infer_inherited_ivars` は親 → 子の一方向しかやらない**。
2 で局所が poly になったことを受けて 4 の `infer_ivar_types` が子の ivar を
さらに広げても、その結果を親へ返す経路がここには無い。1 の上方伝播はもう
終わっているので、親は古い狭い型のまま取り残される。

つまり **「最後に型を広げられるのは子だけ、しかしその後で親へ返す機会が無い」**
という穴。今回の `@attached_uis` はちょうどそこに落ちた。

## 修正 (フォーク `fmrb-dev` の `ca0709c`)

上方伝播を関数に括り出し、最終ループの中でも回す (`analyze.c`)。

```c
for (int it = 0; it < 8; it++) {
    int ch = infer_ivar_types(c);
    ch |= infer_inherited_ivars(c);      /* 親 -> 子 */
    ch |= propagate_ivars_up(c);         /* 子 -> 親 (追加) */
    ch |= reconcile_locals_reading_ivars(c);
    if (!ch) break;
}
```

`ty_unify` は広げる方向にしか動かないので単調で、既存の不動点と同じ性質を持つ。
修正後、追跡ログ (`SP_IVWATCH=attached_uis`) は最終ループで親が広がる様子を
そのまま出す。

```
[ivwatch attached_uis] unknown_backstop   0(unknown)    -> 18(int_array)
[ivwatch attached_uis] usage_push         18(int_array) -> 21(poly_array)
[ivwatch attached_uis] late_up_merge      21(poly_array) -> 43(poly)      <- 追加分
```

## 検証

- Spinel 単体: `make test` 1997 pass / 1 fail。**修正前と同じ**
  (`nilclass_bool_ops_conversions` は元から落ちている Complex の表示差)。
- 生成物の総点検: `gen/*.c` 7 本すべてについて、`(sp_X *)self` へキャストして
  呼ばれる基底 12 種と、それを継承しうる全構造体の先頭一致を機械的に照合。
  **不一致 0**。修正前はエディタの 1 件だけが不一致だった。
- 実機能: `rake build:linux` → sim 起動 → `default/editor` を spawn。
  ウィンドウ・メニュー・本文・ステータス行がすべて正しく出て、"hello" を
  打鍵して `Ln 1, Col 6 *` まで進む。ログの TypeError は 0 件。

## 再発防止

- fmruby-core の `CLAUDE.md` 「テスト」節に、標準構成の sim 検証には
  エディタ起動 1 回を含める旨を明記した。検収表や個別の指示書に書くと
  次の指示書へ引き継がれないため、毎回読まれる場所に置く。
- `ruby_writing_constraints.md` への追記はしない。**Ruby の書き方で避けられる
  類ではない**ため (基底に ivar を足しただけで、書き方に問題は無い)。
  避けようがない以上、直すべきはコンパイラの側。
- **codegen に先頭一致の検査を入れた** (`622750c`)。構造体を出す直前に
  各クラスの祖先を辿り、祖先の ivar が名前・順序・C 型まで派生の先頭一致で
  あることを確かめ、崩れていればビルドを止める。今回の不具合は**黙って
  壊れる**のが最大の問題だったので、同種の推論バグが将来入っても実行前に
  止まる。

  検査自体の生存も確かめた。`analyze.c` の直しを一時的に外してビルドすると、
  エディタの生成でこう止まる。

  ```
  spinel: internal error: @attached_uis is poly in EditorApp but poly_array in
  its ancestor FmrbApp; the differing field widths shift every later member, and
  inherited methods writing through the (sp_FmrbApp *) cast would corrupt the
  object
  ```

  原因の ivar・両クラス・両方の型・キャスト先が 1 行に出るので、gdb を持ち出す
  必要は無い。直しを戻せば通る。既存のプログラムで誤検知するものは無い
  (Spinel の `make test` 1997 本と fmruby の生成物 7 本すべて通過)。

## 既知 (別タスク)

- **Spinel アプリの例外でバックトレースが空**。今回、`Exception caught:
  TypeError` のログだけでは落ちた場所が全く分からず、gdb を当てるまで
  何も分からなかった。最低限「落ちた手続き名」だけでも出せば、次回は
  gdb を持ち出さずに済む。
