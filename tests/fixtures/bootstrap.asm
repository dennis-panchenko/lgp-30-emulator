; The 1957 Programming Manual's own bootstrap-loader program (p.35),
; also cataloged in the Subroutine Manual as "Program 09.0". Repeatedly
; reads a word from tape and stores it at a self-incrementing address
; starting at 2000. See tests/test_bootstrap.c for the library-level
; version of this same program with commentary.
start 0000
0000 p 0000
0001 i 0000
0002 c 2000
0003 b 0002
0004 a 0007
0005 y 0002
0006 u 0000
0007 dw 4
