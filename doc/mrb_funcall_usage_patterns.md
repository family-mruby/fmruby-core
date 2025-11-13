# mrb_funcall使用パターン分析レポート（改訂版）

picoruby-esp32配下の**既存の実績ある**mrbgemにおけるmrb_funcallの使用パターンを徹底調査し、3つの観点（GCアリーナ管理、mrb_protect、例外処理）から分析しました。

**重要:** このレポートは`lib/add/`配下の実装中のコードを除外し、`components/picoruby-esp32/picoruby/mrbgems/`配下の既存の安定したmrbgemのみを対象としています。

作成日: 2025-11-13
調査対象: components/picoruby-esp32/picoruby/mrbgems/（fmrb-*を除く）

---

## 重要な発見：標準mrbgemではmrb_funcallの使用は限定的

調査の結果、picoruby標準のmrbgemでは**mrb_funcallの使用は非常に限定的**であることが判明：

- **picoruby-filesystem-fat**: mrb_funcall使用なし
- **picoruby-env**: mrb_funcall使用なし
- **その他多数のmrbgem**: mrb_funcall使用なし

### なぜmrb_funcallがほとんど使われていないのか？

**理由:**
- ほとんどのmrbgemはC関数から直接Rubyオブジェクトを操作
- mrb_funcallはRubyメソッドを呼び出すための機能だが、mrbgem内では直接C実装を使用するのが一般的
- mrb_funcallを使うのは主に：
  - トップレベルのテストドライバー（mruby-test）
  - デバッガー（mruby-bin-debugger）
  - Rubyコールバックを呼び出す必要がある場合

---

## 1. GCアリーナ管理 (mrb_gc_arena_save/restore)

### 1-1. 基本パターン: forループ全体を囲む

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/picoruby-machine/src/mruby/machine.c` (206-211行)

```c
int ai = mrb_gc_arena_save(mrb);
for (mrb_int i = 0; i < argc; i++) {
  print_sub(mrb, argv[i]);  // 内部でmrb_funcall(mrb, obj, "to_s", 0)を呼び出す
  hal_write(1, "\n", 1);
}
mrb_gc_arena_restore(mrb, ai);
```

**同じパターン**: (221-226行)

```c
int ai = mrb_gc_arena_save(mrb);
mrb_get_args(mrb, "*", &argv, &argc);
for (mrb_int i = 0; i < argc; i++) {
  print_sub(mrb, argv[i]);  // 内部でmrb_funcall呼び出し
}
mrb_gc_arena_restore(mrb, ai);
```

**ポイント:**
- ループ全体を1つのsave/restoreで囲む
- 各イテレーションで生成される一時オブジェクトはループ終了後にまとめて解放
- シンプルで理解しやすいパターン

---

### 1-2. whileループでのパターン

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/picoruby-env/src/mruby/env.c` (37-47行)

```c
int ai = mrb_gc_arena_save(mrb);
while (1) {
  ENV_get_key_value(&key, &value);
  if (key == NULL) {
    break;
  }
  mrb_value key_value = mrb_str_new_cstr(mrb, key);
  mrb_value value_value = mrb_str_new_cstr(mrb, value);
  mrb_hash_set(mrb, env->hash, key_value, value_value);
}
mrb_gc_arena_restore(mrb, ai);
```

**ポイント:**
- whileループ全体を囲んでsave/restoreを1回だけ実行
- ループ内で多数のオブジェクトを生成する場合でもこのパターンが適切
- mrb_funcallは使っていないが、mrb_str_new_cstrなどでオブジェクト生成

---

### 1-3. 各イテレーション毎にsave/restore（上級パターン）

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/src/array.c` (Array#inspect実装の例)

mrubyの公式ドキュメント `gc-arena-howto.md` に記載されているパターン：

```c
for (i=0; i<RARRAY_LEN(ary); i++) {
  int ai = mrb_gc_arena_save(mrb);

  if (i > 0) {
    mrb_str_cat(mrb, arystr, sep, sizeof(sep));
  }
  if (mrb_array_p(RARRAY_PTR(ary)[i])) {
    s = inspect_ary(mrb, RARRAY_PTR(ary)[i], list);
  }
  else {
    s = mrb_inspect(mrb, RARRAY_PTR(ary)[i]);  // 内部でmrb_funcall呼び出し
  }
  mrb_str_cat(mrb, arystr, RSTRING_PTR(s), RSTRING_LEN(s));
  mrb_gc_arena_restore(mrb, ai);
}
```

**ポイント:**
- 各イテレーションで多数の一時オブジェクトが生成される場合
- イテレーション毎にアリーナをリセットすることでメモリ効率を最大化
- **重要:** 最終的に必要なオブジェクト（arystr）はrestore前に作成・保存する

---

### 1-4. GC公式ドキュメントからの重要な引用

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/doc/guides/gc-arena-howto.md` (175-180行)

> We must also mention that when `mrb_funcall` is called in top level, the return
> value is also registered to GC arena, so repeated use of `mrb_funcall` may
> eventually lead to an "arena overflow error".
>
> Use `mrb_gc_arena_save()` and `mrb_gc_arena_restore()` or possible use of
> `mrb_gc_protect()` to workaround this.

**重要なポイント:**
- **mrb_funcallの戻り値は自動的にGCアリーナに登録される**
- 繰り返しmrb_funcallを呼び出すとアリーナオーバーフローの原因になる
- これが今回のSegfaultの潜在的な原因の1つかもしれない

---

## 2. mrb_protect の使用パターン

### 2-1. 標準mrbgemではmrb_protectはほとんど使われていない

調査結果：
- picoruby-machine: 使用なし
- picoruby-filesystem-fat: 使用なし
- picoruby-env: 使用なし
- その他のmrbgem: ほとんど使用なし

**理由:**
- mrbgem内部ではRubyメソッドを呼び出すのではなく、C関数を直接実装
- 例外が発生する可能性がある処理は、直接mrb_raiseを呼び出すか、エラーコードを返す

### 2-2. mrb_protectが使われる場所

**mruby-testのドライバーコード** など、トップレベルでRubyコードを実行する場合のみ。

ただし、mruby-testでも`mrb_protect`ではなく、直接mrb_funcall後にmrb->excをチェックするパターンを使用：

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/mruby-test/driver.c` (37-47行)

```c
mrb_value result = mrb_funcall(mrb, mrb_top_self(mrb), "report", 0);
/* did an exception occur? */
if (mrb->exc) {
  mrb_print_error(mrb);
  mrb->exc = 0;
  return EXIT_FAILURE;
}
else {
  return mrb_bool(result) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

---

## 3. 例外処理パターン

### 3-1. 標準パターン: mrb->excの直接チェック

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/mruby-test/driver.c` (39-46行)

```c
mrb_value result = mrb_funcall(mrb, mrb_top_self(mrb), "report", 0);
/* did an exception occur? */
if (mrb->exc) {
  mrb_print_error(mrb);  // 標準エラー出力に出力
  mrb->exc = 0;          // 例外クリア（NULLでも可）
  return EXIT_FAILURE;
}
```

**ポイント:**
- mrb_funcall呼び出し後、必ずmrb->excをチェック
- 例外発生時は`mrb_print_error(mrb)`でログ出力
- **必ず** `mrb->exc = 0` または `mrb->exc = NULL` でクリア
- これが**picoruby標準の例外処理パターン**

---

### 3-2. 例外クリアの方法

picorubyのコードベースでは以下の2つの方法が使われている：

1. `mrb->exc = 0`
2. `mrb->exc = NULL`

**どちらも同じ意味**で、どちらを使っても問題ありません。

---

## 4. print_sub関数の実装（参考）

mrb_funcallの典型的な使用例として、`print_sub`関数の実装を見てみます。

**ファイル**: `components/picoruby-esp32/picoruby/mrbgems/picoruby-machine/src/mruby/machine.c` (180-195行)

```c
static void
print_sub(mrb_state *mrb, mrb_value obj)
{
  if (mrb_string_p(obj)) {
    hal_write(1, RSTRING_PTR(obj), RSTRING_LEN(obj));
  }
  else {
    mrb_value str = mrb_funcall(mrb, obj, "to_s", 0);  // mrb_funcall使用
    if (mrb_string_p(str)) {
      hal_write(1, RSTRING_PTR(str), RSTRING_LEN(str));
    }
  }
}
```

**ポイント:**
- to_sメソッドの呼び出しにmrb_funcallを使用
- **mrb_protectは使用していない** （to_sは安全なメソッドのため）
- **例外チェックもしていない** （to_sが例外を投げることは想定していない）
- シンプルで直感的な実装

---

## 実装推奨パターン

### パターンA: ループ内での繰り返しmrb_funcall呼び出し

```c
void process_items(mrb_state *mrb, mrb_value items, int count)
{
    int ai = mrb_gc_arena_save(mrb);

    for (int i = 0; i < count; i++) {
        mrb_value result = mrb_funcall(mrb, items, "process", 1, mrb_fixnum_value(i));
        // resultを使った処理
    }

    mrb_gc_arena_restore(mrb, ai);
}
```

### パターンB: 単発のmrb_funcall呼び出し（GC管理不要）

```c
void simple_call(mrb_state *mrb, mrb_value obj)
{
    mrb_value result = mrb_funcall(mrb, obj, "to_s", 0);
    // resultを使った処理
    // 関数終了時に自動的にarenaクリア
}
```

### パターンC: トップレベルでの例外処理付き呼び出し

```c
int run_ruby_code(mrb_state *mrb)
{
    mrb_value result = mrb_funcall(mrb, mrb_top_self(mrb), "main", 0);

    if (mrb->exc) {
        mrb_print_error(mrb);
        mrb->exc = NULL;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
```

---

## 重要な注意事項

### GCアリーナ管理について

1. **繰り返しmrb_funcallを呼び出す場合は必須**
   - mrb_funcallの戻り値は自動的にGCアリーナに登録される
   - save/restoreを使わないとアリーナオーバーフローが発生する

2. **ループの外側でsave/restoreが基本**
   - シンプルで理解しやすい
   - メモリ効率が重要な場合は各イテレーション内でも検討

3. **単発の呼び出しでは不要**
   - 関数終了時に自動的にクリアされる

### mrb_protectについて

1. **標準mrbgemではほとんど使われていない**
   - 必要性が低い
   - 直接mrb->excをチェックする方がシンプル

2. **使うべき場合**
   - ユーザー定義のRubyコールバックを呼び出す場合
   - 例外を確実にキャッチしたい場合
   - ただし、実装中のコード（picoruby-fmrb-app）以外ではほとんど見られない

### 例外処理について

1. **mrb_funcall後は必ずmrb->excをチェック**（トップレベルの場合）
2. **例外クリアは `mrb->exc = NULL` または `mrb->exc = 0`**
3. **`mrb_print_error(mrb)`で詳細な例外情報を出力**

---

## 調査対象ファイル一覧

### 主要な参照元

1. **picoruby-machine/src/mruby/machine.c**
   - print_sub関数でのmrb_funcall使用例
   - GCアリーナ管理の典型的なパターン

2. **mruby-test/driver.c**
   - トップレベルでの例外処理パターン
   - mrb_print_errorの使用例

3. **picoruby-env/src/mruby/env.c**
   - whileループでのGCアリーナ管理

4. **picoruby-mruby/lib/mruby/doc/guides/gc-arena-howto.md**
   - 公式GCアリーナガイド
   - mrb_funcallに関する重要な警告

### 調査したが使用例がなかったmrbgem

- picoruby-filesystem-fat: mrb_funcall使用なし
- その他多数

---

## 結論

1. **標準mrbgemではmrb_funcallの使用は非常に限定的**
   - ほとんどのmrbgemは直接C実装を提供
   - mrb_funcallはRubyコールバック呼び出しなど特殊な場合のみ

2. **GCアリーナ管理は繰り返し呼び出し時に必須**
   - 公式ドキュメントでも明記されている
   - アリーナオーバーフロー防止のため

3. **mrb_protectは標準では使われていない**
   - 直接mrb->excチェックがシンプルで一般的
   - ただし、ユーザーコールバック呼び出しでは有用

4. **例外処理は `mrb->exc = NULL` でクリア**
   - これがpicoruby標準パターン

---

## 参考文献

- `components/picoruby-esp32/picoruby/mrbgems/picoruby-machine/src/mruby/machine.c`
- `components/picoruby-esp32/picoruby/mrbgems/mruby-test/driver.c`
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-env/src/mruby/env.c`
- `components/picoruby-esp32/picoruby/mrbgems/picoruby-mruby/lib/mruby/doc/guides/gc-arena-howto.md`
