#### presym (Pre-Symbolization) について

presymはPicoRubyのビルド時に、Rubyコードを静的解析してシンボルIDを事前生成する仕組み。

**重要な制約:**
- Cコード内で `MRB_SYM(method_name)` や `MRB_SYM_E(method_name)` を使用する場合、そのシンボルは必ずRubyコード内で参照されている必要がある
- Rubyコード内で参照されていないシンボルは presym が生成せず、コンパイルエラーになる
- 実行時には到達しないダミーメソッド内でも、メソッド呼び出しを記述すればpresymが認識する

**presymエラーの対処方法:**
1. `MRB_SYM__xxx undeclared` エラーが出たら、該当CファイルでどのシンボルがMRB_SYM()で使われているか確認
2. 対応するRubyファイル（mrblib/*.rb）に、ダミーメソッドを作成してそのシンボルを参照
3. インスタンスメソッドの場合は、そのクラス内にダミーメソッドを配置（例: FAT::Dir, FAT::File）
4. Rubyコードを変更した場合は、必ず `rm -rf components/picoruby-esp32/picoruby/build/host` でビルドキャッシュをクリーンしてから再ビルド

**ダミーメソッドの例:**
```ruby
class FAT
  class Dir
    def self._dummy_for_presym
      dir = FAT::Dir.new("/")
      dir.findnext    # MRB_SYM(findnext) を生成
      dir.pat = ""    # MRB_SYM_E(pat) を生成
      dir.rewind      # MRB_SYM(rewind) を生成
    end
  end
end
```

**注意:**
- presymエラーが出た時に「ビルドキャッシュを疑う」ことは禁物。まずRubyコード内でのシンボル参照を確認すること
- ビルドキャッシュのクリーンが必要なのは、Rubyコードを変更した後に presym が更新されない場合のみ
