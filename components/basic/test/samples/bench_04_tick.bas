10 REM Benchmark: frame clock. PAUSE counts frames, so 600 of them should take
20 REM ten seconds. The gap between that and the measured time is the tick
30 REM error, and a large one means frames are being dropped (phase_b5 T5-2).
40 PRINT "BENCH START tick"
50 PAUSE 600
60 PRINT "BENCH END tick"
70 END
