#define MODE 2

int main(void) {
#if MODE == 1
    return 1;
#elif MODE == 2
    return 2;
#else
    return 3;
#endif
#if defined(MODE) && !defined(NOPE)
    return 4;
#else
    return 5;
#endif
#ifdef MODE
    return 6;
#else
    return 7;
#endif
#ifndef NOPE
    return 8;
#else
    return 9;
#endif
}
