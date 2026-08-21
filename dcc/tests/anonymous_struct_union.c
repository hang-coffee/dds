struct Outer {
    union {
        int i;
        int j;
    };
    struct {
        int a;
        int b;
    };
    int c;
};

int main(void) {
    struct Outer o;
    o.i = 40;
    o.a = 1;
    o.b = 2;
    o.c = 3;
    return o.i + o.a + o.b + o.c;
}
