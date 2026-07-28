10 REM Benchmark: bare loop. 100 x 1000 iterations of FOR/NEXT with nothing
20 REM inside, so the number is the interpreter's per statement overhead.
30 REM The loop is nested because a FOR counter is a 16 bit signed value.
40 REM _SPEED 0 lifts the 60 statements per frame pacing (phase_b5 T5-2).
50 _SPEED 0
60 PRINT "BENCH START loop"
70 FOR J=1 TO 100
80 FOR I=1 TO 1000
90 NEXT
100 NEXT
110 PRINT "BENCH END loop"
120 END
