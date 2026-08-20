; bin_crt.asm - DOCTOR 平坦二进制 CRT
; 不设置 S，只跳转到 func_main。
; 用户自行汇编/链接本文件，而不是由 dcc 自动加入。

SECTION TEXT
ORG 0

_start:
LET E, DWORD func_main
JMP
