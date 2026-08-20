char *gmsg = "global";

int len(char *s) {
    int n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}

int main(void) {
    char *p = "hello";
    char msg[6] = "hello";
    int r;
    r = p[0] + p[4];
    r = r + msg[0] + msg[4];
    r = r + len("hi");
    r = r + len(gmsg);
    return r;
}
