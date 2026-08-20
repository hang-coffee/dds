typedef int myint;
typedef struct Point Point;
struct Point { int x; int y; };
union U { int i; char c; };
enum Color { RED, GREEN, BLUE };

int main(void) {
    myint a;
    Point p;
    union U u;
    a = RED;
    p.x = 1;
    p.y = 2;
    u.i = 65;
    return u.c + p.x + GREEN;
}
