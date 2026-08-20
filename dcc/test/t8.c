/* t8.c - 递归斐波那契：fib(10) = 55 */   /* expect A = 0x37 */
int fib(int n) {
    int r;
    if (n < 2) {
        r = n;
    } else {
        r = fib(n - 1) + fib(n - 2);
    }
    return r;
}

int main(void) {
    return fib(10);
}
