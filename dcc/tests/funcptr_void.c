typedef int (*Binop)(int, int);

int add(int a, int b) {
    return a + b;
}

int apply(Binop f, int a, int b) {
    return f(a, b);
}

int main(void) {
    int x;
    void *p;
    int *q;
    Binop fp = add;
    x = 42;
    p = &x;
    q = (int *)p;
    return apply(fp, 3, 4) + *q;
}
