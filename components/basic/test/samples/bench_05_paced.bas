10 REM Benchmark: the pacing ceiling. Same loop as bench_01 but without
20 REM _SPEED 0, so the interpreter holds itself to its statements per frame
30 REM budget. The rate this reports is the speed a program actually sees;
40 REM bench_01 reports what the hardware could do if it were let go.
50 PRINT "BENCH START paced"
60 FOR J=1 TO 20
70 FOR I=1 TO 100
80 NEXT
90 NEXT
100 PRINT "BENCH END paced"
110 END
