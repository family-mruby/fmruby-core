10 REM Benchmark: string work. 10000 rounds of build, slice and compare,
20 REM the operations a text heavy program spends its time in.
30 _SPEED 0
40 PRINT "BENCH START string"
50 A$="FAMILY BASIC"
60 FOR I=1 TO 10000
70 B$=LEFT$(A$,6)+RIGHT$(A$,5)
80 C$=MID$(B$,3,4)
90 IF C$="MILY" THEN N=N+1
100 D=LEN(B$)+ASC(C$)
110 NEXT
120 PRINT "BENCH END string"
130 END
