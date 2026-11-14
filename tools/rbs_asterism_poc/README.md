# RBS の記録型から Asterism の型の宣言を作る試作 (2026-11-14)

`rbs` の gem に入っている、Ruby に依存しない C の解析器 (`src/`・`include/rbs/`) だけを使って、
RBS の記録型の別名を読み、Asterism の `define_type` に渡す形 (名前、`[[フィールド, 型], ...]`、
説明) を JSON で出す。端末 (ESP32) の上で RBS を解析できるかを見るための、PC の上の試作。
ファームウェアには組み込んでいない。

## 流し方

```
d=$(ruby -e 'puts Gem::Specification.find_by_name("rbs").gem_dir')
gcc -Os -I$d/include rbs2ast.c $d/src/*.c $d/src/util/*.c \
    -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free -o rbs2ast
./rbs2ast asterism-games games.rbs
```

## 型の対応

| RBS | Asterism |
|---|---|
| `Integer`・`Float`・`String`・`bool`・`untyped` | `int`・`float`・`str`・`bool`・`any` |
| `Array[T]` | `T[]` |
| `bin` という名前の別名 | `bin` |
| ほかの別名 `Ns::name` | `<接頭辞>/ns.name` |
| `T?` | `T` (省略できる印は今は捨てている) |
| 別名の前のコメント | 説明 |

## 結果 (rbs 4.1.3、PC の x86-64、1 回)

- `games.rbs` (卓のゲームの型 5 つを RBS で書いたもの) から出た宣言は、実際に網に流れている
  宣言 (フィールドと説明) と 5 つとも一致し、Asterism の検査 (`Asterism::Types.definition!`) も
  通った。
- 解析器のコードは `-Os` で約 132 KB (構文解析 48、文字コードの表 41、構文木 16、字句解析 14、
  ほか 13)。静的なデータは約 5 KB。文字コードの表は UTF-8 だけに絞れば減らせる見込み。
- 解析中に確保したメモリの最大: 1.1 KB の `games.rbs` で約 30 KB、57 KB の
  `sig/fmrb_app.rbs` (説明文が多い) で約 430 KB。確保は 4 KB の単位でまとめて取り、終わったら
  全部返す。

## まだ見ていないこと

- ESP32 (RISC-V・Xtensa) でのコードの大きさと、確保の単位。
- 解析器のメモリの確保先を、アプリのプールに向ける方法 (今は標準の `malloc`)。
- メソッドの定義 (公開するオブジェクトの引数と説明) を読むこと。
- 省略できるフィールド、和集合、組 (タプル) の扱い。
