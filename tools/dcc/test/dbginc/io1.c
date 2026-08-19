// io.c
#include <stdint.h>
#include <io.h>

uint8_t inb(uint16_t port) {
	uint8_t res;
	__asm__	(
			"PUSH DWORD X\n"
			"PUSH DWORD A");
	__reg_X=(uint32_t)port;
	__asm__ ("IN BYTE A, X");
	res=(uint8_t)__reg_A;
	__asm__ (
			"POP DWORD A\n"
			"POP DWORD X");
	return res;
}

uint16_t inw(uint16_t port) {
	uint16_t res;
	__asm__	(
			"PUSH DWORD X\n"
			"PUSH DWORD A");
	__reg_X=(uint32_t)port;
	__asm__ ("IN WORD A, X");
	res=(uint16_t)__reg_A;
	__asm__ (
			"POP DWORD A\n"
			"POP DWORD X");
