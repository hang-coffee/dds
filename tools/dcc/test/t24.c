/* t24.c - #ifdef / #ifndef / #else / #endif 条件编译 */
/* expect A = 0x0B */
#define FEATURE_ON 1

int main(void) {
    int r;
    r = 0;
#ifdef FEATURE_ON
    r = r + 10;
#else
    r = r + 100;
#endif
#ifndef FEATURE_OFF
    r = r + 1;
#endif
#ifdef FEATURE_OFF
    r = r + 1000;
#endif
    return r;              /* 11 = 0x0B */
}
