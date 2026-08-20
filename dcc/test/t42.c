/* t42.c - stdarg.h 可变参数支持 */
/* expect A = 0x06 */
#include <stdarg.h>

int sum(int n, ...) {
    va_list ap;
    int s = 0;
    va_start(ap, n);
    while (n-- > 0)
        s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int main(void) {
    return sum(3, 1, 2, 3);
}
