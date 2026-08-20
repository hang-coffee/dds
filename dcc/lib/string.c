#include <stddef.h>
#include <stdint.h>

#include <string.h>
void *memchr(const void *str, int c, size_t n) {
	for(int i=0; i<n; i++) {
		if((uint8_t)str[i]==(uint8_t)c) return str+i;
	}
	return NULL;
}

void *memrchr(const void *str, int c, size_t n) {
	for(int i=n; i>0; i--) {
		if((uint8_t)str[i]==(uint8_t)c) return str+i;
	}
	return NULL;
}

int memcmp(const void *str1, const void *str2, size_t n) {
	if(n==0) return 0;
	for(int i=0; i<n; i++) {
		if((uint8_t)str1[i]>(uint8_t)str2[i]) return 1;
		if((uint8_t)str1[i]<(uint8_t)str2[i]) return -1;
	}
	return 0;
}

void *memcpy(void *dest, const void *src, size_t n) {
	for(int i=0; i<n; i++) {
		dest[i]=src[i];
	}
	return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
	char *d = (char *)dest;
	const char *s = (const char *)src;
	size_t i;
	if (d < s) {
		for(i=0; i<n; i++) d[i]=s[i];
	} else if (d > s) {
		for(i=n; i>0; i--) d[i-1]=s[i-1];
	}
	return dest;
}

void *memset(void *str, int c, size_t n) {
	for(int i=0; i<n; i++) {
		str[i]=(uint8_t)c;
	}
	return str;
}

char *strcat(char *dest, const char *src) {
	int i=0, j=0;
	while(src[j]!=0) {
		j++;
	}
	while(src[i]!=0) {
		dest[j]=src[i];
		j++;
		i++;
	}
	return dest;
}

