# fmruby-core

## ビルドに必要なもの

- Docker
- Ruby

## ホスト側開発用ビルドに必要なパッケージ

- libsdl2-dev
- libmsgpack-dev
- pkg-config
- clang-format
- clang-tidy
- llvm

## gem

- serialport

## ドキュメント生成

### C/C++ API ドキュメント

Doxygenを使用してC/C++ APIドキュメントを生成します。

```bash
# Doxygenインストール
sudo apt-get install doxygen  # Ubuntu/Debian
# or
brew install doxygen  # macOS

# ドキュメント生成
rake doc:c

# 生成されたドキュメントを開く
open doc/api/html/index.html  # macOS
xdg-open doc/api/html/index.html  # Linux
```

### Ruby API ドキュメント

YARDを使用してRuby APIドキュメントを生成します。

```bash
# YARDインストール
gem install yard

# ドキュメント生成
rake doc:ruby

# 生成されたドキュメントを開く
open doc/ruby_api/index.html  # macOS
xdg-open doc/ruby_api/index.html  # Linux
```

### すべてのドキュメント生成

```bash
rake doc:all
```

### ドキュメントクリーン

```bash
rake doc:clean
```

## kill

killall -9 fmrb_host_sdl2 2>/dev/null; sleep 1; rm -f /tmp/fmrb_socket
