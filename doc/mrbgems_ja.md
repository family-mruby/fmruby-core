<!-- summary: ビルド時ライブラリマネージャについて -->

# mrbgems

mrbgemsは、CとRuby拡張機能をmrubyに簡単かつ標準化された方法で統合するためのライブラリマネージャです。慣例として、各mrbgem名には`mruby-`というプレフィックスが付けられます（例：`Time`クラス機能を提供するgemは`mruby-time`）。

## 使い方

ビルド設定でmrbgemsを明示的に有効化する必要があります。gemを追加するには、ビルド設定ファイルに以下の行を追加します：

```ruby
conf.gem '/path/to/your/gem/dir'
```

gemの指定には相対パスも使用できます。

```ruby
conf.gem 'examples/mrbgems/ruby_extension_example'
```

この場合：

- ビルド設定ファイルが`build_config`ディレクトリにある場合、`MRUBY_ROOT`からの相対パスになります。
- それ以外の場合は、ビルド設定ファイルがあるディレクトリからの相対パスになります。

リモートGITリポジトリの場所も指定できます：

```ruby
conf.gem :git => 'https://github.com/masuidrive/mrbgems-example.git', :branch => 'master'
conf.gem :github => 'masuidrive/mrbgems-example', :branch => 'master'
conf.gem :bitbucket => 'mruby/mrbgems-example', :branch => 'master'
```

注意：`:bitbucket`オプションはgitのみをサポートします。このバージョンではHgはサポートされていません。

`:path`オプションでリポジトリのサブディレクトリを指定できます：

```ruby
conf.gem github: 'mruby/mruby', path: 'mrbgems/mruby-socket'
```

[mgem-list](https://github.com/mruby/mgem-list)からmrbgemを使用するには`:mgem`オプションを使用します：

```ruby
conf.gem :mgem => 'mruby-yaml'
conf.gem :mgem => 'yaml' # 'mruby-'プレフィックスは省略可能
```

チェックアウトするコミットハッシュを指定するには`:checksum_hash`オプションを使用します：

```ruby
conf.gem mgem: 'mruby-redis', checksum_hash: '3446d19fc4a3f9697b5ddbf2a904f301c42f2f4e'
```

依存関係が不足している場合、mrbgem依存関係リゾルバはコアまたはmgem-listからmrbgemを参照します。

同じベース名（デフォルトのチェックアウトディレクトリ名）を持つ複数のgitベースのgemがある場合、同じリポジトリURL、ブランチ名、コミットID（チェックサムハッシュ）を持つ場合を**除いて**、エラーになります。これを回避するには、優先するバージョンを**最初に**明示的にインポートし、`canonical:`オプションを`true`に設定します：

```ruby
conf.gem github: 'me/mruby-yaml', branch: 'my-hacked-branch', canonical: true
```

これを行うと、システムはこの名前のgemをクローンする他の試みを（ほぼ）黙って無視します。

これはgitからgemをクローンすることにのみ影響することに注意してください。バージョンの競合は解決しません。gemのrakefileで指定されたバージョンが依存関係と互換性がない場合、ビルドは失敗します。

### ビルド設定ファイルでgemを調整する

`conf.gem`呼び出しでブロックを渡すことで、元のgemが想定していない環境での調整ができます：

```ruby
conf.gem core: "mruby-bin-mirb" do |g|
  # NetBSDへのクロスビルド用
  g.linker.libraries = %w(edit termcap)
end
```

ただし、gemの作者の意図から逸脱する可能性があるため、注意して使用する必要があります。

### Gemのテスト

ビルドで`enable_test`でユニットテストを有効にすると、デフォルトですべてのgemとその依存関係に対してテストが生成されます。必要に応じて、特定のgemのテストを抑制できます：

```ruby
conf.gem 'mruby-noisygem' do |g|
  g.skip_test = true
end
```

ただし、可能な限りすべてのテストを有効にしておくことがベストプラクティスとされています。テストが無効になっているgemごとに警告メッセージが生成されます。

## GemBox

mrbgemsのコレクションを一度にmrubyに追加したい場合や、設定に基づいてmrbgemsを置換できるようにしたい場合があります。mrbgemsのパッケージ化されたコレクションをGemBoxと呼びます。GemBoxは、ビルド設定に`config.gem`で追加するのと同じ形式でmrubyに読み込むmrbgemsのリストを含むファイルですが、`MRuby::GemBox`オブジェクトでラップされています。GemBoxは`config.gembox 'boxname'`でmrubyに読み込まれます。

以下は`mruby-time`と`mrbgems-example`を含むGemBoxの例です：

```ruby
MRuby::GemBox.new do |conf|
  conf.gem "#{root}/mrbgems/mruby-time"
  conf.gem :github => 'masuidrive/mrbgems-example'
end
```

前述のように、GemBoxは`MRuby::Build`と同じ規約を使用します。GemBoxは、mrubyによって認識されるように、`mrbgems`ディレクトリ内に`.gembox`拡張子で保存する必要があります。

この例のGemBoxを使用するには、mrubyの`mrbgems`ディレクトリ内に`custom.gembox`として保存し、ビルドブロック内のビルド設定ファイルに以下を追加します：

```ruby
conf.gembox 'custom'
```

これにより、ビルドプロセス中に`custom` GemBoxが読み込まれ、`mruby-time`と`mrbgems-example`がビルドに追加されます。

必要に応じて、GemBoxをmrubyディレクトリの外に配置することもできます。その場合は、以下のように絶対パスを指定する必要があります。

```ruby
conf.gembox "#{ENV["HOME"]}/mygemboxes/custom"
```

mrubyには2つのGemBoxが付属しています：[default](../../mrbgems/default.gembox)と[full-core](../../mrbgems/full-core.gembox)。[default](../../mrbgems/default.gembox) GemBoxにはmrubyのいくつかのコアコンポーネントが含まれ、[full-core](../../mrbgems/full-core.gembox)には`mrbgems`ディレクトリにあるすべてのgemが含まれます。

## GEM構造

最大のGEM構造は次のようになります：

```
+- GEM_NAME             <- GEMの名前
    |
    +- README.md        <- GEMのReadme
    |
    +- mrbgem.rake      <- GEM仕様
    |
    +- include/         <- Ruby拡張用ヘッダー（エクスポートされる）
    |
    +- mrblib/          <- Ruby拡張用ソース
    |
    +- src/             <- C拡張用ソース
    |
    +- tools/           <- 実行可能ファイル用ソース（C）
    |
    +- test/            <- テストコード（Ruby）
```

`mrblib`ディレクトリには、mrubyを拡張する純粋なRubyファイルが含まれます。`src`ディレクトリには、mrubyを拡張するC/C++ファイルが含まれます。`include`ディレクトリにはC/C++ヘッダーファイルが含まれます。`test`ディレクトリには、`mrbtest`で使用されるテスト用のC/C++および純粋なRubyファイルが含まれます。`mrbgem.rake`には、CおよびRubyファイルをコンパイルするための仕様が含まれます。`README.md`はGEMの簡単な説明です。

## ビルドプロセス

mrbgemsは、GEMディレクトリ内に`mrbgem.rake`という仕様ファイルを期待します。典型的なGEM仕様は次のようになります：

```ruby
MRuby::Gem::Specification.new('c_and_ruby_extension_example') do |spec|
  spec.license = 'MIT'
  spec.author  = 'mruby developers'
  spec.summary = 'Example mrbgem using C and Ruby'
end
```

mrbgemsビルドプロセスは、この仕様を使用してオブジェクトとRubyファイルをコンパイルします。コンパイル結果は`lib/libmruby.a`に追加されます。このファイルは、`mruby`や`mirb`などのツールにGEM機能を公開します。

情報提供のために、`MRuby::Gem::Specification`内で以下のプロパティを設定できます：

- `spec.license`または`spec.licenses`（このGEMがライセンスされる単一のライセンスまたはそのリスト）
- `spec.author`または`spec.authors`（開発者名またはそのリスト）
- `spec.version`（現在のバージョン）
- `spec.description`（詳細な説明）
- `spec.summary`
  - mrbgemの1行の短い説明。
  - 設定されている場合、rakeのビルドサマリーに出力されます。
- `spec.homepage`（ホームページ）
- `spec.requirements`（ユーザーへの情報としての外部要件）

すべてのGEMで`license`と`author`プロパティは必須です！

GEMが他のGEMに依存している場合は、次のように`spec.add_dependency(gem, *requirements[, default_get_info])`を使用してください：

```ruby
MRuby::Gem::Specification.new('c_and_ruby_extension_example') do |spec|
  spec.license = 'MIT'
  spec.author  = 'mruby developers'

  # GEM依存関係mruby-parserを追加。
  # バージョンは1.0.0から1.5.2の間である必要があります。
  spec.add_dependency('mruby-parser', '>= 1.0.0', '<= 1.5.2')

  # GitHubから任意のバージョンのmruby-uvを使用。
  spec.add_dependency('mruby-uv', '>= 0.0.0', :github => 'mattn/mruby-uv')

  # GitHubから最新のmruby-onig-regexpを使用。（バージョン要件は省略可能）
  spec.add_dependency('mruby-onig-regexp', :github => 'mattn/mruby-onig-regexp')

  # テスト時のみ有効な追加のmgemsを追加できます
  spec.add_test_dependency('mruby-process', :github => 'iij/mruby-process')
end
```

バージョン要件とデフォルトgem情報はオプションです。

バージョン要件は以下の演算子をサポートします：

- '=': 等しい
- '!=': 等しくない
- '>': より大きい
- '<': より小さい
- '>=': 等しいまたはより大きい
- '<=': 等しいまたはより小さい
- '~>': 等しいまたはより大きく、次のメジャーバージョンより小さい
  - 例1: '~> 2.2.2'は'>= 2.2.2'かつ'< 2.3.0'を意味します
  - 例2: '~> 2.2'は'>= 2.2.0'かつ'< 3.0.0'を意味します

複数のバージョン要件が渡された場合、依存関係はそのすべてを満たす必要があります。

ビルド設定で定義されていない場合に依存関係として使用するデフォルトgemを設定できます。`add_dependency`呼び出しの最後の引数が`Hash`の場合、デフォルトgem情報として扱われます。その形式は`MRuby::Build#gem`メソッドの引数と同じですが、パスgem位置として扱うことはできません。

特別なバージョンの依存関係が必要な場合は、ビルド設定で`MRuby::Build#gem`を使用してデフォルトgemを上書きします。

競合するGEMがある場合は、以下のメソッドを使用します：

- `spec.add_conflict(gem, *requirements)`
  - `requirements`引数は`add_dependency`メソッドと同じです。

次のようなコード：

```ruby
MRuby::Gem::Specification.new 'some-regexp-binding' do |spec|
  spec.license = 'BSD'
  spec.author = 'John Doe'

  spec.add_conflict 'mruby-onig-regexp', '> 0.0.0'
  spec.add_conflict 'mruby-hs-regexp'
  spec.add_conflict 'mruby-pcre-regexp'
  spec.add_conflict 'mruby-regexp-pcre'
end
```

GEMがより複雑なビルド要件を持つ場合、GEM仕様内で以下のオプションを追加で使用できます：

- `spec.cc.flags`（Cコンパイラフラグ）
- `spec.cc.defines`（Cコンパイラ定義）
- `spec.cc.include_paths`（Cコンパイラインクルードパス）
- `spec.linker.flags`（リンカーフラグ）
- `spec.linker.libraries`（リンカーライブラリ）
- `spec.linker.library_paths`（リンカー追加ライブラリパス）
- `spec.bins`（バイナリファイルを生成）
- `spec.rbfiles`（コンパイルするRubyファイル）
- `spec.objs`（コンパイルするオブジェクトファイル）
- `spec.test_rbfiles`（mrbtest統合用のRubyテストファイル）
- `spec.test_objs`（mrbtest統合用のオブジェクトテストファイル）
- `spec.test_preload`（mrbtest用の初期化ファイル）

`spec.mruby.cc`と`spec.mruby.linker`を使用して、コンパイラとリンカーの追加のグローバルパラメータを追加することもできます。

### include_pathsと依存関係

GEMは、GEMに依存する他のGEMにincludeパスをエクスポートできます。デフォルトでは、`/...絶対パス.../{GEM_NAME}/include`がエクスポートされます。したがって、GEMのローカルヘッダーファイルをinclude/に配置しないことをお勧めします。

これらのエクスポートは遡及的です。例えば、BがCに依存し、AがBに依存する場合、AはCによってエクスポートされたincludeパスを取得します。

エクスポートされたinclude_pathsは、rakeによってGEMローカルinclude_pathsに自動的に追加されます。より複雑なビルドが必要な場合は、`spec.export_include_paths`アクセサを使用できます。

## C拡張

mrubyはCで拡張できます。これは、C APIを使用してCライブラリをmrubyに統合することで可能になります。

### 前提条件

mrbgemsは、`mrb_YOURGEMNAME_gem_init(mrb_state)`というCメソッドが実装されていることを期待します。`YOURGEMNAME`はGEMの名前に置き換えられます。GEMを`c_extension_example`と呼ぶ場合、初期化メソッドは次のようになります：

```c
void
mrb_c_extension_example_gem_init(mrb_state* mrb) {
  struct RClass *class_cextension = mrb_define_module(mrb, "CExtension");
  mrb_define_class_method(mrb, class_cextension, "c_method", mrb_c_method, MRB_ARGS_NONE());
}
```

### 終了処理

mrbgemsは、`mrb_YOURGEMNAME_gem_final(mrb_state)`というCメソッドが実装されていることを期待します。`YOURGEMNAME`はGEMの名前に置き換えられます。GEMを`c_extension_example`と呼ぶ場合、終了処理メソッドは次のようになります：

```c
void
mrb_c_extension_example_gem_final(mrb_state* mrb) {
  free(someone);
}
```

### 例

```
+- c_extension_example/
    |
    +- README.md        (オプション)
    |
    +- src/
    |   |
    |   +- example.c    <- C拡張ソース
    |
    +- test/
    |   |
    |   +- example.rb   <- C拡張用テストコード
    |
    +- mrbgem.rake      <- GEM仕様
```

## Ruby拡張

mrubyは純粋なRubyで拡張できます。この方法で既存のクラスを上書きしたり、新しいクラスを追加したりできます。すべてのRubyファイルを`mrblib`ディレクトリに配置します。

### 前提条件

なし

### 例

```
+- ruby_extension_example/
    |
    +- README.md        (オプション)
    |
    +- mrblib/
    |   |
    |   +- example.rb   <- Ruby拡張ソース
    |
    +- test/
    |   |
    |   +- example.rb   <- Ruby拡張用テストコード
    |
    +- mrbgem.rake      <- GEM仕様
```

## CとRuby拡張

mrubyはCとRubyを同時に拡張できます。この方法で既存のクラスを上書きしたり、新しいクラスを追加したりできます。すべてのRubyファイルを`mrblib`ディレクトリに、すべてのCファイルを`src`ディレクトリに配置します。

`mrblib`ディレクトリ下のmrubyコードは、gem初期化C関数が呼び出された後に実行されます。_mrubyスクリプト_が_Cコード_に依存し、_Cコード_が_mrubyスクリプト_に依存しないようにしてください。

### 前提条件

CとRubyの例を参照してください。

### 例

```
+- c_and_ruby_extension_example/
    |
    +- README.md        (オプション)
    |
    +- mrblib/
    |   |
    |   +- example.rb   <- Ruby拡張ソース
    |
    +- src/
    |   |
    |   +- example.c    <- C拡張ソース
    |
    +- test/
    |   |
    |   +- example.rb   <- CとRuby拡張用テストコード
    |
    +- mrbgem.rake      <- GEM仕様
```

## バイナリgem

一部のgemは`bin`ディレクトリ下に実行可能ファイルを生成できます。これらのgemはバイナリgemと呼ばれます。バイナリgemの名前は慣例的に`mruby-bin`というプレフィックスが付けられます（例：`mruby-bin-mirb`、`mruby-bin-strip`）。

実行可能ファイルの名前を指定するには、`mrbgem.rake`で`spec.bins`を指定する必要があります。エントリポイント`main()`は、`tools/<bin>/*.c`下のCソースファイルにある必要があります（`<bin>`は実行可能ファイルの名前）。`<bin>`ディレクトリ下のCファイルはコンパイルされて実行可能ファイルにリンクされますが、`libmruby.a`には含まれません。一方、`mrblib`と`src`下のファイルは含まれます。

通常のgemとバイナリgemを分離するために、バイナリgemに`mrblib`と`src`ディレクトリを含めないことを強くお勧めします。

### 例

```
+- mruby-bin-example/
    |
    +- README.md          (オプション)
    |
    +- bintest/
    |   |
    |   +- example.rb     <- バイナリgem用テストコード
    |
    +- mrbgem.rake        <- Gem仕様
    |
    +- mrblib/            <- Ruby拡張用ソース（オプション）
    |
    +- src/               <- C拡張用ソース（オプション）
    |
    +- tools/
        |
        +- example/       <- 実行可能ファイル名ディレクトリ
            |
            +- example.c  <- 実行可能ファイル用ソース（mainを含む）
```
