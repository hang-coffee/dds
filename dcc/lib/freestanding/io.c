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
	return res;
}

uint32_t ind(uint16_t port) {
	uint32_t res;
	__asm__	(
			"PUSH DWORD X\n"
			"PUSH DWORD A");
	__reg_X=(uint32_t)port;
	__asm__ ("IN DWORD A, X");
	res=__reg_A;
	__asm__ (
			"POP DWORD A\n"
			"POP DWORD X");
	return res;
}

void outb(uint16_t port, uint8_t data) {
	__asm__ (
			"PUSH DWORD X\n"
			"PUSH DWORD A");
	__reg_X=(uint32_t)port;
	__reg_A=(uint32_t)data;
	__asm__	("OUT BYTE X, A");
	__asm__ (
			"POP DWORD A\n"
			"POP DWORD X");
	return;
}

void outw(uint16_t port, uint16_t data) {
	__asm__ (
			"PUSH DWORD X\n"
			"PUSH DWORD A");
	__reg_X=(uint32_t)port;
	__reg_A=(uint32_t)data;
	__asm__	("OUT WORD X, A");
	__asm__ (
			"POP DWORD A\n"
			"POP DWORD X");
	return;
}

void outd(uint16_t port, uint32_t data) {
	__asm__ (
			"PUSH DWORD X\n"
			"PUSH DWORD A");
	__reg_X=(uint32_t)port;
	__reg_A=data;
	__asm__	("OUT DWORD X, A");
	__asm__ (
			"POP DWORD A\n"
			"POP DWORD X");
	return;
}

