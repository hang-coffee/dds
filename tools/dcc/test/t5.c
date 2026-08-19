/* t5.c - char 数组字符串初始化 + 逐字符求和 */   /* expect A = 0xB2 */
int main(void) {
    char msg[6];
    int i;
    int s;
    msg[0] = 'H';
    msg[1] = 'I';
    msg[2] = '!';
    msg[3] = 0;
    s = 0;
    for (i = 0; i < 3; i = i + 1) {
        s = s + msg[i];
    }
    return s;          /* 'H'+'I'+'!' = 72+73+33 = 178 */
}
