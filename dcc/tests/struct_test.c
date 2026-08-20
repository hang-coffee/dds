struct Point { int x; int y; };
struct Point g;

struct Point make(int x, int y) {
    struct Point p;
    p.x = x;
    p.y = y;
    return p;
}

struct Point *getptr(void) {
    return &g;
}

int main(void) {
    struct Point a;
    struct Point b;
    struct Point *q;
    a.x = 1;
    a.y = 2;
    b = a;
    q = &b;
    q->x = 5;
    b = make(7, 8);
    g = make(3, 4);
    q = getptr();
    return q->x + b.y;
}
