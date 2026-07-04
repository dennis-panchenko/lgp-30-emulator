; Small input-free program for exercising the REPL: adds the number 5
; (stored via dw) into the accumulator, then stops. Avoids `i` entirely
; since the REPL's simulated input always reads EOF (see repl.c).
start 0000
0000 a 0002
0001 z 0000
0002 dw 5
