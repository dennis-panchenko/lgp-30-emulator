; Prints "hi" using the Flexowriter Commands-block codes for the
; lowercase opcode-mnemonic letters h and i (tracks 49 and 17 -- print's
; operand address encodes the code in its track field, see ABOUT.md).
start 0000
0000 p 4900
0001 p 1700
0002 z 0000
