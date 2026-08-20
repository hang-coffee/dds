int main(void) {
    _Bool b;
    int x;
    b = 5;
    x = b;
    b = 0;
    x = x + b;
    return x;
}
