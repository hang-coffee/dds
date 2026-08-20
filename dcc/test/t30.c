/* t30.c - sizeof、void*、long、const、宏函数 */
/* expect A = 0x35 */
#define SUM3(a, b, c) ((a) + (b) + (c))
#define MUL2(x) ((x) * 2)

struct Big { char c; int i; short s; };   /* 1+4+2 = 7 */

int main(void) {
    const int k = 10;
    long l;
    void *pv;
    int *ip;
    char buf[8];
    int r;

    r = sizeof(int);            /* 4 */
    r = r + sizeof(struct Big); /* +7 = 11 */
    r = r + sizeof(buf);        /* +8 = 19 */
    r = r + sizeof(char);       /* +1 = 20 */
    r = r + sizeof(l);          /* +8 = 28（long 8 字节） */

    l = 5;
    r = r + (int)l;             /* +5 = 33 */

    ip = &k;                    /* const 取地址（读） */
    pv = (void *)ip;            /* int* → void* */
    ip = (int *)pv;             /* void* → int* */
    r = r + *ip;                /* +10 = 43 */

    r = r + SUM3(1, 2, 3);      /* +6 = 49 */
    r = r + MUL2(4);            /* +8 = 57 */
    r = r - 4;                  /* 53 = 0x35 */
    return r;                   /* 53 */
}
