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

## kill

killall -9 fmrb_host_sdl2 2>/dev/null; sleep 1; rm -f /tmp/fmrb_socket
