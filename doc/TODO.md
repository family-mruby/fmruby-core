# TODO

## Dir.open のパス変換をHAL層で吸収する

現状、`File.open` はHAL層 (`fmrb_hal_file_open` の `build_path`) がプラットフォーム差 (Linux: `flash/` prefix, ESP32: `/flash/` prefix) を自動吸収するため、アプリ側でパス変換を意識する必要がない。

一方、`Dir.open` は mruby 標準ライブラリ経由で直接 OS の opendir を呼ぶため、HAL を通らない。そのためアプリ側で `to_os_dir_path` を使って明示的にプラットフォーム依存のプレフィックスを付加する必要がある。

Dir.open もHAL層を経由するようにして、File.open と同様に仮想パス (`/home/music/`) をそのまま渡せるようにすべき。

## Path長チェック

メッセージ構造体で送れるサイズで、パスの長さが決まる。
最大長を検討するとともに、フレームワーク側で、長すぎるパスを弾けるようにする。

