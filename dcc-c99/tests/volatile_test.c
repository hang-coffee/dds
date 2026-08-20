volatile int x;
int main(void) {
    volatile int a = 1;
    x = a;
    return x;
}
