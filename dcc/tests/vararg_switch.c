#include <stdarg.h>

int sum(int n, ...) {
    va_list ap;
    int i;
    int s;
    s = 0;
    va_start(ap, n);
    for (i = 0; i < n; i = i + 1) {
        s = s + va_arg(ap, int);
    }
    va_end(ap);
    return s;
}

int classify(int x) {
    switch (x) {
        case 1:
            return 10;
        case 2:
            return 20;
        default:
            return 0;
    }
}

int main(void) {
    return sum(3, 1, 2, 3) + classify(2);
}
