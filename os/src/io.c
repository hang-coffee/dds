#include <stdint.h>
#include "io.h"
#include "_reg.h"

uint8_t inb(uint16_t port) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__asm__("IN BYTE X, A");
	uint8_t ret=(uint8_t)__reg_X;
	__asm__("POP DWORD X\n"
			"POP DWORD A");
	return ret;
}

uint16_t inw(uint16_t port) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__asm__("IN WORD X, A");
	uint16_t ret=(uint16_t)__reg_X;
	__asm__("POP DWORD X\n"
			"POP DWORD A");
	return ret;
}

uint32_t ind(uint16_t port) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__asm__("IN DWORD X, A");
	uint32_t ret=__reg_X;
	__asm__("POP DWORD X\n"
			"POP DWORD A");
	return ret;
}

void outb(uint16_t port, uint8_t data) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__reg_X=(uint32_t)data;
	__asm__("OUT BYTE A, X\n"
			"POP DWORD X\n"
			"POP DWORD A");
	return;
}

void outw(uint16_t port, uint16_t data) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__reg_X=(uint32_t)data;
	__asm__("OUT WORD A, X\n"
			"POP DWORD X\n"
			"POP DWORD A");
	return;
}

void outd(uint16_t port, uint32_t data) {
	__asm__("PUSH DWORD A\n"
			"PUSH DWORD X");
	__reg_A=(uint32_t)port;
	__reg_X=data;
	__asm__("OUT DWORD A, X\n"
			"POP DWORD X\n"
			"POP DWORD A");
	return;
}
