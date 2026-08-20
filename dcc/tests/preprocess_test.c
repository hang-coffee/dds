#include "include_defs.h"

int main(void) {
    int x;
    x = ADD2(3, 4);
    x = SQUARE(x);
    x = x + INC_VALUE;
    return x;
}
