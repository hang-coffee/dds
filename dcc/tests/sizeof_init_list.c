int g[3] = {10, 20, 30};
int h[] = {1, 2, 3, 4};

int main(void) {
    int a[3] = {1, 2, 3};
    int b[] = {4, 5, 6};
    int s;
    s = sizeof(int) + sizeof(a) + sizeof(b);
    return s + a[0] + b[2] + g[2] + h[3];
}
