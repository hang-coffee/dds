#ifndef INTR_H
#define INTR_H

#include "stdint.h"

struct ict_gate {
	uint32_t base;
	uint32_t attr;
};

void ict_set_gate(struct ict_gate *gate, uint32_t base);
void init_ict();
void init_intr();
void blank_isr(void)__interrupt__;

#endif

