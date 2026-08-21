long double addld(long double a, long double b) {
    return a + b;
}

int main(void) {
    long double x;
    x = addld(1.5L, 2.25L);
    return (int)x;
}
