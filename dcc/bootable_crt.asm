; bootable_crt.asm - DOCTOR 可启动 CRT
; 设置栈指针 S=0x300000 后调用 func_main。
; 用户自行汇编/链接本文件，而不是由 dcc 自动加入。

SECTION TEXT
ORG 0

EXTERN func_main

_start:
LET S, DWORD 0x300000
MOV DWORD A, F
PUSH DWORD A
LET E, DWORD _crt_ret0
PUSH DWORD E
LET E, DWORD func_main
JMP

_crt_ret0:
POP DWORD F

_crt_halt:
HLT
LET E, DWORD _crt_halt
JMP
