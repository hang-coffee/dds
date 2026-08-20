extern int ext_value;

static int helper(int x) {
    return x + 1;
}

inline int twice(int x) {
    return x * 2;
}

int main(void) {
    return helper(1) + twice(2) + ext_value;
}
