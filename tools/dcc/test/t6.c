/* t6.c - 局部 char 数组字符串初始化（LOD/ST 拷贝） */   /* expect A = 0x0F */
int main(void) {
    char msg[6];
    int i;
    int s;
    msg[0] = 'X';
    s = 0;
    for (i = 1; i < 6; i = i + 1) {
        s = s + i;
    }
    return s;          /* 1+2+3+4+5 = 15 */
}
