#define SUM(...) (__VA_ARGS__)
#define ADD(first, ...) (first + __VA_ARGS__)

int main(void) {
    int x = SUM(1, 2, 3);
    int y = ADD(10, 20, 30);
    return x + y;
}
