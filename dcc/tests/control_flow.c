int main(void) {
    int x = 1;
    int y = 2;
    int z;
    z = x ? y : 5;
    z = (x = 3, x + y);
    do {
        z = z + 1;
    } while (z < 10);
    goto done;
    z = 999;
done:
    return z;
}
