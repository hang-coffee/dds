/* t29.c - 结构体指针成员、->、链表、枚举显式值 */
/* expect A = 0x6E */
struct Node { int val; struct Node *next; };
enum Weekday { MON, TUE = 3, WED };   /* 0, 3, 4 */
int sum_vals(struct Node *p) {
    int s;
    s = 0;
    while (p != 0) {
        s = s + p->val;
        p = p->next;
    }
    return s;
}
int main(void) {
    struct Node a;
    struct Node b;
    struct Node c;
    int r;
    a.val = 1; b.val = 2; c.val = 3;
    a.next = &b; b.next = &c; c.next = 0;
    r = sum_vals(&a);          /* 1+2+3 = 6 */
    r = r + WED;               /* +4 = 10 */
    if (TUE == 3) r = r + 100; /* 110 */
    return r;                  /* 110 = 0x6E */
}
