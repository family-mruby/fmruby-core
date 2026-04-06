10 REM Sample BASIC Program for FMRuby
20 REM This demonstrates various BASIC features
30 PRINT "Hello from BASIC!"
40 PRINT "FMRuby BASIC Interpreter"
50 PRINT ""
51 WAIT 1000
60 REM Test variables and arithmetic
70 LET A = 10
80 LET B = 20
90 LET C = A + B
100 PRINT "A = "; A
110 PRINT "B = "; B
120 PRINT "C = A + B = "; C
130 PRINT ""
140 REM Test FOR loop
150 PRINT "Counting from 1 to 5:"
160 FOR I = 1 TO 5
161 WAIT 500
170 PRINT "  ", I
180 NEXT I
190 PRINT ""
200 REM Test FOR loop with STEP
210 PRINT "Counting down from 10 to 0 by 2:"
220 FOR I = 10 TO 0 STEP -2
230 PRINT "  ", I
240 NEXT I
250 PRINT ""
260 REM Test IF statement
270 LET X = 15
280 PRINT "X = "; X
290 IF X > 10 THEN PRINT "X is greater than 10"
300 IF X < 10 THEN PRINT "X is less than 10"
310 IF X = 15 THEN PRINT "X equals 15"
320 PRINT ""
330 REM Test GOSUB/RETURN
331 WAIT 500
340 PRINT "Calling subroutine..."
350 GOSUB 500
351 WAIT 500
360 PRINT "Returned from subroutine"
370 PRINT ""
380 REM Test multiplication and division
390 PRINT "Testing arithmetic:"
400 PRINT "  5 * 3 = "; 5 * 3
410 PRINT "  20 / 4 = "; 20 / 4
420 PRINT "  (2 + 3) * 4 = "; (2 + 3) * 4
430 PRINT ""
440 PRINT "Program completed!"
441 WAIT 5000
450 END
500 REM Subroutine
510 PRINT "  Inside subroutine"
520 LET Y = 100
530 PRINT "  Y = "; Y
540 RETURN
