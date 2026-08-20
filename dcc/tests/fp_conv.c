int main(void) {
    double d = 3.7;
    float f = 1.5f;
    int a;
    int b;
    int c;
    double d2;
    float f2;
    a = (int)d;
    b = (int)f;
    d2 = (double)f;
    f2 = (float)d;
    c = (d > 2.0);
    return a + b + c;
}
