struct S {
    unsigned int a : 3;
    unsigned int b : 5;
    int c : 4;
};
int main(void) {
    struct S s;
    s.a = 5;
    s.b = 10;
    s.c = -3;
    return s.a + s.b + s.c;
}
