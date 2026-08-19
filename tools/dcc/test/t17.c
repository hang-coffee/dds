/* t17.c - 全局 char* 字符串指针、全局数组字符串、指针遍历 */
/* expect A = 0x19D */
char *gmsg = "hi";
char garr[6] = "abc";

int main(void) {
    char *p;
    int r;
    r = gmsg[0];          /* 'h' = 104 */
    r = r + gmsg[1];      /* +105 = 209 */
    r = r + garr[2];      /* +99 = 308 */
    p = gmsg;
    p = p + 1;            /* 指向 'i' */
    r = r + *p;           /* +105 = 413 */
    return r;             /* 413 = 0x19D */
}
