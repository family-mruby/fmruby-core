# sim の起動が、ソケットの準備の割り込み (EINTR) で失敗する件

> 状態: 進行中 | 更新: 2026-10-05 | Linux の sim で、graphics-audio のソケットの bind / listen が EINTR で失敗して起動しない。FreeRTOS の POSIX ポートの SIGALRM が系統呼び出しを割り込むのが原因の見込み。E1 で原因を確かめて直す

## 目的

sim (3 つのコンテナ) が毎回確実に起動するようにする。検証のたびに起動の失敗で止まらないようにする。

## 分かっていること

- graphics-audio のログ: `input_socket: Failed to listen: Interrupted system call`、
  `input_handler_ipc: Failed to bind socket: Interrupted system call` で、表示の初期化が失敗する。core は
  `INIT_DISPLAY ACK timeout` で止まる。2026-10-03 に互換構成の起動で 3 回連続、H1 の作業中にも 3 回。
- core 側でも `Failed to connect to socket server`、debugd の `transport init failed` (Broken pipe) を見た。
- graphics-audio の `display_shm.cpp` には「FreeRTOS の SIGALRM が系統呼び出しを割り込むので EINTR で再試行する」
  手当てが既にあるが、ソケットの bind / listen / accept / connect (input_socket.c、input_handler_ipc.c、
  comm_socket_server.c) には無い。
- fmruby-core の boot.c に、SIGALRM がブロックされているかを記録する診断がある。

## 方針

- 推論で直さない。どの呼び出しが、どの信号で、どのくらいの頻度で割り込まれているかを確かめてから直す。
- 直し方の候補: (1) EINTR で再試行する (既存の display_shm と同じ形)、(2) ソケットの準備の間だけ SIGALRM を
  ブロックする、(3) SA_RESTART。既存の手当てとそろえる。
- 利用者から見た動きは変えない (sim の中の話)。実機のファームには影響させない (Linux の経路だけ)。

## 段階

| 段階 | 内容 | 状態 |
|---|---|---|
| E1 | 原因の確認と修正、くり返しの起動の試験 (instruction_e1.md) | 着手 |
