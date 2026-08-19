/* t40.c - long 数组、指针、结构体成员、全局变量 */
/* expect A = 0x7F */
long gl = 0x100000000;
struct S { long x; int y; };
long arr[3];

int main(void) {
    int r = 0;
    struct S s;
    long *p;

    if (gl == 0x100000000) r = r + 1;   /* 全局 long 初始化（8 字节） */

    arr[0] = 0xFFFFFFFF;
    arr[1] = 2;
    arr[2] = arr[0] + arr[1];           /* 0x100000001 */
    if (arr[2] == 0x100000001) r = r + 2;

    p = &arr[1];
    *p = *p * 3;                        /* 6（指针解引用 long） */
    if (*p == 6) r = r + 4;

    p[1] = p[1] + 5;                    /* arr[2] += 5 → 0x100000006 */
    if (arr[2] == 0x100000006) r = r + 8;

    s.x = 0x100000000;
    s.y = 7;
    if (s.x == 0x100000000) r = r + 16; /* 结构体 long 成员 */
    if (s.y == 7) r = r + 32;

    s.x++;
    if (s.x == 0x100000001) r = r + 64;

    return r;                           /* 127 = 0x7F */
}
