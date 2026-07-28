10 REM Benchmark: expression evaluation. 20000 rounds of arithmetic,
20 REM comparison and a function call, which is what game logic is made of.
30 _SPEED 0
40 PRINT "BENCH START expr"
50 A=7:B=3:C=0
60 FOR I=1 TO 20000
70 C=(A*B+I)/2-ABS(B-A)
80 IF C>32000 THEN C=0
90 D=RND(1)
100 NEXT
110 PRINT "BENCH END expr"
120 END
