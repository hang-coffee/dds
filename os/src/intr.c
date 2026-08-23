#include "intr.h"

struct ict_gate ict[256];

void ict_set_gate(struct ict_gate *gate, uint32_t base) {
	gate->base=base;
	gate->attr=0;
	return;
}

void init_ict() {
	int i;
	for(i=0; i<256; i++) {
		ict_set_gate(&(ict[i]), blank_isr);
	}
	return;
}

void init_intr() {
	init_ict();
	__asm__("PUSH DWORD A");
	__reg_A=&ict;
	__asm__("SETB ICTB, A\n"
			"LET DWORD A, 0xffffffff\n"
			"PUSH DWORD A\n"
			"POP RIN1\n"
			"LET DWORD A, 0x80000000\n"
			"SETB RIN3_CTRL, A\n"
			"POP DWORD A\n");
	return;
}

void blank_isr(void) {
	return;
}

