int main(void) {
    int x = 1;
    int r = 0;
    switch (x) {
        case 1:
            r = r + 10;
        case 2:
            r = r + 100;
            break;
        default:
            r = 999;
    }
    return r;
}
