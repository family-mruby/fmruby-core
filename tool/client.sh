#!/bin/bash

ruby ./fmrb_transfer.rb --port /tmp/fmrb_client shell

exit

シェル内で以下のコマンドが使えます:
ls /
cd /subdir
ls
get test.txt downloaded.txt
put local_file.txt /uploaded.txt
ワンショットコマンド:
# リモートファイル一覧
ruby tool/fmrb_transfer.rb --port /tmp/fmrb_client remote ls /

# ファイルダウンロード
ruby tool/fmrb_transfer.rb --port /tmp/fmrb_client transfer down /test.txt local.txt

# ファイルアップロード
ruby tool/fmrb_transfer.rb --port /tmp/fmrb_client transfer up local.txt /remote.txt
