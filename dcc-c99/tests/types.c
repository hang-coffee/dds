const int add(short a, long b, long long c, unsigned int d, signed char e) {
    int x;
    x = a + b + c + d + e;
    return x;
}
int main(void) {
    int y;
    y = add(1, 2, 3, 4, 5);
    return y;
}
