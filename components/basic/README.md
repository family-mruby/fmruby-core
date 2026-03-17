# FMRuby BASIC Interpreter

シンプルなBASICインタプリタ実装。Family BASICスタイルの構文をサポートします。

## ディレクトリ構造

```
basic/
├── include/
│   └── fmrb_basic.h          # 公開API
├── basic/
│   ├── basic_internal.h      # 内部定義
│   ├── lexer.c               # 字句解析器
│   ├── parser.c              # 構文解析器・ステートメント実行
│   └── runtime.c             # ランタイム・式評価
├── extension/
│   ├── fmrb_basic_gfx.h      # グラフィックス拡張(将来用)
│   └── fmrb_basic_gfx.c
├── fmrb_basic.c              # FMRubyラッパー実装
└── CMakeLists.txt            # ビルド設定
```

## サポートされている機能

### 基本構文
- **行番号**: `10 PRINT "Hello"`
- **代入**: `LET A = 10` または `A = 10`
- **コメント**: `REM This is a comment`
- **終了**: `END`

### 演算子
- **算術演算**: `+`, `-`, `*`, `/`
- **比較演算**: `=`, `<>`, `<`, `>`, `<=`, `>=`
- **括弧**: `(`, `)`

### 制御構造
- **条件分岐**:
  - `IF expr THEN line`
  - `IF expr THEN statement`
- **無条件分岐**: `GOTO line`
- **サブルーチン**: `GOSUB line`, `RETURN`
- **ループ**:
  - `FOR var = start TO end`
  - `FOR var = start TO end STEP step`
  - `NEXT` または `NEXT var`

### 入出力
- **出力**: `PRINT expr [, expr] [; expr]`
- **入力**: `INPUT var`

## 使用例

```c
#include "fmrb_basic.h"

// 初期化
fmrb_basic_init();

// インタプリタ状態の作成
basic_state_t* state = fmrb_basic_newstate(ctx);

// 出力コールバックの設定
fmrb_basic_set_output_cb(state, my_output_callback, user_data);

// プログラムのロード
const char* program =
    "10 PRINT \"Hello, BASIC!\"\n"
    "20 FOR I = 1 TO 5\n"
    "30 PRINT I\n"
    "40 NEXT I\n"
    "50 END\n";
fmrb_basic_load(state, program);

// プログラムの実行
fmrb_basic_run(state);

// クリーンアップ
fmrb_basic_close(state);
```

## サンプルプログラム

`flash/app/sample/basic.app.bas` に実行可能なサンプルプログラムがあります。

## 命令の追加方法

将来的にFamily BASICのSprite命令や描画命令を追加する場合、以下の手順で拡張できます:

### 1. トークンタイプの追加

`basic/basic_internal.h` の `token_type_t` enumに新しいトークンを追加:

```c
typedef enum {
    // ... 既存のトークン
    TOK_SPRITE,   // 新しい命令
    TOK_DRAW,
    // ...
} token_type_t;
```

### 2. キーワードの登録

`basic/lexer.c` の `keywords` テーブルに追加:

```c
static const keyword_entry_t keywords[] = {
    // ... 既存のキーワード
    {"SPRITE", TOK_SPRITE},
    {"DRAW", TOK_DRAW},
    // ...
};
```

### 3. パーサーへの追加

`basic/parser.c` の `parse_statement()` 関数にケースを追加:

```c
fmrb_err_t parse_statement(basic_state_t* state, const char* line) {
    // ... 既存のコード

    switch (tok.type) {
        // ... 既存のケース

        case TOK_SPRITE:
            lexer_next_token();
            return exec_sprite(state);

        // ...
    }
}
```

### 4. 実行関数の実装

`extension/fmrb_basic_gfx.c` などに実装:

```c
static fmrb_err_t exec_sprite(basic_state_t* state) {
    // パラメータの解析
    value_t sprite_num = eval_expression(state);

    token_t tok = lexer_next_token();  // ','
    value_t x = eval_expression(state);

    // ... 追加のパラメータ

    // グラフィックスAPIの呼び出し
    // fmrb_gfx_sprite(sprite_num.num, x.num, y.num, ...);

    return FMRB_OK;
}
```

## 設計上の特徴

- **モジュラー設計**: 字句解析、構文解析、実行が分離されているため、拡張が容易
- **メモリ管理**: FMRubyのメモリプールシステムと統合
- **コールバック方式**: 入出力はコールバック経由で柔軟に対応
- **拡張性**: `extension/` ディレクトリで機能を追加可能

## 制限事項

- 最大行数: 1000行
- 最大変数数: 256個
- FORループのネスト: 最大16段
- GOSUBのネスト: 最大16段
- 行の最大長: 256文字

## 今後の拡張予定

- グラフィックス命令 (SPRITE, DRAW, CIRCLE, etc.)
- サウンド命令 (SOUND, PLAY, etc.)
- 配列のサポート (DIM)
- 文字列関数 (LEFT$, RIGHT$, MID$, etc.)
- 数学関数 (SIN, COS, ABS, etc.)
