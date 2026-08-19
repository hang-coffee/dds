/* t19.c - 内联汇编 __asm__（DASM 格式） */
/* expect A = 0x52 */
int main(void) {
    int r;
    int t;
    r = 0;
    t = 1;
    __asm__("LET A, DWORD 0x40");      /* A = 64 */
    __asm__("ADD DWORD A, 1");         /* A = 65 */
    __asm__("MOV B, F\n"
            "ADD DWORD B, 8\n"
            "ST DWORD *B, A");         /* t = A = 65 */
    r = r + t;                         /* 65 */
    __asm__("MOV A, F\n"
            "ADD DWORD A, 4\n"
            "LR DWORD A, *A\n"
            "ADD DWORD A, 1\n"
            "MOV B, F\n"
            "ADD DWORD B, 8\n"
            "ST DWORD *B, A");         /* t = r + 1 = 66 */
    r = r + t;                         /* 65+66 = 131 */
    __asm__("MNE DWORD A");            /* A = -131 (忽略) */
    r = r - 49;                        /* 82 */
    return r;                          /* 82 = 0x52 */
}
