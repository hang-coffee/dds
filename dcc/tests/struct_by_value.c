struct S { int a; int b; };
int sum(struct S s) {
    return s.a + s.b;
}
int main(void) {
    struct S x;
    x.a = 1;
    x.b = 2;
    return sum(x);
}
