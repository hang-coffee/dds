/* t41.c - 函数指针：typedef、赋值、间接调用、参数、全局/结构体成员 */
/* expect A = 0x20 */
typedef int (*binop)(int, int);

int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }

binop global_add = add;

struct S { binop cb; };

int apply(binop f, int a, int b) {
    return f(a, b);
}

int main(void) {
    int r = 0;
    binop fp;
    struct S s;

    fp = add;
    r = r + fp(2, 3);        /* 5  */

    r = r + apply(sub, 10, 4); /* 6 -> 11 */

    r = r + global_add(5, 6);  /* 11 -> 22 */

    s.cb = add;
    r = r + s.cb(1, 2);        /* 3 -> 25 */

    fp = &add;
    r = r + (*fp)(3, 4);       /* 7 -> 32 */

    return r;
}
