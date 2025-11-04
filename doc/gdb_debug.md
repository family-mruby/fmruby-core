GDBによるメモリウォッチポイントデバッグ
手順
1. GDBでプログラムを起動
cd ~/kishima/fmruby-core
gdb --args ./build/fmruby-core.elf
2. バッファ作成地点にブレークポイントを設定
(gdb) break fmrb_gfx_command_buffer_create
Breakpoint 1 at 0x...
3. プログラムを実行
(gdb) run
プログラムがブレークポイントで停止します。
4. バッファが作成されるまで実行を進める
(gdb) finish
これで関数が終了し、戻り値（バッファのアドレス）が表示されます：
Value returned is $1 = (fmrb_gfx_command_buffer_t *) 0x561aacc7c078
5. ハードウェアウォッチポイントを設定
このアドレスに対してウォッチポイントを設定します：
(gdb) watch *(uint64_t*)0x561aacc7c078
Hardware watchpoint 2: *(uint64_t*)0x561aacc7c078
これにより、このアドレスの最初の8バイト（commandsポインタ）が変更された瞬間に実行が停止します。
6. 実行を継続
(gdb) continue
7. ウォッチポイントがトリガーされたら
メモリが変更された瞬間に停止し、以下のような出力が表示されます：
Hardware watchpoint 2: *(uint64_t*)0x561aacc7c078

Old value = 0x561aacc7c3c0
New value = 0x0000000000000000
0x00007ffff7a5b123 in memset () from /lib/x86_64-linux-gnu/libc.so.6
8. スタックトレースを確認
(gdb) backtrace
これにより、誰がこのメモリを0で上書きしたかがわかります。
より詳細な監視
バッファ全体（24バイト）を監視したい場合は、複数のウォッチポイントを設定：
(gdb) watch *(uint64_t*)0x561aacc7c078      # commands pointer
(gdb) watch *(uint64_t*)(0x561aacc7c078+8)  # max_commands
(gdb) watch *(uint64_t*)(0x561aacc7c078+16) # count
SDL2プロセスとの連携
FMRuby Coreはホストプロセス（SDL2）と通信するため、ホストプロセスを先に起動してからGDBでデバッグする必要があります。
方法1: 手動起動
ターミナル1（ホストプロセス）:
cd ~/kishima/fmruby-core
./host/sdl2/build/fmrb_host
ターミナル2（GDBデバッグ）:
cd ~/kishima/fmruby-core
gdb --args ./build/fmruby-core.elf
方法2: rake run:linuxを修正
Rakefileを見て、run:linuxタスクがどのように起動しているか確認し、GDBを組み込む方法もあります。
便利なGDBコマンド
# 現在の変数値を表示
(gdb) print buffer
(gdb) print *buffer

# メモリダンプ（16進数で32バイト表示）
(gdb) x/32xb 0x561aacc7c078

# 条件付きブレークポイント
(gdb) break fmrb_gfx_command_buffer_add_clear if buffer->max_commands == 0

# 自動コマンド（ブレークポイントで停止するたびに実行）
(gdb) commands 2
Type commands for breakpoint(s) 2, one per line.
End with a line saying just "end".
>backtrace
>info registers
>end
デバッグセッションの例
$ gdb --args ./build/fmruby-core.elf
(gdb) break fmrb_gfx_command_buffer_create
(gdb) run
...
Breakpoint 1, fmrb_gfx_command_buffer_create (max_commands=128) at fmrb_gfx_commands.c:65

(gdb) finish
Run till exit from #0  fmrb_gfx_command_buffer_create...
Value returned is $1 = (fmrb_gfx_command_buffer_t *) 0x561aacc7c078

(gdb) watch *(uint64_t*)0x561aacc7c078
Hardware watchpoint 2: *(uint64_t*)0x561aacc7c078

(gdb) continue
Continuing.
...
Hardware watchpoint 2: *(uint64_t*)0x561aacc7c078

Old value = 94661590196160
New value = 0
... ここで犯人が判明！ ...

(gdb) backtrace
#0  0x... in 犯人の関数 ()
#1  0x... in 呼び出し元 ()
...

(gdb) info locals
(gdb) info args