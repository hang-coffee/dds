/* t28.c - struct/union/enum/typedef */
/* expect A = 0x55 */
typedef unsigned int u32;
typedef struct Point { int x; int y; } Point;
typedef struct Rect { Point tl; Point br; } Rect;

struct Counter {
    int count;
    char tag;
};

union Value {
    int i;
    char bytes[4];
};

enum Color { RED, GREEN = 5, BLUE };

int main(void) {
    struct Counter c;
    Point p;
    Rect r;
    union Value v;
    enum Color col;
    int total;

    c.count = 10;
    c.tag = 'A';              /* 65 */
    p.x = 3;
    p.y = 4;
    r.tl = p;                 /* 结构体整体赋值 */
    r.br.x = 6;
    r.br.y = 8;
    v.i = 0x12345678;
    col = BLUE;               /* 6 */

    total = c.count + c.tag;  /* 75 */
    total = total + r.tl.x + r.tl.y;   /* +7 = 82 */
    total = total + r.br.x;   /* +6 = 88 */
    total = total + v.bytes[0];  /* 0x78 = 120 → 208 */
    total = total + col;      /* +6 = 214 */
    total = total - 129;      /* 85 */
    return total;             /* 85 = 0x55? 不，214-129=85 = 0x55 */
}
