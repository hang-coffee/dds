void init_uart() {
	__asm__(
			"PUSH DWORD X\n"
			"LET X, BYTE 0x01\n"
			"OUT DWORD 0x18, X\n"
			"POP DWORD X"
			);
	return;
}

void display(char *s) {
	int i=0;
	char c=s[0];
	while(c!=0) {
		__asm__("PUSH DWORD X");
		__reg_X=(int)c;
		__asm__ (
				"OUT BYTE 0x16, X\n"
				);
		__asm__("POP DWORD X");
		i++;
		c=s[i];
	}
	return;
}

void num2str(unsigned int num, char *str) {
	char buffer[16];      // 临时倒序缓冲区（32位最大 4294967295，10位足够，16绝对安全）
    int i = 0;
    int j = 0;

    // 特殊情况：如果数字是 0，直接返回 "0"
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    // 1. 从低位到高位逐位取出，存入 buffer（此时是倒序）
    while (num > 0) {
        buffer[i] = '0' + (num % 10);  // 取个位数字转为 ASCII
        num = num / 10;                // 去掉个位
        i++;
    }

    // 2. 将 buffer 倒序复制回 str
    while (i > 0) {
        i--;               // 先减，指向最后一个有效字符
        str[j] = buffer[i];
        j++;
    }
    str[j] = '\0';         // 添加字符串结束符
	return;
}

unsigned int calc() {
}

int main() {
	init_uart();
	char s[20];
	num2str(12000, s);
	display(s);
	return 0;
}
