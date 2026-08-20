int counter(void) {
    static int x = 5;
    x = x + 1;
    return x;
}

void handler(void) __interrupt__ {
    __asm__("NOP");
}

int main(void) {
    return counter();
}
