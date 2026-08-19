/* t7.c - 全局变量 + 全局数组 */   /* expect A = 0x43 */
int g;
int garr[4];

int main(void) {
    g = 7;
    garr[0] = 10;
    garr[1] = 20;
    garr[2] = 30;
    garr[3] = 40;
    return g + garr[1] + garr[3];   /* 7+20+40 = 67 */
}
