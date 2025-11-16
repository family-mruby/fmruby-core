<!-- summary: GC Arenaについて -->

# `mrb_gc_arena_save()`/`mrb_gc_arena_restore()`/`mrb_gc_protect()`の使い方

_これは[Matzのブログ記事][matz blog post]の英語翻訳を日本語に再翻訳したものです。_
_一部は最近の変更を反映して更新されています。_

[matz blog post]: https://www.rubyist.net/~matz/20130731.html

C言語を使ってmrubyを拡張する際、不思議な「arena overflow error」やメモリリーク、非常に遅い実行速度に遭遇することがあります。これは「conservative GC」を実装する「GCアリーナ」のオーバーフローを示すエラーです。

GC（ガベージコレクタ）は、オブジェクトが「生きている」こと、言い換えれば、プログラムのどこかから参照されていることを保証する必要があります。これは、オブジェクトがルートから直接または間接的に参照できるかどうかをチェックすることで判定できます。ローカル変数、グローバル変数、定数などがルートになります。

プログラムの実行がmruby VM内で行われている場合、GCはVMが所有するすべてのルートにアクセスできるため、心配する必要はありません。

問題が発生するのは、C関数を実行している時です。C変数によって参照されているオブジェクトも「生きている」のですが、mrubyのGCはこれを認識できないため、C変数によってのみ参照されているオブジェクトを誤って死んでいると認識してしまう可能性があります。

GCが生きているオブジェクトを回収しようとすると、これは致命的なバグになり得ます。

CRubyでは、Cスタック領域をスキャンし、オブジェクトが生きているかどうかをチェックするためにC変数をルートとして使用します。もちろん、Cスタックを単なるメモリ領域としてアクセスしているため、それが整数なのかポインタなのかは決して分かりません。これに対処するために、ポインタのように見えればポインタと仮定するという回避策を使います。これを「保守的」と呼びます。

ところで、CRubyの「conservative GC」にはいくつかの問題があります。

最大の問題は、スタック領域にポータブルな方法でアクセスする手段がないことです。そのため、mrubyのように高度にポータブルなランタイムを実装したい場合、この方法を使用することはできません。

そこで、mrubyで「conservative GC」を実装するための別の計画を考え出しました。

繰り返しになりますが、問題は、C関数内で作成されたオブジェクトが、Ruby世界では参照されなくなり、ガベージとして扱えなくなることです。

mrubyでは、C関数内で作成されたすべてのオブジェクトを生きているものとして認識します。そうすれば、生きているオブジェクトを死んでいると勘違いするような問題は発生しません。

これは、本当に死んでいるオブジェクトを回収できないため、効率を失う可能性があることを意味しますが、トレードオフとしてGC自体が高度にポータブルになります。CRubyで時々発生する、最適化によってGCが生きているオブジェクトを削除してしまう問題とは無縁になります。

この考えに従って、C関数内で作成されたオブジェクトを記憶する「GCアリーナ」と呼ばれるテーブルがあります。

アリーナはスタック構造であり、C関数の実行がmruby VMに戻ると、アリーナに登録されたすべてのオブジェクトがポップされます。

これは非常にうまく機能しますが、別の問題を引き起こす可能性があります:「arena overflow error」またはメモリリークです。

本稿執筆時点では、mrubyはオブジェクトを記憶するためにアリーナを自動的に拡張します（[doc/guides/mrbconf.md](mrbconf.md)の`MRB_GC_FIXED_ARENA`と`MRB_GC_ARENA_SIZE`を参照）。

C関数内で多くのオブジェクトを作成すると、GCが決して起動しないため、メモリ使用量が増加します。このメモリ使用量はメモリリークのように見える可能性がありますが、より多くのメモリを割り当てる必要があるため、実行速度も低下します。

ビルド時設定で、アリーナの最大サイズを制限できます（例：100）。多くのオブジェクトを作成すると、アリーナがオーバーフローし、「arena overflow error」が発生します。

これらの問題を回避するために、`mrb_gc_arena_save()`と`mrb_gc_arena_restore()`関数があります。

`int mrb_gc_arena_save(mrb)`は、GCアリーナのスタックトップの現在位置を返し、`void mrb_gc_arena_restore(mrb, idx)`は、スタックトップの位置を指定された`idx`に戻します。

次のように使用できます:

```c
int arena_idx = mrb_gc_arena_save(mrb);

// ...オブジェクトを作成...
mrb_gc_arena_restore(mrb, arena_idx);

```

mrubyでは、C関数呼び出しはこのsave/restoreで囲まれていますが、save/restoreで囲むことでメモリ使用量をさらに最適化し、アリーナオーバーフローバグの作成を回避できます。

実際の例を見てみましょう。これは`Array#inspect`のソースコードです:

```c
static mrb_value
inspect_ary(mrb_state *mrb, mrb_value ary, mrb_value list)
{
  mrb_int i;
  mrb_value s, arystr;
  char head[] = { '[' };
  char sep[] = { ',', ' ' };
  char tail[] = { ']' };

  /* check recursive */
  for (i=0; i<RARRAY_LEN(list); i++) {
    if (mrb_obj_equal(mrb, ary, RARRAY_PTR(list)[i])) {
      return mrb_str_new(mrb, "[...]", 5);
    }
  }

  mrb_ary_push(mrb, list, ary);

  arystr = mrb_str_new_capa(mrb, 64);
  mrb_str_cat(mrb, arystr, head, sizeof(head));

  for (i=0; i<RARRAY_LEN(ary); i++) {
    int ai = mrb_gc_arena_save(mrb);

    if (i > 0) {
      mrb_str_cat(mrb, arystr, sep, sizeof(sep));
    }
    if (mrb_array_p(RARRAY_PTR(ary)[i])) {
      s = inspect_ary(mrb, RARRAY_PTR(ary)[i], list);
    }
    else {
      s = mrb_inspect(mrb, RARRAY_PTR(ary)[i]);
    }
    mrb_str_cat(mrb, arystr, RSTRING_PTR(s), RSTRING_LEN(s));
    mrb_gc_arena_restore(mrb, ai);
  }

  mrb_str_cat(mrb, arystr, tail, sizeof(tail));
  mrb_ary_pop(mrb, list);

  return arystr;
}
```

これは実際の例なので、少し複雑ですが、我慢してください。
`Array#inspect`の本質は、配列の各要素を`inspect`メソッドを使って文字列化した後、それらを結合して配列全体の`inspect`表現を取得することです。

`inspect`表現が作成された後は、個々の文字列表現は不要になります。つまり、これらの一時オブジェクトをGCアリーナに登録する必要はありません。

したがって、アリーナサイズを小さく保つために、`ary_inspect()`関数は以下のことを行います:

- `mrb_gc_arena_save()`を使用してスタックトップの位置を保存する。
- 各要素の`inspect`表現を取得する。
- 配列全体の`inspect`表現を構築しているものに追加する。
- `mrb_gc_arena_restore()`を使用してスタックトップの位置を復元する。

配列全体の最終的な`inspect`表現は、`mrb_gc_arena_restore()`の呼び出し前に作成されたことに注意してください。そうでなければ、必要な一時オブジェクトがGCによって削除される可能性があります。

多くの一時オブジェクトを作成した後、そのうちのいくつかを保持したいというユースケースがあるかもしれません。この場合、`ary_inspect()`のように既存のオブジェクトに追加するという同じ考えを使用することはできません。
代わりに、`mrb_gc_arena_restore()`の後に、保持したいオブジェクトを`mrb_gc_protect(mrb, obj)`を使用してアリーナに再登録する必要があります。
`mrb_gc_protect()`は「arena overflow error」につながる可能性もあるため、注意して使用してください。

トップレベルで`mrb_funcall`が呼び出されると、戻り値もGCアリーナに登録されるため、`mrb_funcall`を繰り返し使用すると、最終的に「arena overflow error」につながる可能性があることも言及しておく必要があります。

これを回避するには、`mrb_gc_arena_save()`と`mrb_gc_arena_restore()`を使用するか、場合によっては`mrb_gc_protect()`を使用してください。
